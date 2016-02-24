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

#include "storagedlinuxglusterfsbrickobject.h"
#include "storagedlinuxglusterfsbrick.h"
#include "storagedlinuxglusterfsvolumeobject.h"
#include "storagedglusterfsutils.h"
#include "storagedglusterfsinfo.h"
#include "storaged-glusterfs-generated.h"

struct _StoragedLinuxGlusterFSBrickObject
{
  StoragedObjectSkeleton parent_instance;

  StoragedDaemon *daemon;
  StoragedLinuxGlusterFSVolumeObject *volume_object;
  gchar *name;

  /* Interfaces */
  StoragedGlusterFSBrick *iface_glusterfs_brick;
};

struct _StoragedLinuxGlusterFSBrickObjectClass
{
  StoragedObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_NAME,
  PROP_VOLUME
};

G_DEFINE_TYPE (StoragedLinuxGlusterFSBrickObject, storaged_linux_glusterfs_brick_object, STORAGED_TYPE_OBJECT_SKELETON);

static void
storaged_linux_glusterfs_brick_object_dispose (GObject *_object)
{
  StoragedLinuxGlusterFSBrickObject *object = STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT (_object);

  if (object->iface_glusterfs_brick != NULL)
    g_clear_object (&object->iface_glusterfs_brick);

  if (G_OBJECT_CLASS (storaged_linux_glusterfs_brick_object_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_brick_object_parent_class)->dispose (_object);
}

static void
storaged_linux_glusterfs_brick_object_finalize (GObject *_object)
{
  StoragedLinuxGlusterFSBrickObject *object = STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT (_object);
  /* note: we don't hold a ref to object->daemon */

  g_free (object->name);

  if (G_OBJECT_CLASS (storaged_linux_glusterfs_brick_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_brick_object_parent_class)->finalize (_object);
}

static void
storaged_linux_glusterfs_brick_object_get_property (GObject    *__object,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec)
{
  StoragedLinuxGlusterFSBrickObject *object = STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT (__object);
  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_glusterfs_brick_object_get_daemon (object));
      break;

    case PROP_NAME:
      g_value_set_string (value, storaged_linux_glusterfs_brick_object_get_name (object));
      break;

    case PROP_VOLUME:
      g_value_set_object (value, storaged_linux_glusterfs_brick_object_get_volume_object (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_glusterfs_brick_object_set_property (GObject      *__object,
                                                    guint         prop_id,
                                                    const GValue *value,
                                                    GParamSpec   *pspec)
{
  StoragedLinuxGlusterFSBrickObject *object = STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT (__object);
  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (object->daemon == NULL);
      /* we don't take a reference to the daemon */
      object->daemon = g_value_get_object (value);
      break;

    case PROP_NAME:
      g_assert (object->name == NULL);
      object->name = g_value_dup_string (value);
      break;

    case PROP_VOLUME:
      g_assert (object->volume_object == NULL);
      object->volume_object = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_glusterfs_brick_object_init (StoragedLinuxGlusterFSBrickObject *object)
{
}

static void
storaged_linux_glusterfs_brick_object_constructed (GObject *_object)
{
  StoragedLinuxGlusterFSBrickObject *object = STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT (_object);
  GString *s;

  if (G_OBJECT_CLASS (storaged_linux_glusterfs_brick_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_brick_object_parent_class)->constructed (_object);


  /* compute the object path */
  s = g_string_new ("/org/storaged/Storaged/glusterfs/brick/");
  storaged_safe_append_to_object_path (s, object->name);
  storaged_notice ("New GlusterFS brick object with path %s", s->str);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), s->str);
  g_string_free (s, TRUE);

  /* create the DBus interface */
  object->iface_glusterfs_brick = storaged_linux_glusterfs_brick_new ();
  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                        G_DBUS_INTERFACE_SKELETON (object->iface_glusterfs_brick));
}

static void
storaged_linux_glusterfs_brick_object_class_init (StoragedLinuxGlusterFSBrickObjectClass *klass)
{
  GObjectClass *gobject_class;
  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose      = storaged_linux_glusterfs_brick_object_dispose;
  gobject_class->finalize     = storaged_linux_glusterfs_brick_object_finalize;
  gobject_class->constructed  = storaged_linux_glusterfs_brick_object_constructed;
  gobject_class->set_property = storaged_linux_glusterfs_brick_object_set_property;
  gobject_class->get_property = storaged_linux_glusterfs_brick_object_get_property;

  /**
   * StoragedLinuxGlusterFSBrickObject:daemon:
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
  /**
   * StoragedLinuxGlusterFSBrickObject:name:
   *
   * The name of the GlusterFS brick.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "The name of the glusterfs brick",
                                                        NULL,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
  /**
   * StoragedLinuxGlusterFSVolumeObject:volume:
   *
   * The name of the GlusterFS volume to which the brick belongs.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_VOLUME,
                                   g_param_spec_object ("volume",
                                                        "Volume Object",
                                                        "The name of the glusterfs volume",
                                                        STORAGED_TYPE_LINUX_GLUSTERFS_VOLUME_OBJECT,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

StoragedLinuxGlusterFSBrickObject *
storaged_linux_glusterfs_brick_object_new (StoragedDaemon                     *daemon,
                                           StoragedLinuxGlusterFSVolumeObject *volume_object,
                                           const gchar                        *name)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  return STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT (g_object_new (STORAGED_TYPE_LINUX_GLUSTERFS_BRICK_OBJECT,
                                                              "daemon", daemon,
                                                              "name", name,
                                                              "volume", volume_object,
                                                              NULL));
}

StoragedDaemon *
storaged_linux_glusterfs_brick_object_get_daemon (StoragedLinuxGlusterFSBrickObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_GLUSTERFS_BRICK_OBJECT (object), NULL);
  return object->daemon;
}

void
storaged_linux_glusterfs_brick_object_destroy (StoragedLinuxGlusterFSBrickObject *object)
{
  g_dbus_object_manager_server_unexport (storaged_daemon_get_object_manager (object->daemon),
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
}

const gchar *
storaged_linux_glusterfs_brick_object_get_name (StoragedLinuxGlusterFSBrickObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_GLUSTERFS_BRICK_OBJECT (object), NULL);
  return object->name;
}

StoragedLinuxGlusterFSVolumeObject *
storaged_linux_glusterfs_brick_object_get_volume_object (StoragedLinuxGlusterFSBrickObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_GLUSTERFS_BRICK_OBJECT (object), NULL);
  return object->volume_object;
}

void
storaged_linux_glusterfs_brick_object_update (StoragedLinuxGlusterFSBrickObject *object,
                                              GVariant                          *brick_info)
{
  g_return_if_fail (STORAGED_IS_LINUX_GLUSTERFS_BRICK_OBJECT (object));

  storaged_linux_glusterfs_brick_update (STORAGED_LINUX_GLUSTERFS_BRICK (object->iface_glusterfs_brick),
                                         object->volume_object,
                                         brick_info);
}

