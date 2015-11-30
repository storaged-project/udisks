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

#include "storagedlinuxglusterfsvolumeobject.h"
#include "storagedlinuxglusterfsvolume.h"
#include "storagedglusterfsutils.h"
#include "storagedglusterfsinfo.h"
#include "storaged-glusterfs-generated.h"

struct _StoragedLinuxGlusterFSVolumeObject {
  StoragedObjectSkeleton parent_instance;

  StoragedDaemon *daemon;
  gchar *name;

  /* Interfaces */
  StoragedLinuxGlusterFSVolume *iface_glusterfs_volume; 
};

struct _StoragedLinuxGlusterFSVolumeObjectClass {
  StoragedObjectSkeletonClass parent_class;
};

enum
{ 
  PROP_0,
  PROP_DAEMON,
  PROP_NAME,
};

G_DEFINE_TYPE (StoragedLinuxGlusterFSVolumeObject, storaged_linux_glusterfs_volume_object, STORAGED_TYPE_OBJECT_SKELETON);

static void
storaged_linux_glusterfs_volume_object_finalize (GObject *_object)
{ 
  StoragedLinuxGlusterFSVolumeObject *object = STORAGED_LINUX_GLUSTERFS_VOLUME_OBJECT (_object);
  
  /* note: we don't hold a ref to object->daemon */
  
  if (object->iface_glusterfs_volume != NULL)
    g_object_unref (object->iface_glusterfs_volume);
  
  g_free (object->name);
  
  if (G_OBJECT_CLASS (storaged_linux_glusterfs_volume_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_volume_object_parent_class)->finalize (_object);
}

static void
storaged_linux_glusterfs_volume_object_get_property (GObject    *__object,
                                                     guint       prop_id,
                                                     GValue     *value,
                                                     GParamSpec *pspec)
{ 
  StoragedLinuxGlusterFSVolumeObject *object = STORAGED_LINUX_GLUSTERFS_VOLUME_OBJECT (__object);
  
  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_glusterfs_volume_object_get_daemon (object));
      break;
    
    case PROP_NAME:
      g_value_set_string (value, storaged_linux_glusterfs_volume_object_get_name (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_glusterfs_volume_object_set_property (GObject      *__object,
                                                     guint         prop_id,
                                                     const GValue *value,
                                                     GParamSpec   *pspec)
{
  StoragedLinuxGlusterFSVolumeObject *object = STORAGED_LINUX_GLUSTERFS_VOLUME_OBJECT (__object);

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

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_glusterfs_volume_object_init (StoragedLinuxGlusterFSVolumeObject *object)
{
}

static void
storaged_linux_glusterfs_volume_object_constructed (GObject *_object)
{
  StoragedLinuxGlusterFSVolumeObject *object = STORAGED_LINUX_GLUSTERFS_VOLUME_OBJECT (_object);
  GString *s;

  if (G_OBJECT_CLASS (storaged_linux_glusterfs_volume_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_volume_object_parent_class)->constructed (_object);


  /* compute the object path */
  s = g_string_new ("/org/storaged/Storaged/glusterfs/volume/");
  storaged_safe_append_to_object_path (s, object->name);
  storaged_notice ("New GlusterFS Volume object with path %s", s->str);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), s->str);
  g_string_free (s, TRUE);

  /* create the DBus interface */
  object->iface_glusterfs_volume = storaged_linux_glusterfs_volume_new ();
  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                        G_DBUS_INTERFACE_SKELETON (object->iface_glusterfs_volume));
}

static void
storaged_linux_glusterfs_volume_object_class_init (StoragedLinuxGlusterFSVolumeObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_linux_glusterfs_volume_object_finalize;
  gobject_class->constructed  = storaged_linux_glusterfs_volume_object_constructed;
  gobject_class->set_property = storaged_linux_glusterfs_volume_object_set_property;
  gobject_class->get_property = storaged_linux_glusterfs_volume_object_get_property;
                                    
  /**                               
   * StoragedLinuxGlusterFSVolumeObject:daemon:
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
   * StoragedLinuxGlusterFSVolumeObject:name:
   *
   * The name of the GlusterFS volume.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "The name of the glusterfs volume",
                                                        NULL,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

StoragedLinuxGlusterFSVolumeObject *
storaged_linux_glusterfs_volume_object_new (StoragedDaemon  *daemon,
                                            const gchar     *name)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  return STORAGED_LINUX_GLUSTERFS_VOLUME_OBJECT (g_object_new (STORAGED_TYPE_LINUX_GLUSTERFS_VOLUME_OBJECT,
                                                               "daemon", daemon,
                                                               "name", name,
                                                               NULL));
}

StoragedDaemon *
storaged_linux_glusterfs_volume_object_get_daemon (StoragedLinuxGlusterFSVolumeObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_GLUSTERFS_VOLUME_OBJECT (object), NULL);
  return object->daemon;
}

static void
update_from_variant (GVariant *volume_info_xml,
                     GError *error,
                     gpointer user_data)
{
  StoragedLinuxGlusterFSVolumeObject *object;
  StoragedDaemon *daemon;
  GDBusObjectManagerServer *manager;
  GVariant *gfs_volume_info;

  if (error != NULL)
    {
      storaged_warning ("Couldn't get volume info: %s", error->message);
      return;
    }

  object = user_data;
  daemon = storaged_linux_glusterfs_volume_object_get_daemon (object);
  manager = storaged_daemon_get_object_manager (daemon);


  gfs_volume_info = storaged_process_glusterfs_volume_info (g_variant_get_bytestring (volume_info_xml));

  storaged_linux_glusterfs_volume_update (STORAGED_LINUX_GLUSTERFS_VOLUME (object->iface_glusterfs_volume), gfs_volume_info);

  if (!g_dbus_object_manager_server_is_exported (manager, G_DBUS_OBJECT_SKELETON (object))) {
    g_dbus_object_manager_server_export_uniquely (manager, G_DBUS_OBJECT_SKELETON (object));
  }
  g_object_unref (object);
}

void
storaged_linux_glusterfs_volume_object_update (StoragedLinuxGlusterFSVolumeObject *object)
{
  const gchar *args[] = { "/usr/sbin/gluster", "volume", "info", object->name, "--xml", NULL };
  storaged_glusterfs_spawn_for_variant (args, G_VARIANT_TYPE("s"),
                                        update_from_variant, g_object_ref (object));
}

void
storaged_linux_glusterfs_volume_object_destroy (StoragedLinuxGlusterFSVolumeObject *object)
{
  g_dbus_object_manager_server_unexport (storaged_daemon_get_object_manager (object->daemon),
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
}

const gchar *
storaged_linux_glusterfs_volume_object_get_name (StoragedLinuxGlusterFSVolumeObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_GLUSTERFS_VOLUME_OBJECT (object), NULL);
  return object->name;
}
