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
#include <sys/stat.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>

#include "udiskslogging.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxnvmefabrics.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksbasejob.h"
#include "udiskssimplejob.h"
#include "udisksthreadedjob.h"
#include "udiskslinuxdevice.h"

/**
 * SECTION:udiskslinuxnvmefabrics
 * @title: UDisksLinuxNVMeFabrics
 * @short_description: Linux implementation of #UDisksNVMeFabrics
 *
 * This type provides an implementation of the #UDisksNVMeFabrics
 * interface on Linux.
 */

typedef struct _UDisksLinuxNVMeFabricsClass   UDisksLinuxNVMeFabricsClass;

/**
 * UDisksLinuxNVMeFabrics:
 *
 * The #UDisksLinuxNVMeFabrics structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxNVMeFabrics
{
  UDisksNVMeFabricsSkeleton parent_instance;
};

struct _UDisksLinuxNVMeFabricsClass
{
  UDisksNVMeFabricsSkeletonClass parent_class;
};

static void nvme_fabrics_iface_init (UDisksNVMeFabricsIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxNVMeFabrics, udisks_linux_nvme_fabrics, UDISKS_TYPE_NVME_FABRICS_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_NVME_FABRICS, nvme_fabrics_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_nvme_fabrics_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_nvme_fabrics_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_nvme_fabrics_parent_class)->finalize (object);
}

static void
udisks_linux_nvme_fabrics_init (UDisksLinuxNVMeFabrics *ctrl)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (ctrl),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_nvme_fabrics_class_init (UDisksLinuxNVMeFabricsClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = udisks_linux_nvme_fabrics_finalize;
}

/**
 * udisks_linux_nvme_fabrics_new:
 *
 * Creates a new #UDisksLinuxNVMeFabrics instance.
 *
 * Returns: A new #UDisksLinuxNVMeFabrics. Free with g_object_unref().
 */
UDisksNVMeFabrics *
udisks_linux_nvme_fabrics_new (void)
{
  return UDISKS_NVME_FABRICS (g_object_new (UDISKS_TYPE_LINUX_NVME_FABRICS, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_nvme_fabrics_update:
 * @ctrl: A #UDisksLinuxNVMeFabrics.
 * @object: The enclosing #UDisksLinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_nvme_fabrics_update (UDisksLinuxNVMeFabrics *ctrl,
                                     UDisksLinuxDriveObject    *object)
{
  UDisksNVMeFabrics *iface = UDISKS_NVME_FABRICS (ctrl);
  UDisksLinuxDevice *device;
  const gchar *hostnqn = NULL;
  const gchar *hostid = NULL;
  const gchar *transport = NULL;
  const gchar *tr_addr = NULL;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    return FALSE;

  g_object_freeze_notify (G_OBJECT (object));

  hostnqn = g_udev_device_get_sysfs_attr (device->udev_device, "hostnqn");
  hostid = g_udev_device_get_sysfs_attr (device->udev_device, "hostid");
  transport = g_udev_device_get_sysfs_attr (device->udev_device, "transport");
  tr_addr = g_udev_device_get_sysfs_attr (device->udev_device, "address");

  if (hostnqn)
    udisks_nvme_fabrics_set_host_nqn (iface, hostnqn);
  if (hostid)
    udisks_nvme_fabrics_set_host_id (iface, hostid);
  if (transport)
    udisks_nvme_fabrics_set_transport (iface, transport);
  if (tr_addr)
    udisks_nvme_fabrics_set_transport_address (iface, tr_addr);

  g_object_thaw_notify (G_OBJECT (object));

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (ctrl));
  g_object_unref (device);

  return FALSE;   /* don't re-apply the drive 'configuration' (PM, etc.) */
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksObject *
wait_for_disconnect (UDisksDaemon *daemon,
                     gpointer      user_data)
{
  const gchar *object_path = user_data;

  return udisks_daemon_find_object (daemon, object_path);
}

static gboolean
handle_disconnect (UDisksNVMeFabrics     *_object,
                   GDBusMethodInvocation *invocation,
                   GVariant              *arg_options)
{
  UDisksLinuxDriveObject *object;
  UDisksLinuxDevice *device = NULL;
  UDisksDaemon *daemon = NULL;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;
  gchar *object_path = NULL;

  object = udisks_daemon_util_dup_object (_object, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * requests disconnect of a NVMeoF connected controller.
   *
   * Do not translate $(device.name), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to disconnect a NVMe over Fabrics controller $(device.name)");
  action_id = "org.freedesktop.udisks2.nvme-disconnect";

  /* Check that the user is authorized */
  daemon = udisks_linux_drive_object_get_daemon (object);
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    arg_options,
                                                    message,
                                                    invocation))
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  g_assert (device != NULL);

  if (!bd_nvme_disconnect_by_path (g_udev_device_get_device_file (device->udev_device),
                                   &error))
    {
      udisks_debug ("Error disconnecting NVMeoF controller %s: %s (%s, %d)",
                    g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                    error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  object_path = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  if (! udisks_daemon_wait_for_object_to_disappear_sync (daemon,
                                                         wait_for_disconnect,
                                                         object_path,
                                                         NULL,
                                                         UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                         &error))
    {
      g_prefix_error (&error, "Error waiting for the NVMeoF object to disappear after disconnecting: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_nvme_fabrics_complete_disconnect (_object, invocation);

 out:
  g_clear_object (&device);
  g_clear_object (&object);
  g_free (object_path);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
nvme_fabrics_iface_init (UDisksNVMeFabricsIface *iface)
{
  iface->handle_disconnect = handle_disconnect;
}
