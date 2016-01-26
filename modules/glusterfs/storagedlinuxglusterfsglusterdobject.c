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

#include <storaged/storaged-generated.h>
#include <src/storageddaemonutil.h>
#include <src/storageddaemon.h>
#include <src/storagedlogging.h>

#include "storagedlinuxglusterfsglusterdobject.h"
#include "storagedlinuxglusterfsglusterd.h"
#include "storagedglusterfsutils.h"
#include "storaged-glusterfs-generated.h"

struct _StoragedLinuxGlusterFSGlusterdObject {
  StoragedObjectSkeleton parent_instance;
  StoragedDaemon *daemon;

  /* Interfaces */
  StoragedLinuxGlusterFSGlusterd *iface_glusterfs_glusterd; 
};

struct _StoragedLinuxGlusterFSGlusterdObjectClass {
  StoragedObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
};

G_DEFINE_TYPE (StoragedLinuxGlusterFSGlusterdObject, storaged_linux_glusterfs_glusterd_object, STORAGED_TYPE_OBJECT_SKELETON);

static void
storaged_linux_glusterfs_glusterd_object_finalize (GObject *_object)
{
  StoragedLinuxGlusterFSGlusterdObject *object = STORAGED_LINUX_GLUSTERFS_GLUSTERD_OBJECT (_object);

  /* note: we don't hold a ref to object->daemon */

  if (object->iface_glusterfs_glusterd != NULL)
    g_object_unref (object->iface_glusterfs_glusterd);

  if (G_OBJECT_CLASS (storaged_linux_glusterfs_glusterd_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_glusterd_object_parent_class)->finalize (_object);
}

static void
storaged_linux_glusterfs_glusterd_object_get_property (GObject    *__object,
                                                     guint       prop_id,
                                                     GValue     *value,
                                                     GParamSpec *pspec)
{
  StoragedLinuxGlusterFSGlusterdObject *object = STORAGED_LINUX_GLUSTERFS_GLUSTERD_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_glusterfs_glusterd_object_get_daemon (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_glusterfs_glusterd_object_set_property (GObject      *__object,
                                                       guint         prop_id,
                                                       const GValue *value,
                                                       GParamSpec   *pspec)
{
  StoragedLinuxGlusterFSGlusterdObject *object = STORAGED_LINUX_GLUSTERFS_GLUSTERD_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (object->daemon == NULL);
      /* we don't take a reference to the daemon */
      object->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_glusterfs_glusterd_object_init (StoragedLinuxGlusterFSGlusterdObject *object)
{
}

static void
storaged_linux_glusterfs_glusterd_object_constructed (GObject *_object)
{
  StoragedLinuxGlusterFSGlusterdObject *object = STORAGED_LINUX_GLUSTERFS_GLUSTERD_OBJECT (_object);

  if (G_OBJECT_CLASS (storaged_linux_glusterfs_glusterd_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_glusterd_object_parent_class)->constructed (_object);

  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object),
                                          "/org/storaged/Storaged/glusterfs/daemons/glusterd");

  /* create the DBus interface */
  object->iface_glusterfs_glusterd = storaged_linux_glusterfs_glusterd_new ();
  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                        G_DBUS_INTERFACE_SKELETON (object->iface_glusterfs_glusterd));
}

static void
storaged_linux_glusterfs_glusterd_object_class_init (StoragedLinuxGlusterFSGlusterdObjectClass *klass)
{
  storaged_notice ("In class_init");
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_linux_glusterfs_glusterd_object_finalize;
  gobject_class->constructed  = storaged_linux_glusterfs_glusterd_object_constructed;
  gobject_class->set_property = storaged_linux_glusterfs_glusterd_object_set_property;
  gobject_class->get_property = storaged_linux_glusterfs_glusterd_object_get_property;

  /**
   * StoragedLinuxGlusterFSGlusterdObject:daemon:
   *
   * The #StoragedDaemon the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the object is for",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

}

void
storaged_linux_glusterfs_glusterd_object_update (StoragedLinuxGlusterFSGlusterdObject *object)
{
  GVariant *info;
  StoragedDaemon *daemon;
  GDBusObjectManagerServer *manager;

  info =  storaged_get_glusterd_info ();
  daemon = storaged_linux_glusterfs_glusterd_object_get_daemon (object);
  manager = storaged_daemon_get_object_manager (daemon);

  storaged_linux_glusterfs_glusterd_update (STORAGED_LINUX_GLUSTERFS_GLUSTERD (object->iface_glusterfs_glusterd),
                                            info);

  if (!g_dbus_object_manager_server_is_exported (manager, G_DBUS_OBJECT_SKELETON (object)))
    g_dbus_object_manager_server_export_uniquely (manager, G_DBUS_OBJECT_SKELETON (object));

}

StoragedLinuxGlusterFSGlusterdObject *
storaged_linux_glusterfs_glusterd_object_new (StoragedDaemon  *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_LINUX_GLUSTERFS_GLUSTERD_OBJECT (g_object_new (STORAGED_TYPE_LINUX_GLUSTERFS_GLUSTERD_OBJECT,
                                                                 "daemon", daemon,
                                                                 NULL));
}

StoragedDaemon *
storaged_linux_glusterfs_glusterd_object_get_daemon (StoragedLinuxGlusterFSGlusterdObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_GLUSTERFS_GLUSTERD_OBJECT (object), NULL);
  return object->daemon;
}

void
storaged_linux_glusterfs_glusterd_object_destroy (StoragedLinuxGlusterFSGlusterdObject *object)
{
  g_dbus_object_manager_server_unexport (storaged_daemon_get_object_manager (object->daemon),
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
}

