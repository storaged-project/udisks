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

#include <src/udiskslogging.h>
#include <src/udiskslinuxprovider.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslinuxblockobject.h>

#include "udiskslinuxvolumegroupobject.h"
#include "udiskslinuxvolumegroup.h"
#include "udiskslinuxlogicalvolumeobject.h"
#include "udiskslinuxphysicalvolume.h"
#include "udiskslinuxblocklvm2.h"

#include "udiskslvm2daemonutil.h"
#include "udiskslvm2dbusutil.h"
#include "udisks-lvm2-generated.h"
#include "jobhelpers.h"

/**
 * SECTION:udiskslinuxvolumegroupobject
 * @title: UDisksLinuxVolumeGroupObject
 * @short_description: Object representing a LVM volume group
 */

typedef struct _UDisksLinuxVolumeGroupObjectClass   UDisksLinuxVolumeGroupObjectClass;

/**
 * UDisksLinuxVolumeGroupObject:
 *
 * The #UDisksLinuxVolumeGroupObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxVolumeGroupObject
{
  UDisksObjectSkeleton parent_instance;

  UDisksDaemon *daemon;

  gchar *name;

  GHashTable *logical_volumes;
  guint32 poll_epoch;
  guint poll_timeout_id;
  gboolean poll_requested;

  /* interface */
  UDisksVolumeGroup *iface_volume_group;
};

struct _UDisksLinuxVolumeGroupObjectClass
{
  UDisksObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_NAME,
};

G_DEFINE_TYPE (UDisksLinuxVolumeGroupObject, udisks_linux_volume_group_object, UDISKS_TYPE_OBJECT_SKELETON);

static void etctabs_changed (UDisksFstabMonitor *monitor,
                             UDisksFstabEntry   *entry,
                             gpointer            user_data);

typedef struct {
  BDLVMVGdata *vg_info;
  GSList *vg_pvs;
} VGUpdateData;

