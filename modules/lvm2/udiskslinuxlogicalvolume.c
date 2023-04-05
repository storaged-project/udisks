/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
 * Copyright (C) 2013 Marius Vollmer <marius.vollmer@redhat.com>
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
#include <fcntl.h>
#include <glib/gi18n-lib.h>
#include <stdio.h>

#include <blockdev/lvm.h>

#include <src/udiskslogging.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udisksdaemon.h>
#include <src/udisksstate.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslinuxblock.h>

#include "udiskslinuxlogicalvolume.h"
#include "udiskslinuxlogicalvolumeobject.h"
#include "udiskslinuxvolumegroup.h"
#include "udiskslinuxvolumegroupobject.h"

#include "udiskslvm2daemonutil.h"
#include "jobhelpers.h"

/**
 * SECTION:udiskslinuxlogicalvolume
 * @title: UDisksLinuxLogicalVolume
 * @short_description: Linux implementation of #UDisksLogicalVolume
 *
 * This type provides an implementation of the #UDisksLogicalVolume
 * interface on Linux.
 */

typedef struct _UDisksLinuxLogicalVolumeClass   UDisksLinuxLogicalVolumeClass;

/**
 * UDisksLinuxLogicalVolume:
 *
 * The #UDisksLinuxLogicalVolume structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxLogicalVolume
{
  UDisksLogicalVolumeSkeleton parent_instance;

  gboolean needs_udev_hack;
};

struct _UDisksLinuxLogicalVolumeClass
{
  UDisksLogicalVolumeSkeletonClass parent_class;
};

struct WaitData {
  UDisksLinuxVolumeGroupObject *group_object;
  const gchar *name;
};

static void logical_volume_iface_init (UDisksLogicalVolumeIface *iface);
static UDisksObject * wait_for_logical_volume_object (UDisksDaemon *daemon, gpointer user_data);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxLogicalVolume, udisks_linux_logical_volume,
                         UDISKS_TYPE_LOGICAL_VOLUME_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_LOGICAL_VOLUME, logical_volume_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_logical_volume_init (UDisksLinuxLogicalVolume *logical_volume)
{
  logical_volume->needs_udev_hack = TRUE;
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (logical_volume),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_logical_volume_constructed (GObject *_object)
{
  if (G_OBJECT_CLASS (udisks_linux_logical_volume_parent_class)->constructed != NULL)
      G_OBJECT_CLASS (udisks_linux_logical_volume_parent_class)->constructed (_object);
}

static void
udisks_linux_logical_volume_finalize (GObject *_object)
{
  if (G_OBJECT_CLASS (udisks_linux_logical_volume_parent_class)->finalize != NULL)
      G_OBJECT_CLASS (udisks_linux_logical_volume_parent_class)->finalize (_object);
}

static void
udisks_linux_logical_volume_class_init (UDisksLinuxLogicalVolumeClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize    = udisks_linux_logical_volume_finalize;
  gobject_class->constructed = udisks_linux_logical_volume_constructed;
}

/**
 * udisks_linux_logical_volume_new:
 *
 * Creates a new #UDisksLinuxLogicalVolume instance.
 *
 * Returns: A new #UDisksLinuxLogicalVolume. Free with g_object_unref().
 */
