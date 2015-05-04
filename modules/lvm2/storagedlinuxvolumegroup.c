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

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <mntent.h>

#include <glib/gstdio.h>

#include <src/storagedlogging.h>
#include <src/storagedlinuxprovider.h>
#include <src/storageddaemon.h>
#include <src/storagedstate.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlinuxblock.h>
#include <src/storagedlinuxblockobject.h>

#include "storagedlinuxvolumegroup.h"
#include "storagedlinuxvolumegroupobject.h"
#include "storagedlinuxlogicalvolume.h"
#include "storagedlinuxlogicalvolumeobject.h"

#include "storagedlvm2daemonutil.h"
#include "storagedlvm2dbusutil.h"
#include "storaged-lvm2-generated.h"

/**
 * SECTION:storagedlinuxvolume_group
 * @title: StoragedLinuxVolumeGroup
 * @short_description: Linux implementation of #StoragedVolumeGroup
 *
 * This type provides an implementation of the #StoragedVolumeGroup interface
 * on Linux.
 */

typedef struct _StoragedLinuxVolumeGroupClass   StoragedLinuxVolumeGroupClass;

/**
 * StoragedLinuxVolumeGroup:
 *
 * The #StoragedLinuxVolumeGroup structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxVolumeGroup
{
  StoragedVolumeGroupSkeleton parent_instance;
};

struct _StoragedLinuxVolumeGroupClass
{
  StoragedVolumeGroupSkeletonClass parent_class;
};

static void volume_group_iface_init (StoragedVolumeGroupIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxVolumeGroup, storaged_linux_volume_group, STORAGED_TYPE_VOLUME_GROUP_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_VOLUME_GROUP, volume_group_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_volume_group_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_volume_group_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_volume_group_parent_class)->finalize (object);
}

static void
storaged_linux_volume_group_init (StoragedLinuxVolumeGroup *volume_group)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (volume_group),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_volume_group_class_init (StoragedLinuxVolumeGroupClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = storaged_linux_volume_group_finalize;
}

/**
 * storaged_linux_volume_group_new:
 *
 * Creates a new #StoragedLinuxVolumeGroup instance.
 *
 * Returns: A new #StoragedLinuxVolumeGroup. Free with g_object_unref().
 */
StoragedVolumeGroup *
storaged_linux_volume_group_new (void)
{
  return STORAGED_VOLUME_GROUP (g_object_new (STORAGED_TYPE_LINUX_VOLUME_GROUP,
                                              NULL));
}

