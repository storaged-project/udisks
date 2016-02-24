/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Samikshan Bairagya <sbairagy@redhat.com>
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

#include "storagedlinuxglusterfsvolume.h"
#include "storagedlinuxglusterfsvolumeobject.h"
#include "storagedglusterfsutils.h"

/**
 * SECTION:storagedlinuxglusterfsvolume
 * @title: StoragedLinuxGlusterFSVolume
 * @short_description: Linux implementation of #StoragedGlusterFSVolume
 *
 * This type provides an implementation of the #StoragedGlusterFSVolume interface
 * on Linux.
 */

typedef struct _StoragedLinuxGlusterFSVolumeClass StoragedLinuxGlusterFSVolumeClass;

/**
 * StoragedLinuxGlusterFSVolume:
 *
 * The #StoragedLinuxGlusterFSVolume structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxGlusterFSVolume
{
  StoragedGlusterFSVolumeSkeleton parent_instance;
};

struct _StoragedLinuxGlusterFSVolumeClass
{
  StoragedGlusterFSVolumeSkeletonClass parent_class;
};

static void storaged_linux_glusterfs_volume_iface_init (StoragedGlusterFSVolumeIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxGlusterFSVolume,
                         storaged_linux_glusterfs_volume,
                         STORAGED_TYPE_GLUSTERFS_VOLUME_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_GLUSTERFS_VOLUME,
                                                storaged_linux_glusterfs_volume_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_glusterfs_volume_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_glusterfs_volume_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_volume_parent_class)->finalize (object);
}

static void
storaged_linux_glusterfs_volume_init (StoragedLinuxGlusterFSVolume *gfs_volume)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (gfs_volume),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_glusterfs_volume_class_init (StoragedLinuxGlusterFSVolumeClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = storaged_linux_glusterfs_volume_finalize;
}

/**
 * storaged_linux_glusterfs_volume_new:
 *
 * Creates a new #StoragedLinuxGlusterFSVolume instance.
 *
 * Returns: A new #StoragedLinuxGlusterFSVolume. Free with g_object_unref().
 */
StoragedGlusterFSVolume *
storaged_linux_glusterfs_volume_new (void)
{
  return STORAGED_GLUSTERFS_VOLUME (g_object_new (STORAGED_TYPE_LINUX_GLUSTERFS_VOLUME,
                                                  NULL));
}

static void
storaged_linux_glusterfs_add_brick_to_volume (StoragedLinuxGlusterFSVolume *gfs_volume,
                                              GVariant                     *brick_info)

{
  const gchar *const *obj_paths;
  const gchar *brick_name;
  GString *brick_obj_path;
  const gchar **p;
  guint n;

  brick_obj_path = g_string_new ("/org/storaged/Storaged/glusterfs/brick/");
  obj_paths = storaged_glusterfs_volume_get_bricks (STORAGED_GLUSTERFS_VOLUME (gfs_volume));

  if (g_variant_lookup (brick_info, "name", "&s", &brick_name)) {

    for (n = 0; obj_paths != NULL && obj_paths[n] != NULL; n++);
    p = g_new0 (const gchar *, n + 2);

    storaged_safe_append_to_object_path (brick_obj_path, brick_name);
    n = 0;
    while (obj_paths != NULL && obj_paths[n] != NULL) {
      if (g_strcmp0 (brick_obj_path->str, obj_paths[n]) == 0)
        return;
      p[n] = obj_paths[n];
      n++;
    }
    p[n] = g_string_free (brick_obj_path, FALSE);
    storaged_glusterfs_volume_set_bricks (STORAGED_GLUSTERFS_VOLUME (gfs_volume), p);
    g_free (p);
  }
}


/**
 * storaged_linux_glusterfs_volume_update:
 * @gfs_volume: A #StoragedLinuxGlusterFSVolume.
 * @object: The enclosing #StoragedLinuxGlusterFSVolumeObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_glusterfs_volume_update (StoragedLinuxGlusterFSVolume *gfs_volume,
                                        GVariant                     *info)
{
  StoragedGlusterFSVolume *iface = STORAGED_GLUSTERFS_VOLUME (gfs_volume);
  const gchar *str;
  guint num;
  GVariantIter *iter;

  if (g_variant_lookup (info, "name", "&s", &str))
    storaged_glusterfs_volume_set_name (iface, str);

  if (g_variant_lookup (info, "id", "&s", &str))
    storaged_glusterfs_volume_set_id (iface, str);

  if (g_variant_lookup (info, "status", "u", &num))
    storaged_glusterfs_volume_set_status (iface, num);

  if (g_variant_lookup (info, "brickCount", "u", &num))
    storaged_glusterfs_volume_set_brickcount (iface, num);

  if (g_variant_lookup (info, "bricks", "av", &iter)) {
    GVariant *brick_info = NULL;
    while (g_variant_iter_loop (iter, "v", &brick_info))
      storaged_linux_glusterfs_add_brick_to_volume (gfs_volume, brick_info);
  }
}

/* ---------------------------------------------------------------------------------------------------- */

