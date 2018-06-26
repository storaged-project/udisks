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

#include <blockdev/blockdev.h>
#include <blockdev/lvm.h>

#include <modules/udisksmoduleiface.h>

#include <udisks/udisks-generated.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdriveobject.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksdaemon.h>
#include <src/udisksmodulemanager.h>
#include <src/udiskslogging.h>

#include "udisks-lvm2-generated.h"
#include "udiskslvm2types.h"
#include "udiskslvm2dbusutil.h"
#include "udiskslvm2daemonutil.h"
#include "udiskslvm2state.h"
#include "jobhelpers.h"

#include "udiskslinuxmanagerlvm2.h"
#include "udiskslinuxvolumegroupobject.h"
#include "udiskslinuxphysicalvolume.h"


/* ---------------------------------------------------------------------------------------------------- */

gchar *
udisks_module_id (void)
{
  return g_strdup (LVM2_MODULE_NAME);
}

gpointer
udisks_module_init (UDisksDaemon *daemon)
{
  gboolean ret = FALSE;
  GError *error = NULL;

  BDPluginSpec lvm_plugin = {BD_PLUGIN_LVM, "libbd_lvm.so.2"};
  BDPluginSpec *plugins[] = {&lvm_plugin, NULL};

  if (!bd_is_plugin_available (BD_PLUGIN_LVM))
    {
      ret = bd_reinit (plugins, FALSE, NULL, &error);
      if (!ret)
        {
          udisks_error ("Error initializing the lvm libblockdev plugin: %s (%s, %d)",
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
          /* XXX: can do nothing more here even though we know the module will be unusable! */
        }
    }

  return udisks_lvm2_state_new (daemon);
}

void
udisks_module_teardown (UDisksDaemon *daemon)
{
  UDisksModuleManager *manager = udisks_daemon_get_module_manager (daemon);
  UDisksLVM2State *state_pointer = (UDisksLVM2State *) \
                                    udisks_module_manager_get_module_state_pointer (manager,
                                                                                    LVM2_MODULE_NAME);

  udisks_lvm2_state_free (state_pointer);
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksLVM2State *
get_module_state (UDisksDaemon *daemon)
{
  UDisksLVM2State *state;
  UDisksModuleManager *manager;

  manager = udisks_daemon_get_module_manager (daemon);
  g_assert (manager != NULL);

  state = (UDisksLVM2State *) udisks_module_manager_get_module_state_pointer (manager, LVM2_MODULE_NAME);
  g_assert (state != NULL);

  return state;
}

/* ---------------------------------------------------------------------------------------------------- */

UDisksModuleInterfaceInfo **
udisks_module_get_block_object_iface_setup_entries (void)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

UDisksModuleInterfaceInfo **
udisks_module_get_drive_object_iface_setup_entries (void)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
lvm_update_vgs (GObject      *source_obj,
                GAsyncResult *result,
                gpointer      user_data)
{
  UDisksLVM2State *state;
  UDisksDaemon *daemon = UDISKS_DAEMON (source_obj);
  GDBusObjectManagerServer *manager;

  GTask *task = G_TASK (result);
  GError *error = NULL;
  VGsPVsData *data = g_task_propagate_pointer (task, &error);
  BDLVMVGdata **vgs = NULL;
  BDLVMPVdata **pvs = NULL;

  GHashTableIter vg_name_iter;
  gpointer key, value;
  const gchar *vg_name;

  if (!data)
    {
      if (error)
        {
          udisks_warning ("LVM2 plugin: %s", error->message);
          g_clear_error (&error);
        }
      else
        /* this should never happen */
        udisks_warning ("LVM2 plugin: failure but no error when getting VGs!");

      return;
    }
  vgs = data->vgs;
  pvs = data->pvs;

  /* free the data container (but not 'vgs' and 'pvs') */
  g_free (data);

  manager = udisks_daemon_get_object_manager (daemon);
  state = get_module_state (daemon);

  /* Remove obsolete groups */
  g_hash_table_iter_init (&vg_name_iter,
                          udisks_lvm2_state_get_name_to_volume_group (state));
  while (g_hash_table_iter_next (&vg_name_iter, &key, &value))
    {
      UDisksLinuxVolumeGroupObject *group;
      gboolean found = FALSE;

      vg_name = key;
      group = value;

      for (BDLVMVGdata **vgs_p=vgs; !found && (*vgs_p); vgs_p++)
          found = g_strcmp0 ((*vgs_p)->name, vg_name) == 0;

      if (!found)
        {
          udisks_linux_volume_group_object_destroy (group);
          g_dbus_object_manager_server_unexport (manager,
                                                 g_dbus_object_get_object_path (G_DBUS_OBJECT (group)));
          g_hash_table_iter_remove (&vg_name_iter);
        }
    }

  /* Add new groups and update existing groups */
  for (BDLVMVGdata **vgs_p=vgs; *vgs_p; vgs_p++)
    {
      UDisksLinuxVolumeGroupObject *group;
      GSList *vg_pvs = NULL;
      vg_name = (*vgs_p)->name;
      group = g_hash_table_lookup (udisks_lvm2_state_get_name_to_volume_group (state),
                                   vg_name);

      if (group == NULL)
        {
          group = udisks_linux_volume_group_object_new (daemon, vg_name);
          g_hash_table_insert (udisks_lvm2_state_get_name_to_volume_group (state),
                               g_strdup (vg_name), group);
        }

      for (BDLVMPVdata **pvs_p=pvs; *pvs_p; pvs_p++)
        if (g_strcmp0 ((*pvs_p)->vg_name, vg_name) == 0)
            vg_pvs = g_slist_prepend (vg_pvs, *pvs_p);

      udisks_linux_volume_group_object_update (group, *vgs_p, vg_pvs);
    }

  /* this is safe to do -- all BDLVMPVdata objects are still existing because
     the function that frees them is scheduled in main loop by the
     udisks_linux_volume_group_object_update() call above */
  for (BDLVMPVdata **pvs_p=pvs; *pvs_p; pvs_p++)
    if ((*pvs_p)->vg_name == NULL)
      bd_lvm_pvdata_free (*pvs_p);

  /* only free the containers, the contents were passed further */
  g_free (vgs);
  g_free (pvs);
}

static void
lvm_update (UDisksDaemon *daemon)
{
  /* the callback (lvm_update_vgs) is called in the default main loop (context) */
  GTask *task = g_task_new (daemon, NULL /* cancellable */, lvm_update_vgs, NULL /* callback_data */);

  /* holds a reference to 'task' until it is finished */
  g_task_run_in_thread (task, (GTaskThreadFunc) vgs_task_func);

  g_object_unref (task);
}

static gboolean
delayed_lvm_update (gpointer user_data)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (user_data);
  UDisksLVM2State *state;

  state = get_module_state (daemon);

  lvm_update (daemon);
  udisks_lvm2_state_set_lvm_delayed_update_id (state, 0);
  return FALSE;
}

static void
trigger_delayed_lvm_update (UDisksDaemon *daemon)
{
  UDisksLVM2State *state;

  state = get_module_state (daemon);

  if (udisks_lvm2_state_get_lvm_delayed_update_id (state) > 0)
    return;

  if (! udisks_lvm2_state_get_coldplug_done (state))
    {
      /* Update immediately when doing coldplug, i.e. when lvm2 module has just
       * been activated. This is not 100% effective as this affects only the
       * first request but from the plugin nature we don't know whether
       * coldplugging has been finished or not. Might be subject to change in
       * the future. */
      udisks_lvm2_state_set_coldplug_done (state, TRUE);
      lvm_update (daemon);
    }
  else
    {
      udisks_lvm2_state_set_lvm_delayed_update_id (state,
                                                   g_timeout_add (100,
                                                                  delayed_lvm_update,
                                                                  daemon));
    }
}

static gboolean
is_logical_volume (UDisksLinuxDevice *device)
{
  const gchar *dm_vg_name = g_udev_device_get_property (device->udev_device, "DM_VG_NAME");
  return dm_vg_name && *dm_vg_name;
}

static gboolean
has_physical_volume_label (UDisksLinuxDevice *device)
{
  const gchar *id_fs_type = g_udev_device_get_property (device->udev_device, "ID_FS_TYPE");
  return g_strcmp0 (id_fs_type, "LVM2_member") == 0;
}

static gboolean
is_recorded_as_physical_volume (UDisksDaemon      *daemon,
                                UDisksLinuxDevice *device)
{
  UDisksObject *object = udisks_daemon_find_block (daemon, g_udev_device_get_device_number (device->udev_device));
  return object && udisks_object_peek_physical_volume (object) != NULL;
}

static GDBusObjectSkeleton *
lvm2_object_new (UDisksDaemon      *daemon,
                 UDisksLinuxDevice *device)
{
  /* This is bit of a hack. We never return any instance and thus effectively
   * taking the UDisksLinuxProvider module uevent machinery out of sight. We
   * only get an uevent and related UDisksLinuxDevice where we perform basic
   * checks if the device could be related to LVM and schedule a probe. We take
   * reference to UDisksDaemon instance though for manually performing dbus
   * stuff onto. */

  if (is_logical_volume (device)
      || has_physical_volume_label (device)
      || is_recorded_as_physical_volume (daemon, device))
    trigger_delayed_lvm_update (daemon);

  return NULL;
}

UDisksModuleObjectNewFunc *
udisks_module_get_object_new_funcs (void)
{
  UDisksModuleObjectNewFunc *funcs;

  funcs = g_new0 (UDisksModuleObjectNewFunc, 2);
  funcs[0] = &lvm2_object_new;

  return funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
new_manager_iface (UDisksDaemon *daemon)
{
  UDisksLinuxManagerLVM2 *manager;

  manager = udisks_linux_manager_lvm2_new (daemon);

  return G_DBUS_INTERFACE_SKELETON (manager);
}

UDisksModuleNewManagerIfaceFunc *
udisks_module_get_new_manager_iface_funcs (void)
{
  UDisksModuleNewManagerIfaceFunc *funcs;

  funcs = g_new0 (UDisksModuleNewManagerIfaceFunc, 2);
  funcs[0] = &new_manager_iface;

  return funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
udisks_module_track_parent (UDisksDaemon  *daemon,
                            const gchar   *path,
                            gchar        **uuid_ret)
{
  const gchar *parent_path = NULL;
  const gchar *parent_uuid = NULL;

  UDisksObject *object;
  UDisksObject *lvol_object;
  UDisksBlockLVM2 *block_lvm2;
  UDisksLogicalVolume *lvol;

  object = udisks_daemon_find_object (daemon, path);
  if (object == NULL)
    goto out;

  block_lvm2 = udisks_object_peek_block_lvm2 (object);
  if (block_lvm2)
    {
      lvol_object = udisks_daemon_find_object (daemon, udisks_block_lvm2_get_logical_volume (block_lvm2));
      if (lvol_object)
        {
          lvol = udisks_object_peek_logical_volume (lvol_object);
          if (lvol)
            {
              parent_uuid = udisks_logical_volume_get_uuid (lvol);
              parent_path = udisks_block_lvm2_get_logical_volume (block_lvm2);
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