/**
 * storaged_linux_volume_group_update:
 * @volume_group: A #StoragedLinuxVolumeGroup.
 * @object: The enclosing #StoragedLinuxVolumeGroupObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_volume_group_update (StoragedLinuxVolumeGroup *volume_group,
                                    GVariant                 *info,
                                    gboolean                 *needs_polling_ret)
{
  StoragedVolumeGroup *iface = STORAGED_VOLUME_GROUP (volume_group);
  const gchar *str;
  guint64 num;

  if (g_variant_lookup (info, "name", "&s", &str))
    storaged_volume_group_set_name (iface, str);

  if (g_variant_lookup (info, "uuid", "&s", &str))
    storaged_volume_group_set_uuid (iface, str);

  if (g_variant_lookup (info, "size", "t", &num))
    storaged_volume_group_set_size (iface, num);

  if (g_variant_lookup (info, "free-size", "t", &num))
    storaged_volume_group_set_free_size (iface, num);

  if (g_variant_lookup (info, "extent-size", "t", &num))
    storaged_volume_group_set_extent_size (iface, num);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_poll (StoragedVolumeGroup *_group,
             GDBusMethodInvocation *invocation)
{
  StoragedLinuxVolumeGroup *group = STORAGED_LINUX_VOLUME_GROUP (_group);
  StoragedLinuxVolumeGroupObject *object = NULL;
  GError *error = NULL;

  object = storaged_daemon_util_dup_object (group, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_linux_volume_group_object_poll (object);

  storaged_volume_group_complete_poll (_group, invocation);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

GList *
storaged_linux_volume_group_get_logical_volumes (StoragedVolumeGroup *group,
                                                 StoragedDaemon      *daemon)
{
  GList *ret = NULL;
  GList *l, *objects = NULL;
  GDBusObject *object;
  StoragedLogicalVolume *volume;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (group));
  if (object == NULL)
    goto out;

  objects = storaged_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      volume = storaged_object_peek_logical_volume (STORAGED_OBJECT(l->data));
      if (volume &&
          g_strcmp0 (storaged_logical_volume_get_volume_group (volume),
                     g_dbus_object_get_object_path (object)) == 0)
        ret = g_list_append (ret, g_object_ref (volume));
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
teardown_volume_group (StoragedVolumeGroup   *group,
                       StoragedDaemon        *daemon,
                       GDBusMethodInvocation *invocation,
                       GVariant              *options,
                       GError               **error)
{
  GList *volumes;
  StoragedLogicalVolume *volume;

  volumes = storaged_linux_volume_group_get_logical_volumes (group, daemon);
  for (GList *l = volumes; l; l = l->next)
    {
      volume = STORAGED_LOGICAL_VOLUME (l->data);
      if (g_strcmp0 (storaged_logical_volume_get_type_ (volume), "pool") != 0)
        {
          if (!storaged_linux_logical_volume_teardown_block (volume,
                                                             daemon,
                                                             invocation,
                                                             options,
                                                             error))
            {
              g_list_free_full (volumes, g_object_unref);
              return FALSE;
            }
        }
    }
  g_list_free_full (volumes, g_object_unref);

  return TRUE;
}

static gboolean
handle_delete (StoragedVolumeGroup   *_group,
               GDBusMethodInvocation *invocation,
               gboolean               arg_wipe,
               GVariant              *arg_options)
{
  GError *error = NULL;
  StoragedLinuxVolumeGroup *group = STORAGED_LINUX_VOLUME_GROUP (_group);
  StoragedLinuxVolumeGroupObject *object = NULL;
  StoragedDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  gboolean teardown_flag = FALSE;
  gchar *escaped_name = NULL;
  gchar *error_message = NULL;
  GList *objects_to_wipe = NULL;
  GList *l;

  g_variant_lookup (arg_options, "tear-down", "b", &teardown_flag);

  object = storaged_daemon_util_dup_object (group, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_volume_group_object_get_daemon (object);

  /* Find physical volumes to wipe. */
  if (arg_wipe)
    {
      GList *objects = storaged_daemon_get_objects (daemon);
      for (l = objects; l; l = l->next)
        {
          StoragedPhysicalVolume *physical_volume;
          physical_volume = storaged_object_peek_physical_volume (STORAGED_OBJECT (l->data));
          if (physical_volume
              && g_strcmp0 (storaged_physical_volume_get_volume_group (physical_volume),
                            g_dbus_object_get_object_path (G_DBUS_OBJECT (object))) == 0)
            objects_to_wipe = g_list_append (objects_to_wipe, g_object_ref (l->data));
        }
      g_list_free_full (objects, g_object_unref);
    }

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

  message = N_("Authentication is required to delete a volume group");
  action_id = "org.storaged.Storaged.lvm2.manage-lvm";
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      STORAGED_OBJECT (object),
                                                      action_id,
                                                      arg_options,
                                                      message,
                                                      invocation))
    goto out;

  if (teardown_flag &&
      !teardown_volume_group (_group,
                              daemon,
                              invocation,
                              arg_options,
                              &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  escaped_name = storaged_daemon_util_escape_and_quote (storaged_linux_volume_group_object_get_name (object));

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                STORAGED_OBJECT (object),
                                                "lvm-vg-delete", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "vgremove -f %s",
                                                escaped_name))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error deleting volume group: %s",
                                             error_message);
      goto out;
    }

  for (l = objects_to_wipe; l; l = l->next)
    {
      StoragedBlock *block = storaged_object_peek_block (l->data);
      if (block)
        storaged_daemon_util_lvm2_wipe_block (daemon, block, NULL);
    }

  storaged_volume_group_complete_delete (_group, invocation);

 out:
  g_list_free_full (objects_to_wipe, g_object_unref);
  g_free (error_message);
  g_free (escaped_name);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static StoragedObject *
