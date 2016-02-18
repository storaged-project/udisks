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

#include <lvm.h>

#include <src/storagedlogging.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storageddaemon.h>
#include <src/storagedstate.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlinuxblock.h>

#include "storagedlinuxlogicalvolume.h"
#include "storagedlinuxlogicalvolumeobject.h"
#include "storagedlinuxvolumegroup.h"
#include "storagedlinuxvolumegroupobject.h"

#include "storagedlvm2daemonutil.h"
#include "storagedlvm2dbusutil.h"
#include "storagedlvm2util.h"
#include "storaged-lvm2-generated.h"

/**
 * SECTION:storagedlinuxlogicalvolume
 * @title: StoragedLinuxLogicalVolume
 * @short_description: Linux implementation of #StoragedLogicalVolume
 *
 * This type provides an implementation of the #StoragedLogicalVolume
 * interface on Linux.
 */

typedef struct _StoragedLinuxLogicalVolumeClass   StoragedLinuxLogicalVolumeClass;

/**
 * StoragedLinuxLogicalVolume:
 *
 * The #StoragedLinuxLogicalVolume structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxLogicalVolume
{
  StoragedLogicalVolumeSkeleton parent_instance;

  gboolean needs_udev_hack;
};

struct _StoragedLinuxLogicalVolumeClass
{
  StoragedLogicalVolumeSkeletonClass parent_class;
};

static void logical_volume_iface_init (StoragedLogicalVolumeIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxLogicalVolume, storaged_linux_logical_volume,
                         STORAGED_TYPE_LOGICAL_VOLUME_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_LOGICAL_VOLUME, logical_volume_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_logical_volume_init (StoragedLinuxLogicalVolume *logical_volume)
{
  logical_volume->needs_udev_hack = TRUE;
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (logical_volume),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_logical_volume_class_init (StoragedLinuxLogicalVolumeClass *klass)
{
}

/**
 * storaged_linux_logical_volume_new:
 *
 * Creates a new #StoragedLinuxLogicalVolume instance.
 *
 * Returns: A new #StoragedLinuxLogicalVolume. Free with g_object_unref().
 */
StoragedLogicalVolume *
storaged_linux_logical_volume_new (void)
{
  return STORAGED_LOGICAL_VOLUME (g_object_new (STORAGED_TYPE_LINUX_LOGICAL_VOLUME,
                                                NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_logical_volume_update:
 * @logical_volume: A #StoragedLinuxLogicalVolume.
 * @vg: LVM volume group
 * @lv: LVM logical volume
 *
 * Updates the interface.
 */
void
storaged_linux_logical_volume_update (StoragedLinuxLogicalVolume     *logical_volume,
                                      StoragedLinuxVolumeGroupObject *group_object,
                                      GVariant                       *info,
                                      gboolean                       *needs_polling_ret)
{
  StoragedLogicalVolume *iface;
  const char *type;
  gboolean active;
  const char *pool_objpath;
  const char *origin_objpath;
  const gchar *dev_file;
  const gchar *str;
  const gchar *uuid;
  guint64 num;

  iface = STORAGED_LOGICAL_VOLUME (logical_volume);

  if (g_variant_lookup (info, "name", "&s", &str))
    storaged_logical_volume_set_name (iface, str);

  if (g_variant_lookup (info, "uuid", "&s", &uuid))
    storaged_logical_volume_set_uuid (iface, uuid);

  if (g_variant_lookup (info, "size", "t", &num))
    storaged_logical_volume_set_size (iface, num);

  type = "block";
  active = FALSE;
  if (g_variant_lookup (info, "lv_attr", "&s", &str)
      && str && strlen (str) > 6)
    {
      char volume_type = str[0];
      char state       = str[4];
      char target_type = str[6];

      if (target_type == 't')
        *needs_polling_ret = TRUE;

      if (target_type == 't' && volume_type == 't')
        type = "pool";

      if (state == 'a')
        active = TRUE;
    }
  storaged_logical_volume_set_type_ (iface, type);
  storaged_logical_volume_set_active (iface, active);

  if (g_variant_lookup (info, "data_percent", "t", &num)
      && (int64_t)num >= 0)
    storaged_logical_volume_set_data_allocated_ratio (iface, num/100000000.0);

  if (g_variant_lookup (info, "metadata_percent", "t", &num)
      && (int64_t)num >= 0)
    storaged_logical_volume_set_metadata_allocated_ratio (iface, num/100000000.0);

  pool_objpath = "/";
  if (g_variant_lookup (info, "pool_lv", "&s", &str)
      && str != NULL && *str)
    {
      StoragedLinuxLogicalVolumeObject *pool_object = storaged_linux_volume_group_object_find_logical_volume_object (group_object, str);
      if (pool_object)
        pool_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
    }
  storaged_logical_volume_set_thin_pool (iface, pool_objpath);

  origin_objpath = "/";
  if (g_variant_lookup (info, "origin", "&s", &str)
      && str != NULL && *str)
    {
      StoragedLinuxLogicalVolumeObject *origin_object = storaged_linux_volume_group_object_find_logical_volume_object (group_object, str);
      if (origin_object)
        origin_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (origin_object));
    }
  storaged_logical_volume_set_origin (iface, origin_objpath);

  storaged_logical_volume_set_volume_group (iface, g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object)));

  dev_file = NULL;
  if (logical_volume->needs_udev_hack
      && g_variant_lookup (info, "lv_path", "&s", &dev_file))
    {
      /* LVM2 versions before 2.02.105 sometimes incorrectly leave the
       * DM_UDEV_DISABLE_OTHER_RULES flag set for thin volumes. As a
       * workaround, we trigger an extra udev "change" event which
       * will clear this up.
       *
       * https://www.redhat.com/archives/linux-lvm/2014-January/msg00030.html
       */
      storaged_daemon_util_lvm2_trigger_udev (dev_file);
      logical_volume->needs_udev_hack = FALSE;
    }
}