static void
udisks_linux_volume_group_object_finalize (GObject *_object)
{
  UDisksLinuxVolumeGroupObject *object = UDISKS_LINUX_VOLUME_GROUP_OBJECT (_object);

  /* note: we don't hold a ref to object->daemon */

  if (object->iface_volume_group != NULL)
    g_object_unref (object->iface_volume_group);

  g_hash_table_unref (object->logical_volumes);
  g_free (object->name);

  g_signal_handlers_disconnect_by_func (udisks_daemon_get_fstab_monitor (object->daemon),
                                        G_CALLBACK (etctabs_changed),
                                        object);
  g_signal_handlers_disconnect_by_func (udisks_daemon_get_crypttab_monitor (object->daemon),
                                        G_CALLBACK (etctabs_changed),
                                        object);

  if (G_OBJECT_CLASS (udisks_linux_volume_group_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_volume_group_object_parent_class)->finalize (_object);
}

static void
udisks_linux_volume_group_object_get_property (GObject    *__object,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
  UDisksLinuxVolumeGroupObject *object = UDISKS_LINUX_VOLUME_GROUP_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_volume_group_object_get_daemon (object));
      break;

    case PROP_NAME:
      g_value_set_string (value, udisks_linux_volume_group_object_get_name (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_volume_group_object_set_property (GObject      *__object,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
  UDisksLinuxVolumeGroupObject *object = UDISKS_LINUX_VOLUME_GROUP_OBJECT (__object);

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
udisks_linux_volume_group_object_init (UDisksLinuxVolumeGroupObject *object)
{
  object->poll_epoch = 0;
  object->poll_timeout_id = 0;
  object->poll_requested = FALSE;
}

static void
udisks_linux_volume_group_object_constructed (GObject *_object)
{
  UDisksLinuxVolumeGroupObject *object = UDISKS_LINUX_VOLUME_GROUP_OBJECT (_object);
  GString *s;

  if (G_OBJECT_CLASS (udisks_linux_volume_group_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_volume_group_object_parent_class)->constructed (_object);

  object->logical_volumes = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   g_free,
                                                   (GDestroyNotify) g_object_unref);

  /* compute the object path */
  s = g_string_new ("/org/freedesktop/UDisks2/lvm/");
  udisks_safe_append_to_object_path (s, object->name);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), s->str);
  g_string_free (s, TRUE);

  /* create the DBus interface */
  object->iface_volume_group = udisks_linux_volume_group_new ();
  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                        G_DBUS_INTERFACE_SKELETON (object->iface_volume_group));

  /* Watch fstab and crypttab for changes.
   */
  g_signal_connect (udisks_daemon_get_fstab_monitor (object->daemon),
                    "entry-added",
                    G_CALLBACK (etctabs_changed),
                    object);
  g_signal_connect (udisks_daemon_get_fstab_monitor (object->daemon),
                    "entry-removed",
                    G_CALLBACK (etctabs_changed),
                    object);
  g_signal_connect (udisks_daemon_get_crypttab_monitor (object->daemon),
                    "entry-added",
                    G_CALLBACK (etctabs_changed),
                    object);
  g_signal_connect (udisks_daemon_get_crypttab_monitor (object->daemon),
                    "entry-removed",
                    G_CALLBACK (etctabs_changed),
                    object);
}

static void
udisks_linux_volume_group_object_class_init (UDisksLinuxVolumeGroupObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_volume_group_object_finalize;
  gobject_class->constructed  = udisks_linux_volume_group_object_constructed;
  gobject_class->set_property = udisks_linux_volume_group_object_set_property;
  gobject_class->get_property = udisks_linux_volume_group_object_get_property;

  /**
   * UDisksLinuxVolumeGroupObject:daemon:
   *
   * The #UDisksDaemon the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the object is for",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksLinuxVolumeGroupObject:name:
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
 * udisks_linux_volume_group_object_new:
 * @daemon: A #UDisksDaemon.
 * @name: The name of the volume group.
 *
 * Create a new VolumeGroup object.
 *
 * Returns: A #UDisksLinuxVolumeGroupObject object. Free with g_object_unref().
 */
UDisksLinuxVolumeGroupObject *
udisks_linux_volume_group_object_new (UDisksDaemon *daemon,
                                      const gchar  *name)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  return UDISKS_LINUX_VOLUME_GROUP_OBJECT (g_object_new (UDISKS_TYPE_LINUX_VOLUME_GROUP_OBJECT,
                                                         "daemon", daemon,
                                                         "name", name,
                                                         NULL));
}

/**
 * udisks_linux_volume_group_object_get_daemon:
 * @object: A #UDisksLinuxVolumeGroupObject.
 *
 * Gets the daemon used by @object.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @object.
 */
UDisksDaemon *
udisks_linux_volume_group_object_get_daemon (UDisksLinuxVolumeGroupObject *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_VOLUME_GROUP_OBJECT (object), NULL);
  return object->daemon;
}

static void
update_etctabs (UDisksLinuxVolumeGroupObject *object)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, object->logical_volumes);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      UDisksLinuxLogicalVolumeObject *volume = value;

      udisks_linux_logical_volume_object_update_etctabs (volume);
    }
}

static void
etctabs_changed (UDisksFstabMonitor *monitor,
                 UDisksFstabEntry   *entry,
                 gpointer            user_data)
{
  update_etctabs (UDISKS_LINUX_VOLUME_GROUP_OBJECT (user_data));
}

static gboolean
lv_is_pvmove_volume (const gchar *name)
{
  return name && g_str_has_prefix (name, "pvmove");
}

static void
update_progress_for_device (UDisksDaemon *daemon,
                            const gchar  *operation,
                            const gchar  *dev,
                            double        progress)
{
  GDBusObjectManager *object_manager;
  GList *objects, *l;

  object_manager = G_DBUS_OBJECT_MANAGER (udisks_daemon_get_object_manager (daemon));
  objects = g_dbus_object_manager_get_objects (object_manager);

  for (l = objects; l; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksJob *job;
      const gchar *const *job_objects;
      int i;

      job = udisks_object_peek_job (object);
      if (job == NULL)
        continue;

      if (g_strcmp0 (udisks_job_get_operation (job), operation) != 0)
        continue;

      job_objects = udisks_job_get_objects (job);
      for (i = 0; job_objects[i]; i++)
        {
          UDisksBlock *block = UDISKS_BLOCK (g_dbus_object_manager_get_interface (object_manager,
                                                                                  job_objects[i],
                                                                                  "org.freedesktop.UDisks2.Block"));

          if (block)
            {
              const gchar *const *symlinks;
              int j;
              if (g_strcmp0 (udisks_block_get_device (block), dev) == 0)
                goto found;
              symlinks = udisks_block_get_symlinks (block);
              for (j = 0; symlinks[j]; j++)
                if (g_strcmp0 (symlinks[j], dev) == 0)
                  goto found;

              continue;
            found:
              udisks_job_set_progress (job, progress);
              udisks_job_set_progress_valid (job, TRUE);
            }
        }

    }
  g_list_free_full (objects, g_object_unref);
}

