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
#include "udiskslinuxnvmecontroller.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksbasejob.h"
#include "udiskssimplejob.h"
#include "udisksthreadedjob.h"
#include "udiskslinuxdevice.h"

/**
 * SECTION:udiskslinuxnvmecontroller
 * @title: UDisksLinuxNVMeController
 * @short_description: Linux implementation of #UDisksNVMeController
 *
 * This type provides an implementation of the #UDisksNVMeController
 * interface on Linux.
 */

typedef struct _UDisksLinuxNVMeControllerClass   UDisksLinuxNVMeControllerClass;

/**
 * UDisksLinuxNVMeController:
 *
 * The #UDisksLinuxNVMeController structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxNVMeController
{
  UDisksNVMeControllerSkeleton parent_instance;
};

struct _UDisksLinuxNVMeControllerClass
{
  UDisksNVMeControllerSkeletonClass parent_class;
};

static void nvme_controller_iface_init (UDisksNVMeControllerIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxNVMeController, udisks_linux_nvme_controller, UDISKS_TYPE_NVME_CONTROLLER_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_NVME_CONTROLLER, nvme_controller_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_nvme_controller_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_nvme_controller_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_nvme_controller_parent_class)->finalize (object);
}


static void
udisks_linux_nvme_controller_init (UDisksLinuxNVMeController *drive)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (drive),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_nvme_controller_class_init (UDisksLinuxNVMeControllerClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = udisks_linux_nvme_controller_finalize;
}

/**
 * udisks_linux_nvme_controller_new:
 *
 * Creates a new #UDisksLinuxNVMeController instance.
 *
 * Returns: A new #UDisksLinuxNVMeController. Free with g_object_unref().
 */
UDisksNVMeController *
udisks_linux_nvme_controller_new (void)
{
  return UDISKS_NVME_CONTROLLER (g_object_new (UDISKS_TYPE_LINUX_NVME_CONTROLLER, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_nvme_controller_update:
 * @ctrl: A #UDisksLinuxNVMeController.
 * @object: The enclosing #UDisksLinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_nvme_controller_update (UDisksLinuxNVMeController *ctrl,
                                     UDisksLinuxDriveObject    *object)
{
  UDisksNVMeController *iface = UDISKS_NVME_CONTROLLER (ctrl);
  UDisksLinuxDevice *device;
  gint cntl_id = 0;
  const gchar *subsysnqn = NULL;
  const gchar *transport = NULL;
  const gchar *state = NULL;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    return FALSE;

  g_object_freeze_notify (G_OBJECT (object));

  subsysnqn = g_udev_device_get_sysfs_attr (device->udev_device, "subsysnqn");
  cntl_id = g_udev_device_get_sysfs_attr_as_int (device->udev_device, "cntlid");
  transport = g_udev_device_get_sysfs_attr (device->udev_device, "transport");
  state = g_udev_device_get_sysfs_attr (device->udev_device, "state");

  if (device->nvme_ctrl_info)
    {
      udisks_nvme_controller_set_nvme_revision (iface, device->nvme_ctrl_info->nvme_ver);
      udisks_nvme_controller_set_unallocated_capacity (iface, device->nvme_ctrl_info->size_unalloc);
      udisks_nvme_controller_set_fguid (iface, device->nvme_ctrl_info->fguid);

      cntl_id = device->nvme_ctrl_info->ctrl_id;
      if (device->nvme_ctrl_info->subsysnqn && strlen (device->nvme_ctrl_info->subsysnqn) > 0)
        subsysnqn = device->nvme_ctrl_info->subsysnqn;
    }

  udisks_nvme_controller_set_controller_id (iface, cntl_id);
  if (subsysnqn)
    udisks_nvme_controller_set_subsystem_nqn (iface, subsysnqn);
  if (transport)
    udisks_nvme_controller_set_transport (iface, transport);
  if (state)
    udisks_nvme_controller_set_state (iface, state);

  g_object_thaw_notify (G_OBJECT (object));

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (ctrl));
  g_object_unref (device);

  return FALSE;   /* don't re-apply the drive 'configuration' (PM, etc.) */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
nvme_controller_iface_init (UDisksNVMeControllerIface *iface)
{
}
