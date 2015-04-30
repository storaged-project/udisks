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

#ifndef __STORAGED_LVM2_DBUS_UTIL_H__
#define __STORAGED_LVM2_DBUS_UTIL_H__

#include <storaged/storaged-generated.h>
#include "storaged-lvm2-generated.h"

G_BEGIN_DECLS

StoragedBlockLVM2 *storaged_object_get_block_lvm2  (StoragedObject *object);
StoragedBlockLVM2 *storaged_object_peek_block_lvm2 (StoragedObject *object);

StoragedPhysicalVolume *storaged_object_get_physical_volume  (StoragedObject *object);
StoragedPhysicalVolume *storaged_object_peek_physical_volume (StoragedObject *object);

StoragedVolumeGroup *storaged_object_get_volume_group  (StoragedObject *object);
StoragedVolumeGroup *storaged_object_peek_volume_group (StoragedObject *object);

StoragedLogicalVolume *storaged_object_get_logical_volume  (StoragedObject *object);
StoragedLogicalVolume *storaged_object_peek_logical_volume (StoragedObject *object);

G_END_DECLS

#endif /* __STORAGED_LVM2_DBUS_UTIL_H__ */
