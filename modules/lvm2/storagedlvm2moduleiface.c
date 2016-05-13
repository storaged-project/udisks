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

#include <modules/storagedmoduleiface.h>

#include <storaged/storaged-generated.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storagedlinuxdriveobject.h>
#include <src/storagedlinuxdevice.h>
#include <src/storageddaemon.h>
#include <src/storagedmodulemanager.h>
#include <src/storagedlogging.h>

#include "storaged-lvm2-generated.h"
#include "storagedlvm2types.h"
#include "storagedlvm2dbusutil.h"
#include "storagedlvm2daemonutil.h"
#include "storagedlvm2state.h"

#include "storagedlinuxmanagerlvm2.h"
#include "storagedlinuxvolumegroupobject.h"
#include "storagedlinuxphysicalvolume.h"


/* ---------------------------------------------------------------------------------------------------- */

gchar *
storaged_module_id (void)
{
  return g_strdup (LVM2_MODULE_NAME);
}

gpointer
storaged_module_init (StoragedDaemon *daemon)
{
  return storaged_lvm2_state_new (daemon);
}

void
storaged_module_teardown (StoragedDaemon *daemon)
{
  StoragedModuleManager *manager = storaged_daemon_get_module_manager (daemon);
  StoragedLVM2State *state_pointer = (StoragedLVM2State *) \
                                      storaged_module_manager_get_module_state_pointer (manager,
                                                                                        LVM2_MODULE_NAME);

  storaged_lvm2_state_free (state_pointer);
}

/* ---------------------------------------------------------------------------------------------------- */

