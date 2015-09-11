/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Dominika Hodovska <dhodovsk@redhat.com>
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

#include <modules/storagedmoduleiface.h>

#include <storaged/storaged-generated.h>
#include <src/storageddaemon.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlogging.h>
#include <src/storagedmodulemanager.h>

#include "storagedzramtypes.h"
#include "storagedzramstate.h"

#include "storagedlinuxblockzram.h"
#include "storagedlinuxmanagerzram.h"

gchar *
storaged_module_id (void)
{
  return g_strdup (ZRAM_MODULE_NAME);
}

gpointer
storaged_module_init (StoragedDaemon *daemon)
{
  return storaged_zram_state_new (daemon);
}

void
storaged_module_teardown (StoragedDaemon *daemon)
{
  StoragedModuleManager *manager = storaged_daemon_get_module_manager (daemon);
  StoragedZRAMState *state_pointer = (StoragedZRAMState *) \
                                      storaged_module_manager_get_module_state_pointer (manager,
                                                                                        ZRAM_MODULE_NAME);

  storaged_zram_state_free (state_pointer);
}

/* ------------------------------------------------------------------------------------ */

static gboolean
zram_block_check (StoragedObject *object)
{
  const gchar *devname = NULL;
  StoragedLinuxDevice *device = NULL;
  gboolean rval = FALSE;

  g_return_val_if_fail (STORAGED_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  /* Check device name */
  device = storaged_linux_block_object_get_device (STORAGED_LINUX_BLOCK_OBJECT (object));
  devname = g_strdup (g_udev_device_get_device_file (device->udev_device));

  rval = g_str_has_prefix (devname, "/dev/zram");

  g_free ((gpointer) devname);

  return rval;
}

static void
zram_block_connect (StoragedObject *object)
{
}

static gboolean
zram_block_update (StoragedObject  *object,
                   const gchar     *uevent_action,
                   GDBusInterface  *_iface)
{
  return storaged_linux_block_zram_update (STORAGED_LINUX_BLOCK_ZRAM (_iface),
                                           STORAGED_LINUX_BLOCK_OBJECT (object));
}

StoragedModuleInterfaceInfo **
storaged_module_get_block_object_iface_setup_entries (void)
{
  StoragedModuleInterfaceInfo **iface;

  iface = g_new0 (StoragedModuleInterfaceInfo *, 2);
  iface[0] = g_new0 (StoragedModuleInterfaceInfo, 1);
  iface[0]->has_func = &zram_block_check;
  iface[0]->connect_func = &zram_block_connect;
  iface[0]->update_func = &zram_block_update;
  iface[0]->skeleton_type = STORAGED_TYPE_LINUX_BLOCK_ZRAM;

  return iface;
}

/* ------------------------------------------------------------------------------------ */

StoragedModuleInterfaceInfo **
storaged_module_get_drive_object_iface_setup_entries (void)
{
  return NULL;
}

StoragedModuleObjectNewFunc *
storaged_module_get_object_new_funcs (void)
{
  return NULL;
}

/* ------------------------------------------------------------------------------------ */

static GDBusInterfaceSkeleton *
new_manager_zram_manager_iface (StoragedDaemon *daemon)
{
  StoragedLinuxManagerZRAM *manager;

  manager = storaged_linux_manager_zram_new (daemon);

  return G_DBUS_INTERFACE_SKELETON (manager);
}

StoragedModuleNewManagerIfaceFunc *
storaged_module_get_new_manager_iface_funcs (void)
{
  StoragedModuleNewManagerIfaceFunc *funcs;

  funcs = g_new0 (StoragedModuleNewManagerIfaceFunc, 2);
  funcs[0] = &new_manager_zram_manager_iface;

  return funcs;
}
