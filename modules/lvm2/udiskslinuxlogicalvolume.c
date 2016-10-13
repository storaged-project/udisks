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
#include "udiskslvm2dbusutil.h"
#include "udiskslvm2util.h"
#include "udisks-lvm2-generated.h"

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

static void logical_volume_iface_init (UDisksLogicalVolumeIface *iface);

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
udisks_linux_logical_volume_class_init (UDisksLinuxLogicalVolumeClass *klass)
{
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
                                    GVariant                     *info,
                                    gboolean                     *needs_polling_ret)
{
  UDisksLogicalVolume *iface;
  const char *type;
  gboolean active;
  const char *pool_objpath;
  const char *origin_objpath;
  const gchar *dev_file;
  const gchar *str;
  const gchar *uuid;
  guint64 num;
  guint64 size;
  guint64 metadata_size;

  iface = UDISKS_LOGICAL_VOLUME (logical_volume);

  if (g_variant_lookup (info, "name", "&s", &str))
    udisks_logical_volume_set_name (iface, str);

  if (g_variant_lookup (info, "uuid", "&s", &uuid))
    udisks_logical_volume_set_uuid (iface, uuid);

  size = 0;
  metadata_size = 0;

  if (g_variant_lookup (info, "size", "t", &num))
    size = num;

  if (g_variant_lookup (info, "lv_metadata_size", "t", &num))
    metadata_size = num;

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

      if (target_type == 't' && volume_type == 't') {
        type = "pool";
        size += metadata_size;
      }

      if (state == 'a')
        active = TRUE;
    }
  udisks_logical_volume_set_type_ (iface, type);
  udisks_logical_volume_set_active (iface, active);
  udisks_logical_volume_set_size (iface, size);

  if (g_variant_lookup (info, "data_percent", "t", &num)
      && (int64_t)num >= 0)
    udisks_logical_volume_set_data_allocated_ratio (iface, num/100000000.0);

  if (g_variant_lookup (info, "metadata_percent", "t", &num)
      && (int64_t)num >= 0)
    udisks_logical_volume_set_metadata_allocated_ratio (iface, num/100000000.0);

  pool_objpath = "/";
  if (g_variant_lookup (info, "pool_lv", "&s", &str)
      && str != NULL && *str)
    {
      UDisksLinuxLogicalVolumeObject *pool_object = udisks_linux_volume_group_object_find_logical_volume_object (group_object, str);
      if (pool_object)
        pool_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
    }
  udisks_logical_volume_set_thin_pool (iface, pool_objpath);

  origin_objpath = "/";
  if (g_variant_lookup (info, "origin", "&s", &str)
      && str != NULL && *str)
    {
      UDisksLinuxLogicalVolumeObject *origin_object = udisks_linux_volume_group_object_find_logical_volume_object (group_object, str);
      if (origin_object)
        origin_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (origin_object));
    }
  udisks_logical_volume_set_origin (iface, origin_objpath);

  udisks_logical_volume_set_volume_group (iface, g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object)));

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
      udisks_daemon_util_lvm2_trigger_udev (dev_file);
      logical_volume->needs_udev_hack = FALSE;
    }
}

void
udisks_linux_logical_volume_update_etctabs (UDisksLinuxLogicalVolume     *logical_volume,
                                            UDisksLinuxVolumeGroupObject *group_object)
{
  UDisksDaemon *daemon;
  UDisksLogicalVolume *iface;
  const gchar *uuid;

  daemon = udisks_linux_volume_group_object_get_daemon (group_object);
  iface = UDISKS_LOGICAL_VOLUME (logical_volume);
  uuid = udisks_logical_volume_get_uuid (iface);

  udisks_logical_volume_set_child_configuration (iface,
                                                 udisks_linux_find_child_configuration (daemon,
                                                                                        uuid));
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
handle_delete (UDisksLogicalVolume   *_volume,
               GDBusMethodInvocation *invocation,
               GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  gid_t caller_gid;
  gboolean teardown_flag = FALSE;
  UDisksLinuxVolumeGroupObject *group_object;
  gchar *escaped_group_name = NULL;
  gchar *escaped_name = NULL;
  gchar *error_message = NULL;

  g_variant_lookup (options, "tear-down", "b", &teardown_flag);

  object = udisks_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_logical_volume_object_get_daemon (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
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

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
                                              "lvm-lvol-delete", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "lvremove -f %s/%s",
                                              escaped_group_name,
                                              escaped_name))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error deleting logical volume: %s",
                                             error_message);
      goto out;
    }

  udisks_logical_volume_complete_delete (_volume, invocation);

 out:
  g_free (error_message);
  g_free (escaped_name);
  g_free (escaped_group_name);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