wait_for_volume_group_object (StoragedDaemon *daemon,
                              gpointer        user_data)
{
  const gchar *name = user_data;

  return STORAGED_OBJECT (storaged_daemon_util_lvm2_find_volume_group_object (daemon, name));
}

static gboolean
handle_rename (StoragedVolumeGroup   *_group,
               GDBusMethodInvocation *invocation,
               const gchar           *new_name,
               GVariant              *options)
{
  GError *error = NULL;
  StoragedLinuxVolumeGroup *group = STORAGED_LINUX_VOLUME_GROUP (_group);
  StoragedLinuxVolumeGroupObject *object = NULL;
  StoragedDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  gchar *escaped_name = NULL;
  gchar *escaped_new_name = NULL;
  gchar *error_message = NULL;
  StoragedObject *group_object = NULL;

  object = storaged_daemon_util_dup_object (group, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_volume_group_object_get_daemon (object);

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

  message = N_("Authentication is required to rename a volume group");
  action_id = "org.storaged.Storaged.lvm2.manage-lvm";
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      STORAGED_OBJECT (object),
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  escaped_name = storaged_daemon_util_escape_and_quote (storaged_linux_volume_group_object_get_name (object));
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
                                                "vgrename %s %s",
                                                escaped_name,
                                                escaped_new_name))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error renaming volume group: %s",
                                             error_message);
      goto out;
    }

  group_object = storaged_daemon_wait_for_object_sync (daemon,
                                                       wait_for_volume_group_object,
                                                       (gpointer) new_name,
                                                       NULL,
                                                       10, /* timeout_seconds */
                                                       &error);
  if (group_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for volume group object for %s",
                      new_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_volume_group_complete_rename (_group,
                                         invocation,
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object)));

 out:
  g_free (error_message);
  g_free (escaped_name);
  g_free (escaped_new_name);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_add_device (StoragedVolumeGroup      *_group,
                   GDBusMethodInvocation    *invocation,
                   const gchar              *new_member_device_objpath,
                   GVariant                 *options)
{
  StoragedLinuxVolumeGroup *group = STORAGED_LINUX_VOLUME_GROUP (_group);
  StoragedDaemon *daemon;
  StoragedLinuxVolumeGroupObject *object;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  const gchar *new_member_device_file = NULL;
  gchar *escaped_new_member_device_file = NULL;
  GError *error = NULL;
  gchar *error_message = NULL;
  StoragedObject *new_member_device_object = NULL;
  StoragedBlock *new_member_device = NULL;
  gchar *escaped_name = NULL;

  object = storaged_daemon_util_dup_object (group, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_volume_group_object_get_daemon (object);

  error = NULL;
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

  new_member_device_object = storaged_daemon_find_object (daemon, new_member_device_objpath);
  if (new_member_device_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "No device for given object path");
      goto out;
    }

  new_member_device = storaged_object_get_block (new_member_device_object);
  if (new_member_device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "No block interface on given object");
      goto out;
    }

  message = N_("Authentication is required to add a device to a volume group");
  action_id = "org.storaged.Storaged.lvm2.manage-lvm";
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      STORAGED_OBJECT (object),
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  if (!storaged_daemon_util_lvm2_block_is_unused (new_member_device, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!storaged_daemon_util_lvm2_wipe_block (daemon, new_member_device, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  escaped_name = storaged_daemon_util_escape_and_quote (storaged_linux_volume_group_object_get_name (object));
  new_member_device_file = storaged_block_get_device (new_member_device);
  escaped_new_member_device_file = storaged_daemon_util_escape_and_quote (new_member_device_file);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                STORAGED_OBJECT (object),
                                                "lvm-vg-add-device", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "vgextend %s %s",
                                                escaped_name,
                                                escaped_new_member_device_file))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error adding %s to volume group: %s",
                                             new_member_device_file,
                                             error_message);
      goto out;
    }

  storaged_volume_group_complete_add_device (_group, invocation);

 out:
  g_free (error_message);
  g_free (escaped_name);
  g_free (escaped_new_member_device_file);
  g_clear_object (&new_member_device_object);
  g_clear_object (&new_member_device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_remove_device (StoragedVolumeGroup      *_group,
                      GDBusMethodInvocation    *invocation,
                      const gchar              *member_device_objpath,
                      gboolean                  arg_wipe,
                      GVariant                 *options)
{
  StoragedLinuxVolumeGroup *group = STORAGED_LINUX_VOLUME_GROUP (_group);
  StoragedDaemon *daemon;
  StoragedLinuxVolumeGroupObject *object;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  const gchar *member_device_file = NULL;
  gchar *escaped_member_device_file = NULL;
  GError *error = NULL;
  gchar *error_message = NULL;
  StoragedObject *member_device_object = NULL;
  StoragedBlock *member_device = NULL;
  gchar *escaped_name = NULL;

  object = storaged_daemon_util_dup_object (group, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_volume_group_object_get_daemon (object);

  error = NULL;
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

  member_device_object = storaged_daemon_find_object (daemon, member_device_objpath);
  if (member_device_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "No device for given object path");
      goto out;
    }

  member_device = storaged_object_get_block (member_device_object);
  if (member_device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "No block interface on given object");
      goto out;
    }

  message = N_("Authentication is required to remove a device from a volume group");
  action_id = "org.storaged.Storaged.lvm2.manage-lvm";
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      STORAGED_OBJECT (object),
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  escaped_name = storaged_daemon_util_escape_and_quote (storaged_linux_volume_group_object_get_name (object));
  member_device_file = storaged_block_get_device (member_device);
  escaped_member_device_file = storaged_daemon_util_escape_and_quote (member_device_file);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                STORAGED_OBJECT (object),
                                                "lvm-vg-rem-device", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "vgreduce %s %s",
                                                escaped_name,
                                                escaped_member_device_file))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error remove %s from volume group: %s",
                                             member_device_file,
                                             error_message);
      goto out;
    }

  if (arg_wipe)
    {
      if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                    STORAGED_OBJECT (member_device_object),
                                                    "format-erase", caller_uid,
                                                    NULL, /* GCancellable */
                                                    0,    /* uid_t run_as_uid */
                                                    0,    /* uid_t run_as_euid */
                                                    NULL, /* gint *out_status */
                                                    &error_message,
                                                    NULL,  /* input_string */
                                                    "wipefs -a %s",
                                                    escaped_member_device_file))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Error wiping  %s after removal from volume group %s: %s",
                                                 member_device_file,
                                                 storaged_linux_volume_group_object_get_name (object),
                                                 error_message);
          goto out;
        }
    }

  storaged_volume_group_complete_remove_device (_group, invocation);

 out:
  g_free (error_message);
  g_free (escaped_name);
  g_free (escaped_member_device_file);
  g_clear_object (&member_device_object);
  g_clear_object (&member_device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_empty_device (StoragedVolumeGroup      *_group,
                     GDBusMethodInvocation    *invocation,
                     const gchar              *member_device_objpath,
                     GVariant                 *options)
{
  StoragedLinuxVolumeGroup *group = STORAGED_LINUX_VOLUME_GROUP (_group);
  StoragedDaemon *daemon;
  StoragedLinuxVolumeGroupObject *object;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  const gchar *member_device_file = NULL;
  gchar *escaped_member_device_file = NULL;
  GError *error = NULL;
  gchar *error_message = NULL;
  StoragedObject *member_device_object = NULL;
  StoragedBlock *member_device = NULL;

  object = storaged_daemon_util_dup_object (group, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_volume_group_object_get_daemon (object);

  error = NULL;
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

  member_device_object = storaged_daemon_find_object (daemon, member_device_objpath);
  if (member_device_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "No device for given object path");
      goto out;
    }

  member_device = storaged_object_get_block (member_device_object);
  if (member_device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "No block interface on given object");
      goto out;
    }

  message = N_("Authentication is required to empty a device in a volume group");
  action_id = "org.storaged.Storaged.lvm2.manage-lvm";
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      STORAGED_OBJECT (object),
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  member_device_file = storaged_block_get_device (member_device);
  escaped_member_device_file = storaged_daemon_util_escape_and_quote (member_device_file);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                STORAGED_OBJECT (member_device_object),
                                                "lvm-vg-empty-device", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "pvmove %s",
                                                escaped_member_device_file))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error emptying %s: %s",
                                             member_device_file,
                                             error_message);
      goto out;
    }

  storaged_volume_group_complete_remove_device (_group, invocation);

 out:
  g_free (error_message);
  g_free (escaped_member_device_file);
  g_clear_object (&member_device_object);
  g_clear_object (&member_device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
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
                              const                           gchar *name,
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

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_create_plain_volume (StoragedVolumeGroup   *_group,
                            GDBusMethodInvocation *invocation,
                            const gchar           *arg_name,
                            guint64                arg_size,
                            GVariant              *options)
{
  GError *error = NULL;
  StoragedLinuxVolumeGroup *group = STORAGED_LINUX_VOLUME_GROUP (_group);
  StoragedLinuxVolumeGroupObject *object = NULL;
  StoragedDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  gchar *escaped_volume_name = NULL;
  gchar *escaped_group_name = NULL;
  GString *cmd = NULL;
  gchar *error_message = NULL;
  const gchar *lv_objpath;

  object = storaged_daemon_util_dup_object (group, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_volume_group_object_get_daemon (object);

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

  message = N_("Authentication is required to create a logical volume");
  action_id = "org.storaged.Storaged.lvm2.manage-lvm";
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      STORAGED_OBJECT (object),
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  escaped_volume_name = storaged_daemon_util_escape_and_quote (arg_name);
  escaped_group_name = storaged_daemon_util_escape_and_quote (storaged_linux_volume_group_object_get_name (object));
  arg_size -= arg_size % 512;

  cmd = g_string_new ("");
  g_string_append_printf (cmd, "lvcreate %s -L %" G_GUINT64_FORMAT "b -n %s",
                          escaped_group_name, arg_size, escaped_volume_name);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                STORAGED_OBJECT (object),
                                                "lvm-vg-create-volume", caller_uid,
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
                                             "Error creating volume: %s",
                                             error_message);
      goto out;
    }

  lv_objpath = wait_for_logical_volume_path (object, arg_name, &error);
  if (lv_objpath == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for logical volume object for %s",
                      arg_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_volume_group_complete_create_plain_volume (_group, invocation, lv_objpath);

 out:
  g_free (error_message);
  g_free (escaped_group_name);
  g_free (escaped_volume_name);
  g_string_free (cmd, TRUE);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_create_thin_pool_volume (StoragedVolumeGroup   *_group,
                                GDBusMethodInvocation *invocation,
                                const gchar           *arg_name,
                                guint64                arg_size,
                                GVariant              *options)
{
  GError *error = NULL;
  StoragedLinuxVolumeGroup *group = STORAGED_LINUX_VOLUME_GROUP (_group);
  StoragedLinuxVolumeGroupObject *object = NULL;
  StoragedDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  gchar *escaped_volume_name = NULL;
  gchar *escaped_group_name = NULL;
  GString *cmd = NULL;
  gchar *error_message = NULL;
  const gchar *lv_objpath;

  object = storaged_daemon_util_dup_object (group, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_volume_group_object_get_daemon (object);

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

  message = N_("Authentication is required to create a logical volume");
  action_id = "org.storaged.Storaged.lvm2.manage-lvm";
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      STORAGED_OBJECT (object),
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  escaped_volume_name = storaged_daemon_util_escape_and_quote (arg_name);
  escaped_group_name = storaged_daemon_util_escape_and_quote (storaged_linux_volume_group_object_get_name (object));
  arg_size -= arg_size % 512;

  cmd = g_string_new ("");
  g_string_append_printf (cmd, "lvcreate %s -T -L %" G_GUINT64_FORMAT "b --thinpool %s",
                          escaped_group_name, arg_size, escaped_volume_name);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                STORAGED_OBJECT (object),
                                                "lvm-vg-create-volume", caller_uid,
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
                                             "Error creating volume: %s",
                                             error_message);
      goto out;
    }

  lv_objpath = wait_for_logical_volume_path (object, arg_name, &error);
  if (lv_objpath == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for logical volume object for %s",
                      arg_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_volume_group_complete_create_thin_pool_volume (_group, invocation, lv_objpath);

 out:
  g_free (error_message);
  g_free (escaped_volume_name);
  g_free (escaped_group_name);
  g_string_free (cmd, TRUE);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_create_thin_volume (StoragedVolumeGroup   *_group,
                           GDBusMethodInvocation *invocation,
                           const gchar           *arg_name,
                           guint64                arg_size,
                           const gchar           *arg_pool,
                           GVariant *options)
{
  GError *error = NULL;
  StoragedLinuxVolumeGroup *group = STORAGED_LINUX_VOLUME_GROUP (_group);
  StoragedLinuxVolumeGroupObject *object = NULL;
  StoragedDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  StoragedLinuxLogicalVolumeObject *pool_object = NULL;
  gchar *escaped_volume_name = NULL;
  gchar *escaped_group_name = NULL;
  gchar *escaped_pool_name = NULL;
  GString *cmd = NULL;
  gchar *error_message = NULL;
  const gchar *lv_objpath;

  object = storaged_daemon_util_dup_object (group, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_volume_group_object_get_daemon (object);

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

  message = N_("Authentication is required to create a logical volume");
  action_id = "org.storaged.Storaged.lvm2.manage-lvm";
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      STORAGED_OBJECT (object),
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  pool_object = STORAGED_LINUX_LOGICAL_VOLUME_OBJECT (storaged_daemon_find_object (daemon, arg_pool));
  if (pool_object == NULL || !STORAGED_IS_LINUX_LOGICAL_VOLUME_OBJECT (pool_object))
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Not a logical volume");
      goto out;
    }

  escaped_volume_name = storaged_daemon_util_escape_and_quote (arg_name);
  escaped_group_name = storaged_daemon_util_escape_and_quote (storaged_linux_volume_group_object_get_name (object));
  arg_size -= arg_size % 512;
  escaped_pool_name = storaged_daemon_util_escape_and_quote (storaged_linux_logical_volume_object_get_name (pool_object));

  cmd = g_string_new ("");
  g_string_append_printf (cmd, "lvcreate %s --thinpool %s -V %" G_GUINT64_FORMAT "b -n %s",
                          escaped_group_name, escaped_pool_name, arg_size, escaped_volume_name);

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                STORAGED_OBJECT (object),
                                                "lvm-vg-create-volume", caller_uid,
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
                                             "Error creating volume: %s",
                                             error_message);
      goto out;
    }

  lv_objpath = wait_for_logical_volume_path (object, arg_name, &error);
  if (lv_objpath == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for logical volume object for %s",
                      arg_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_volume_group_complete_create_thin_pool_volume (_group, invocation, lv_objpath);

 out:
  g_free (error_message);
  g_free (escaped_volume_name);
  g_free (escaped_group_name);
  g_free (escaped_pool_name);
  g_string_free (cmd, TRUE);
  g_clear_object (&pool_object);
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
volume_group_iface_init (StoragedVolumeGroupIface *iface)
{
  iface->handle_poll = handle_poll;

  iface->handle_delete = handle_delete;
  iface->handle_rename = handle_rename;

  iface->handle_add_device = handle_add_device;
  iface->handle_remove_device = handle_remove_device;
  iface->handle_empty_device = handle_empty_device;

  iface->handle_create_plain_volume = handle_create_plain_volume;
  iface->handle_create_thin_pool_volume = handle_create_thin_pool_volume;
  iface->handle_create_thin_volume = handle_create_thin_volume;
}
