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

#ifndef __UDISKS_LVM2_DBUS_UTIL_H__
#define __UDISKS_LVM2_DBUS_UTIL_H__

#include <udisks/udisks-generated.h>
#include "udisks-lvm2-generated.h"

G_BEGIN_DECLS

UDisksBlockLVM2 *udisks_object_get_block_lvm2  (UDisksObject *object);
UDisksBlockLVM2 *udisks_object_peek_block_lvm2 (UDisksObject *object);

UDisksPhysicalVolume *udisks_object_get_physical_volume  (UDisksObject *object);
UDisksPhysicalVolume *udisks_object_peek_physical_volume (UDisksObject *object);

UDisksVolumeGroup *udisks_object_get_volume_group  (UDisksObject *object);
UDisksVolumeGroup *udisks_object_peek_volume_group (UDisksObject *object);

UDisksLogicalVolume *udisks_object_get_logical_volume  (UDisksObject *object);
UDisksLogicalVolume *udisks_object_peek_logical_volume (UDisksObject *object);

G_END_DECLS

#endif /* __UDISKS_LVM2_DBUS_UTIL_H__ */