UDisksLogicalVolume *
udisks_linux_logical_volume_new (void)
{
  return UDISKS_LOGICAL_VOLUME (g_object_new (UDISKS_TYPE_LINUX_LOGICAL_VOLUME,
                                              NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static GVariant *
build_segment (UDisksDaemon *daemon,
               UDisksVolumeGroup *group,
               BDLVMSEGdata *seg)
{
  GVariantBuilder seg_builder;
  UDisksObject *block_object;
  guint64 extent_size = udisks_volume_group_get_extent_size (group);

  g_variant_builder_init (&seg_builder, G_VARIANT_TYPE ("(tto)"));
  g_variant_builder_add (&seg_builder, "t", seg->pv_start_pe * extent_size);
  g_variant_builder_add (&seg_builder, "t", seg->size_pe * extent_size);
  block_object = udisks_daemon_find_block_by_device_file (daemon, seg->pvdev);
  if (block_object)
    {
      g_variant_builder_add (&seg_builder, "o", g_dbus_object_get_object_path (G_DBUS_OBJECT (block_object)));
      g_object_unref (block_object);
    }
  else
    g_variant_builder_add (&seg_builder, "o", "/");
  return g_variant_builder_end (&seg_builder);
}

static GVariant *build_structure (UDisksDaemon *daemon, UDisksVolumeGroup *group, BDLVMLVdata *lv, BDLVMLVdata **all);

static gboolean
lvnameeq (gchar *name, gchar *maybe_bracketed_name)
{
  size_t len = strlen (maybe_bracketed_name);
  return (g_strcmp0 (name, maybe_bracketed_name) == 0
          || (maybe_bracketed_name[0] == '['
              && maybe_bracketed_name[len-1] == ']'
              && strlen (name) == len-2
              && strncmp (name, maybe_bracketed_name+1, len-2) == 0));
}

static GVariant *
build_structures (UDisksDaemon *daemon,
                  UDisksVolumeGroup *group,
                  gchar **lv_names,
                  BDLVMLVdata **all)
{
  GVariantBuilder list_builder;
  g_variant_builder_init (&list_builder, G_VARIANT_TYPE ("aa{sv}"));
  for (int i = 0; lv_names[i]; i++)
    {
      int j;
      for (j = 0; all[j]; j++) {
        if (lvnameeq (lv_names[i], all[j]->lv_name))
          break;
      }
      if (all[j])
        g_variant_builder_add_value (&list_builder, build_structure (daemon, group, all[j], all));
    }
  return g_variant_builder_end (&list_builder);
}

static GVariant *
build_structure (UDisksDaemon *daemon,
                 UDisksVolumeGroup *group,
                 BDLVMLVdata *lv,
                 BDLVMLVdata **all)
{
  GVariantBuilder lv_builder;

  g_variant_builder_init (&lv_builder, G_VARIANT_TYPE_VARDICT);
  if (lv->lv_name[0] == '[')
    {
      gchar *name = g_strndup (lv->lv_name + 1, strlen (lv->lv_name) - 2);
      g_variant_builder_add (&lv_builder, "{sv}", "name", g_variant_new_string (name));
      g_free (name);
    }
  else
    g_variant_builder_add (&lv_builder, "{sv}", "name", g_variant_new_string (lv->lv_name));

  g_variant_builder_add (&lv_builder, "{sv}", "type", g_variant_new_string (lv->segtype));
  g_variant_builder_add (&lv_builder, "{sv}", "size", g_variant_new_uint64 (lv->size));

  if (lv->segs) {
    GVariantBuilder segs_builder;
    g_variant_builder_init (&segs_builder, G_VARIANT_TYPE ("a(tto)"));
    for (int i = 0; lv->segs[i]; i++)
      g_variant_builder_add_value (&segs_builder, build_segment (daemon, group, lv->segs[i]));
    g_variant_builder_add (&lv_builder, "{sv}", "segments", g_variant_builder_end (&segs_builder));
  } else {
    if (lv->data_lvs)
      g_variant_builder_add (&lv_builder, "{sv}", "data", build_structures (daemon, group, lv->data_lvs, all));
    if (lv->metadata_lvs)
      g_variant_builder_add (&lv_builder, "{sv}", "metadata", build_structures (daemon, group, lv->metadata_lvs, all));
  }

  return g_variant_builder_end (&lv_builder);
}

/**
 * udisks_linux_logical_volume_update:
 * @logical_volume: A #UDisksLinuxLogicalVolume.
 * @vg: LVM volume group
 * @lv: LVM logical volume
 *
 * Updates the interface.
 */
void
udisks_linux_logical_volume_update (UDisksLinuxLogicalVolume     *logical_volume,
                                    UDisksLinuxVolumeGroupObject *group_object,
                                    BDLVMLVdata                  *lv_info,
                                    BDLVMLVdata                  *meta_lv_info,
                                    BDLVMLVdata                 **all_lv_infos,
                                    gboolean                     *needs_polling_ret)
{
  UDisksLinuxModuleLVM2 *module;
  UDisksDaemon *daemon;
  UDisksLogicalVolume *iface;
  UDisksVolumeGroup *group;
  const char *type;
  gboolean active;
  const char *pool_objpath;
  const char *origin_objpath;
  guint64 size = 0;

  module = udisks_linux_volume_group_object_get_module (group_object);
  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));

  iface = UDISKS_LOGICAL_VOLUME (logical_volume);
  group = udisks_object_get_volume_group (UDISKS_OBJECT (group_object));

  udisks_logical_volume_set_name (iface, lv_info->lv_name);
  udisks_logical_volume_set_uuid (iface, lv_info->uuid);

  size = lv_info->size;
  type = "block";
  active = FALSE;
  if (lv_info->attr)
    {
      gchar volume_type = lv_info->attr[0];
      gchar state       = lv_info->attr[4];
      gchar target_type = lv_info->attr[6];

      if (target_type == 't')
        *needs_polling_ret = TRUE;

      if (target_type == 't' && volume_type == 't')
        type = "pool";
      else if (volume_type == 'd')
        type = "vdopool";
      if (meta_lv_info && meta_lv_info->size)
        size += meta_lv_info->size;

      if (state == 'a')
        active = TRUE;
    }
  udisks_logical_volume_set_type_ (iface, type);
  if (g_strcmp0 (lv_info->segtype, "error") == 0)
    udisks_logical_volume_set_layout (iface, "linear");
  else
    udisks_logical_volume_set_layout (iface, lv_info->segtype);
  if ((g_str_has_prefix (lv_info->segtype, "raid") && g_strcmp0 (lv_info->segtype, "raid0") != 0)
      || g_strcmp0 (lv_info->segtype, "mirror") == 0)
    {
      udisks_logical_volume_set_sync_ratio (iface, lv_info->copy_percent / 100.0);
      if (lv_info->copy_percent != 100)
        *needs_polling_ret = TRUE;
    }
  else
    udisks_logical_volume_set_sync_ratio (iface, 1.0);

  udisks_logical_volume_set_active (iface, active);
  udisks_logical_volume_set_size (iface, size);

  /* LV is not active --> no block device
     XXX: Object path for active LVs is not set here because this runs before
          block device update, so it is possible that the block device is not
          added yet. BlockDevice property for active LVs is set when updating
          the block device.
   */
  if (!active)
    udisks_logical_volume_set_block_device (iface, "/");

  udisks_logical_volume_set_data_allocated_ratio (iface, lv_info->data_percent / 100.0);
  udisks_logical_volume_set_metadata_allocated_ratio (iface, lv_info->metadata_percent / 100.0);

  pool_objpath = "/";
  if (lv_info->pool_lv && g_strcmp0 (lv_info->segtype, "thin") == 0)
    {
      UDisksLinuxLogicalVolumeObject *pool_object = udisks_linux_volume_group_object_find_logical_volume_object (group_object, lv_info->pool_lv);
      if (pool_object)
        pool_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
    }
  udisks_logical_volume_set_thin_pool (iface, pool_objpath);

  origin_objpath = "/";
  if (lv_info->origin)
    {
      UDisksLinuxLogicalVolumeObject *origin_object = udisks_linux_volume_group_object_find_logical_volume_object (group_object, lv_info->origin);
      if (origin_object)
        origin_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (origin_object));
    }
  udisks_logical_volume_set_origin (iface, origin_objpath);

  udisks_logical_volume_set_volume_group (iface, g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object)));

  if (logical_volume->needs_udev_hack)
    {
      gchar *dev_file = g_strdup_printf ("/dev/%s/%s", lv_info->vg_name, lv_info->lv_name);
      /* LVM2 versions before 2.02.105 sometimes incorrectly leave the
       * DM_UDEV_DISABLE_OTHER_RULES flag set for thin volumes. As a
       * workaround, we trigger an extra udev "change" event which
       * will clear this up.
       *
       * https://www.redhat.com/archives/linux-lvm/2014-January/msg00030.html
       */
      udisks_daemon_util_lvm2_trigger_udev (dev_file);
      logical_volume->needs_udev_hack = FALSE;
      g_free (dev_file);
    }

  udisks_logical_volume_set_structure (iface, build_structure (daemon, group, lv_info, all_lv_infos));

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (iface));
  g_clear_object (&group);
}

