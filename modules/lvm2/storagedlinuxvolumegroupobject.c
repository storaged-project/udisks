/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
 * Copyright (C) 2013 Marius Vollmer <marius.vollmer@gmail.com>
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <src/storagedlogging.h>
#include <src/storagedlinuxprovider.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlinuxblockobject.h>

#include "storagedlinuxvolumegroupobject.h"
#include "storagedlinuxvolumegroup.h"
#include "storagedlinuxlogicalvolumeobject.h"
#include "storagedlinuxphysicalvolume.h"
#include "storagedlinuxblocklvm2.h"

#include "storagedlvm2daemonutil.h"
#include "storagedlvm2dbusutil.h"
#include "storaged-lvm2-generated.h"

/**
 * SECTION:storagedlinuxvolumegroupobject
 * @title: StoragedLinuxVolumeGroupObject
 * @short_description: Object representing a LVM volume group
 */

typedef struct _StoragedLinuxVolumeGroupObjectClass   StoragedLinuxVolumeGroupObjectClass;

/**
 * StoragedLinuxVolumeGroupObject:
 *
 * The #StoragedLinuxVolumeGroupObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _StoragedLinuxVolumeGroupObject
{
  StoragedObjectSkeleton parent_instance;

  StoragedDaemon *daemon;

  gchar *name;

  GHashTable *logical_volumes;
  GPid poll_pid;
  guint poll_timeout_id;
  gboolean poll_requested;

  /* interface */
  StoragedVolumeGroup *iface_volume_group;
};

struct _StoragedLinuxVolumeGroupObjectClass
{
  StoragedObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_NAME,
};

G_DEFINE_TYPE (StoragedLinuxVolumeGroupObject, storaged_linux_volume_group_object, STORAGED_TYPE_OBJECT_SKELETON);

