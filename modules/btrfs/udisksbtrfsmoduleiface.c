/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Peter Hatina <phatina@redhat.com>
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
 */

#include "config.h"

#include <blockdev/blockdev.h>

#include <modules/udisksmoduleiface.h>

#include <udisks/udisks-generated.h>
#include <src/udisksdaemon.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslogging.h>
#include <src/udisksmodulemanager.h>

#include "udisksbtrfstypes.h"
#include "udisksbtrfsstate.h"

#include "udiskslinuxfilesystembtrfs.h"
#include "udiskslinuxmanagerbtrfs.h"

/* ---------------------------------------------------------------------------------------------------- */

gchar *
udisks_module_id (void)
{
  return g_strdup (BTRFS_MODULE_NAME);
}

gpointer
udisks_module_init (UDisksDaemon *daemon)
{
  gboolean ret = FALSE;
  GError *error = NULL;

  /* NULL means no specific so_name (implementation) */
  BDPluginSpec btrfs_plugin = {BD_PLUGIN_BTRFS, NULL};
  BDPluginSpec *plugins[] = {&btrfs_plugin, NULL};

  if (!bd_is_plugin_available (BD_PLUGIN_BTRFS))
    {
      ret = bd_reinit (plugins, FALSE, NULL, &error);
      if (!ret)
        {
          udisks_error ("Error initializing the btrfs libblockdev plugin: %s (%s, %d)",
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
          /* XXX: can do nothing more here even though we know the module will be unusable! */
        }
    }

  return udisks_btrfs_state_new (daemon);
}

void
udisks_module_teardown (UDisksDaemon *daemon)
{
  UDisksModuleManager *manager = udisks_daemon_get_module_manager (daemon);
  UDisksBTRFSState *state_pointer = (UDisksBTRFSState *) \
                                     udisks_module_manager_get_module_state_pointer (manager,
                                                                                     BTRFS_MODULE_NAME);

  udisks_btrfs_state_free (state_pointer);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
btrfs_block_check (UDisksObject *object)
{
  const gchar *fs_type = NULL;
  UDisksLinuxDevice *device = NULL;
  int rc = 0;

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  /* Check filesystem type from udev property. */
  device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));
  fs_type = g_udev_device_get_property (device->udev_device, "ID_FS_TYPE");

  rc = g_strcmp0 (fs_type, "btrfs");
  g_object_unref(device);

  return rc == 0;
}

static void
btrfs_block_connect (UDisksObject *object)
{
}

static gboolean
btrfs_block_update (UDisksObject   *object,
                    const gchar    *uevent_action,
                    GDBusInterface *_iface)
{
  return udisks_linux_filesystem_btrfs_update (UDISKS_LINUX_FILESYSTEM_BTRFS (_iface),
                                               UDISKS_LINUX_BLOCK_OBJECT (object));
}

UDisksModuleInterfaceInfo **
udisks_module_get_block_object_iface_setup_entries (void)
{
  UDisksModuleInterfaceInfo **iface;

  iface = g_new0 (UDisksModuleInterfaceInfo *, 2);
  iface[0] = g_new0 (UDisksModuleInterfaceInfo, 1);
  iface[0]->has_func = &btrfs_block_check;
  iface[0]->connect_func = &btrfs_block_connect;
  iface[0]->update_func = &btrfs_block_update;
  iface[0]->skeleton_type = UDISKS_TYPE_LINUX_FILESYSTEM_BTRFS;

  return iface;
}

/* ---------------------------------------------------------------------------------------------------- */

UDisksModuleInterfaceInfo **
udisks_module_get_drive_object_iface_setup_entries (void)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

UDisksModuleObjectNewFunc *
udisks_module_get_object_new_funcs (void)
{
  UDisksModuleObjectNewFunc *funcs = NULL;

  /* TODO: Add object_new funcs */
  /* funcs = g_new0 (UDisksModuleObjectNewFunc, 2); */

  return funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
new_manager_btrfs_manager_iface (UDisksDaemon *daemon)
{
  UDisksLinuxManagerBTRFS *manager;

  manager = udisks_linux_manager_btrfs_new (daemon);

  return G_DBUS_INTERFACE_SKELETON (manager);
}

UDisksModuleNewManagerIfaceFunc *
udisks_module_get_new_manager_iface_funcs (void)
{
  UDisksModuleNewManagerIfaceFunc *funcs;

  funcs = g_new0 (UDisksModuleNewManagerIfaceFunc, 2);
  funcs[0] = &new_manager_btrfs_manager_iface;

  return funcs;
}
