/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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

#ifndef __UDISKS_LINUX_VOLUME_GROUP_OBJECT_H__
#define __UDISKS_LINUX_VOLUME_GROUP_OBJECT_H__

#include <src/udisksdaemontypes.h>
#include "udiskslvm2types.h"
#include "udisks-lvm2-generated.h"

#include <gudev/gudev.h>
#include <blockdev/lvm.h>

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_VOLUME_GROUP_OBJECT  (udisks_linux_volume_group_object_get_type ())
#define UDISKS_LINUX_VOLUME_GROUP_OBJECT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_VOLUME_GROUP_OBJECT, UDisksLinuxVolumeGroupObject))
#define UDISKS_IS_LINUX_VOLUME_GROUP_OBJECT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_VOLUME_GROUP_OBJECT))

GType                           udisks_linux_volume_group_object_get_type      (void) G_GNUC_CONST;
UDisksLinuxVolumeGroupObject   *udisks_linux_volume_group_object_new           (UDisksDaemon                 *daemon,
                                                                                const gchar                  *name);
const gchar                    *udisks_linux_volume_group_object_get_name      (UDisksLinuxVolumeGroupObject *object);
UDisksDaemon                   *udisks_linux_volume_group_object_get_daemon    (UDisksLinuxVolumeGroupObject *object);
void                            udisks_linux_volume_group_object_update        (UDisksLinuxVolumeGroupObject *object,
                                                                                BDLVMVGdata *vginfo,
                                                                                GSList *pvs);

void                            udisks_linux_volume_group_object_poll          (UDisksLinuxVolumeGroupObject *object);

void                            udisks_linux_volume_group_object_destroy       (UDisksLinuxVolumeGroupObject *object);

UDisksLinuxLogicalVolumeObject *udisks_linux_volume_group_object_find_logical_volume_object (UDisksLinuxVolumeGroupObject *object,
                                                                                             const gchar                  *name);

G_END_DECLS

#endif /* __UDISKS_LINUX_VOLUME_GROUP_OBJECT_H__ */