static void
update_operations (UDisksDaemon *daemon,
                   const gchar  *lv_name,
                   BDLVMLVdata  *lv_info,
                   gboolean     *needs_polling_ret)
{
  if (lv_is_pvmove_volume (lv_name))
    {
      if (lv_info->move_pv && lv_info->copy_percent)
        {
          update_progress_for_device (daemon,
                                      "lvm-vg-empty-device",
                                      lv_info->move_pv,
                                      lv_info->copy_percent/100.0);
        }
      *needs_polling_ret = TRUE;
    }
}

static void
block_object_update_lvm_iface (UDisksLinuxBlockObject *object,
                               const gchar *lv_obj_path)
{
  UDisksBlockLVM2 *iface_block_lvm2;

  iface_block_lvm2 = udisks_object_peek_block_lvm2 (UDISKS_OBJECT (object));

  if (iface_block_lvm2 == NULL)
    {
      iface_block_lvm2 = udisks_linux_block_lvm2_new ();
      g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                            G_DBUS_INTERFACE_SKELETON (iface_block_lvm2));
      g_object_unref (iface_block_lvm2);
    }

  udisks_linux_block_lvm2_update (UDISKS_LINUX_BLOCK_LVM2 (iface_block_lvm2), object);
  udisks_block_lvm2_set_logical_volume (iface_block_lvm2, lv_obj_path);
}

static void
lv_object_update_block_path (UDisksLinuxBlockObject       *block_object,
                             UDisksLinuxLogicalVolumeObject *lv_object)
{
  UDisksLogicalVolume *lv = NULL;
  const char *block_objpath = NULL;

  lv = udisks_object_peek_logical_volume (UDISKS_OBJECT (lv_object));
  if (lv)
    {
      block_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (block_object));
      udisks_logical_volume_set_block_device (UDISKS_LOGICAL_VOLUME (lv), block_objpath);
    }
}

static void
update_block (UDisksLinuxBlockObject       *block_object,
              UDisksLinuxVolumeGroupObject *group_object,
              GHashTable                   *new_lvs,
              GHashTable                   *new_pvs)
{
  UDisksBlock *block;
  BDLVMPVdata *pv_info;

  block = udisks_object_peek_block (UDISKS_OBJECT (block_object));
  if (block == NULL)
    return;

  // XXX - move this elsewhere?
  {
    UDisksLinuxDevice *device;
    UDisksLinuxLogicalVolumeObject *lv_object;
    const gchar *block_vg_name;
    const gchar *block_lv_name;

    device = udisks_linux_block_object_get_device (block_object);
    if (device)
      {
        block_vg_name = g_udev_device_get_property (device->udev_device, "DM_VG_NAME");
        block_lv_name = g_udev_device_get_property (device->udev_device, "DM_LV_NAME");

        if (g_strcmp0 (block_vg_name, udisks_linux_volume_group_object_get_name (group_object)) == 0
            && (lv_object = g_hash_table_lookup (new_lvs, block_lv_name)))
          {
            block_object_update_lvm_iface (block_object, g_dbus_object_get_object_path (G_DBUS_OBJECT (lv_object)));
            lv_object_update_block_path (block_object, lv_object);
          }

        g_object_unref(device);
      }
  }

  pv_info = g_hash_table_lookup (new_pvs, udisks_block_get_device (block));
  if (!pv_info)
    {
      const gchar *const *symlinks;
      int i;
      symlinks = udisks_block_get_symlinks (block);
      for (i = 0; symlinks[i]; i++)
        {
          pv_info = g_hash_table_lookup (new_pvs, symlinks[i]);
          if (pv_info)
            break;
        }
    }

  if (pv_info)
    {
      udisks_linux_block_object_update_lvm_pv (block_object, group_object, pv_info);
    }
  else
    {
      UDisksPhysicalVolume *pv = udisks_object_peek_physical_volume (UDISKS_OBJECT (block_object));
      if (pv && g_strcmp0 (udisks_physical_volume_get_volume_group (pv),
                           g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object))) == 0)
        udisks_linux_block_object_update_lvm_pv (block_object, NULL, NULL);
    }
}