static void
storaged_linux_volume_group_object_finalize (GObject *_object)
{
  StoragedLinuxVolumeGroupObject *object = STORAGED_LINUX_VOLUME_GROUP_OBJECT (_object);

  /* note: we don't hold a ref to object->daemon */

  if (object->iface_volume_group != NULL)
    g_object_unref (object->iface_volume_group);

  g_hash_table_unref (object->logical_volumes);
  g_free (object->name);

  if (G_OBJECT_CLASS (storaged_linux_volume_group_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_volume_group_object_parent_class)->finalize (_object);
}

static void
storaged_linux_volume_group_object_get_property (GObject    *__object,
                                                 guint       prop_id,
                                                 GValue     *value,
                                                 GParamSpec *pspec)
{
  StoragedLinuxVolumeGroupObject *object = STORAGED_LINUX_VOLUME_GROUP_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_volume_group_object_get_daemon (object));
      break;

    case PROP_NAME:
      g_value_set_string (value, storaged_linux_volume_group_object_get_name (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_volume_group_object_set_property (GObject      *__object,
                                                 guint         prop_id,
                                                 const GValue *value,
                                                 GParamSpec   *pspec)
{
  StoragedLinuxVolumeGroupObject *object = STORAGED_LINUX_VOLUME_GROUP_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (object->daemon == NULL);
      /* we don't take a reference to the daemon */
      object->daemon = g_value_get_object (value);
      break;

    case PROP_NAME:
      g_assert (object->name == NULL);
      object->name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
storaged_linux_volume_group_object_init (StoragedLinuxVolumeGroupObject *object)
{
}

static void
storaged_linux_volume_group_object_constructed (GObject *_object)
{
  StoragedLinuxVolumeGroupObject *object = STORAGED_LINUX_VOLUME_GROUP_OBJECT (_object);
  GString *s;

  if (G_OBJECT_CLASS (storaged_linux_volume_group_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (storaged_linux_volume_group_object_parent_class)->constructed (_object);

  object->logical_volumes = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   g_free,
                                                   (GDestroyNotify) g_object_unref);

  /* compute the object path */
  s = g_string_new ("/org/storaged/Storaged/lvm/");
  storaged_safe_append_to_object_path (s, object->name);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), s->str);
  g_string_free (s, TRUE);

  /* create the DBus interface */
  object->iface_volume_group = storaged_linux_volume_group_new ();
  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                        G_DBUS_INTERFACE_SKELETON (object->iface_volume_group));
}

static void
storaged_linux_volume_group_object_class_init (StoragedLinuxVolumeGroupObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_linux_volume_group_object_finalize;
  gobject_class->constructed  = storaged_linux_volume_group_object_constructed;
  gobject_class->set_property = storaged_linux_volume_group_object_set_property;
  gobject_class->get_property = storaged_linux_volume_group_object_get_property;

  /**
   * StoragedLinuxVolumeGroupObject:daemon:
   *
   * The #StoragedDaemon the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the object is for",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedLinuxVolumeGroupObject:name:
   *
   * The name of the volume group.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "The name of the volume group",
                                                        NULL,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * storaged_linux_volume_group_object_new:
 * @daemon: A #StoragedDaemon.
 * @name: The name of the volume group.
 *
 * Create a new VolumeGroup object.
 *
 * Returns: A #StoragedLinuxVolumeGroupObject object. Free with g_object_unref().
 */
StoragedLinuxVolumeGroupObject *
storaged_linux_volume_group_object_new (StoragedDaemon  *daemon,
                                        const gchar     *name)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  return STORAGED_LINUX_VOLUME_GROUP_OBJECT (g_object_new (STORAGED_TYPE_LINUX_VOLUME_GROUP_OBJECT,
                                                           "daemon", daemon,
                                                           "name", name,
                                                           NULL));
}

/**
 * storaged_linux_volume_group_object_get_daemon:
 * @object: A #StoragedLinuxVolumeGroupObject.
 *
 * Gets the daemon used by @object.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @object.
 */
StoragedDaemon *
storaged_linux_volume_group_object_get_daemon (StoragedLinuxVolumeGroupObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_VOLUME_GROUP_OBJECT (object), NULL);
  return object->daemon;
}

static gboolean
lv_is_pvmove_volume (const gchar *name)
{
  return name && g_str_has_prefix (name, "pvmove");
}

static void
update_progress_for_device (StoragedDaemon *daemon,
                            const gchar    *operation,
                            const gchar    *dev,
                            double          progress)
{
  GDBusObjectManager *object_manager;
  GList *objects, *l;

  object_manager = G_DBUS_OBJECT_MANAGER (storaged_daemon_get_object_manager (daemon));
  objects = g_dbus_object_manager_get_objects (object_manager);

  for (l = objects; l; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedJob *job;
      const gchar *const *job_objects;
      int i;

      job = storaged_object_peek_job (object);
      if (job == NULL)
        continue;

      if (g_strcmp0 (storaged_job_get_operation (job), operation) != 0)
        continue;

      job_objects = storaged_job_get_objects (job);
      for (i = 0; job_objects[i]; i++)
        {
          StoragedBlock *block = STORAGED_BLOCK (g_dbus_object_manager_get_interface (object_manager,
                                                                                  job_objects[i],
                                                                                  "org.storaged.Storaged.Block"));

          if (block)
            {
              const gchar *const *symlinks;
              int j;
              if (g_strcmp0 (storaged_block_get_device (block), dev) == 0)
                goto found;
              symlinks = storaged_block_get_symlinks (block);
              for (j = 0; symlinks[j]; j++)
                if (g_strcmp0 (symlinks[j], dev) == 0)
                  goto found;

              continue;
            found:
              storaged_job_set_progress (job, progress);
              storaged_job_set_progress_valid (job, TRUE);
            }
        }

    }
  g_list_free_full (objects, g_object_unref);
}

static void
update_operations (StoragedDaemon *daemon,
                   const gchar    *lv_name,
                   GVariant       *lv_info,
                   gboolean       *needs_polling_ret)
{
  const gchar *move_pv;
  guint64 copy_percent;

  if (lv_is_pvmove_volume (lv_name)
      && g_variant_lookup (lv_info, "move_pv", "&s", &move_pv)
      && g_variant_lookup (lv_info, "copy_percent", "t", &copy_percent))
    {
      update_progress_for_device (daemon,
                                  "lvm-vg-empty-device",
                                  move_pv,
                                  copy_percent/100000000.0);
      *needs_polling_ret = TRUE;
    }
}

static void
block_object_update_lvm_iface (StoragedLinuxBlockObject *object,
                               const gchar *lv_obj_path)
{
  StoragedBlockLVM2 *iface_block_lvm2;

  iface_block_lvm2 = storaged_object_peek_block_lvm2 (STORAGED_OBJECT (object));

  if (iface_block_lvm2 == NULL)
    {
      iface_block_lvm2 = storaged_linux_block_lvm2_new ();
      g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                            G_DBUS_INTERFACE_SKELETON (iface_block_lvm2));
      g_object_unref (iface_block_lvm2);
    }

  storaged_linux_block_lvm2_update (STORAGED_LINUX_BLOCK_LVM2 (iface_block_lvm2), object);
  storaged_block_lvm2_set_logical_volume (iface_block_lvm2, lv_obj_path);
}

