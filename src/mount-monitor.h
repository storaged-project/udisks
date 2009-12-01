/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
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

#ifndef __MOUNT_MONITOR_H__
#define __MOUNT_MONITOR_H__

#include "types.h"

G_BEGIN_DECLS

#define TYPE_MOUNT_MONITOR         (mount_monitor_get_type ())
#define MOUNT_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_MOUNT_MONITOR, MountMonitor))
#define MOUNT_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_MOUNT_MONITOR, MountMonitorClass))
#define IS_MOUNT_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_MOUNT_MONITOR))
#define IS_MOUNT_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_MOUNT_MONITOR))
#define MOUNT_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_MOUNT_MONITOR, MountMonitorClass))

typedef struct MountMonitorClass MountMonitorClass;
typedef struct MountMonitorPrivate MountMonitorPrivate;

struct MountMonitor
{
  GObject parent;

  /*< private >*/
  MountMonitorPrivate *priv;
};

struct MountMonitorClass
{
  GObjectClass parent_class;

  /*< public >*/
  /* signals */
  void (*mount_added) (MountMonitor *monitor,
                       Mount *mount);
  void (*mount_removed) (MountMonitor *monitor,
                         Mount *mount);
};

GType mount_monitor_get_type (void) G_GNUC_CONST;
MountMonitor *mount_monitor_new (void);
GList *mount_monitor_get_mounts_for_dev (MountMonitor *monitor,
                                         dev_t dev);
void mount_monitor_invalidate (MountMonitor *monitor);

G_END_DECLS

#endif /* __MOUNT_MONITOR_H__ */
