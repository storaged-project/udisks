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

#include "udiskslvm2dbusutil.h"

/**
 * udisks_object_get_block_lvm2:
 * @object: A #UDisksObject.
 *
 * Gets the #UDisksBlockLVM2 instance for the D-Bus interface <link linkend="gdbus-interface-org-freedesktop-UDisks2-Block-LVM2.top_of_page">org.freedesktop.UDisks2.Block.LVM2</link> on @object, if any.
 *
 * Returns: (transfer full): A #UDisksBlockLVM2 that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
UDisksBlockLVM2 *
udisks_object_get_block_lvm2 (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.Block.LVM2");
  if (ret == NULL)
    return NULL;
  return UDISKS_BLOCK_LVM2 (ret);
}

/**
 * udisks_object_peek_block_lvm2: (skip)
 * @object: A #UDisksObject.
 *
 * Like udisks_object_get_block_lvm2() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #GDBusObjectManagerClient or #GDBusObjectManagerServer for @object is running.</warning>
 *
 * Returns: (transfer none): A #UDisksBlockLVM2 or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
UDisksBlockLVM2 *
udisks_object_peek_block_lvm2 (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.Block.LVM2");
  if (ret == NULL)
    return NULL;
  g_object_unref (ret);
  return UDISKS_BLOCK_LVM2 (ret);
}

/**
 * udisks_object_get_physical_volume:
 * @object: A #UDisksObject.
 *
 * Gets the #UDisksPhysicalVolume instance for the D-Bus interface <link linkend="gdbus-interface-org-freedesktop-UDisks2-PhysicalVolume.top_of_page">org.freedesktop.UDisks2.PhysicalVolume</link> on @object, if any.
 *
 * Returns: (transfer full): A #UDisksPhysicalVolume that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
UDisksPhysicalVolume *
udisks_object_get_physical_volume (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.PhysicalVolume");
  if (ret == NULL)
    return NULL;
  return UDISKS_PHYSICAL_VOLUME (ret);
}

/**
 * udisks_object_peek_physical_volume: (skip)
 * @object: A #UDisksObject.
 *
 * Like udisks_object_get_physical_volume() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #GDBusObjectManagerClient or #GDBusObjectManagerServer for @object is running.</warning>
 *
 * Returns: (transfer none): A #UDisksPhysicalVolume or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
UDisksPhysicalVolume *
udisks_object_peek_physical_volume (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.PhysicalVolume");
  if (ret == NULL)
    return NULL;
  g_object_unref (ret);
  return UDISKS_PHYSICAL_VOLUME (ret);
}

/**
 * udisks_object_get_volume_group:
 * @object: A #UDisksObject.
 *
 * Gets the #UDisksVolumeGroup instance for the D-Bus interface <link linkend="gdbus-interface-org-freedesktop-UDisks2-VolumeGroup.top_of_page">org.freedesktop.UDisks2.VolumeGroup</link> on @object, if any.
 *
 * Returns: (transfer full): A #UDisksVolumeGroup that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
UDisksVolumeGroup *
udisks_object_get_volume_group (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.VolumeGroup");
  if (ret == NULL)
    return NULL;
  return UDISKS_VOLUME_GROUP (ret);
}

/**
 * udisks_object_peek_volume_group: (skip)
 * @object: A #UDisksObject.
 *
 * Like udisks_object_get_volume_group() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #GDBusObjectManagerClient or #GDBusObjectManagerServer for @object is running.</warning>
 *
 * Returns: (transfer none): A #UDisksVolumeGroup or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
UDisksVolumeGroup *
udisks_object_peek_volume_group (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.VolumeGroup");
  if (ret == NULL)
    return NULL;
  g_object_unref (ret);
  return UDISKS_VOLUME_GROUP (ret);
}

/**
 * udisks_object_get_logical_volume:
 * @object: A #UDisksObject.
 *
 * Gets the #UDisksLogicalVolume instance for the D-Bus interface <link linkend="gdbus-interface-org-freedesktop-UDisks2-LogicalVolume.top_of_page">org.freedesktop.UDisks2.LogicalVolume</link> on @object, if any.
 *
 * Returns: (transfer full): A #UDisksLogicalVolume that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
UDisksLogicalVolume *
udisks_object_get_logical_volume (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.LogicalVolume");
  if (ret == NULL)
    return NULL;
  return UDISKS_LOGICAL_VOLUME (ret);
}

/**
 * udisks_object_peek_logical_volume: (skip)
 * @object: A #UDisksObject.
 *
 * Like udisks_object_get_logical_volume() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #GDBusObjectManagerClient or #GDBusObjectManagerServer for @object is running.</warning>
 *
 * Returns: (transfer none): A #UDisksLogicalVolume or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
UDisksLogicalVolume *
udisks_object_peek_logical_volume (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.LogicalVolume");
  if (ret == NULL)
    return NULL;
  g_object_unref (ret);
  return UDISKS_LOGICAL_VOLUME (ret);
}