static void
update_block (StoragedLinuxBlockObject       *block_object,
              StoragedLinuxVolumeGroupObject *group_object,
              GHashTable                     *new_lvs,
              GHashTable                     *new_pvs)
{
  StoragedBlock *block;
  GVariant *pv_info;

  block = storaged_object_peek_block (STORAGED_OBJECT (block_object));
  if (block == NULL)
    return;

  // XXX - move this elsewhere?
  {
    StoragedLinuxDevice *device;
    StoragedLinuxLogicalVolumeObject *lv_object;
    const gchar *block_vg_name;
    const gchar *block_lv_name;

    device = storaged_linux_block_object_get_device (block_object);
    if (device)
      {
        block_vg_name = g_udev_device_get_property (device->udev_device, "DM_VG_NAME");
        block_lv_name = g_udev_device_get_property (device->udev_device, "DM_LV_NAME");

        if (g_strcmp0 (block_vg_name, storaged_linux_volume_group_object_get_name (group_object)) == 0
            && (lv_object = g_hash_table_lookup (new_lvs, block_lv_name)))
          {
            block_object_update_lvm_iface (block_object, g_dbus_object_get_object_path (G_DBUS_OBJECT (lv_object)));
          }
      }
  }

  pv_info = g_hash_table_lookup (new_pvs, storaged_block_get_device (block));
  if (!pv_info)
    {
      const gchar *const *symlinks;
      int i;
      symlinks = storaged_block_get_symlinks (block);
      for (i = 0; symlinks[i]; i++)
        {
          pv_info = g_hash_table_lookup (new_pvs, symlinks[i]);
          if (pv_info)
            break;
        }
    }

  if (pv_info)
    {
      storaged_linux_block_object_update_lvm_pv (block_object, group_object, pv_info);
    }
  else
    {
      StoragedPhysicalVolume *pv = storaged_object_peek_physical_volume (STORAGED_OBJECT (block_object));
      if (pv && g_strcmp0 (storaged_physical_volume_get_volume_group (pv),
                           g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object))) == 0)
        storaged_linux_block_object_update_lvm_pv (block_object, NULL, NULL);
    }
}