void
udisks_linux_logical_volume_update_etctabs (UDisksLinuxLogicalVolume     *logical_volume,
                                            UDisksLinuxVolumeGroupObject *group_object)
{
  UDisksLinuxModuleLVM2 *module;
  UDisksDaemon *daemon;
  UDisksLogicalVolume *iface;
  const gchar *uuid;

  module = udisks_linux_volume_group_object_get_module (group_object);
  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  iface = UDISKS_LOGICAL_VOLUME (logical_volume);
  uuid = udisks_logical_volume_get_uuid (iface);

  udisks_logical_volume_set_child_configuration (iface,
                                                 udisks_linux_find_child_configuration (daemon,
                                                                                        uuid));
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (iface));
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksBlock *
peek_block_for_logical_volume (UDisksLogicalVolume *volume,
                               UDisksDaemon        *daemon)
{
  UDisksBlock *ret = NULL;
  GDBusObject *object;
  GList *l, *objects = NULL;
  UDisksBlockLVM2 *block_lvm2;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (volume));
  if (object == NULL)
    goto out;

  objects = udisks_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      block_lvm2 = udisks_object_peek_block_lvm2 (UDISKS_OBJECT(l->data));
      if (block_lvm2 &&
          g_strcmp0 (udisks_block_lvm2_get_logical_volume (block_lvm2),
                     g_dbus_object_get_object_path (object)) == 0)
        {
          ret = udisks_object_peek_block (UDISKS_OBJECT(l->data));
          goto out;
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

gboolean
udisks_linux_logical_volume_teardown_block (UDisksLogicalVolume   *volume,
                                            UDisksDaemon          *daemon,
                                            GDBusMethodInvocation *invocation,
                                            GVariant              *options,
                                            GError               **error)
{
  UDisksBlock *block;

  block = peek_block_for_logical_volume (volume, daemon);
  if (block)
    {
      /* The volume is active.  Tear down its block device.
       */
      if (!udisks_linux_block_teardown (block,
                                        invocation,
                                        options,
                                        error))
        return FALSE;
    }
  else
    {
      /* The volume is inactive.  Remove the child configurations.
       */
      if (!udisks_linux_remove_configuration (udisks_logical_volume_get_child_configuration (volume),
                                              error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
teardown_logical_volume (UDisksLogicalVolume   *volume,
                         UDisksDaemon          *daemon,
                         GDBusMethodInvocation *invocation,
                         GVariant              *options,
                         GError               **error)
{
  GDBusObject *volume_object;
  UDisksObject *group_object;
  UDisksVolumeGroup *group;
  UDisksLogicalVolume *sibling_volume;
  GList *siblings;
  GList *l;

  if (!udisks_linux_logical_volume_teardown_block (volume,
                                                   daemon,
                                                   invocation,
                                                   options,
                                                   error))
    return FALSE;

  /* Recurse for pool members and snapshots.
   */

  volume_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (volume));
  group_object = udisks_daemon_find_object (daemon, udisks_logical_volume_get_volume_group (volume));
  if (volume_object && group_object)
    {
      group = udisks_object_peek_volume_group (group_object);
      if (group)
        {
          siblings = udisks_linux_volume_group_get_logical_volumes (group, daemon);
          for (l = siblings; l; l = l->next)
            {
              sibling_volume = UDISKS_LOGICAL_VOLUME (l->data);
              if (g_strcmp0 (udisks_logical_volume_get_thin_pool (sibling_volume),
                             g_dbus_object_get_object_path (volume_object)) == 0 ||
                  g_strcmp0 (udisks_logical_volume_get_origin (sibling_volume),
                             g_dbus_object_get_object_path (volume_object)) == 0)
                {
                  if (!teardown_logical_volume (sibling_volume,
                                                daemon,
                                                invocation,
                                                options,
                                                error))
                    {
                      g_list_free_full (siblings, g_object_unref);
                      g_clear_object (&group_object);
                      return FALSE;
                    }
                }
            }
          g_list_free_full (siblings, g_object_unref);
        }
    }

  g_clear_object (&group_object);
  return TRUE;
}

static gboolean
common_setup (UDisksLinuxLogicalVolume           *volume,
              GDBusMethodInvocation              *invocation,
              GVariant                           *options,
              const gchar                        *auth_err_msg,
              UDisksLinuxLogicalVolumeObject    **object,
              UDisksDaemon                      **daemon,
              uid_t                              *out_uid)
{
  UDisksLinuxModuleLVM2 *module;
  gboolean rc = FALSE;
  GError *error = NULL;

  *object = udisks_daemon_util_dup_object (volume, &error);
  if (*object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  module = udisks_linux_logical_volume_object_get_module (*object);
  *daemon = udisks_module_get_daemon (UDISKS_MODULE (module));

  if (!udisks_daemon_util_get_caller_uid_sync (*daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               out_uid,
                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (*daemon,
                                     UDISKS_OBJECT (*object),
                                     LVM2_POLICY_ACTION_ID,
                                     options,
                                     auth_err_msg,
                                     invocation);
  rc = TRUE;
 out:
  return rc;
}

static gboolean
handle_delete (UDisksLogicalVolume   *_volume,
               GDBusMethodInvocation *invocation,
               GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon = NULL;
  uid_t caller_uid;
  gboolean teardown_flag = FALSE;
  UDisksLinuxVolumeGroupObject *group_object;
  LVJobData data;
  struct WaitData wait_data;

  g_variant_lookup (options, "tear-down", "b", &teardown_flag);

  if (!common_setup (volume, invocation, options,
                     N_("Authentication is required to delete a logical volume"),
                     &object, &daemon, &caller_uid))
    goto out;

  if (teardown_flag &&
      !teardown_logical_volume (_volume,
                                daemon,
                                invocation,
                                options,
                                &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lvol-delete",
                                               caller_uid,
                                               lvremove_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error deleting logical volume: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  wait_data.group_object = group_object;
  wait_data.name = data.lv_name;

  if (! udisks_daemon_wait_for_object_to_disappear_sync (daemon,
                                                         wait_for_logical_volume_object,
                                                         &wait_data,
                                                         NULL,
                                                         UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                         &error))
    {
      g_prefix_error (&error,
                      "Error waiting for block object to disappear after deleting '%s': ",
                      udisks_logical_volume_get_name (_volume));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_logical_volume_complete_delete (_volume, invocation);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_repair (UDisksLogicalVolume   *_volume,
               GDBusMethodInvocation *invocation,
               const gchar *const    *arg_pvs,
               GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon = NULL;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  LVJobData data = {0};
  g_auto(GStrv) pvs = NULL;

  if (!common_setup (volume, invocation, options,
                     N_("Authentication is required to repair a logical volume"),
                     &object, &daemon, &caller_uid))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);
  pvs = udisks_daemon_util_lvm2_gather_pvs (daemon, group_object, arg_pvs, &error);
  if (pvs == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  data.new_lv_pvs = (const gchar **)pvs;

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lvol-repair",
                                               caller_uid,
                                               lvrepair_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error repairing logical volume: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_logical_volume_complete_repair (_volume, invocation);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksObject *
wait_for_logical_volume_object (UDisksDaemon *daemon,
                                gpointer      user_data)
{
  struct WaitData *data = user_data;
  UDisksLinuxLogicalVolumeObject *object;

  object = udisks_linux_volume_group_object_find_logical_volume_object (data->group_object, data->name);
  if (object == NULL)
    return NULL;

  return g_object_ref (UDISKS_OBJECT (object));
}

static const gchar *
wait_for_logical_volume_path (UDisksLinuxVolumeGroupObject  *group_object,
                              const gchar                   *name,
                              GError                       **error)
{
  struct WaitData data;
  UDisksLinuxModuleLVM2 *module;
  UDisksDaemon *daemon;
  UDisksObject *volume_object;
  const gchar *object_path;

  data.group_object = group_object;
  data.name = name;
  module = udisks_linux_volume_group_object_get_module (group_object);
  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  volume_object = udisks_daemon_wait_for_object_sync (daemon,
                                                      wait_for_logical_volume_object,
                                                      &data,
                                                      NULL,
                                                      UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                      error);
  if (volume_object == NULL)
    return NULL;

  object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (volume_object));
  g_object_unref (volume_object);

  return object_path;
}

static gboolean
handle_rename (UDisksLogicalVolume   *_volume,
               GDBusMethodInvocation *invocation,
               const gchar           *new_name,
               GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  const gchar *lv_objpath;
  LVJobData data;

  if (!common_setup (volume, invocation, options,
                     N_("Authentication is required to rename a logical volume"),
                     &object, &daemon, &caller_uid))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);
  data.new_lv_name = new_name;

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lvol-rename",
                                               caller_uid,
                                               lvrename_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error renaming logical volume: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  lv_objpath = wait_for_logical_volume_path (group_object, new_name, &error);
  if (lv_objpath == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for logical volume object for '%s': ",
                      new_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_logical_volume_complete_rename (_volume, invocation, lv_objpath);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_resize (UDisksLogicalVolume   *_volume,
               GDBusMethodInvocation *invocation,
               guint64                new_size,
               GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  LVJobData data;
  gchar **opt_pvs = NULL;
  g_auto(GStrv) pvs = NULL;

  if (!common_setup (volume, invocation, options,
                     N_("Authentication is required to resize a logical volume"),
                     &object, &daemon, &caller_uid))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);
  data.new_lv_size = new_size;

  data.resize_fs = FALSE;
  data.force = FALSE;
  g_variant_lookup (options, "resize_fsys", "b", &(data.resize_fs));
  g_variant_lookup (options, "force", "b", &(data.force));
  g_variant_lookup (options, "pvs", "^a&o", &opt_pvs);

  if (opt_pvs)
    {
      pvs = udisks_daemon_util_lvm2_gather_pvs (daemon, group_object, (const gchar **)opt_pvs, &error);
      if (pvs == NULL)
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
    }

  data.new_lv_pvs = (const gchar **)pvs;

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lvol-resize",
                                               caller_uid,
                                               lvresize_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error resizing logical volume: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_logical_volume_complete_resize (_volume, invocation);

 out:
  g_clear_object (&object);
  g_free (opt_pvs);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

struct WaitForBlockData {
  UDisksLinuxLogicalVolumeObject *volume_object;
};

static UDisksObject *
wait_for_logical_volume_block_object (UDisksDaemon *daemon,
                                      gpointer      user_data)
{
  UDisksLinuxLogicalVolumeObject *volume_object = user_data;
  const gchar *volume_objpath;
  GList *objects, *l;
  UDisksObject *ret = NULL;

  volume_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (volume_object));

  objects = udisks_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlockLVM2 *block;

      block = udisks_object_peek_block_lvm2 (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_lvm2_get_logical_volume (block), volume_objpath) == 0)
        {
          ret = g_object_ref (object);
          goto out;
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

static gboolean
handle_activate (UDisksLogicalVolume *_volume,
                 GDBusMethodInvocation *invocation,
                 GVariant *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  UDisksObject *block_object = NULL;
  LVJobData data;

  if (!common_setup (volume, invocation, options,
                     N_("Authentication is required to activate a logical volume"),
                     &object, &daemon, &caller_uid))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lvol-activate",
                                               caller_uid,
                                               lvactivate_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))

    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error activating logical volume: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  block_object = udisks_daemon_wait_for_object_sync (daemon,
                                                     wait_for_logical_volume_block_object,
                                                     object,
                                                     NULL,
                                                     UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                     &error);
  if (block_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for block object for '%s': ",
                      udisks_logical_volume_get_name (_volume));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_logical_volume_complete_activate (_volume, invocation,
                                           g_dbus_object_get_object_path (G_DBUS_OBJECT (block_object)));

 out:
  g_clear_object (&block_object);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_deactivate (UDisksLogicalVolume   *_volume,
                   GDBusMethodInvocation *invocation,
                   GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  LVJobData data;

  if (!common_setup (volume, invocation, options,
                     N_("Authentication is required to deactivate a logical volume"),
                     &object, &daemon, &caller_uid))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lvol-deactivate",
                                               caller_uid,
                                               lvdeactivate_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error deactivating logical volume: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  if (! udisks_daemon_wait_for_object_to_disappear_sync (daemon,
                                                         wait_for_logical_volume_block_object,
                                                         object,
                                                         NULL,
                                                         UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                         &error))
    {
      g_prefix_error (&error,
                      "Error waiting for block object to disappear after deactivating '%s': ",
                      udisks_logical_volume_get_name (_volume));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_logical_volume_complete_deactivate (_volume, invocation);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_create_snapshot (UDisksLogicalVolume   *_volume,
                        GDBusMethodInvocation *invocation,
                        const gchar           *name,
                        guint64                size,
                        GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  const gchar *lv_objpath = NULL;
  LVJobData data;

  if (!common_setup (volume, invocation, options,
                     N_("Authentication is required to create a snapshot of a logical volume"),
                     &object, &daemon, &caller_uid))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);
  data.new_lv_name = name;
  data.new_lv_size = size;

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lvol-snapshot",
                                               caller_uid,
                                               lvsnapshot_create_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error creating snapshot: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  lv_objpath = wait_for_logical_volume_path (group_object, name, &error);
  if (lv_objpath == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for logical volume object for '%s': ",
                      name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_logical_volume_complete_create_snapshot (_volume, invocation, lv_objpath);

 out:
  g_clear_object (&object);
  return TRUE;
}

static gboolean
handle_cache_attach (UDisksLogicalVolume   *volume_,
                     GDBusMethodInvocation *invocation,
                     const gchar           *cache_name,
                     GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (volume_);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  LVJobData data;

  if (!common_setup (volume, invocation, options,
                     N_("Authentication is required to convert logical volume to cache"),
                     &object, &daemon, &caller_uid))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);
  data.pool_name = cache_name;

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lv-make-cache",
                                               caller_uid,
                                               lvcache_attach_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             N_("Error converting volume: %s"),
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_logical_volume_complete_cache_attach (volume_, invocation);
out:
  g_clear_object (&object);

  return TRUE;
}


static gboolean
handle_cache_detach_or_split (UDisksLogicalVolume    *volume_,
                              GDBusMethodInvocation  *invocation,
                              GVariant               *options,
                              gboolean                destroy)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (volume_);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  LVJobData data;

  if (!common_setup (volume, invocation, options,
                     N_("Authentication is required to split cache pool LV off of a cache LV"),
                     &object, &daemon, &caller_uid))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);
  data.destroy = destroy;

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lv-split-cache",
                                               caller_uid,
                                               lvcache_detach_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             N_("Error converting volume: %s"),
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_logical_volume_complete_cache_split (volume_, invocation);
out:
  g_clear_object (&object);

  return TRUE;
}

static gboolean
handle_cache_split (UDisksLogicalVolume    *volume_,
                    GDBusMethodInvocation  *invocation,
                    GVariant               *options)
{
  return handle_cache_detach_or_split(volume_, invocation, options, FALSE);
}

static gboolean
handle_cache_detach (UDisksLogicalVolume    *volume_,
                     GDBusMethodInvocation  *invocation,
                     GVariant               *options)
{
  return handle_cache_detach_or_split(volume_, invocation, options, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
logical_volume_iface_init (UDisksLogicalVolumeIface *iface)
{
  iface->handle_delete = handle_delete;
  iface->handle_rename = handle_rename;
  iface->handle_resize = handle_resize;
  iface->handle_repair = handle_repair;
  iface->handle_activate = handle_activate;
  iface->handle_deactivate = handle_deactivate;
  iface->handle_create_snapshot = handle_create_snapshot;

  iface->handle_cache_attach = handle_cache_attach;
  iface->handle_cache_split = handle_cache_split;
  iface->handle_cache_detach = handle_cache_detach;
}
