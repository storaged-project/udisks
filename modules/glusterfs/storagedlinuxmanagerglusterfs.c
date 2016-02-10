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
 */

#include "config.h"

#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlogging.h>

#include <glib/gi18n-lib.h>

#include "storagedglusterfsutils.h"
#include "storagedglusterfsstate.h"
#include "storagedlinuxmanagerglusterfs.h"
#include "storaged-glusterfs-generated.h"

struct _StoragedLinuxManagerGlusterFS {

  StoragedManagerGlusterFSSkeleton parent_instance;

  StoragedDaemon *daemon;
};

struct _StoragedLinuxManagerGlusterFSClass {
  StoragedManagerGlusterFSSkeletonClass parent_class;
};

enum {
  PROP_0,
  PROP_DAEMON
};

static void storaged_linux_manager_glusterfs_iface_init (StoragedManagerGlusterFSIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxManagerGlusterFS, storaged_linux_manager_glusterfs,
                         STORAGED_TYPE_MANAGER_GLUSTERFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MANAGER_GLUSTERFS,
                                                storaged_linux_manager_glusterfs_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_manager_glusterfs_get_property (GObject *object,
                                              guint property_id,
                                              GValue *value,
                                              GParamSpec *pspec)
{
  StoragedLinuxManagerGlusterFS *manager = STORAGED_LINUX_MANAGER_GLUSTERFS(object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_manager_glusterfs_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_glusterfs_set_property (GObject *object,
                                              guint property_id,
                                              GValue *value,
                                              GParamSpec *pspec)
{
  StoragedLinuxManagerGlusterFS *manager = STORAGED_LINUX_MANAGER_GLUSTERFS(object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_assert (manager->daemon == NULL);
      manager->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_glusterfs_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_glusterfs_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_glusterfs_parent_class)->dispose (object);
}

static void
storaged_linux_manager_glusterfs_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_glusterfs_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_glusterfs_parent_class)->finalize (object);
}

static void
storaged_linux_manager_glusterfs_class_init (StoragedLinuxManagerGlusterFSClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = storaged_linux_manager_glusterfs_get_property;
  gobject_class->set_property = storaged_linux_manager_glusterfs_set_property;
  gobject_class->dispose = storaged_linux_manager_glusterfs_dispose;
  gobject_class->finalize = storaged_linux_manager_glusterfs_finalize;

  /** StoragedLinuxManager:daemon
   *
   * The #StoragedDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon", 
                                                        "The daemon for the object",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE | 
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}


static void
storaged_linux_manager_glusterfs_init (StoragedLinuxManagerGlusterFS *manager)
{
  manager->daemon = NULL;

  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * storaged_linux_manager_glusterfs_new:
 * @daemon: A #StoragedDaemon.
 *
 * Creates a new #StoragedLinuxManagerGlusterFS instance.
 *
 * Returns: A new #StoragedLinuxManagerGlusterFS. Free with g_object_unref().
 */
StoragedLinuxManagerGlusterFS *
storaged_linux_manager_glusterfs_new (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_LINUX_MANAGER_GLUSTERFS (g_object_new (STORAGED_TYPE_LINUX_MANAGER_GLUSTERFS,
                                                               "daemon", daemon,
                                                               NULL));
}

/**
 * storaged_linux_manager_glusterfs_get_daemon:
 * @manager: A #StoragedLinuxManagerGlusterFS.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @manager.
 */
StoragedDaemon *
storaged_linux_manager_glusterfs_get_daemon (StoragedLinuxManagerGlusterFS *manager)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER_GLUSTERFS (manager), NULL);
  return manager->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_reload (StoragedManagerGlusterFS *object,
               GDBusMethodInvocation    *invocation)
{
  storaged_notice ("Reloading GlusterFS state");
  StoragedLinuxManagerGlusterFS *manager = STORAGED_LINUX_MANAGER_GLUSTERFS (object);

  storaged_glusterfs_volumes_update (manager->daemon);
  storaged_glusterfs_daemons_update (manager->daemon);
  return TRUE;
}

static gboolean
handle_glusterd_start (StoragedManagerGlusterFS *object,
                       GDBusMethodInvocation    *invocation)
{
  storaged_notice ("Starting glusterd.service");

  StoragedLinuxManagerGlusterFS *manager = STORAGED_LINUX_MANAGER_GLUSTERFS (object);

  GError *error;
  GDBusProxy *proxy;
  GVariant *sdjob_path;

  error = NULL;
  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.freedesktop.systemd1",
                                         "/org/freedesktop/systemd1",
                                         "org.freedesktop.system1.Manager",
                                         NULL,
                                         &error);
  if (proxy == NULL)
    {
      storaged_error ("Error creating proxy for systemd dbus: %s\n", error->message);
      g_error_free (error);
      return FALSE;
    }

  error = NULL;
  sdjob_path = g_dbus_proxy_call_sync (proxy,
                                       "StartUnit",
                                       g_variant_new ("(ss)",
                                                      "glusterd.service",
                                                      "replace"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       NULL,
                                       &error);
  if (sdjob_path == NULL)
    {
      storaged_error ("Couldn't start glusterd.service: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  storaged_glusterfs_daemons_update (manager->daemon);

out:
  return (sdjob_path != NULL);
}

static gboolean
handle_glusterd_stop (StoragedManagerGlusterFS *object,
                      GDBusMethodInvocation    *invocation)
{
  storaged_notice ("Stopping glusterd.service");

  StoragedLinuxManagerGlusterFS *manager = STORAGED_LINUX_MANAGER_GLUSTERFS (object);

  GError *error;
  GDBusProxy *proxy;
  GVariant *sdjob_path;

  error = NULL;
  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.freedesktop.systemd1",
                                         "/org/freedesktop/systemd1",
                                         "org.freedesktop.system1.Manager",
                                         NULL,
                                         &error);
  if (proxy == NULL)
    {
      storaged_error ("Error creating proxy for systemd dbus: %s\n", error->message);
      g_error_free (error);
      return FALSE;
    }

  error = NULL;
  sdjob_path = g_dbus_proxy_call_sync (proxy,
                                       "StopUnit",
                                       g_variant_new ("(ss)",
                                                      "glusterd.service",
                                                      "replace"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       NULL,
                                       &error);
  if (sdjob_path == NULL)
    {
      storaged_error ("Couldn't stop glusterd.service: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  storaged_glusterfs_daemons_update (manager->daemon);

out:
  return (sdjob_path != NULL);
}

static StoragedObject *
wait_for_gluster_volume_object (StoragedDaemon *daemon,
                                gpointer        userdata)
{
  const gchar *name = userdata;

  return STORAGED_OBJECT (storaged_glusterfs_util_find_volume_object (daemon, name));
}

static gboolean
handle_volume_create (StoragedLinuxManagerGlusterFS *_object,
                      GDBusMethodInvocation         *invocation,
                      const gchar                   *arg_name,
                      const gchar *const            *arg_bricks,
                      GVariant                      *arg_options)
{
  StoragedLinuxManagerGlusterFS *manager = STORAGED_LINUX_MANAGER_GLUSTERFS(_object);
  uid_t caller_uid;
  GError *error = NULL;
  guint n;
  gchar *escaped_name = NULL;
  GString *str = NULL;
  gint status;
  gchar *error_message = NULL;
  StoragedObject *volume_object = NULL;

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_manager_glusterfs_get_daemon (manager),
                                       NULL,
                                       glusterfs_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to create a volume group"),
                                       invocation);


  /* Create the gluster volume... */
  escaped_name = storaged_daemon_util_escape_and_quote (arg_name);
  str = g_string_new ("gluster volume create");
  g_string_append_printf (str, " %s", escaped_name);

  for (n = 0; arg_bricks != NULL && arg_bricks[n] != NULL; n++)
    {
      gchar *escaped_brick;
      escaped_brick = storaged_daemon_util_escape_and_quote (arg_bricks[n]);
      g_string_append_printf (str, " %s", escaped_brick);
      g_free (escaped_brick);
    }

  if (!storaged_daemon_launch_spawned_job_sync (manager->daemon,
                                                NULL,
                                                "gluster-volume-create", caller_uid,
                                                NULL, /* cancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                &status,
                                                &error_message,
                                                NULL, /* input_string */
                                                "%s",
                                                str->str))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error creating gluster volume: %s",
                                             error_message);
      g_free (error_message);
      goto out;
    }

  /* ... then, sit and wait for the object to show up */
  volume_object = storaged_daemon_wait_for_object_sync (manager->daemon,
                                                        wait_for_gluster_volume_object,
                                                        (gpointer) arg_name,
                                                        NULL,
                                                        10, /* timeout_seconds */
                                                        &error);
  if (volume_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for gluster volume object for %s",
                      arg_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_manager_glusterfs_complete_volume_create (_object,
                                                     invocation,
                                                     g_dbus_object_get_object_path (G_DBUS_OBJECT (volume_object)));

 out:
  if (str != NULL)
    g_string_free (str, TRUE);
  g_free (escaped_name);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}


static void
storaged_linux_manager_glusterfs_iface_init (StoragedManagerGlusterFSIface *iface)
{
  iface->handle_reload = handle_reload;
  iface->handle_glusterd_start = handle_glusterd_start;
  iface->handle_glusterd_stop = handle_glusterd_stop;
  iface->handle_volume_create = handle_volume_create;
}