void
storaged_linux_logical_volume_update_etctabs (StoragedLinuxLogicalVolume     *logical_volume,
                                              StoragedLinuxVolumeGroupObject *group_object)
{
  StoragedDaemon *daemon;
  StoragedLogicalVolume *iface;
  const gchar *uuid;

  daemon = storaged_linux_volume_group_object_get_daemon (group_object);
  iface = STORAGED_LOGICAL_VOLUME (logical_volume);
  uuid = storaged_logical_volume_get_uuid (iface);

  storaged_logical_volume_set_child_configuration (iface,
                                                   storaged_linux_find_child_configuration (daemon,
                                                                                            uuid));
}

/* ---------------------------------------------------------------------------------------------------- */

static StoragedBlock *
peek_block_for_logical_volume (StoragedLogicalVolume *volume,
                               StoragedDaemon        *daemon)
{
  StoragedBlock *ret = NULL;
  GDBusObject *object;
  GList *l, *objects = NULL;
  StoragedBlockLVM2 *block_lvm2;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (volume));
  if (object == NULL)
    goto out;

  objects = storaged_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      block_lvm2 = storaged_object_peek_block_lvm2 (STORAGED_OBJECT(l->data));
      if (block_lvm2 &&
          g_strcmp0 (storaged_block_lvm2_get_logical_volume (block_lvm2),
                     g_dbus_object_get_object_path (object)) == 0)
        {
          ret = storaged_object_peek_block (STORAGED_OBJECT(l->data));
          goto out;
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

gboolean
storaged_linux_logical_volume_teardown_block (StoragedLogicalVolume *volume,
                                              StoragedDaemon        *daemon,
                                              GDBusMethodInvocation *invocation,
                                              GVariant              *options,
                                              GError               **error)
{
  StoragedBlock *block;

  block = peek_block_for_logical_volume (volume, daemon);
  if (block)
    {
      /* The volume is active.  Tear down its block device.
       */
      if (!storaged_linux_block_teardown (block,
                                          invocation,
                                          options,
                                          error))
        return FALSE;
    }
  else
    {
      /* The volume is inactive.  Remove the child configurations.
       */
      if (!storaged_linux_remove_configuration (storaged_logical_volume_get_child_configuration (volume),
                                                error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
teardown_logical_volume (StoragedLogicalVolume *volume,
                         StoragedDaemon        *daemon,
                         GDBusMethodInvocation *invocation,
                         GVariant              *options,
                         GError               **error)
{
  GDBusObject *volume_object;
  StoragedObject *group_object;
  StoragedVolumeGroup *group;
  StoragedLogicalVolume *sibling_volume;
  GList *siblings;
  GList *l;

  if (!storaged_linux_logical_volume_teardown_block (volume,
                                                     daemon,
                                                     invocation,
                                                     options,
                                                     error))
    return FALSE;

  /* Recurse for pool members and snapshots.
   */

  volume_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (volume));
  group_object = storaged_daemon_find_object (daemon, storaged_logical_volume_get_volume_group (volume));
  if (volume_object && group_object)
    {
      group = storaged_object_peek_volume_group (group_object);
      if (group)
        {
          siblings = storaged_linux_volume_group_get_logical_volumes (group, daemon);
          for (l = siblings; l; l = l->next)
            {
              sibling_volume = STORAGED_LOGICAL_VOLUME (l->data);
              if (g_strcmp0 (storaged_logical_volume_get_thin_pool (sibling_volume),
                             g_dbus_object_get_object_path (volume_object)) == 0 ||
                  g_strcmp0 (storaged_logical_volume_get_origin (sibling_volume),
                             g_dbus_object_get_object_path (volume_object)) == 0)
                {
                  if (!teardown_logical_volume (sibling_volume,
                                                daemon,
                                                invocation,
                                                options,
                                                error))
                    {
                      g_list_free_full (siblings, g_object_unref);
                      return FALSE;
                    }
                }
            }
          g_list_free_full (siblings, g_object_unref);
        }
    }

  return TRUE;
}

static gboolean
handle_delete (StoragedLogicalVolume *_volume,
               GDBusMethodInvocation *invocation,
               GVariant              *options)
{
  GError *error = NULL;
  StoragedLinuxLogicalVolume *volume = STORAGED_LINUX_LOGICAL_VOLUME (_volume);
  StoragedLinuxLogicalVolumeObject *object = NULL;
  StoragedDaemon *daemon;
  gboolean teardown_flag = FALSE;
  StoragedLinuxVolumeGroupObject *group_object;
  const gchar *vg_name;
  const gchar *lv_name;

  g_variant_lookup (options, "tear-down", "b", &teardown_flag);

  object = storaged_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_logical_volume_object_get_daemon (object);


  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to delete a logical volume"),
                                       invocation);

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

  group_object = storaged_linux_logical_volume_object_get_volume_group (object);
  vg_name = storaged_linux_volume_group_object_get_name (group_object);
  lv_name = storaged_linux_logical_volume_object_get_name (object);

  if (bd_lvm_lvremove ((gchar *)vg_name, (gchar *)lv_name, FALSE, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error deleting logical volume: %s",
                                             error->message);
      g_error_free (error);
      goto out;
    }

  storaged_logical_volume_complete_delete (_volume, invocation);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

struct WaitData {
  StoragedLinuxVolumeGroupObject *group_object;
  const gchar *name;
};

static StoragedObject *
wait_for_logical_volume_object (StoragedDaemon *daemon,
                                gpointer        user_data)
{
  struct WaitData *data = user_data;
  return STORAGED_OBJECT (storaged_linux_volume_group_object_find_logical_volume_object (data->group_object,
                                                                                       data->name));
}

static const gchar *
wait_for_logical_volume_path (StoragedLinuxVolumeGroupObject *group_object,
                              const gchar                    *name,
                              GError                        **error)
{
  struct WaitData data;
  StoragedDaemon *daemon;
  StoragedObject *volume_object;

  data.group_object = group_object;
  data.name = name;
  daemon = storaged_linux_volume_group_object_get_daemon (group_object);
  volume_object = storaged_daemon_wait_for_object_sync (daemon,
                                                        wait_for_logical_volume_object,
                                                        &data,
                                                        NULL,
                                                        10, /* timeout_seconds */
                                                        error);
  if (volume_object == NULL)
    return NULL;

  return g_dbus_object_get_object_path (G_DBUS_OBJECT (volume_object));
}

static gboolean
handle_rename (StoragedLogicalVolume   *_volume,
               GDBusMethodInvocation   *invocation,
               const gchar             *new_name,
               GVariant                *options)
{
  GError *error = NULL;
  StoragedLinuxLogicalVolume *volume = STORAGED_LINUX_LOGICAL_VOLUME (_volume);
  StoragedLinuxLogicalVolumeObject *object = NULL;
  StoragedDaemon *daemon;
  uid_t caller_uid;
  gid_t caller_gid;
  StoragedLinuxVolumeGroupObject *group_object;
  gchar *escaped_group_name = NULL;
  gchar *escaped_name = NULL;
  gchar *escaped_new_name = NULL;
  gchar *error_message = NULL;
  const gchar *lv_objpath;

  object = storaged_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_logical_volume_object_get_daemon (object);

  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &caller_gid,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to rename a logical volume"),
                                       invocation);

  group_object = storaged_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = storaged_daemon_util_escape_and_quote (storaged_linux_volume_group_object_get_name (group_object));
  escaped_name = storaged_daemon_util_escape_and_quote (storaged_linux_logical_volume_object_get_name (object));
  escaped_new_name = storaged_daemon_util_escape_and_quote (new_name);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                STORAGED_OBJECT (object),
                                                "lvm-vg-rename", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "lvrename %s/%s %s",
                                                escaped_group_name,
                                                escaped_name,
                                                escaped_new_name))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error renaming volume volume: %s",
                                             error_message);
      goto out;
    }

  lv_objpath = wait_for_logical_volume_path (group_object, new_name, &error);
  if (lv_objpath == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for logical volume object for %s",
                      new_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_logical_volume_complete_rename (_volume, invocation, lv_objpath);

 out:
  g_free (error_message);
  g_free (escaped_name);
  g_free (escaped_group_name);
  g_free (escaped_new_name);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_resize (StoragedLogicalVolume *_volume,
               GDBusMethodInvocation *invocation,
               guint64                new_size,
               GVariant              *options)
{
  GError *error = NULL;
  StoragedLinuxLogicalVolume *volume = STORAGED_LINUX_LOGICAL_VOLUME (_volume);
  StoragedLinuxLogicalVolumeObject *object = NULL;
  StoragedDaemon *daemon;
  uid_t caller_uid;
  gid_t caller_gid;
  StoragedLinuxVolumeGroupObject *group_object;
  GString *cmd = NULL;
  gchar *escaped_group_name;
  gchar *escaped_name;
  gchar *error_message = NULL;
  gboolean resize_fsys = FALSE;

  object = storaged_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_logical_volume_object_get_daemon (object);

  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &caller_gid,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to resize a logical volume"),
                                       invocation);

  /* TODO: libblockdev can't resize generic file systems */
  group_object = storaged_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = storaged_daemon_util_escape_and_quote (storaged_linux_volume_group_object_get_name (group_object));
  escaped_name = storaged_daemon_util_escape_and_quote (storaged_linux_logical_volume_object_get_name (object));
  new_size -= new_size % 512;

  g_variant_lookup (options, "resize_fsys", "b", &resize_fsys);

  cmd = g_string_new ("");
  g_string_append_printf (cmd, "lvresize %s/%s -L %" G_GUINT64_FORMAT "b",
                          escaped_group_name, escaped_name, new_size);
  if (resize_fsys)
    g_string_append (cmd, " -r");

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                STORAGED_OBJECT (object),
                                                "lvm-vg-resize", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "%s", cmd->str))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error resizing logical volume: %s",
                                             error_message);
      goto out;
    }

  storaged_logical_volume_complete_resize (_volume, invocation);

 out:
  if (cmd)
    g_string_free (cmd, TRUE);
  g_free (error_message);
  g_free (escaped_name);
  g_free (escaped_group_name);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

