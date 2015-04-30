/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014 Tomas Bzatek <tbzatek@redhat.com>
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

#include "storagedlvm2dbusutil.h"

/**
 * storaged_object_get_block_lvm2:
 * @object: A #StoragedObject.
 *
 * Gets the #StoragedBlockLVM2 instance for the D-Bus interface <link linkend="gdbus-interface-org-storaged-Storaged-Block-LVM2.top_of_page">org.storaged.Storaged.Block.LVM2</link> on @object, if any.
 *
 * Returns: (transfer full): A #StoragedBlockLVM2 that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
StoragedBlockLVM2 *
storaged_object_get_block_lvm2 (StoragedObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.storaged.Storaged.Block.LVM2");
  if (ret == NULL)
    return NULL;
  return STORAGED_BLOCK_LVM2 (ret);
}

/**
 * storaged_object_peek_block_lvm2: (skip)
 * @object: A #StoragedObject.
 *
 * Like storaged_object_get_block_lvm2() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #GDBusObjectManagerClient or #GDBusObjectManagerServer for @object is running.</warning>
 *
 * Returns: (transfer none): A #StoragedBlockLVM2 or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
StoragedBlockLVM2 *
storaged_object_peek_block_lvm2 (StoragedObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.storaged.Storaged.Block.LVM2");
  if (ret == NULL)
    return NULL;
  g_object_unref (ret);
  return STORAGED_BLOCK_LVM2 (ret);
}

/**
 * storaged_object_get_physical_volume:
 * @object: A #StoragedObject.
 *
 * Gets the #StoragedPhysicalVolume instance for the D-Bus interface <link linkend="gdbus-interface-org-storaged-Storaged-PhysicalVolume.top_of_page">org.storaged.Storaged.PhysicalVolume</link> on @object, if any.
 *
 * Returns: (transfer full): A #StoragedPhysicalVolume that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
StoragedPhysicalVolume *
storaged_object_get_physical_volume (StoragedObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.storaged.Storaged.PhysicalVolume");
  if (ret == NULL)
    return NULL;
  return STORAGED_PHYSICAL_VOLUME (ret);
}

/**
 * storaged_object_peek_physical_volume: (skip)
 * @object: A #StoragedObject.
 *
 * Like storaged_object_get_physical_volume() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #GDBusObjectManagerClient or #GDBusObjectManagerServer for @object is running.</warning>
 *
 * Returns: (transfer none): A #StoragedPhysicalVolume or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
StoragedPhysicalVolume *
storaged_object_peek_physical_volume (StoragedObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.storaged.Storaged.PhysicalVolume");
  if (ret == NULL)
    return NULL;
  g_object_unref (ret);
  return STORAGED_PHYSICAL_VOLUME (ret);
}

/**
 * storaged_object_get_volume_group:
 * @object: A #StoragedObject.
 *
 * Gets the #StoragedVolumeGroup instance for the D-Bus interface <link linkend="gdbus-interface-org-storaged-Storaged-VolumeGroup.top_of_page">org.storaged.Storaged.VolumeGroup</link> on @object, if any.
 *
 * Returns: (transfer full): A #StoragedVolumeGroup that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
StoragedVolumeGroup *
storaged_object_get_volume_group (StoragedObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.storaged.Storaged.VolumeGroup");
  if (ret == NULL)
    return NULL;
  return STORAGED_VOLUME_GROUP (ret);
}

/**
 * storaged_object_peek_volume_group: (skip)
 * @object: A #StoragedObject.
 *
 * Like storaged_object_get_volume_group() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #GDBusObjectManagerClient or #GDBusObjectManagerServer for @object is running.</warning>
 *
 * Returns: (transfer none): A #StoragedVolumeGroup or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
StoragedVolumeGroup *
storaged_object_peek_volume_group (StoragedObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.storaged.Storaged.VolumeGroup");
  if (ret == NULL)
    return NULL;
  g_object_unref (ret);
  return STORAGED_VOLUME_GROUP (ret);
}

/**
 * storaged_object_get_logical_volume:
 * @object: A #StoragedObject.
 *
 * Gets the #StoragedLogicalVolume instance for the D-Bus interface <link linkend="gdbus-interface-org-storaged-Storaged-LogicalVolume.top_of_page">org.storaged.Storaged.LogicalVolume</link> on @object, if any.
 *
 * Returns: (transfer full): A #StoragedLogicalVolume that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
StoragedLogicalVolume *
storaged_object_get_logical_volume (StoragedObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.storaged.Storaged.LogicalVolume");
  if (ret == NULL)
    return NULL;
  return STORAGED_LOGICAL_VOLUME (ret);
}

/**
 * storaged_object_peek_logical_volume: (skip)
 * @object: A #StoragedObject.
 *
 * Like storaged_object_get_logical_volume() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #GDBusObjectManagerClient or #GDBusObjectManagerServer for @object is running.</warning>
 *
 * Returns: (transfer none): A #StoragedLogicalVolume or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
StoragedLogicalVolume *
storaged_object_peek_logical_volume (StoragedObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.storaged.Storaged.LogicalVolume");
  if (ret == NULL)
    return NULL;
  g_object_unref (ret);
  return STORAGED_LOGICAL_VOLUME (ret);
}
