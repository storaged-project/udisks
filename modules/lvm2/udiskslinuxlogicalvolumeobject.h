/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
 * Copyright (C) 2013 Marius Vollmer <marius.vollmer@redhat.com>
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

#ifndef __UDISKS_LINUX_MDRAID_OBJECT_H__
#define __UDISKS_LINUX_MDRAID_OBJECT_H__

#include <blockdev/lvm.h>

#include <src/udisksdaemontypes.h>
#include "udiskslvm2types.h"

#include <gudev/gudev.h>

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_LOGICAL_VOLUME_OBJECT  (udisks_linux_logical_volume_object_get_type ())
#define UDISKS_LINUX_LOGICAL_VOLUME_OBJECT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_LOGICAL_VOLUME_OBJECT, UDisksLinuxLogicalVolumeObject))
#define UDISKS_IS_LINUX_LOGICAL_VOLUME_OBJECT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_LOGICAL_VOLUME_OBJECT))

GType                           udisks_linux_logical_volume_object_get_type         (void) G_GNUC_CONST;
UDisksLinuxLogicalVolumeObject *udisks_linux_logical_volume_object_new              (UDisksDaemon                   *daemon,
                                                                                     UDisksLinuxVolumeGroupObject   *vg_object,
                                                                                     const gchar                    *name);
UDisksDaemon                   *udisks_linux_logical_volume_object_get_daemon       (UDisksLinuxLogicalVolumeObject *object);
UDisksLinuxVolumeGroupObject   *udisks_linux_logical_volume_object_get_volume_group (UDisksLinuxLogicalVolumeObject *object);
const gchar                    *udisks_linux_logical_volume_object_get_name         (UDisksLinuxLogicalVolumeObject *object);

void                            udisks_linux_logical_volume_object_update           (UDisksLinuxLogicalVolumeObject *object,
                                                                                     BDLVMLVdata                    *lv_info,
                                                                                     BDLVMLVdata                    *meta_lv_info,
                                                                                     gboolean                       *needs_polling_ret);
void                            udisks_linux_logical_volume_object_update_etctabs   (UDisksLinuxLogicalVolumeObject *object);


G_END_DECLS

#endif /* __UDISKS_LINUX_LOGICAL_VOLUME_OBJECT_H__ */
