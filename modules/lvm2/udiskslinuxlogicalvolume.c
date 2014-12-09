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

#include "udiskslinuxlogicalvolume.h"
#include "udiskslinuxlogicalvolumeobject.h"
#include "udiskslinuxvolumegroup.h"
#include "udiskslinuxvolumegroupobject.h"

#include "udiskslvm2daemonutil.h"
#include "udiskslvm2dbusutil.h"
#include "module-lvm2-generated.h"

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
udisks_linux_logical_volume_update (UDisksLinuxLogicalVolume *logical_volume,
                                    UDisksLinuxVolumeGroupObject *group_object,
                                    GVariant *info,
                                    gboolean *needs_polling_ret)
{
  UDisksLogicalVolume *iface;
  const char *type;
  gboolean active;
  const char *pool_objpath;
  const char *origin_objpath;
  const gchar *dev_file;
  const gchar *str;
  guint64 num;

  iface = UDISKS_LOGICAL_VOLUME (logical_volume);

  if (g_variant_lookup (info, "name", "&s", &str))
    {
      gchar *decoded = udisks_daemon_util_lvm2_decode_lvm_name (str);
      udisks_logical_volume_set_name (iface, str);
      udisks_logical_volume_set_display_name (iface, decoded);
      g_free (decoded);
    }

  if (g_variant_lookup (info, "uuid", "&s", &str))
    udisks_logical_volume_set_uuid (iface, str);

  if (g_variant_lookup (info, "size", "t", &num))
    udisks_logical_volume_set_size (iface, num);

  type = "unsupported";
  active = FALSE;
  if (g_variant_lookup (info, "lv_attr", "&s", &str)
      && str && strlen (str) > 6)
    {
      char volume_type = str[0];
      char state       = str[4];
      char target_type = str[6];

      switch (target_type)
        {
        case 's':
          type = "snapshot";
          break;
        case 'm':
          type = "mirror";
          break;
        case 't':
          if (volume_type == 't')
            type = "thin-pool";
          else
            type = "thin";
          *needs_polling_ret = TRUE;
          break;
        case 'r':
          type = "raid";
          break;
        case '-':
          type = "plain";
          break;
        }
      if (state == 'a')
        active = TRUE;
    }
  udisks_logical_volume_set_type_ (iface, type);
  udisks_logical_volume_set_active (iface, active);

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
      int fd = open (dev_file, O_RDWR);
      if (fd >= 0)
        close (fd);

      logical_volume->needs_udev_hack = FALSE;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_delete (UDisksLogicalVolume *_volume,
               GDBusMethodInvocation *invocation,
               GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
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

  message = N_("Authentication is required to delete a logical volume");
  action_id = "org.freedesktop.udisks2.lvm2.manage-lvm";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

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
wait_for_logical_volume_path (UDisksLinuxVolumeGroupObject *group_object,
                              const gchar *name,
                              GError **error)
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
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  UDisksLinuxVolumeGroupObject *group_object;
  gchar *escaped_group_name = NULL;
  gchar *escaped_name = NULL;
  gchar *encoded_new_name = NULL;
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

  message = N_("Authentication is required to rename a logical volume");
  action_id = "org.freedesktop.udisks2.lvm2.manage-lvm";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));
  encoded_new_name = udisks_daemon_util_lvm2_encode_lvm_name (new_name, TRUE);
  escaped_new_name = udisks_daemon_util_escape_and_quote (encoded_new_name);

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

  lv_objpath = wait_for_logical_volume_path (group_object, encoded_new_name, &error);
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
  g_free (encoded_new_name);
  g_free (escaped_new_name);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_resize (UDisksLogicalVolume *_volume,
               GDBusMethodInvocation *invocation,
               guint64 new_size,
               int stripes,
               guint64 stripesize,
               GVariant *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  UDisksLinuxVolumeGroupObject *group_object;
  GString *cmd = NULL;
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

  message = N_("Authentication is required to rename a logical volume");
  action_id = "org.freedesktop.udisks2.lvm2.manage-lvm";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  escaped_group_name = udisks_daemon_util_escape_and_quote (udisks_linux_volume_group_object_get_name (group_object));
  escaped_name = udisks_daemon_util_escape_and_quote (udisks_linux_logical_volume_object_get_name (object));
  new_size -= new_size % 512;

  cmd = g_string_new ("");
  g_string_append_printf (cmd, "lvresize %s/%s -r -L %" G_GUINT64_FORMAT "b",
                          escaped_group_name, escaped_name, new_size);

  if (stripes > 0)
    g_string_append_printf (cmd, " -i %d", stripes);

  if (stripesize > 0)
    g_string_append_printf (cmd, " -I %" G_GUINT64_FORMAT "b", stripesize);

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
  const gchar *action_id;
  const gchar *message;
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

  message = N_("Authentication is required to activate a logical volume");
  action_id = "org.freedesktop.udisks2.lvm2.manage-lvm";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

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
                                              "lvchange %s/%s -a y",
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
handle_deactivate (UDisksLogicalVolume *_volume,
                   GDBusMethodInvocation *invocation,
                   GVariant *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
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

  message = N_("Authentication is required to deactivate a logical volume");
  action_id = "org.freedesktop.udisks2.lvm2.manage-lvm";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

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
                                              "lvchange %s/%s -a n",
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
handle_create_snapshot (UDisksLogicalVolume *_volume,
                        GDBusMethodInvocation *invocation,
                        const gchar *name,
                        guint64 size,
                        GVariant *options)
{
  GError *error = NULL;
  UDisksLinuxLogicalVolume *volume = UDISKS_LINUX_LOGICAL_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  UDisksLinuxVolumeGroupObject *group_object;
  gchar *encoded_volume_name = NULL;
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

  message = N_("Authentication is required to create a snapshot of a logical volume");
  action_id = "org.freedesktop.udisks2.lvm2.manage-lvm";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  encoded_volume_name = udisks_daemon_util_lvm2_encode_lvm_name (name, TRUE);
  escaped_volume_name = udisks_daemon_util_escape_and_quote (encoded_volume_name);
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

  lv_objpath = wait_for_logical_volume_path (group_object, encoded_volume_name, &error);
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
  g_free (encoded_volume_name);
  g_free (escaped_volume_name);
  g_free (escaped_origin_name);
  g_free (escaped_group_name);
  if (cmd)
    g_string_free (cmd, TRUE);
  g_clear_object (&object);
  return TRUE;
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
}
