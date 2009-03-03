/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
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

#ifndef __DEVKIT_DISKS_MOUNT_H__
#define __DEVKIT_DISKS_MOUNT_H__

#include <sys/types.h>

#include "devkit-disks-types.h"

G_BEGIN_DECLS

#define DEVKIT_DISKS_TYPE_MOUNT         (devkit_disks_mount_get_type ())
#define DEVKIT_DISKS_MOUNT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), DEVKIT_DISKS_TYPE_MOUNT, DevkitDisksMount))
#define DEVKIT_DISKS_MOUNT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), DEVKIT_DISKS_TYPE_MOUNT, DevkitDisksMountClass))
#define DEVKIT_DISKS_IS_MOUNT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), DEVKIT_DISKS_TYPE_MOUNT))
#define DEVKIT_DISKS_IS_MOUNT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), DEVKIT_DISKS_TYPE_MOUNT))
#define DEVKIT_DISKS_MOUNT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DEVKIT_DISKS_TYPE_MOUNT, DevkitDisksMountClass))

typedef struct DevkitDisksMountClass   DevkitDisksMountClass;
typedef struct DevkitDisksMountPrivate DevkitDisksMountPrivate;

struct DevkitDisksMount
{
        GObject                  parent;
        DevkitDisksMountPrivate *priv;
};

struct DevkitDisksMountClass
{
        GObjectClass   parent_class;
};

GType         devkit_disks_mount_get_type        (void) G_GNUC_CONST;
const gchar  *devkit_disks_mount_get_mount_path  (DevkitDisksMount *mount);
dev_t         devkit_disks_mount_get_dev         (DevkitDisksMount *mount);

gint          devkit_disks_mount_compare         (DevkitDisksMount *a,
                                                  DevkitDisksMount *b);

G_END_DECLS

#endif /* __DEVKIT_DISKS_MOUNT_H__ */