struct WaitForBlockData {
  StoragedLinuxLogicalVolumeObject *volume_object;
};

static StoragedObject *
wait_for_logical_volume_block_object (StoragedDaemon *daemon,
                                      gpointer        user_data)
{
  StoragedLinuxLogicalVolumeObject *volume_object = user_data;
  const gchar *volume_objpath;
  GList *objects, *l;
  StoragedObject *ret = NULL;

  volume_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (volume_object));

  objects = storaged_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlockLVM2 *block;

      block = storaged_object_peek_block_lvm2 (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (storaged_block_lvm2_get_logical_volume (block), volume_objpath) == 0)
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
handle_activate (StoragedLogicalVolume *_volume,
                 GDBusMethodInvocation *invocation,
                 GVariant *options)
{
  GError *error = NULL;
  StoragedLinuxLogicalVolume *volume = STORAGED_LINUX_LOGICAL_VOLUME (_volume);
  StoragedLinuxLogicalVolumeObject *object = NULL;
  StoragedDaemon *daemon;
  StoragedLinuxVolumeGroupObject *group_object;
  const gchar *vg_name = NULL;
  const gchar *lv_name = NULL;
  StoragedObject *block_object = NULL;

  object = storaged_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_logical_volume_object_get_daemon (object);

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to activate a logical volume"),
                                       invocation);

  group_object = storaged_linux_logical_volume_object_get_volume_group (object);
  vg_name = storaged_linux_volume_group_object_get_name (group_object);
  lv_name = storaged_linux_logical_volume_object_get_name (object);

  if (!bd_lvm_lvactivate (vg_name, lv_name, TRUE, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error activating logical volume: %s",
                                             error->message);
      g_error_free (error);
      goto out;
    }

  block_object = storaged_daemon_wait_for_object_sync (daemon,
                                                       wait_for_logical_volume_block_object,
                                                       object,
                                                       NULL,
                                                       10, /* timeout_seconds */
                                                       &error);
  if (block_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for block object for %s",
                      storaged_logical_volume_get_name (_volume));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_logical_volume_complete_activate (_volume, invocation,
                                             g_dbus_object_get_object_path (G_DBUS_OBJECT (block_object)));

 out:
  g_clear_object (&block_object);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_deactivate (StoragedLogicalVolume *_volume,
                   GDBusMethodInvocation *invocation,
                   GVariant              *options)
{
  GError *error = NULL;
  StoragedLinuxLogicalVolume *volume = STORAGED_LINUX_LOGICAL_VOLUME (_volume);
  StoragedLinuxLogicalVolumeObject *object = NULL;
  StoragedDaemon *daemon;
  StoragedLinuxVolumeGroupObject *group_object;
  const gchar *vg_name = NULL;
  const gchar *lv_name = NULL;

  object = storaged_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_logical_volume_object_get_daemon (object);

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to deactivate a logical volume"),
                                       invocation);

  group_object = storaged_linux_logical_volume_object_get_volume_group (object);
  vg_name = storaged_linux_volume_group_object_get_name (group_object);
  lv_name = storaged_linux_logical_volume_object_get_name (object);

  if (!bd_lvm_lvdeactivate (vg_name, lv_name, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error deleting logical volume: %s",
                                             error->message);
      g_error_free (error);
      goto out;
    }

  storaged_logical_volume_complete_deactivate (_volume, invocation);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_create_snapshot (StoragedLogicalVolume *_volume,
                        GDBusMethodInvocation *invocation,
                        const gchar           *name,
                        guint64                size,
                        GVariant              *options)
{
  GError *error = NULL;
  StoragedLinuxLogicalVolume *volume = STORAGED_LINUX_LOGICAL_VOLUME (_volume);
  StoragedLinuxLogicalVolumeObject *object = NULL;
  StoragedDaemon *daemon;
  StoragedLinuxVolumeGroupObject *group_object;
  const gchar *vg_name = NULL;
  const gchar *origin_name = NULL;
  const gchar *lv_objpath = NULL;

  object = storaged_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_logical_volume_object_get_daemon (object);

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to create a snapshot of a logical volume"),
                                       invocation);

  group_object = storaged_linux_logical_volume_object_get_volume_group (object);
  vg_name = storaged_linux_volume_group_object_get_name (group_object);
  origin_name = storaged_linux_logical_volume_object_get_name (object);

  if (size > 0)
    {
      size -= size % 512;
    }

  if (!bd_lvm_lvsnapshotcreate (vg_name, origin_name, name, size, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error creating snapshot: %s",
                                             error->message);
      g_error_free (error);
      goto out;
    }

  lv_objpath = wait_for_logical_volume_path (group_object, name, &error);
  if (lv_objpath == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for logical volume object for %s",
                      name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_logical_volume_complete_create_snapshot (_volume, invocation, lv_objpath);

 out:
  g_clear_object (&object);
  return TRUE;
}