static void
update_with_variant (GPid pid,
                     GVariant *info,
                     GError *error,
                     gpointer user_data)
{
  StoragedLinuxVolumeGroupObject *object = user_data;
  StoragedDaemon *daemon;
  GDBusObjectManagerServer *manager;
  GVariantIter *iter;
  GHashTableIter volume_iter;
  gpointer key, value;
  GHashTable *new_lvs;
  GHashTable *new_pvs;
  GList *objects, *l;
  gboolean needs_polling = FALSE;

  daemon = storaged_linux_volume_group_object_get_daemon (object);
  manager = storaged_daemon_get_object_manager (daemon);

  if (error)
    {
      storaged_warning ("Failed to update LVM volume group %s: %s",
                        storaged_linux_volume_group_object_get_name (object),
                        error->message);
      g_object_unref (object);
      return;
    }

  storaged_linux_volume_group_update (STORAGED_LINUX_VOLUME_GROUP (object->iface_volume_group), info, &needs_polling);

  if (!g_dbus_object_manager_server_is_exported (manager, G_DBUS_OBJECT_SKELETON (object)))
    g_dbus_object_manager_server_export_uniquely (manager, G_DBUS_OBJECT_SKELETON (object));

  new_lvs = g_hash_table_new (g_str_hash, g_str_equal);

  if (g_variant_lookup (info, "lvs", "aa{sv}", &iter))
    {
      GVariant *lv_info = NULL;
      while (g_variant_iter_loop (iter, "@a{sv}", &lv_info))
        {
          const gchar *name;
          StoragedLinuxLogicalVolumeObject *volume;

          g_variant_lookup (lv_info, "name", "&s", &name);

          update_operations (daemon, name, lv_info, &needs_polling);

          if (lv_is_pvmove_volume (name))
            needs_polling = TRUE;

          if (storaged_daemon_util_lvm2_name_is_reserved (name))
            continue;

          volume = g_hash_table_lookup (object->logical_volumes, name);
          if (volume == NULL)
            {
              volume = storaged_linux_logical_volume_object_new (daemon, object, name);
              storaged_linux_logical_volume_object_update (volume, lv_info, &needs_polling);
              g_dbus_object_manager_server_export_uniquely (manager, G_DBUS_OBJECT_SKELETON (volume));
              g_hash_table_insert (object->logical_volumes, g_strdup (name), g_object_ref (volume));
            }
          else
            storaged_linux_logical_volume_object_update (volume, lv_info, &needs_polling);

          g_hash_table_insert (new_lvs, (gchar *)name, volume);
        }
      g_variant_iter_free (iter);
    }

  g_hash_table_iter_init (&volume_iter, object->logical_volumes);
  while (g_hash_table_iter_next (&volume_iter, &key, &value))
    {
      const gchar *name = key;
      StoragedLinuxLogicalVolumeObject *volume = value;

      if (!g_hash_table_contains (new_lvs, name))
        {
          g_dbus_object_manager_server_unexport (manager,
                                                 g_dbus_object_get_object_path (G_DBUS_OBJECT (volume)));
          g_hash_table_iter_remove (&volume_iter);
        }
    }

  storaged_volume_group_set_needs_polling (STORAGED_VOLUME_GROUP (object->iface_volume_group),
                                           needs_polling);

  /* Update block objects. */

  new_pvs = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)g_variant_unref);
  if (g_variant_lookup (info, "pvs", "aa{sv}", &iter))
    {
      const gchar *name;
      GVariant *pv_info;
      while (g_variant_iter_next (iter, "@a{sv}", &pv_info))
        {
          if (g_variant_lookup (pv_info, "device", "&s", &name))
            g_hash_table_insert (new_pvs, (gchar *)name, pv_info);
          else
            g_variant_unref (pv_info);
        }
    }

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
  for (l = objects; l != NULL; l = l->next)
    {
      if (STORAGED_IS_LINUX_BLOCK_OBJECT (l->data))
        update_block (STORAGED_LINUX_BLOCK_OBJECT (l->data), object, new_lvs, new_pvs);
    }
  g_list_free_full (objects, g_object_unref);

  g_hash_table_destroy (new_lvs);
  g_hash_table_destroy (new_pvs);

  g_object_unref (object);
}

void
storaged_linux_volume_group_object_update (StoragedLinuxVolumeGroupObject *object)
{
  const gchar *args[] = { LVM_HELPER_DIR "storaged-lvm", "-b", "show", object->name, NULL };
  storaged_daemon_util_lvm2_spawn_for_variant (args, G_VARIANT_TYPE("a{sv}"),
                                               update_with_variant, g_object_ref (object));
}