/**
 * cmp_int_lv_name: (skip)
 *
 * Compare name of an internal LV (possibly enclosed in square brackets) with a
 * given other name.
 */
static gboolean
cmp_int_lv_name (const gchar *int_lv_name, const gchar *lv_name)
{
  const gchar *c = NULL;
  if (!int_lv_name || !lv_name)
    return FALSE;

  if (*int_lv_name == '[')
    int_lv_name++;

  for (c=int_lv_name; *c != '\0' && *c != ']'; c++)
    if (*c != lv_name[c - int_lv_name])
      return FALSE;

  if (*c == ']')
    c++;
  if (*c == '\0' && lv_name[c - int_lv_name] == '\0')
    return TRUE;

  return FALSE;
}

static void
update_vg (GObject      *source_obj,
           GAsyncResult *result,
           gpointer      user_data)
{
  UDisksDaemon *daemon;
  GDBusObjectManagerServer *manager;
  GHashTableIter volume_iter;
  gpointer key, value;
  GHashTable *new_lvs;
  GHashTable *new_pvs;
  GList *objects, *l;
  gboolean needs_polling = FALSE;
  GError *error = NULL;

  UDisksLinuxVolumeGroupObject *object = UDISKS_LINUX_VOLUME_GROUP_OBJECT (source_obj);
  GTask *task = G_TASK (result);
  VGUpdateData *data = user_data;
  BDLVMLVdata **lvs = g_task_propagate_pointer (task, &error);
  BDLVMVGdata *vg_info = data->vg_info;
  GSList *vg_pvs = data->vg_pvs;

  /* free the data container (but not 'vg_info' and 'vg_pvs') */
  g_free (data);

  if (!lvs)
    {
      if (error)
        {
          udisks_warning ("Failed to update LVM volume group %s: %s",
                          udisks_linux_volume_group_object_get_name (object),
                          error->message);
          g_clear_error (&error);
        }
      else
        {
          /* this should never happen */
          udisks_warning ("Failed to update LVM volume group %s: no error reported",
                          udisks_linux_volume_group_object_get_name (object));
        }
      g_slist_free_full (vg_pvs, (GDestroyNotify) bd_lvm_pvdata_free);
      bd_lvm_vgdata_free (vg_info);
      g_object_unref (object);
      return;
    }

  daemon = udisks_linux_volume_group_object_get_daemon (object);
  manager = udisks_daemon_get_object_manager (daemon);

  udisks_linux_volume_group_update (UDISKS_LINUX_VOLUME_GROUP (object->iface_volume_group), vg_info, &needs_polling);

  if (!g_dbus_object_manager_server_is_exported (manager, G_DBUS_OBJECT_SKELETON (object)))
    g_dbus_object_manager_server_export_uniquely (manager, G_DBUS_OBJECT_SKELETON (object));

  new_lvs = g_hash_table_new (g_str_hash, g_str_equal);

  for (BDLVMLVdata **lvs_p=lvs; *lvs_p; lvs_p++)
    {
      UDisksLinuxLogicalVolumeObject *volume;
      BDLVMLVdata *lv_info = *lvs_p;
      const gchar *lv_name = lv_info->lv_name;
      BDLVMLVdata *meta_lv_info = NULL;

      update_operations (daemon, lv_name, lv_info, &needs_polling);

      if (udisks_daemon_util_lvm2_name_is_reserved (lv_name))
        continue;

      if (lv_info->metadata_lv && *(lv_info->metadata_lv) != '\0')
        /* this is not cheap to do, but not many LVs have a metadata LV */
        for (BDLVMLVdata **lvs_p2=lvs; !meta_lv_info && *lvs_p2; lvs_p2++)
          if (cmp_int_lv_name ((*lvs_p2)->lv_name, lv_info->metadata_lv))
            meta_lv_info = *lvs_p2;

      volume = g_hash_table_lookup (object->logical_volumes, lv_name);
      if (volume == NULL)
        {
          volume = udisks_linux_logical_volume_object_new (daemon, object, lv_name);
          udisks_linux_logical_volume_object_update (volume, lv_info, meta_lv_info, &needs_polling);
          udisks_linux_logical_volume_object_update_etctabs (volume);
          g_dbus_object_manager_server_export_uniquely (manager, G_DBUS_OBJECT_SKELETON (volume));
          g_hash_table_insert (object->logical_volumes, g_strdup (lv_name), g_object_ref (volume));
        }
      else
        udisks_linux_logical_volume_object_update (volume, lv_info, meta_lv_info, &needs_polling);

      g_hash_table_insert (new_lvs, (gchar *)lv_name, volume);
    }

  g_hash_table_iter_init (&volume_iter, object->logical_volumes);
  while (g_hash_table_iter_next (&volume_iter, &key, &value))
    {
      const gchar *name = key;
      UDisksLinuxLogicalVolumeObject *volume = value;

      if (!g_hash_table_contains (new_lvs, name))
        {
          g_dbus_object_manager_server_unexport (manager,
                                                 g_dbus_object_get_object_path (G_DBUS_OBJECT (volume)));
          g_hash_table_iter_remove (&volume_iter);

          g_object_unref (G_OBJECT (volume));
        }
    }

  udisks_volume_group_set_needs_polling (UDISKS_VOLUME_GROUP (object->iface_volume_group),
                                         needs_polling);

  /* Update block objects. */
  new_pvs = g_hash_table_new (g_str_hash, g_str_equal);
  for (GSList *vg_pvs_p=vg_pvs; vg_pvs_p; vg_pvs_p=vg_pvs_p->next)
    {
      BDLVMPVdata *pv_info = vg_pvs_p->data;
      gchar *pv_name = pv_info->pv_name;
      if (pv_name)
        g_hash_table_insert (new_pvs, pv_name, pv_info);
    }

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
  for (l = objects; l != NULL; l = l->next)
    {
      if (UDISKS_IS_LINUX_BLOCK_OBJECT (l->data))
        update_block (UDISKS_LINUX_BLOCK_OBJECT (l->data), object, new_lvs, new_pvs);
    }
  g_list_free_full (objects, g_object_unref);

  g_hash_table_destroy (new_lvs);
  g_hash_table_destroy (new_pvs);

  g_slist_free_full (vg_pvs, (GDestroyNotify) bd_lvm_pvdata_free);
  bd_lvm_vgdata_free (vg_info);
  lv_list_free (lvs);

  g_object_unref (object);
}

