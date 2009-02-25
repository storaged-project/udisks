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

#ifndef __DEVKIT_DISKS_MOUNT_MONITOR_H__
#define __DEVKIT_DISKS_MOUNT_MONITOR_H__

#include <glib-object.h>
#include <polkit-dbus/polkit-dbus.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#define DEVKIT_TYPE_DISKS_MOUNT_MONITOR         (devkit_disks_mount_monitor_get_type ())
#define DEVKIT_DISKS_MOUNT_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), DEVKIT_TYPE_DISKS_MOUNT_MONITOR, DevkitDisksMountMonitor))
#define DEVKIT_DISKS_MOUNT_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), DEVKIT_TYPE_DISKS_MOUNT_MONITOR, DevkitDisksMountMonitorClass))
#define DEVKIT_IS_DISKS_MOUNT_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), DEVKIT_TYPE_DISKS_MOUNT_MONITOR))
#define DEVKIT_IS_DISKS_MOUNT_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), DEVKIT_TYPE_DISKS_MOUNT_MONITOR))
#define DEVKIT_DISKS_MOUNT_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DEVKIT_TYPE_DISKS_MOUNT_MONITOR, DevkitDisksMountMonitorClass))

struct DevkitDisksMountMonitorPrivate;
typedef struct DevkitDisksMountMonitorPrivate DevkitDisksMountMonitorPrivate;

typedef struct
{
        GObject                         parent;
        DevkitDisksMountMonitorPrivate *priv;
} DevkitDisksMountMonitor;

typedef struct
{
        GObjectClass   parent_class;
} DevkitDisksMountMonitorClass;

GType                    devkit_disks_mount_monitor_get_type   (void);
DevkitDisksMountMonitor *devkit_disks_mount_monitor_new        (void);
GList                   *devkit_disks_mount_monitor_get_mounts (DevkitDisksMountMonitor *mount_monitor);

struct DevkitDisksMount;
typedef struct DevkitDisksMount DevkitDisksMount;

void        devkit_disks_mount_unref (DevkitDisksMount *mount);
const char *devkit_disks_mount_get_mount_path  (DevkitDisksMount *mount);
const char *devkit_disks_mount_get_device_path (DevkitDisksMount *mount);

G_END_DECLS

#endif /* __DEVKIT_DISKS_MOUNT_MONITOR_H__ */
