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

#include <blockdev/lvm.h>

#include <src/udiskslogging.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxprovider.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslinuxblockobject.h>

#include "udiskslinuxvolumegroup.h"
#include "udiskslinuxvolumegroupobject.h"
#include "udiskslinuxlogicalvolumeobject.h"
#include "udiskslinuxlogicalvolume.h"

#include "udisks-lvm2-generated.h"

/**
 * SECTION:udiskslinuxlogicalvolumeobject
 * @title: UDisksLinuxLogicalVolumeObject
 * @short_description: Object representing a LVM2 logical volume
 */

typedef struct _UDisksLinuxLogicalVolumeObjectClass   UDisksLinuxLogicalVolumeObjectClass;

/**
 * UDisksLinuxLogicalVolumeObject:
 *
 * The #UDisksLinuxLogicalVolumeObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxLogicalVolumeObject
{
  UDisksObjectSkeleton parent_instance;

  UDisksDaemon *daemon;

  gchar *name;
  UDisksLinuxVolumeGroupObject *volume_group;

  UDisksLogicalVolume *iface_logical_volume;
};

struct _UDisksLinuxLogicalVolumeObjectClass
{
  UDisksObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_NAME,
  PROP_VOLUME_GROUP,
  PROP_DAEMON,
};

G_DEFINE_TYPE (UDisksLinuxLogicalVolumeObject, udisks_linux_logical_volume_object, UDISKS_TYPE_OBJECT_SKELETON);

static void
udisks_linux_logical_volume_object_finalize (GObject *_object)
{
  UDisksLinuxLogicalVolumeObject *object = UDISKS_LINUX_LOGICAL_VOLUME_OBJECT (_object);

  /* note: we don't hold a ref to object->daemon */

  if (object->iface_logical_volume != NULL)
    g_object_unref (object->iface_logical_volume);

  g_free (object->name);

  if (G_OBJECT_CLASS (udisks_linux_logical_volume_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_logical_volume_object_parent_class)->finalize (_object);
}

static void
udisks_linux_logical_volume_object_get_property (GObject    *__object,
                                                 guint       prop_id,
                                                 GValue     *value,
                                                 GParamSpec *pspec)
{
  UDisksLinuxLogicalVolumeObject *object = UDISKS_LINUX_LOGICAL_VOLUME_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_logical_volume_object_get_daemon (object));
      break;

    case PROP_VOLUME_GROUP:
      g_value_set_object (value, udisks_linux_logical_volume_object_get_volume_group (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_logical_volume_object_set_property (GObject      *__object,
                                                 guint         prop_id,
                                                 const GValue *value,
                                                 GParamSpec   *pspec)
{
  UDisksLinuxLogicalVolumeObject *object = UDISKS_LINUX_LOGICAL_VOLUME_OBJECT (__object);

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
udisks_linux_logical_volume_object_init (UDisksLinuxLogicalVolumeObject *object)
{
}

static void
udisks_linux_logical_volume_object_constructed (GObject *_object)
{
  UDisksLinuxLogicalVolumeObject *object = UDISKS_LINUX_LOGICAL_VOLUME_OBJECT (_object);
  GString *s;

  if (G_OBJECT_CLASS (udisks_linux_logical_volume_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_logical_volume_object_parent_class)->constructed (_object);

  /* compute the object path */

  s = g_string_new (g_dbus_object_get_object_path (G_DBUS_OBJECT (object->volume_group)));
  g_string_append_c (s, '/');
  udisks_safe_append_to_object_path (s, object->name);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), s->str);
  g_string_free (s, TRUE);

  /* create the DBus interface */
  object->iface_logical_volume = udisks_linux_logical_volume_new ();
  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                        G_DBUS_INTERFACE_SKELETON (object->iface_logical_volume));
}

static void
udisks_linux_logical_volume_object_class_init (UDisksLinuxLogicalVolumeObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_logical_volume_object_finalize;
  gobject_class->constructed  = udisks_linux_logical_volume_object_constructed;
  gobject_class->set_property = udisks_linux_logical_volume_object_set_property;
  gobject_class->get_property = udisks_linux_logical_volume_object_get_property;

  /**
   * UDisksLinuxLogicalVolumeObject:daemon:
   *
   * The #UDisksDaemon the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the object is for",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksLinuxLogicalVolumeObject:name:
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
   * UDisksLinuxLogicalVolumeObject:volume_group:
   *
   * The volume group.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_VOLUME_GROUP,
                                   g_param_spec_object ("volumegroup",
                                                        "Volume Group",
                                                        "The volume group",
                                                        UDISKS_TYPE_LINUX_VOLUME_GROUP_OBJECT,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_linux_logical_volume_object_new:
 *
 * Create a new LogicalVolume object.
 *
 * Returns: A #UDisksLinuxLogicalVolumeObject object. Free with g_object_unref().
 */
UDisksLinuxLogicalVolumeObject *
udisks_linux_logical_volume_object_new (UDisksDaemon                 *daemon,
                                        UDisksLinuxVolumeGroupObject *volume_group,
                                        const gchar                  *name)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (UDISKS_IS_LINUX_VOLUME_GROUP_OBJECT (volume_group), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  return UDISKS_LINUX_LOGICAL_VOLUME_OBJECT (g_object_new (UDISKS_TYPE_LINUX_LOGICAL_VOLUME_OBJECT,
                                                           "daemon", daemon,
                                                           "volumegroup", volume_group,
                                                           "name", name,
                                                           NULL));
}

/**
 * udisks_linux_logical_volume_object_get_daemon:
 * @object: A #UDisksLinuxLogicalVolumeObject.
 *
 * Gets the daemon used by @object.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @object.
 */
UDisksDaemon *
udisks_linux_logical_volume_object_get_daemon (UDisksLinuxLogicalVolumeObject *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_LOGICAL_VOLUME_OBJECT (object), NULL);
  return object->daemon;
}

UDisksLinuxVolumeGroupObject *
udisks_linux_logical_volume_object_get_volume_group (UDisksLinuxLogicalVolumeObject *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_LOGICAL_VOLUME_OBJECT (object), NULL);
  return object->volume_group;
}

const gchar *
udisks_linux_logical_volume_object_get_name (UDisksLinuxLogicalVolumeObject *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_LOGICAL_VOLUME_OBJECT (object), NULL);
  return object->name;
}

void
udisks_linux_logical_volume_object_update (UDisksLinuxLogicalVolumeObject *object,
                                           BDLVMLVdata *lv_info,
                                           BDLVMLVdata *meta_lv_info,
                                           gboolean *needs_polling_ret)
{
  g_return_if_fail (UDISKS_IS_LINUX_LOGICAL_VOLUME_OBJECT (object));

  udisks_linux_logical_volume_update (UDISKS_LINUX_LOGICAL_VOLUME (object->iface_logical_volume),
                                      object->volume_group,
                                      lv_info, meta_lv_info,
                                      needs_polling_ret);
}

void
udisks_linux_logical_volume_object_update_etctabs (UDisksLinuxLogicalVolumeObject *object)
{
  g_return_if_fail (UDISKS_IS_LINUX_LOGICAL_VOLUME_OBJECT (object));

  udisks_linux_logical_volume_update_etctabs (UDISKS_LINUX_LOGICAL_VOLUME (object->iface_logical_volume),
                                              object->volume_group);
}