static StoragedObject *
wait_for_gluster_volume_object (StoragedDaemon *daemon,
                                gpointer        userdata)
{
  const gchar *name = userdata;
  return STORAGED_OBJECT (storaged_glusterfs_util_find_volume_object (daemon, name));
}

static gboolean
handle_start (StoragedGlusterFSVolume *volume,
              GDBusMethodInvocation   *invocation,
              GVariant                *arg_options)
{
  StoragedDaemon *daemon = NULL;
  StoragedLinuxGlusterFSVolumeObject *volume_object = NULL;
  GError *error = NULL;
  uid_t caller_uid;
  gint status;
  gchar *escaped_name = NULL;
  gchar *error_message = NULL;
  const gchar *caller = NULL;

  caller = g_dbus_method_invocation_get_sender (invocation);

  volume_object = storaged_daemon_util_dup_object (volume, &error);
  if (volume_object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_glusterfs_volume_object_get_daemon (volume_object);

  if (!storaged_daemon_util_get_caller_uid_sync (daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (volume_object),
                                       glusterfs_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to start a volume group"),
                                       invocation);
  escaped_name = storaged_daemon_util_escape (storaged_linux_glusterfs_volume_object_get_name (volume_object));

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                NULL,
                                                "gluster-volume-start", caller_uid,
                                                NULL, /* cancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                &status,
                                                &error_message,
                                                NULL, /* input_string */
                                                "gluster volume start %s",
                                                escaped_name))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error starting gluster volume: %s",
                                             error_message);
      g_free (error_message);
      goto out;
    }

  volume_object = storaged_daemon_wait_for_object_sync (daemon,
                                                        wait_for_gluster_volume_object,
                                                        escaped_name,
                                                        NULL,
                                                        20, /* timeout_seconds */
                                                        &error);
  if (volume_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for gluster volume object for %s",
                      escaped_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_glusterfs_volume_complete_start (volume, invocation);

 out:
  g_free (escaped_name);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static gboolean
handle_stop (StoragedGlusterFSVolume *volume,
             GDBusMethodInvocation   *invocation,
             GVariant                *arg_options)
{
  StoragedDaemon *daemon = NULL;
  StoragedLinuxGlusterFSVolumeObject *volume_object = NULL;
  GError *error = NULL;
  uid_t caller_uid;
  gint status;
  gchar *escaped_name = NULL;
  gchar *error_message = NULL;

  volume_object = storaged_daemon_util_dup_object (volume, &error);

  if (volume_object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_glusterfs_volume_object_get_daemon (volume_object);

  if (!storaged_daemon_util_get_caller_uid_sync (daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       STORAGED_OBJECT (volume_object),
                                       glusterfs_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to stop a volume group"),
                                       invocation);

  escaped_name = storaged_daemon_util_escape (storaged_linux_glusterfs_volume_object_get_name (volume_object));

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                NULL,
                                                "gluster-volume-stop", caller_uid,
                                                NULL, /* cancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                &status,
                                                &error_message,
                                                "y\n", /* input_string */
                                                "gluster volume stop %s",
                                                escaped_name))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error starting gluster volume: %s",
                                             error_message);
      g_free (error_message);
      goto out;
    }

  volume_object = storaged_daemon_wait_for_object_sync (daemon,
                                                        wait_for_gluster_volume_object,
                                                        escaped_name,
                                                        NULL,
                                                        20, /* timeout_seconds */
                                                        &error);
  if (volume_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for gluster volume object for %s",
                      escaped_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_glusterfs_volume_complete_stop (volume, invocation);

 out:
  g_free (escaped_name);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static gboolean
handle_add_brick (StoragedGlusterFSVolume *_group,
                  GDBusMethodInvocation   *invocation,
                  const gchar             *arg_brick_path,
                  GVariant                *arg_options)
{
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_glusterfs_volume_iface_init (StoragedGlusterFSVolumeIface *iface)
{
  iface->handle_start = handle_start;
  iface->handle_stop = handle_stop;
  iface->handle_add_brick = handle_add_brick;
}

