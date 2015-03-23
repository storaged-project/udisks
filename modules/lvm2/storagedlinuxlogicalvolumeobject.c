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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <src/storagedlogging.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxprovider.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlinuxblockobject.h>

#include "storagedlinuxvolumegroup.h"
#include "storagedlinuxvolumegroupobject.h"
#include "storagedlinuxlogicalvolumeobject.h"
#include "storagedlinuxlogicalvolume.h"

#include "storaged-lvm2-generated.h"

/**
 * SECTION:storagedlinuxlogicalvolumeobject
 * @title: StoragedLinuxLogicalVolumeObject
 * @short_description: Object representing a LVM2 logical volume
 */

typedef struct _StoragedLinuxLogicalVolumeObjectClass   StoragedLinuxLogicalVolumeObjectClass;

/**
 * StoragedLinuxLogicalVolumeObject:
 *
 * The #StoragedLinuxLogicalVolumeObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _StoragedLinuxLogicalVolumeObject
{
  StoragedObjectSkeleton parent_instance;

  StoragedDaemon *daemon;

  gchar *name;
  StoragedLinuxVolumeGroupObject *volume_group;

  StoragedLogicalVolume *iface_logical_volume;
};

struct _StoragedLinuxLogicalVolumeObjectClass
{
  StoragedObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_NAME,
  PROP_VOLUME_GROUP,
  PROP_DAEMON,
};

G_DEFINE_TYPE (StoragedLinuxLogicalVolumeObject, storaged_linux_logical_volume_object, STORAGED_TYPE_OBJECT_SKELETON);

static void
storaged_linux_logical_volume_object_finalize (GObject *_object)
{
  StoragedLinuxLogicalVolumeObject *object = STORAGED_LINUX_LOGICAL_VOLUME_OBJECT (_object);

  /* note: we don't hold a ref to object->daemon */

  if (object->iface_logical_volume != NULL)
    g_object_unref (object->iface_logical_volume);

  g_free (object->name);

  if (G_OBJECT_CLASS (storaged_linux_logical_volume_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_logical_volume_object_parent_class)->finalize (_object);
}

static void
storaged_linux_logical_volume_object_get_property (GObject    *__object,
                                                   guint       prop_id,
                                                   GValue     *value,
                                                   GParamSpec *pspec)
{
  StoragedLinuxLogicalVolumeObject *object = STORAGED_LINUX_LOGICAL_VOLUME_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_logical_volume_object_get_daemon (object));
      break;

    case PROP_VOLUME_GROUP:
      g_value_set_object (value, storaged_linux_logical_volume_object_get_volume_group (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_logical_volume_object_set_property (GObject      *__object,
                                                   guint         prop_id,
                                                   const GValue *value,
                                                   GParamSpec   *pspec)
{
  StoragedLinuxLogicalVolumeObject *object = STORAGED_LINUX_LOGICAL_VOLUME_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (object->daemon == NULL);
      /* we don't take a reference to the daemon */
      object->daemon = g_value_get_object (value);
      break;

    case PROP_NAME:
      object->name = g_value_dup_string (value);
      break;

    case PROP_VOLUME_GROUP:
      g_assert (object->volume_group == NULL);
      object->volume_group = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_logical_volume_object_init (StoragedLinuxLogicalVolumeObject *object)
{
}

static void
storaged_linux_logical_volume_object_constructed (GObject *_object)
{
  StoragedLinuxLogicalVolumeObject *object = STORAGED_LINUX_LOGICAL_VOLUME_OBJECT (_object);
  GString *s;

  if (G_OBJECT_CLASS (storaged_linux_logical_volume_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (storaged_linux_logical_volume_object_parent_class)->constructed (_object);

  /* compute the object path */

  s = g_string_new (g_dbus_object_get_object_path (G_DBUS_OBJECT (object->volume_group)));
  g_string_append_c (s, '/');
  storaged_safe_append_to_object_path (s, object->name);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), s->str);
  g_string_free (s, TRUE);

  /* create the DBus interface */
  object->iface_logical_volume = storaged_linux_logical_volume_new ();
  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                        G_DBUS_INTERFACE_SKELETON (object->iface_logical_volume));
}

static void
storaged_linux_logical_volume_object_class_init (StoragedLinuxLogicalVolumeObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_linux_logical_volume_object_finalize;
  gobject_class->constructed  = storaged_linux_logical_volume_object_constructed;
  gobject_class->set_property = storaged_linux_logical_volume_object_set_property;
  gobject_class->get_property = storaged_linux_logical_volume_object_get_property;

  /**
   * StoragedLinuxLogicalVolumeObject:daemon:
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
   * StoragedLinuxLogicalVolumeObject:name:
   *
   * The name of the logical volume.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "The name of the volume group",
                                                        NULL,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

 /**
   * StoragedLinuxLogicalVolumeObject:volume_group:
   *
   * The volume group.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_VOLUME_GROUP,
                                   g_param_spec_object ("volumegroup",
                                                        "Volume Group",
                                                        "The volume group",
                                                        STORAGED_TYPE_LINUX_VOLUME_GROUP_OBJECT,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * storaged_linux_logical_volume_object_new:
 *
 * Create a new LogicalVolume object.
 *
 * Returns: A #StoragedLinuxLogicalVolumeObject object. Free with g_object_unref().
 */
StoragedLinuxLogicalVolumeObject *
storaged_linux_logical_volume_object_new (StoragedDaemon                 *daemon,
                                          StoragedLinuxVolumeGroupObject *volume_group,
                                          const gchar                    *name)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (STORAGED_IS_LINUX_VOLUME_GROUP_OBJECT (volume_group), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  return STORAGED_LINUX_LOGICAL_VOLUME_OBJECT (g_object_new (STORAGED_TYPE_LINUX_LOGICAL_VOLUME_OBJECT,
                                                             "daemon", daemon,
                                                             "volumegroup", volume_group,
                                                             "name", name,
                                                             NULL));
}

/**
 * storaged_linux_logical_volume_object_get_daemon:
 * @object: A #StoragedLinuxLogicalVolumeObject.
 *
 * Gets the daemon used by @object.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @object.
 */
StoragedDaemon *
storaged_linux_logical_volume_object_get_daemon (StoragedLinuxLogicalVolumeObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_LOGICAL_VOLUME_OBJECT (object), NULL);
  return object->daemon;
}

StoragedLinuxVolumeGroupObject *
storaged_linux_logical_volume_object_get_volume_group (StoragedLinuxLogicalVolumeObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_LOGICAL_VOLUME_OBJECT (object), NULL);
  return object->volume_group;
}

const gchar *
storaged_linux_logical_volume_object_get_name (StoragedLinuxLogicalVolumeObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_LOGICAL_VOLUME_OBJECT (object), NULL);
  return object->name;
}

void
storaged_linux_logical_volume_object_update (StoragedLinuxLogicalVolumeObject *object,
                                             GVariant *info,
                                             gboolean *needs_polling_ret)
{
  g_return_if_fail (STORAGED_IS_LINUX_LOGICAL_VOLUME_OBJECT (object));

  storaged_linux_logical_volume_update (STORAGED_LINUX_LOGICAL_VOLUME (object->iface_logical_volume),
                                        object->volume_group,
                                        info,
                                        needs_polling_ret);
}