static gboolean
handle_cache_attach (StoragedLogicalVolume  *volume_,
                     GDBusMethodInvocation  *invocation,
                     const gchar            *cache_name,
                     GVariant               *options)
{
#ifndef HAVE_LVMCACHE

  g_dbus_method_invocation_return_error (invocation,
                                         STORAGED_ERROR,
                                         STORAGED_ERROR_FAILED,
                                         N_("LVMCache not enabled at compile time."));
  return TRUE;

#else

  GError *error = NULL;
  StoragedLinuxLogicalVolume *volume = STORAGED_LINUX_LOGICAL_VOLUME (volume_);
  StoragedLinuxLogicalVolumeObject *object = NULL;
  StoragedDaemon *daemon;
  StoragedLinuxVolumeGroupObject *group_object;
  const gchar *vg_name = NULL;
  const gchar *origin_name = NULL;

  object = storaged_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }


  daemon = storaged_linux_logical_volume_object_get_daemon (object);

  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to convert logical volume to cache"),
                                       invocation);

  group_object = storaged_linux_logical_volume_object_get_volume_group (object);

  vg_name = storaged_linux_volume_group_object_get_name (group_object);
  origin_name = storaged_linux_logical_volume_object_get_name (object);

  if (!bd_lvm_cache_attach (vg_name, origin_name, cache_name, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             N_("Error converting volume: %s"),
                                             error->message);
      g_error_free (error);
      goto out;
    }

  storaged_logical_volume_complete_cache_attach (volume_, invocation);
