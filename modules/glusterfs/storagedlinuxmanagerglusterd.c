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
#include "storagedlinuxmanagerglusterd.h"
#include "storaged-glusterfs-generated.h"

struct _StoragedLinuxManagerGlusterD {

  StoragedManagerGlusterDSkeleton parent_instance;

  StoragedDaemon *daemon;
};

struct _StoragedLinuxManagerGlusterDClass {
  StoragedManagerGlusterDSkeletonClass parent_class;
};

enum {
  PROP_0,
  PROP_DAEMON
};

static void storaged_linux_manager_glusterd_iface_init (StoragedManagerGlusterDIface *iface); 

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxManagerGlusterD, storaged_linux_manager_glusterd,
                         STORAGED_TYPE_MANAGER_GLUSTERD_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MANAGER_GLUSTERD,
                                                storaged_linux_manager_glusterd_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_manager_glusterd_get_property (GObject *object,
                                              guint property_id,
                                              GValue *value,
                                              GParamSpec *pspec)
{
  StoragedLinuxManagerGlusterD *manager = STORAGED_LINUX_MANAGER_GLUSTERD(object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_manager_glusterd_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_glusterd_set_property (GObject *object,
                                              guint property_id,
                                              GValue *value,
                                              GParamSpec *pspec)
{
  StoragedLinuxManagerGlusterD *manager = STORAGED_LINUX_MANAGER_GLUSTERD(object);

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
storaged_linux_manager_glusterd_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_glusterd_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_glusterd_parent_class)->dispose (object);
}

static void
storaged_linux_manager_glusterd_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_glusterd_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_glusterd_parent_class)->finalize (object);
}

static void
storaged_linux_manager_glusterd_class_init (StoragedLinuxManagerGlusterDClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);     

  gobject_class->get_property = storaged_linux_manager_glusterd_get_property;
  gobject_class->set_property = storaged_linux_manager_glusterd_set_property;
  gobject_class->dispose = storaged_linux_manager_glusterd_dispose;
  gobject_class->finalize = storaged_linux_manager_glusterd_finalize;

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
storaged_linux_manager_glusterd_init (StoragedLinuxManagerGlusterD *manager)
{
  manager->daemon = NULL;

  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * storaged_linux_manager_glusterd_new:
 * @daemon: A #StoragedDaemon.
 *
 * Creates a new #StoragedLinuxManagerGlusterD instance.
 *
 * Returns: A new #StoragedLinuxManagerGlusterD. Free with g_object_unref().
 */
StoragedLinuxManagerGlusterD *
storaged_linux_manager_glusterd_new (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_LINUX_MANAGER_GLUSTERD (g_object_new (STORAGED_TYPE_LINUX_MANAGER_GLUSTERD,
                                                               "daemon", daemon,
                                                               NULL));
}

/**
 * storaged_linux_manager_glusterd_get_daemon:
 * @manager: A #StoragedLinuxManagerGlusterD.
 * 
 * Gets the daemon used by @manager.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @manager.
 */
StoragedDaemon *
storaged_linux_manager_glusterd_get_daemon (StoragedLinuxManagerGlusterD *manager)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER_GLUSTERD (manager), NULL);
  return manager->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_reload (StoragedManagerGlusterD *object,
               GDBusMethodInvocation   *invocation)
{
  storaged_notice ("Reloading GlusterFS state");
  StoragedLinuxManagerGlusterD *manager = STORAGED_LINUX_MANAGER_GLUSTERD (object);

  storaged_glusterfs_volumes_update (manager->daemon); 
  return TRUE;
}

static gboolean
handle_volume_create (StoragedManagerGlusterD *object,
                      GDBusMethodInvocation   *invocation,
                      const gchar             *arg_name,
                      const gchar *const      *arg_bricks)
{
  ;
}

static void
storaged_linux_manager_glusterd_iface_init (StoragedManagerGlusterDIface *iface)
{
  iface->handle_reload = handle_reload;
  iface->handle_volume_create = handle_volume_create;
}

