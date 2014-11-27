/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014 Tomas Bzatek <tbzatek@redhat.com>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

#include <modules/udisksmoduleiface.h>

#include <udisks/udisks-generated.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdriveobject.h>
#include <src/udiskslinuxdevice.h>

#include "dummy-generated.h"
#include "dummytypes.h"
#include "dummylinuxblock.h"
#include "dummylinuxdrive.h"
#include "dummyloopobject.h"
#include "dummylinuxmanager.h"


/* ---------------------------------------------------------------------------------------------------- */

gpointer
udisks_module_init (gchar **module_id)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
block_check (UDisksObject *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  /* further tests whether to attach the interface should take place here */

  return TRUE;
}

static void
block_connect (UDisksObject *object)
{
}

static gboolean
block_update (UDisksObject   *object,
              const gchar    *uevent_action,
              GDBusInterface *_iface)
{
  return dummy_linux_block_update (DUMMY_LINUX_BLOCK (_iface), UDISKS_LINUX_BLOCK_OBJECT (object));
}


/* ---------------------------------------------------------------------------------------------------- */

static gboolean
drive_check (UDisksObject *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_OBJECT (object), FALSE);

  /* further tests whether to attach the interface should take place here */

  return TRUE;
}

static void
drive_connect (UDisksObject *object)
{
}

static gboolean
drive_update (UDisksObject   *object,
              const gchar    *uevent_action,
              GDBusInterface *_iface)
{
  return dummy_linux_drive_update (DUMMY_LINUX_DRIVE (_iface), UDISKS_LINUX_DRIVE_OBJECT (object));
}

/* ---------------------------------------------------------------------------------------------------- */

UDisksModuleInterfaceInfo **
udisks_module_get_block_object_iface_setup_entries (void)
{
  UDisksModuleInterfaceInfo **iface;

  iface = g_new0 (UDisksModuleInterfaceInfo *, 2);
  iface[0] = g_new0 (UDisksModuleInterfaceInfo, 1);
  iface[0]->has_func = &block_check;
  iface[0]->connect_func = &block_connect;
  iface[0]->update_func = &block_update;
  iface[0]->skeleton_type = DUMMY_TYPE_LINUX_BLOCK;

  return iface;
}

UDisksModuleInterfaceInfo **
udisks_module_get_drive_object_iface_setup_entries (void)
{
  UDisksModuleInterfaceInfo **iface;

  iface = g_new0 (UDisksModuleInterfaceInfo *, 2);
  iface[0] = g_new0 (UDisksModuleInterfaceInfo, 1);
  iface[0]->has_func = &drive_check;
  iface[0]->connect_func = &drive_connect;
  iface[0]->update_func = &drive_update;
  iface[0]->skeleton_type = DUMMY_TYPE_LINUX_DRIVE;

  return iface;
}


/* ---------------------------------------------------------------------------------------------------- */

static GDBusObjectSkeleton *
dummy_object_new (UDisksDaemon      *daemon,
                  UDisksLinuxDevice *device)
{
  DummyLoopObject *object;

  object = dummy_loop_object_new (daemon, device);

  if (object)
    return G_DBUS_OBJECT_SKELETON (object);
  else
    return NULL;
}

UDisksModuleObjectNewFunc *
udisks_module_get_object_new_funcs (void)
{
  UDisksModuleObjectNewFunc *funcs;

  funcs = g_new0 (UDisksModuleObjectNewFunc, 2);
  funcs[0] = &dummy_object_new;

  return funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
dummy_new_manager_iface (UDisksDaemon *daemon)
{
  DummyLinuxManager *manager;

  manager = dummy_linux_manager_new ();

  return G_DBUS_INTERFACE_SKELETON (manager);
}

UDisksModuleNewManagerIfaceFunc *
udisks_module_get_new_manager_iface_funcs (void)
{
  UDisksModuleNewManagerIfaceFunc *funcs;

  funcs = g_new0 (UDisksModuleNewManagerIfaceFunc, 2);
  funcs[0] = &dummy_new_manager_iface;

  return funcs;
}
