/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2022 Tomas Bzatek <tbzatek@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <string.h>

#include <blockdev/nvme.h>

#include "udiskslinuxnvmenamespace.h"
#include "udiskslogging.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udisksprivate.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxprovider.h"
#include "udisksdaemonutil.h"
#include "udisksbasejob.h"
#include "udiskssimplejob.h"
#include "udiskslinuxdevice.h"

/**
 * SECTION:udiskslinuxnvmenamespace
 * @title: UDisksLinuxNVMeNamespace
 * @short_description: Linux implementation of #UDisksNVMeNamespace
 *
 * This type provides an implementation of the #UDisksNVMeNamespace
 * interface on Linux.
 */

typedef struct _UDisksLinuxNVMeNamespaceClass   UDisksLinuxNVMeNamespaceClass;

/**
 * UDisksLinuxNVMeNamespace:
 *
 * The #UDisksLinuxNVMeNamespace structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxNVMeNamespace
{
  UDisksNVMeNamespaceSkeleton parent_instance;
};

struct _UDisksLinuxNVMeNamespaceClass
{
  UDisksNVMeNamespaceSkeletonClass parent_class;
};

static void nvme_namespace_iface_init (UDisksNVMeNamespaceIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxNVMeNamespace, udisks_linux_nvme_namespace, UDISKS_TYPE_NVME_NAMESPACE_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_NVME_NAMESPACE, nvme_namespace_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_nvme_namespace_init (UDisksLinuxNVMeNamespace *ns)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (ns),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_nvme_namespace_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_nvme_namespace_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_nvme_namespace_parent_class)->finalize (object);
}

static void
udisks_linux_nvme_namespace_class_init (UDisksLinuxNVMeNamespaceClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = udisks_linux_nvme_namespace_finalize;
}

/**
 * udisks_linux_nvme_namespace_new:
 *
 * Creates a new #UDisksLinuxNVMeNamespace instance.
 *
 * Returns: A new #UDisksLinuxNVMeNamespace. Free with g_object_unref().
 */
UDisksNVMeNamespace *
udisks_linux_nvme_namespace_new (void)
{
  return UDISKS_NVME_NAMESPACE (g_object_new (UDISKS_TYPE_LINUX_NVME_NAMESPACE, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_nvme_namespace_update:
 * @ns: A #UDisksLinuxNVMeNamespace.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_nvme_namespace_update (UDisksLinuxNVMeNamespace *ns,
                                    UDisksLinuxBlockObject   *object)
{
  UDisksNVMeNamespace *iface = UDISKS_NVME_NAMESPACE (ns);
  UDisksLinuxDevice *device;
  guint nsid = 0;
  const gchar *nguid = NULL;
  const gchar *eui64 = NULL;
  const gchar *uuid = NULL;
  const gchar *wwn = NULL;

  device = udisks_linux_block_object_get_device (object);
  if (device == NULL)
    return;

  g_object_freeze_notify (G_OBJECT (object));

  nsid = g_udev_device_get_sysfs_attr_as_int (device->udev_device, "nsid");
  nguid = g_udev_device_get_sysfs_attr (device->udev_device, "nguid");
  uuid = g_udev_device_get_sysfs_attr (device->udev_device, "uuid");
  wwn = g_udev_device_get_sysfs_attr (device->udev_device, "wwid");
  if (!wwn)
    wwn = g_udev_device_get_property (device->udev_device, "ID_WWN");

  if (device->nvme_ns_info)
    {
      nsid = device->nvme_ns_info->nsid;
      nguid = device->nvme_ns_info->nguid;
      eui64 = device->nvme_ns_info->eui64;
      uuid = device->nvme_ns_info->uuid;

      udisks_nvme_namespace_set_namespace_size (iface, device->nvme_ns_info->nsize);
      udisks_nvme_namespace_set_namespace_capacity (iface, device->nvme_ns_info->ncap);
      udisks_nvme_namespace_set_namespace_utilization (iface, device->nvme_ns_info->nuse);

      if (device->nvme_ns_info->current_lba_format.data_size > 0)
        {
          udisks_nvme_namespace_set_formatted_lbasize (iface,
              g_variant_new ("(qy)", device->nvme_ns_info->current_lba_format.data_size,
                                     device->nvme_ns_info->current_lba_format.relative_performance));
        }

      if (device->nvme_ns_info->lba_formats && *device->nvme_ns_info->lba_formats)
        {
          GVariantBuilder builder;
          BDNVMELBAFormat **f;

          g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(qy)"));

          for (f = device->nvme_ns_info->lba_formats; *f; f++)
            g_variant_builder_add (&builder, "(qy)", (*f)->data_size, (*f)->relative_performance);

          udisks_nvme_namespace_set_lbaformats (iface, g_variant_builder_end (&builder));
        }
    }

  udisks_nvme_namespace_set_nsid (iface, nsid);
  if (nguid)
    udisks_nvme_namespace_set_nguid (iface, nguid);
  if (eui64)
    udisks_nvme_namespace_set_eui64 (iface, eui64);
  if (uuid)
    udisks_nvme_namespace_set_uuid (iface, uuid);
  if (wwn)
    udisks_nvme_namespace_set_wwn (iface, wwn);

  g_object_thaw_notify (G_OBJECT (object));
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (ns));
  g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
nvme_namespace_iface_init (UDisksNVMeNamespaceIface *iface)
{
}