void
udisks_linux_volume_group_object_update (UDisksLinuxVolumeGroupObject *object, BDLVMVGdata *vg_info, GSList *pvs)
{
  VGUpdateData *data = g_new0 (VGUpdateData, 1);
  gchar *vg_name = g_strdup (vg_info->name);
  GTask *task = NULL;

  data->vg_info = vg_info;
  data->vg_pvs = pvs;

  /* the callback (update_vg) is called in the default main loop (context) */
  task = g_task_new (g_object_ref (object), NULL /* cancellable */, update_vg, data /* callback_data */);
  g_task_set_task_data (task, vg_name, g_free);

  /* holds a reference to 'task' until it is finished */
  g_task_run_in_thread (task, (GTaskThreadFunc) lvs_task_func);

  g_object_unref (task);
}

static void
poll_vg_update (GObject      *source_obj,
                GAsyncResult *result,
                gpointer      user_data)
{
  UDisksDaemon *daemon;
  gboolean needs_polling;
  GError *error = NULL;
  UDisksLinuxVolumeGroupObject *object = UDISKS_LINUX_VOLUME_GROUP_OBJECT (source_obj);
  GTask *task = G_TASK (result);
  guint32 epoch_started = GPOINTER_TO_UINT (user_data);
  BDLVMLVdata **lvs = g_task_propagate_pointer (task, &error);

  if (epoch_started != object->poll_epoch)
    {
      /* epoch has changed -> another poll update is on the way */
      lv_list_free (lvs);
      g_object_unref (object);
      return;
    }

  if (!lvs)
    {
      if (error)
        {
          udisks_warning ("Failed to poll LVM volume group %s: %s",
                          udisks_linux_volume_group_object_get_name (object),
                          error->message);
          g_clear_error (&error);
        }
      else
        /* this should never happen */
        udisks_warning ("Failed to poll LVM volume group %s: no error reported",
                        udisks_linux_volume_group_object_get_name (object));

      g_object_unref (object);
      return;
    }

  daemon = udisks_linux_volume_group_object_get_daemon (object);

  /* XXX: we used to do this, but it seems to be pointless (how could a VG change without emitting a uevent on the PVs?) */
  /* udisks_linux_volume_group_update (UDISKS_LINUX_VOLUME_GROUP (object->iface_volume_group), info, &needs_polling); */

  for (BDLVMLVdata **lvs_p=lvs; *lvs_p; lvs_p++)
    {
      UDisksLinuxLogicalVolumeObject *volume;
      BDLVMLVdata *lv_info = *lvs_p;
      BDLVMLVdata *meta_lv_info = NULL;
      const gchar *lv_name = lv_info->lv_name;

      if (lv_info->metadata_lv && *(lv_info->metadata_lv) != '\0')
        /* this is not cheap to do, but not many LVs have a metadata LV */
        for (BDLVMLVdata **lvs_np=lvs; !meta_lv_info && *lvs_np; lvs_np++)
          if (cmp_int_lv_name ((*lvs_np)->lv_name, lv_info->metadata_lv))
            meta_lv_info = *lvs_np;

      update_operations (daemon, lv_name, lv_info, &needs_polling);
      volume = g_hash_table_lookup (object->logical_volumes, lv_name);
      if (volume)
        udisks_linux_logical_volume_object_update (volume, lv_info, meta_lv_info, &needs_polling);
    }

  lv_list_free (lvs);
  g_object_unref (object);
}