struct WaitData {
  UDisksLinuxVolumeGroupObject *group_object;
  const gchar *name;
};

static UDisksObject *
wait_for_logical_volume_object (UDisksDaemon *daemon,
                                gpointer      user_data)
{
  struct WaitData *data = user_data;
  return UDISKS_OBJECT (udisks_linux_volume_group_object_find_logical_volume_object (data->group_object,
                                                                                     data->name));
}

static const gchar *
wait_for_logical_volume_path (UDisksLinuxVolumeGroupObject  *group_object,
                              const gchar                   *name,
                              GError                       **error)
{
  struct WaitData data;
  UDisksDaemon *daemon;
  UDisksObject *volume_object;

  data.group_object = group_object;
  data.name = name;
  daemon = udisks_linux_volume_group_object_get_daemon (group_object);
  volume_object = udisks_daemon_wait_for_object_sync (daemon,
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
  gid_t caller_gid;
  UDisksLinuxVolumeGroupObject *group_object;
  gchar *escaped_group_name = NULL;
  gchar *escaped_name = NULL;
  gchar *escaped_new_name = NULL;
  gchar *error_message = NULL;
  const gchar *lv_objpath;

  object = udisks_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_logical_volume_object_get_daemon (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     lvm2_policy_action_id,
                                     options,
                                     N_("Authentication is required to rename a logical volume"),
                                     invocation);

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));
  escaped_new_name = udisks_daemon_util_escape_and_quote (new_name);

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
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
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
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

  udisks_logical_volume_complete_rename (_volume, invocation, lv_objpath);

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
  gid_t caller_gid;
  UDisksLinuxVolumeGroupObject *group_object;
  GString *cmd = NULL;
  gchar *escaped_group_name = NULL;
  gchar *escaped_name = NULL;
  gchar *error_message = NULL;
  gboolean resize_fsys = FALSE;
  gboolean force = FALSE;

  object = udisks_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_logical_volume_object_get_daemon (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     lvm2_policy_action_id,
                                     options,
                                     N_("Authentication is required to resize a logical volume"),
                                     invocation);

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));
  new_size -= new_size % 512;

  g_variant_lookup (options, "resize_fsys", "b", &resize_fsys);
  g_variant_lookup (options, "force", "b", &force);

  cmd = g_string_new ("");
  g_string_append_printf (cmd, "lvresize %s/%s -L %" G_GUINT64_FORMAT "b",
                          escaped_group_name, escaped_name, new_size);
  if (resize_fsys)
    g_string_append (cmd, " -r");
  if (force)
    g_string_append (cmd, " -f");

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
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
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error resizing logical volume: %s",
                                             error_message);
      goto out;
    }

  udisks_logical_volume_complete_resize (_volume, invocation);

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
  gid_t caller_gid;
  UDisksLinuxVolumeGroupObject *group_object;
  gchar *escaped_group_name = NULL;
  gchar *escaped_name = NULL;
  gchar *error_message = NULL;
  UDisksObject *block_object = NULL;

  object = udisks_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_logical_volume_object_get_daemon (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     lvm2_policy_action_id,
                                     options,
                                     N_("Authentication is required to activate a logical volume"),
                                     invocation);

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
                                              "lvm-lvol-activate", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "lvchange %s/%s -ay -K --yes",
                                              escaped_group_name,
                                              escaped_name))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error deleting logical volume: %s",
                                             error_message);
      goto out;
    }

  block_object = udisks_daemon_wait_for_object_sync (daemon,
                                                     wait_for_logical_volume_block_object,
                                                     object,
                                                     NULL,
                                                     10, /* timeout_seconds */
                                                     &error);
  if (block_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for block object for %s",
                      udisks_logical_volume_get_name (_volume));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_logical_volume_complete_activate (_volume, invocation,
                                           g_dbus_object_get_object_path (G_DBUS_OBJECT (block_object)));

 out:
  g_free (error_message);
  g_free (escaped_name);
  g_free (escaped_group_name);
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
  gid_t caller_gid;
  UDisksLinuxVolumeGroupObject *group_object;
  gchar *escaped_group_name = NULL;
  gchar *escaped_name = NULL;
  gchar *error_message = NULL;

  object = udisks_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_logical_volume_object_get_daemon (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       UDISKS_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to deactivate a logical volume"),
                                       invocation);

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                                UDISKS_OBJECT (object),
                                                "lvm-lvol-deactivate", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "lvchange %s/%s -an -K --yes",
                                                escaped_group_name,
                                                escaped_name))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error deleting logical volume: %s",
                                             error_message);
      goto out;
    }

  udisks_logical_volume_complete_deactivate (_volume, invocation);

 out:
  g_free (error_message);
  g_free (escaped_name);
  g_free (escaped_group_name);
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
  gid_t caller_gid;
  UDisksLinuxVolumeGroupObject *group_object;
  gchar *escaped_volume_name = NULL;
  gchar *escaped_group_name = NULL;
  gchar *escaped_origin_name = NULL;
  GString *cmd = NULL;
  gchar *error_message = NULL;
  const gchar *lv_objpath = NULL;

  object = udisks_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_logical_volume_object_get_daemon (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     lvm2_policy_action_id,
                                     options,
                                     N_("Authentication is required to create a snapshot of a logical volume"),
                                     invocation);

  escaped_volume_name = udisks_daemon_util_escape_and_quote (name);
  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_origin_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));

  cmd = g_string_new ("lvcreate");
  g_string_append_printf (cmd, " -s %s/%s -n %s",
                          escaped_group_name, escaped_origin_name, escaped_volume_name);

  if (size > 0)
    {
      size -= size % 512;
      g_string_append_printf (cmd, " -L %" G_GUINT64_FORMAT "b", size);
    }

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
                                              "lvm-lvol-snapshot", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "%s", cmd->str))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error creating snapshot: %s",
                                             error_message);
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

  udisks_logical_volume_complete_create_snapshot (_volume, invocation, lv_objpath);

 out:
  g_free (error_message);
  g_free (escaped_volume_name);
  g_free (escaped_origin_name);
  g_free (escaped_group_name);
  if (cmd)
    g_string_free (cmd, TRUE);
  g_clear_object (&object);
  return TRUE;
}

