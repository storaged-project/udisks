/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2020 Tomas Bzatek <tbzatek@redhat.com>
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

#include <blockdev/blockdev.h>
#include <blockdev/lvm.h>

#include <src/udisksdaemon.h>
#include <src/udiskslinuxprovider.h>
#include <src/udiskslogging.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodulemanager.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>

#include "udiskslvm2types.h"
#include "udiskslinuxmodulelvm2.h"
#include "udiskslinuxmanagerlvm2.h"
#include "udiskslinuxvolumegroup.h"
#include "udiskslinuxvolumegroupobject.h"
#include "jobhelpers.h"

/**
 * SECTION:udiskslinuxmodulelvm2
 * @title: UDisksLinuxModuleLVM2
 * @short_description: LVM2 module.
 *
 * The LVM2 module.
 */

/**
 * UDisksLinuxModuleLVM2:
 *
 * The #UDisksLinuxModuleLVM2 structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _UDisksLinuxModuleLVM2 {
  UDisksModule parent_instance;

  /* maps from volume group name to UDisksLinuxVolumeGroupObject instances. */
  GHashTable *name_to_volume_group;

  gint delayed_update_id;
};

typedef struct _UDisksLinuxModuleLVM2Class UDisksLinuxModuleLVM2Class;

struct _UDisksLinuxModuleLVM2Class {
  UDisksModuleClass parent_class;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxModuleLVM2, udisks_linux_module_lvm2, UDISKS_TYPE_MODULE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));


static void
udisks_linux_module_lvm2_init (UDisksLinuxModuleLVM2 *module)
{
  g_return_if_fail (UDISKS_IS_LINUX_MODULE_LVM2 (module));
}

static void
udisks_linux_module_lvm2_constructed (GObject *object)
{
  UDisksLinuxModuleLVM2 *module = UDISKS_LINUX_MODULE_LVM2 (object);

  module->name_to_volume_group = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_object_unref);

  if (G_OBJECT_CLASS (udisks_linux_module_lvm2_parent_class)->constructed)
    G_OBJECT_CLASS (udisks_linux_module_lvm2_parent_class)->constructed (object);
}

static void
udisks_linux_module_lvm2_finalize (GObject *object)
{
  UDisksLinuxModuleLVM2 *module = UDISKS_LINUX_MODULE_LVM2 (object);

  g_hash_table_unref (module->name_to_volume_group);

  if (G_OBJECT_CLASS (udisks_linux_module_lvm2_parent_class)->finalize)
    G_OBJECT_CLASS (udisks_linux_module_lvm2_parent_class)->finalize (object);
}

gchar *
udisks_module_id (void)
{
  return g_strdup (LVM2_MODULE_NAME);
}

/**
 * udisks_module_lvm2_new:
 * @daemon: A #UDisksDaemon.
 * @cancellable: (nullable): A #GCancellable or %NULL
 * @error: Return location for error or %NULL.
 *
 * Creates new #UDisksLinuxModuleLVM2 object.
 *
 * Returns: (transfer full) (type UDisksLinuxModuleLVM2): A
 *   #UDisksLinuxModuleLVM2 object or %NULL if @error is set. Free
 *   with g_object_unref().
 */
UDisksModule *
udisks_module_lvm2_new (UDisksDaemon  *daemon,
                        GCancellable  *cancellable,
                        GError       **error)
{
  GInitable *initable;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  initable = g_initable_new (UDISKS_TYPE_LINUX_MODULE_LVM2,
                             cancellable,
                             error,
                             "daemon", daemon,
                             "name", LVM2_MODULE_NAME,
                             NULL);

  if (initable == NULL)
    return NULL;
  else
    return UDISKS_MODULE (initable);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  BDPluginSpec lvm_plugin = { BD_PLUGIN_LVM, "libbd_lvm.so.2" };
  BDPluginSpec *plugins[] = { &lvm_plugin, NULL };

  if (! bd_is_plugin_available (BD_PLUGIN_LVM))
    {
      if (! bd_reinit (plugins, FALSE, NULL, error))
        return FALSE;
    }

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = initable_init;
}

/* ---------------------------------------------------------------------------------------------------- */

GHashTable *
udisks_linux_module_lvm2_get_name_to_volume_group (UDisksLinuxModuleLVM2 *module)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_LVM2 (module), NULL);

  return module->name_to_volume_group;
}