static void poll_now (UDisksLinuxVolumeGroupObject *object);

static gboolean
poll_in_main_thread (gpointer user_data)
{
  UDisksLinuxVolumeGroupObject *object = user_data;

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
  UDisksLinuxVolumeGroupObject *object = user_data;

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
poll_now (UDisksLinuxVolumeGroupObject *object)
{
  gchar *vg_name = g_strdup (udisks_linux_volume_group_object_get_name (object));
  GTask *task = NULL;

  object->poll_timeout_id = g_timeout_add (5000, poll_timeout, g_object_ref (object));

  /* starting a new poll -> increment the epoch */
  object->poll_epoch++;

  /* the callback (poll_vg_update) is called in the default main loop (context) */
  task = g_task_new (g_object_ref (object), NULL /* cancellable */,
                     poll_vg_update, GUINT_TO_POINTER (object->poll_epoch) /* callback_data */);
  g_task_set_task_data (task, vg_name, g_free);

  /* holds a reference to 'task' until it is finished */
  g_task_run_in_thread (task, (GTaskThreadFunc) lvs_task_func);

  g_object_unref (task);
}

void
udisks_linux_volume_group_object_poll (UDisksLinuxVolumeGroupObject *object)
{
  g_idle_add (poll_in_main_thread, g_object_ref (object));
}

void
udisks_linux_volume_group_object_destroy (UDisksLinuxVolumeGroupObject *object)
{
  GHashTableIter volume_iter;
  gpointer key, value;

  g_hash_table_iter_init (&volume_iter, object->logical_volumes);
  while (g_hash_table_iter_next (&volume_iter, &key, &value))
    {
      UDisksLinuxLogicalVolumeObject *volume = value;
      g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (object->daemon),
                                             g_dbus_object_get_object_path (G_DBUS_OBJECT (volume)));
    }
}

UDisksLinuxLogicalVolumeObject *
udisks_linux_volume_group_object_find_logical_volume_object (UDisksLinuxVolumeGroupObject *object,
                                                             const gchar                  *name)
{
  return g_hash_table_lookup (object->logical_volumes, name);
}

/**
 * udisks_linux_volume_group_object_get_name:
 * @object: A #UDisksLinuxVolumeGroupObject.
 *
 * Gets the name for @object.
 *
 * Returns: (transfer none): The name for object. Do not free, the string belongs to @object.
 */
const gchar *
udisks_linux_volume_group_object_get_name (UDisksLinuxVolumeGroupObject *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_VOLUME_GROUP_OBJECT (object), NULL);
  return object->name;
}