static gboolean
handle_cache_attach (UDisksLogicalVolume   *volume_,
                     GDBusMethodInvocation *invocation,
                     const gchar           *cache_name,
                     GVariant              *options)
{
#ifndef HAVE_LVMCACHE

  g_dbus_method_invocation_return_error (invocation,
                                         UDISKS_ERROR,
                                         UDISKS_ERROR_FAILED,
                                         N_("LVMCache not enabled at compile time."));
  return TRUE;

#else

  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (volume_);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  GString *cmd = NULL;
  gchar *escaped_group_name = NULL;
  gchar *escaped_origin_name = NULL;
  gchar *escaped_cache_name = NULL;
  gchar *error_message;

  object = udisks_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }


  daemon = udisks_linux_logical_volume_object_get_daemon (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 NULL,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       UDISKS_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to convert logical volume to cache"),
                                       invocation);

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);

  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_origin_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));
  escaped_cache_name = udisks_daemon_util_escape_and_quote (cache_name);

  cmd = g_string_new ("");
  g_string_append_printf (cmd,
                          "lvconvert --type cache --cachepool %s/%s %s/%s -y",
                          escaped_group_name,
                          escaped_cache_name,
                          escaped_group_name,
                          escaped_origin_name);

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                                UDISKS_OBJECT (object),
                                                "lvm-lv-make-cache", caller_uid,
                                                NULL,
                                                0,
                                                0,
                                                NULL,
                                                &error_message,
                                                NULL,
                                                "%s", cmd->str))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             N_("Error converting volume: %s"),
                                             error_message);
      goto out;
    }

  udisks_logical_volume_complete_cache_attach (volume_, invocation);
