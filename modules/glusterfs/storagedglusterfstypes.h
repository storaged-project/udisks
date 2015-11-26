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

#ifndef __STORAGED_GLUSTERFS_TYPES_H__
#define __STORAGED_GLUSTERFS_TYPES_H__

#define GLUSTERFS_MODULE_NAME "glusterfs"

typedef struct _StoragedGlusterFSState StoragedGlusterFSState;

typedef struct _StoragedLinuxManagerGlusterFS StoragedLinuxManagerGlusterFS;
typedef struct _StoragedLinuxManagerGlusterFSClass StoragedLinuxManagerGlusterFSClass;

typedef struct _StoragedLinuxGlusterFSGlusterd StoragedLinuxGlusterFSGlusterd;
typedef struct _StoragedLinuxGlusterFSGlusterdClass StoragedLinuxGlusterFSGlusterdClass;

typedef struct _StoragedLinuxGlusterFSGlusterdObject StoragedLinuxGlusterFSGlusterdObject;
typedef struct _StoragedLinuxGlusterFSGlusterdObjectClass StoragedLinuxGlusterFSGlusterdObjectClass;

typedef struct _StoragedLinuxGlusterFSVolume StoragedLinuxGlusterFSVolume;
typedef struct _StoragedLinuxGlusterFSVolumeClass StoragedLinuxGlusterFSVolumeClass;

typedef struct _StoragedLinuxGlusterFSVolumeObject StoragedLinuxGlusterFSVolumeObject;
typedef struct _StoragedLinuxGlusterFSVolumeObjectClass StoragedLinuxGlusterFSVolumeObjectClass;

typedef struct _StoragedLinuxGlusterFSBrick StoragedLinuxGlusterFSBrick;
typedef struct _StoragedLinuxGlusterFSBrickClass StoragedLinuxGlusterFSBrickClass;

typedef struct _StoragedLinuxGlusterFSBrickObject StoragedLinuxGlusterFSBrickObject;
typedef struct _StoragedLinuxGlusterFSBrickObjectClass StoragedLinuxGlusterFSBrickObjectClass;


#endif /* __STORAGED_GLUSTERFS_TYPES_H__ */
