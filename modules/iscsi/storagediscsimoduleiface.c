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

#include <modules/storagedmoduleiface.h>

#include <storaged/storaged-generated.h>
#include <src/storageddaemon.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlogging.h>
#include <src/storagedmodulemanager.h>

#include "storagediscsitypes.h"
#include "storagediscsistate.h"

#include "storagedlinuxiscsisessionobject.h"
#include "storagedlinuxmanageriscsiinitiator.h"

/* ---------------------------------------------------------------------------------------------------- */

gchar *
storaged_module_id (void)
{
  return g_strdup (ISCSI_MODULE_NAME);
}

gpointer
storaged_module_init (StoragedDaemon *daemon)
{
  return storaged_iscsi_state_new (daemon);
}

void
storaged_module_teardown (StoragedDaemon *daemon)
{
  StoragedModuleManager *manager = storaged_daemon_get_module_manager (daemon);
  StoragedISCSIState *state_pointer = (StoragedISCSIState *) \
                                       storaged_module_manager_get_module_state_pointer (manager,
                                                                                         ISCSI_MODULE_NAME);

  storaged_iscsi_state_free (state_pointer);
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

#ifdef HAVE_LIBISCSI_GET_SESSION_INFOS

static GDBusObjectSkeleton *
iscsi_session_object_new (StoragedDaemon       *daemon,
                          StoragedLinuxDevice  *device)
{
  StoragedLinuxISCSISessionObject *session_object = NULL;

  GDBusObjectManagerServer *object_manager_server = NULL;
  GDBusObject *object = NULL;
  const gchar *sysfs_path = NULL;
  gchar *session_id = NULL;
  gchar *object_path = NULL;

  /* Session ID */
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);
  session_id = storaged_linux_iscsi_session_object_get_session_id_from_sysfs_path (sysfs_path);

  if (session_id)
    {
      /* Check, if such object exists. */
      object_manager_server = storaged_daemon_get_object_manager (daemon);
      object_path = storaged_linux_iscsi_session_object_make_object_path (session_id);
      object = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (object_manager_server),
                                                 object_path);
      g_free (object_path);

      if (! object)
        {
          /* Create a new DBus object. */
          session_object = storaged_linux_iscsi_session_object_new (daemon, session_id);
        }
    }

  g_free (session_id);

  if (session_object)
    return G_DBUS_OBJECT_SKELETON (session_object);
  return NULL;
}

#endif /* HAVE_LIBISCSI_GET_SESSION_INFOS */

StoragedModuleObjectNewFunc *
storaged_module_get_object_new_funcs (void)
{
  StoragedModuleObjectNewFunc *funcs = NULL;

  funcs = g_new0 (StoragedModuleObjectNewFunc, 2);
#ifdef HAVE_LIBISCSI_GET_SESSION_INFOS
  funcs[0] = iscsi_session_object_new;
#endif /* HAVE_LIBISCSI_GET_SESSION_INFOS */

  return funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
new_manager_initiator_iface (StoragedDaemon *daemon)
{
  StoragedLinuxManagerISCSIInitiator *manager;

  manager = storaged_linux_manager_iscsi_initiator_new (daemon);

  return G_DBUS_INTERFACE_SKELETON (manager);
}

StoragedModuleNewManagerIfaceFunc *
storaged_module_get_new_manager_iface_funcs (void)
{
  StoragedModuleNewManagerIfaceFunc *funcs;

  funcs = g_new0 (StoragedModuleNewManagerIfaceFunc, 2);
  funcs[0] = &new_manager_initiator_iface;

  return funcs;
}