out:
  g_free (error_message);
  g_free (escaped_group_name);
  g_free (escaped_origin_name);
  g_free (escaped_cache_name);
  if (cmd)
    g_string_free (cmd, TRUE);
  g_clear_object (&object);

return TRUE;

#endif /* HAVE_LVMCACHE */
}


static gboolean
handle_cache_detach_or_split (UDisksLogicalVolume  *volume_,
                              GDBusMethodInvocation  *invocation,
                              GVariant               *options,
                              gboolean               destroy)
{
#ifndef HAVE_LVMCACHE

  g_dbus_method_invocation_return_error (invocation,
                                         UDISKS_ERROR,
                                         UDISKS_ERROR_FAILED,
                                         N_("LVMCache not enabled at compile time."));
  return TRUE;

#else

  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (volume_);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  UDisksLinuxVolumeGroupObject *group_object;
  GString *cmd = NULL;
  gchar *escaped_group_name = NULL;
  gchar *escaped_origin_name = NULL;
  gchar *error_message;

  object = udisks_daemon_util_dup_object (volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_logical_volume_object_get_daemon (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 NULL,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       UDISKS_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to split cache pool LV off of a cache LV"),
                                       invocation);

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);

  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_origin_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));

  cmd = g_string_new ("");
  g_string_append_printf (cmd,
                          "lvconvert %s %s/%s -y",
                          destroy ? "--splitcache" : "--uncache",
                          escaped_group_name,
                          escaped_origin_name);

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                                UDISKS_OBJECT (object),
                                                "lvm-lv-split-cache", caller_uid,
                                                NULL,
                                                0,
                                                0,
                                                NULL,
                                                &error_message,
                                                NULL,
                                                "%s", cmd->str))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             N_("Error converting volume: %s"),
                                             error_message);
      goto out;
    }

  udisks_logical_volume_complete_cache_split (volume_, invocation);
out:
  g_free (error_message);
  g_free (escaped_group_name);
  g_free (escaped_origin_name);
  if (cmd)
    g_string_free (cmd, TRUE);
  g_clear_object (&object);

  return TRUE;

#endif /* HAVE_LVMCACHE */
}

static gboolean
handle_cache_split (UDisksLogicalVolume    *volume_,
                    GDBusMethodInvocation  *invocation,
                    GVariant               *options)
{
  return handle_cache_detach_or_split(volume_, invocation, options, TRUE);
}

static gboolean
handle_cache_detach (UDisksLogicalVolume    *volume_,
                     GDBusMethodInvocation  *invocation,
                     GVariant               *options)
{
  return handle_cache_detach_or_split(volume_, invocation, options, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
logical_volume_iface_init (UDisksLogicalVolumeIface *iface)
{
  iface->handle_delete = handle_delete;
  iface->handle_rename = handle_rename;
  iface->handle_resize = handle_resize;
  iface->handle_activate = handle_activate;
  iface->handle_deactivate = handle_deactivate;
  iface->handle_create_snapshot = handle_create_snapshot;

  iface->handle_cache_attach = handle_cache_attach;
  iface->handle_cache_split = handle_cache_split;
  iface->handle_cache_detach = handle_cache_detach;
}