static void
poll_with_variant (GPid pid,
                   GVariant *info,
                   GError *error,
                   gpointer user_data)
{
  StoragedLinuxVolumeGroupObject *object = user_data;
  StoragedDaemon *daemon;
  GVariantIter *iter;
  gboolean needs_polling;

  if (pid != object->poll_pid)
    {
      g_object_unref (object);
      return;
    }

  object->poll_pid = 0;

  if (error)
    {
      storaged_warning ("Failed to poll LVM volume group %s: %s",
                        storaged_linux_volume_group_object_get_name (object),
                        error->message);
      g_object_unref (object);
      return;
    }

  daemon = storaged_linux_volume_group_object_get_daemon (object);

  storaged_linux_volume_group_update (STORAGED_LINUX_VOLUME_GROUP (object->iface_volume_group), info, &needs_polling);

  if (g_variant_lookup (info, "lvs", "aa{sv}", &iter))
    {
      GVariant *lv_info = NULL;
      while (g_variant_iter_loop (iter, "@a{sv}", &lv_info))
        {
          const gchar *name;
          StoragedLinuxLogicalVolumeObject *volume;

          g_variant_lookup (lv_info, "name", "&s", &name);
          update_operations (daemon, name, lv_info, &needs_polling);
          volume = g_hash_table_lookup (object->logical_volumes, name);
          if (volume)
            storaged_linux_logical_volume_object_update (volume, lv_info, &needs_polling);
        }
      g_variant_iter_free (iter);
    }

  g_object_unref (object);
}

static void poll_now (StoragedLinuxVolumeGroupObject *object);

static gboolean
poll_in_main_thread (gpointer user_data)
{
  StoragedLinuxVolumeGroupObject *object = user_data;

  if (object->poll_timeout_id)
    object->poll_requested = TRUE;
  else
    poll_now (object);

  g_object_unref (object);
  return FALSE;
}

static gboolean
poll_timeout (gpointer user_data)
{
  StoragedLinuxVolumeGroupObject *object = user_data;

  object->poll_timeout_id = 0;
  if (object->poll_requested)
    {
      object->poll_requested = FALSE;
      poll_now (object);
    }

  g_object_unref (object);
  return FALSE;
}

static void
poll_now (StoragedLinuxVolumeGroupObject *object)
{
  const gchar *args[] = { LVM_HELPER_DIR "storaged-lvm", "-b", "show", object->name, NULL };

  object->poll_timeout_id = g_timeout_add (5000, poll_timeout, g_object_ref (object));

  if (object->poll_pid)
    kill (object->poll_pid, SIGINT);

  object->poll_pid = storaged_daemon_util_lvm2_spawn_for_variant (args, G_VARIANT_TYPE("a{sv}"),
                                                                  poll_with_variant, g_object_ref (object));
}

void
storaged_linux_volume_group_object_poll (StoragedLinuxVolumeGroupObject *object)
{
  g_idle_add (poll_in_main_thread, g_object_ref (object));
}

void
storaged_linux_volume_group_object_destroy (StoragedLinuxVolumeGroupObject *object)
{
  GHashTableIter volume_iter;
  gpointer key, value;

  g_hash_table_iter_init (&volume_iter, object->logical_volumes);
  while (g_hash_table_iter_next (&volume_iter, &key, &value))
    {
      StoragedLinuxLogicalVolumeObject *volume = value;
      g_dbus_object_manager_server_unexport (storaged_daemon_get_object_manager (object->daemon),
                                             g_dbus_object_get_object_path (G_DBUS_OBJECT (volume)));
    }
}

StoragedLinuxLogicalVolumeObject *
storaged_linux_volume_group_object_find_logical_volume_object (StoragedLinuxVolumeGroupObject *object,
                                                               const gchar                    *name)
{
  return g_hash_table_lookup (object->logical_volumes, name);
}

/**
 * storaged_linux_volume_group_object_get_name:
 * @object: A #StoragedLinuxVolumeGroupObject.
 *
 * Gets the name for @object.
 *
 * Returns: (transfer none): The name for object. Do not free, the string belongs to @object.
 */
const gchar *
storaged_linux_volume_group_object_get_name (StoragedLinuxVolumeGroupObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_VOLUME_GROUP_OBJECT (object), NULL);
  return object->name;
}