/*  transfer-none  */
UDisksLinuxVolumeGroupObject *
udisks_linux_module_lvm2_find_volume_group_object (UDisksLinuxModuleLVM2 *module,
                                                   const gchar           *name)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_LVM2 (module), FALSE);

  return g_hash_table_lookup (module->name_to_volume_group, name);
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
udisks_linux_module_lvm2_new_manager (UDisksModule *module)
{
  UDisksLinuxManagerLVM2 *manager;

  manager = udisks_linux_manager_lvm2_new (UDISKS_LINUX_MODULE_LVM2 (module));

  return G_DBUS_INTERFACE_SKELETON (manager);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
lvm_update_vgs (GObject      *source_obj,
                GAsyncResult *result,
                gpointer      user_data)
{
  UDisksLinuxModuleLVM2 *module = UDISKS_LINUX_MODULE_LVM2 (source_obj);
  UDisksDaemon *daemon;
  GDBusObjectManagerServer *manager;

  GTask *task = G_TASK (result);
  GError *error = NULL;
  VGsPVsData *data = g_task_propagate_pointer (task, &error);
  BDLVMVGdata **vgs, **vgs_p;
  BDLVMPVdata **pvs, **pvs_p;

  GHashTableIter vg_name_iter;
  gpointer key, value;
  const gchar *vg_name;

  if (! data)
    {
      if (error)
        {
          udisks_warning ("LVM2 plugin: %s", error->message);
          g_clear_error (&error);
        }
      else
        {
          /* this should never happen */
          udisks_warning ("LVM2 plugin: failure but no error when getting VGs!");
        }
      return;
    }
  vgs = data->vgs;
  pvs = data->pvs;

  /* free the data container (but not 'vgs' and 'pvs') */
  g_free (data);

  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  manager = udisks_daemon_get_object_manager (daemon);

  /* Remove obsolete groups */
  g_hash_table_iter_init (&vg_name_iter, module->name_to_volume_group);
  while (g_hash_table_iter_next (&vg_name_iter, &key, &value))
    {
      UDisksLinuxVolumeGroupObject *group;
      gboolean found = FALSE;

      vg_name = key;
      group = value;

      for (vgs_p = vgs; !found && *vgs_p; vgs_p++)
        found = g_strcmp0 ((*vgs_p)->name, vg_name) == 0;

      if (! found)
        {
          udisks_linux_volume_group_object_destroy (group);
          g_dbus_object_manager_server_unexport (manager, g_dbus_object_get_object_path (G_DBUS_OBJECT (group)));
          g_hash_table_iter_remove (&vg_name_iter);
        }
    }

  /* Add new groups and update existing groups */
  for (vgs_p = vgs; *vgs_p; vgs_p++)
    {
      UDisksLinuxVolumeGroupObject *group;
      GSList *vg_pvs = NULL;

      vg_name = (*vgs_p)->name;
      group = g_hash_table_lookup (module->name_to_volume_group, vg_name);
      if (group == NULL)
        {
          group = udisks_linux_volume_group_object_new (module, vg_name);
          g_hash_table_insert (module->name_to_volume_group, g_strdup (vg_name), group);
        }

      for (pvs_p = pvs; *pvs_p; pvs_p++)
        if (g_strcmp0 ((*pvs_p)->vg_name, vg_name) == 0)
            vg_pvs = g_slist_prepend (vg_pvs, bd_lvm_pvdata_copy (*pvs_p));

      udisks_linux_volume_group_object_update (group, *vgs_p, vg_pvs);
    }

  /* UDisksLinuxVolumeGroupObject carries copies of BDLVMPVdata that belong to the VG.
  *  The rest of the PVs, either not assigned to any VG or assigned to a non-existing VG,
  *  are basically unused and freed here anyway.
  */
  for (pvs_p = pvs; *pvs_p; pvs_p++)
    bd_lvm_pvdata_free (*pvs_p);

  /* only free the containers, the contents were passed further */
  g_free (vgs);
  g_free (pvs);
}

static void
lvm_update (UDisksLinuxModuleLVM2 *module, gboolean coldplug)
{
  GTask *task;

  /* the callback (lvm_update_vgs) is called in the default main loop (context) */
  task = g_task_new (module,
                     NULL /* cancellable */,
                     lvm_update_vgs,
                     NULL /* callback_data */);

  /* holds a reference to 'task' until it is finished */
  if (coldplug)
    g_task_run_in_thread_sync (task, (GTaskThreadFunc) vgs_task_func);
  else
    g_task_run_in_thread (task, (GTaskThreadFunc) vgs_task_func);

  g_object_unref (task);
}

static gboolean
delayed_lvm_update (gpointer user_data)
{
  UDisksLinuxModuleLVM2 *module = UDISKS_LINUX_MODULE_LVM2 (user_data);

  lvm_update (module, FALSE);
  module->delayed_update_id = 0;

  return FALSE;
}

static void
trigger_delayed_lvm_update (UDisksLinuxModuleLVM2 *module)
{
  UDisksDaemon *daemon;
  UDisksLinuxProvider *provider;

  if (module->delayed_update_id > 0)
    return;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  provider = udisks_daemon_get_linux_provider (daemon);

  if (udisks_linux_provider_get_coldplug (provider) ||
      udisks_linux_provider_get_modules_coldplug (provider))
    {
      /* Update immediately in a synchronous fashion when doing coldplug,
       * i.e. when lvm2 module has just been activated. */
      lvm_update (module, TRUE);
    }
  else
    {
      module->delayed_update_id = g_timeout_add (100, delayed_lvm_update, module);
    }
}

static gboolean
is_logical_volume (UDisksLinuxDevice *device)
{
  const gchar *dm_vg_name;

  dm_vg_name = g_udev_device_get_property (device->udev_device, "DM_VG_NAME");
  return dm_vg_name && *dm_vg_name;
}

static gboolean
has_physical_volume_label (UDisksLinuxDevice *device)
{
  const gchar *id_fs_type;

  id_fs_type = g_udev_device_get_property (device->udev_device, "ID_FS_TYPE");
  return g_strcmp0 (id_fs_type, "LVM2_member") == 0;
}

static gboolean
is_recorded_as_physical_volume (UDisksLinuxModuleLVM2 *module,
                                UDisksLinuxDevice     *device)
{
  UDisksDaemon *daemon;
  UDisksObject *object;
  gboolean ret;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  object = udisks_daemon_find_block (daemon, g_udev_device_get_device_number (device->udev_device));
  ret = object && udisks_object_peek_physical_volume (object) != NULL;

  g_clear_object (&object);
  return ret;
}

static GDBusObjectSkeleton **
udisks_linux_module_lvm2_new_object (UDisksModule      *module,
                                     UDisksLinuxDevice *device)
{
  /* This is bit of a hack. We never return any instance and thus effectively
   * taking the #UDisksLinuxProvider module uevent machinery out of sight. We
   * only get an uevent and related #UDisksLinuxDevice where we perform basic
   * checks if the device could be related to LVM and schedule a probe. We take
   * reference to #UDisksModule instance though for manually performing dbus
   * stuff onto. */

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_LVM2 (module), NULL);

  if (is_logical_volume (device)
      || has_physical_volume_label (device)
      || is_recorded_as_physical_volume (UDISKS_LINUX_MODULE_LVM2 (module), device))
    trigger_delayed_lvm_update (UDISKS_LINUX_MODULE_LVM2 (module));

  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
udisks_linux_module_lvm2_track_parent (UDisksModule  *module,
                                       const gchar   *path,
                                       gchar        **uuid)
{
  UDisksDaemon *daemon;
  UDisksObject *object;
  UDisksObject *lvol_object = NULL;
  UDisksBlockLVM2 *block_lvm2;
  UDisksLogicalVolume *lvol;
  const gchar *parent_path = NULL;
  const gchar *parent_uuid = NULL;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_LVM2 (module), NULL);

  daemon = udisks_module_get_daemon (module);
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
  g_clear_object (&lvol_object);
  g_clear_object (&object);
  if (uuid)
    *uuid = g_strdup (parent_uuid);
  return g_strdup (parent_path);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_module_lvm2_class_init (UDisksLinuxModuleLVM2Class *klass)
{
  GObjectClass *gobject_class;
  UDisksModuleClass *module_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = udisks_linux_module_lvm2_constructed;
  gobject_class->finalize = udisks_linux_module_lvm2_finalize;

  module_class = UDISKS_MODULE_CLASS (klass);
  module_class->new_manager = udisks_linux_module_lvm2_new_manager;
  module_class->new_object = udisks_linux_module_lvm2_new_object;
  module_class->track_parent = udisks_linux_module_lvm2_track_parent;
}