out:
  g_clear_object (&object);

return TRUE;

#endif /* HAVE_LVMCACHE */
}


static gboolean
cache_detach_or_split (StoragedLogicalVolume  *volume_,
                       GDBusMethodInvocation  *invocation,
                       GVariant               *options,
                       gboolean               destroy)
{
#ifndef HAVE_LVMCACHE

  g_dbus_method_invocation_return_error (invocation,
                                         STORAGED_ERROR,
                                         STORAGED_ERROR_FAILED,
                                         N_("LVMCache not enabled at compile time."));
  return TRUE;

#else

  GError *error = NULL;
  StoragedLinuxLogicalVolume *volume = STORAGED_LINUX_LOGICAL_VOLUME (volume_);
  StoragedLinuxLogicalVolumeObject *object = NULL;
  StoragedDaemon *daemon;
  StoragedLinuxVolumeGroupObject *group_object;
  const gchar *vg_name = NULL;
  const gchar *origin_name = NULL;

  object = storaged_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_logical_volume_object_get_daemon (object);

  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to split or detach cache pool LV off of a cache LV"),
                                       invocation);

  group_object = storaged_linux_logical_volume_object_get_volume_group (object);

  vg_name = storaged_linux_volume_group_object_get_name (group_object);
  origin_name = storaged_linux_logical_volume_object_get_name (object);

  if (!bd_lvm_cache_detach (vg_name, origin_name, destroy, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             N_("Error converting volume: %s"),
                                             error->message);
      g_error_free (error);
      goto out;
    }

  storaged_logical_volume_complete_cache_split (volume_, invocation);
out:
  g_clear_object (&object);

  return TRUE;

#endif /* HAVE_LVMCACHE */
}


static gboolean
handle_cache_split (StoragedLogicalVolume  *volume_,
                    GDBusMethodInvocation  *invocation,
                    GVariant               *options)
{
  return cache_detach_or_split(volume_, invocation, options, FALSE);
}


static gboolean
handle_cache_detach (StoragedLogicalVolume  *volume_,
                    GDBusMethodInvocation  *invocation,
                    GVariant               *options)
{
  return cache_detach_or_split(volume_, invocation, options, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
logical_volume_iface_init (StoragedLogicalVolumeIface *iface)
{
  iface->handle_delete = handle_delete;
  iface->handle_rename = handle_rename;
  iface->handle_resize = handle_resize;
  iface->handle_activate = handle_activate;
  iface->handle_deactivate = handle_deactivate;
  iface->handle_create_snapshot = handle_create_snapshot;

  iface->handle_cache_attach = handle_cache_attach;
  iface->handle_cache_split = handle_cache_split;
}
