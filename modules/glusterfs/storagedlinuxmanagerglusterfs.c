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

static void
storaged_linux_manager_glusterfs_iface_init (StoragedManagerGlusterFSIface *iface)
{
  iface->handle_reload = handle_reload;
  iface->handle_glusterd_start = handle_glusterd_start;
  iface->handle_glusterd_stop = handle_glusterd_stop;
}

