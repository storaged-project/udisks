/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Samikshan Bairagya <sbairagy@redhat.com>
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
#include <src/storagedlinuxdevice.h>
#include <src/storagedlogging.h>
#include <src/storagedmodulemanager.h>

#include "storagedglusterfstypes.h"
#include "storagedglusterfsstate.h"
#include "storagedglusterfsutils.h"
#include "storagedglusterfsinfo.h"
#include "storagedlinuxmanagerglusterd.h"
#include "storagedlinuxglusterfsvolumeobject.h"

/* ---------------------------------------------------------------------------------------------------- */

gchar *
storaged_module_id (void)
{
  return g_strdup (GLUSTERFS_MODULE_NAME);
}

gpointer
storaged_module_init (StoragedDaemon *daemon)
{
  return storaged_glusterfs_state_new (daemon);
}

void
storaged_module_teardown (StoragedDaemon *daemon)
{
  StoragedModuleManager *manager = storaged_daemon_get_module_manager (daemon);
  StoragedGlusterFSState *state_pointer = (StoragedGlusterFSState *) \
                                       storaged_module_manager_get_module_state_pointer (manager,
                                                                                         GLUSTERFS_MODULE_NAME);

  storaged_glusterfs_state_free (state_pointer);
}

/* ---------------------------------------------------------------------------------------------------- */

StoragedModuleInterfaceInfo **
storaged_module_get_block_object_iface_setup_entries (void)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

StoragedModuleInterfaceInfo **
storaged_module_get_drive_object_iface_setup_entries (void)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */


static GDBusObjectSkeleton *
glusterfs_object_new (StoragedDaemon *daemon)
{
  storaged_debug ("glusterfs_object_new");
  storaged_glusterfs_volumes_update (daemon);
  return NULL;
}


StoragedModuleObjectNewFunc *
storaged_module_get_object_new_funcs (void)
{
  StoragedModuleObjectNewFunc *funcs = NULL;

  funcs = g_new0 (StoragedModuleObjectNewFunc, 2);
  funcs[0] = &glusterfs_object_new;

  return funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
new_manager_glusterd_iface (StoragedDaemon *daemon)
{
  StoragedLinuxManagerGlusterD *manager;

  manager = storaged_linux_manager_glusterd_new (daemon);

  return G_DBUS_INTERFACE_SKELETON (manager);
}

StoragedModuleNewManagerIfaceFunc *
storaged_module_get_new_manager_iface_funcs (void)
{
  StoragedModuleNewManagerIfaceFunc *funcs;

  funcs = g_new0 (StoragedModuleNewManagerIfaceFunc, 2);
  funcs[0] = &new_manager_glusterd_iface;

  return funcs;
}

