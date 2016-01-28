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

#ifndef __UDISKS_LVM2_TYPES_H__
#define __UDISKS_LVM2_TYPES_H__


#define LVM2_MODULE_NAME "lvm2"

struct _UDisksLVM2State;
typedef struct _UDisksLVM2State UDisksLVM2State;

struct _UDisksLinuxVolumeGroupObject;
typedef struct _UDisksLinuxVolumeGroupObject UDisksLinuxVolumeGroupObject;

struct _UDisksLinuxVolumeGroup;
typedef struct _UDisksLinuxVolumeGroup UDisksLinuxVolumeGroup;

struct _UDisksLinuxLogicalVolume;
typedef struct _UDisksLinuxLogicalVolume UDisksLinuxLogicalVolume;

struct _UDisksLinuxLogicalVolumeObject;
typedef struct _UDisksLinuxLogicalVolumeObject UDisksLinuxLogicalVolumeObject;

struct _UDisksLinuxPhysicalVolume;
typedef struct _UDisksLinuxPhysicalVolume UDisksLinuxPhysicalVolume;


struct _UDisksLinuxManagerLVM2;
typedef struct _UDisksLinuxManagerLVM2 UDisksLinuxManagerLVM2;

struct _UDisksLinuxBlockLVM2;
typedef struct _UDisksLinuxBlockLVM2 UDisksLinuxBlockLVM2;

#endif /* __UDISKS_LVM2_TYPES_H__ */
