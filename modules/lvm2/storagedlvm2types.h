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

#ifndef __STORAGED_LVM2_TYPES_H__
#define __STORAGED_LVM2_TYPES_H__


#define LVM2_MODULE_NAME "lvm2"

struct _StoragedLVM2State;
typedef struct _StoragedLVM2State StoragedLVM2State;

struct _StoragedLinuxVolumeGroupObject;
typedef struct _StoragedLinuxVolumeGroupObject StoragedLinuxVolumeGroupObject;

struct _StoragedLinuxVolumeGroup;
typedef struct _StoragedLinuxVolumeGroup StoragedLinuxVolumeGroup;

struct _StoragedLinuxLogicalVolume;
typedef struct _StoragedLinuxLogicalVolume StoragedLinuxLogicalVolume;

struct _StoragedLinuxLogicalVolumeObject;
typedef struct _StoragedLinuxLogicalVolumeObject StoragedLinuxLogicalVolumeObject;

struct _StoragedLinuxPhysicalVolume;
typedef struct _StoragedLinuxPhysicalVolume StoragedLinuxPhysicalVolume;


struct _StoragedLinuxManagerLVM2;
typedef struct _StoragedLinuxManagerLVM2 StoragedLinuxManagerLVM2;

struct _StoragedLinuxBlockLVM2;
typedef struct _StoragedLinuxBlockLVM2 StoragedLinuxBlockLVM2;

#endif /* __STORAGED_LVM2_TYPES_H__ */
