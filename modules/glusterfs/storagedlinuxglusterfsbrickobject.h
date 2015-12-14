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

#ifndef __STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT__
#define __STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT__

#include <src/storageddaemontypes.h>
#include "storagedglusterfstypes.h"
#include "storaged-glusterfs-generated.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_LINUX_GLUSTERFS_BRICK_OBJECT  (storaged_linux_glusterfs_brick_object_get_type ())
#define STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_GLUSTERFS_BRICK_OBJECT, StoragedLinuxGlusterFSBrickObject))
#define STORAGED_IS_LINUX_GLUSTERFS_BRICK_OBJECT(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_GLUSTERFS_BRICK_OBJECT))

GType                                storaged_linux_glusterfs_brick_object_get_type          (void) G_GNUC_CONST;
StoragedLinuxGlusterFSBrickObject   *storaged_linux_glusterfs_brick_object_new               (StoragedDaemon                     *daemon,
                                                                                              StoragedLinuxGlusterFSVolumeObject *volume_object,
                                                                                              const gchar                        *name);

const gchar                         *storaged_linux_glusterfs_brick_object_get_name          (StoragedLinuxGlusterFSBrickObject *object);
StoragedDaemon                      *storaged_linux_glusterfs_brick_object_get_daemon        (StoragedLinuxGlusterFSBrickObject *object);
StoragedLinuxGlusterFSVolumeObject  *storaged_linux_glusterfs_brick_object_get_volume_object (StoragedLinuxGlusterFSBrickObject *object);
void                                 storaged_linux_glusterfs_brick_object_update            (StoragedLinuxGlusterFSBrickObject *object,
                                                                                              GVariant                          *brick_info);

void                                 storaged_linux_glusterfs_brick_object_destroy           (StoragedLinuxGlusterFSBrickObject *object);

G_END_DECLS


#endif /* __STORAGED_LINUX_GLUSTERFS_BRICK_OBJECT__ */