static StoragedLVM2State *
get_module_state (StoragedDaemon *daemon)
{
  StoragedLVM2State *state;
  StoragedModuleManager *manager;

  manager = storaged_daemon_get_module_manager (daemon);
  g_assert (manager != NULL);

  state = (StoragedLVM2State *) storaged_module_manager_get_module_state_pointer (manager, LVM2_MODULE_NAME);
  g_assert (state != NULL);

  return state;
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

static void
lvm_update_from_variant (GPid      pid,
                         GVariant *volume_groups,
                         GError   *error,
                         gpointer  user_data)
{
  StoragedLVM2State *state;
  StoragedDaemon *daemon = STORAGED_DAEMON (user_data);
  GDBusObjectManagerServer *manager;
  GVariantIter var_iter;
  GHashTableIter vg_name_iter;
  gpointer key, value;
  const gchar *name;

  if (error != NULL)
    {
      storaged_warning ("LVM2 plugin: %s", error->message);
      return;
    }

  manager = storaged_daemon_get_object_manager (daemon);
  state = get_module_state (daemon);

  /* Remove obsolete groups */
  g_hash_table_iter_init (&vg_name_iter,
                          storaged_lvm2_state_get_name_to_volume_group (state));
  while (g_hash_table_iter_next (&vg_name_iter, &key, &value))
    {
      const gchar *vg;
      StoragedLinuxVolumeGroupObject *group;
      gboolean found = FALSE;

      name = key;
      group = value;

      g_variant_iter_init (&var_iter, volume_groups);
      while (g_variant_iter_next (&var_iter, "&s", &vg))
        if (g_strcmp0 (vg, name) == 0)
          {
            found = TRUE;
            break;
          }

      if (!found)
        {
          storaged_linux_volume_group_object_destroy (group);
          g_dbus_object_manager_server_unexport (manager,
                                                 g_dbus_object_get_object_path (G_DBUS_OBJECT (group)));
          g_object_unref (G_OBJECT (group));
          g_hash_table_iter_remove (&vg_name_iter);
        }
    }

  /* Add new groups and update existing groups */
  g_variant_iter_init (&var_iter, volume_groups);
  while (g_variant_iter_next (&var_iter, "&s", &name))
    {
      StoragedLinuxVolumeGroupObject *group;
      group = g_hash_table_lookup (storaged_lvm2_state_get_name_to_volume_group (state),
                                   name);

      if (group == NULL)
        {
          group = storaged_linux_volume_group_object_new (daemon, name);
          g_hash_table_insert (storaged_lvm2_state_get_name_to_volume_group (state),
                               g_strdup (name), group);
        }
      storaged_linux_volume_group_object_update (group);
    }
}

static void
lvm_update (StoragedDaemon *daemon,
            gboolean        ignore_locks)
{
  const gchar *args[5];
  int i = 0;

  if (storaged_daemon_get_uninstalled (daemon))
    args[i++] = BUILD_DIR "modules/lvm2/storaged-lvm";
  else
    args[i++] = LVM_HELPER_DIR "storaged-lvm";
  args[i++] = "-b";
  if (ignore_locks)
    args[i++] = "-f";
  args[i++] = "list";
  args[i++] = NULL;

  storaged_daemon_util_lvm2_spawn_for_variant (args, G_VARIANT_TYPE("as"),
                                             lvm_update_from_variant, daemon);
}

static gboolean
delayed_lvm_update (gpointer user_data)
{
  StoragedDaemon *daemon = STORAGED_DAEMON (user_data);
  StoragedLVM2State *state;

  state = get_module_state (daemon);

  lvm_update (daemon, FALSE);
  storaged_lvm2_state_set_lvm_delayed_update_id (state, 0);
  return FALSE;
}

static void
trigger_delayed_lvm_update (StoragedDaemon *daemon)
{
  StoragedLVM2State *state;

  state = get_module_state (daemon);

  if (storaged_lvm2_state_get_lvm_delayed_update_id (state) > 0)
    return;

  if (! storaged_lvm2_state_get_coldplug_done (state))
    {
      /* Spawn immediately and ignore locks when doing coldplug, i.e. when lvm2 module
       * has just been activated. This is not 100% effective as this affects only the
       * first request but from the plugin nature we don't know whether coldplugging
       * has been finished or not. Might be subject to change in the future. */
      storaged_lvm2_state_set_coldplug_done (state, TRUE);
      lvm_update (daemon, TRUE);
    }
  else
    {
      storaged_lvm2_state_set_lvm_delayed_update_id (state,
                                                     g_timeout_add (100,
                                                                    delayed_lvm_update,
                                                                    daemon));
    }
}

static gboolean
is_logical_volume (StoragedLinuxDevice *device)
{
  const gchar *dm_vg_name = g_udev_device_get_property (device->udev_device, "DM_VG_NAME");
  return dm_vg_name && *dm_vg_name;
}

static gboolean
has_physical_volume_label (StoragedLinuxDevice *device)
{
  const gchar *id_fs_type = g_udev_device_get_property (device->udev_device, "ID_FS_TYPE");
  return g_strcmp0 (id_fs_type, "LVM2_member") == 0;
}

static gboolean
is_recorded_as_physical_volume (StoragedDaemon      *daemon,
                                StoragedLinuxDevice *device)
{
  StoragedObject *object = storaged_daemon_find_block (daemon, g_udev_device_get_device_number (device->udev_device));
  return object && storaged_object_peek_physical_volume (object) != NULL;
}

static GDBusObjectSkeleton *
lvm2_object_new (StoragedDaemon      *daemon,
                 StoragedLinuxDevice *device)
{
  /* This is bit of a hack. We never return any instance and thus effectively
   * taking the StoragedLinuxProvider module uevent machinery out of sight. We
   * only get an uevent and related StoragedLinuxDevice where we perform basic
   * checks if the device could be related to LVM and schedule a probe. We take
   * reference to StoragedDaemon instance though for manually performing dbus
   * stuff onto. */

  if (is_logical_volume (device)
      || has_physical_volume_label (device)
      || is_recorded_as_physical_volume (daemon, device))
    trigger_delayed_lvm_update (daemon);

  return NULL;
}

StoragedModuleObjectNewFunc *
storaged_module_get_object_new_funcs (void)
{
  StoragedModuleObjectNewFunc *funcs;

  funcs = g_new0 (StoragedModuleObjectNewFunc, 2);
  funcs[0] = &lvm2_object_new;

  return funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
new_manager_iface (StoragedDaemon *daemon)
{
  StoragedLinuxManagerLVM2 *manager;

  manager = storaged_linux_manager_lvm2_new (daemon);

  return G_DBUS_INTERFACE_SKELETON (manager);
}

StoragedModuleNewManagerIfaceFunc *
storaged_module_get_new_manager_iface_funcs (void)
{
  StoragedModuleNewManagerIfaceFunc *funcs;

  funcs = g_new0 (StoragedModuleNewManagerIfaceFunc, 2);
  funcs[0] = &new_manager_iface;

  return funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
storaged_module_track_parent (StoragedDaemon *daemon,
                              const gchar    *path,
                              gchar         **uuid_ret)
{
  const gchar *parent_path = NULL;
  const gchar *parent_uuid = NULL;

  StoragedObject *object;
  StoragedObject *lvol_object;
  StoragedBlockLVM2 *block_lvm2;
  StoragedLogicalVolume *lvol;

  object = storaged_daemon_find_object (daemon, path);
  if (object == NULL)
    goto out;

  block_lvm2 = storaged_object_peek_block_lvm2 (object);
  if (block_lvm2)
    {
      lvol_object = storaged_daemon_find_object (daemon, storaged_block_lvm2_get_logical_volume (block_lvm2));
      if (lvol_object)
        {
          lvol = storaged_object_peek_logical_volume (lvol_object);
          if (lvol)
            {
              parent_uuid = storaged_logical_volume_get_uuid (lvol);
              parent_path = storaged_block_lvm2_get_logical_volume (block_lvm2);
              goto out;
            }
        }
    }

 out:
  g_clear_object (&object);
  if (uuid_ret)
    *uuid_ret = g_strdup (parent_uuid);
  return g_strdup (parent_path);
}
