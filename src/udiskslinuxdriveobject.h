/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#ifndef __UDISKS_LINUX_DRIVE_OBJECT_H__
#define __UDISKS_LINUX_DRIVE_OBJECT_H__

#include "udisksdaemontypes.h"
#include <gudev/gudev.h>

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_DRIVE_OBJECT  (udisks_linux_drive_object_get_type ())
#define UDISKS_LINUX_DRIVE_OBJECT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_DRIVE_OBJECT, UDisksLinuxDriveObject))
#define UDISKS_IS_LINUX_DRIVE_OBJECT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_DRIVE_OBJECT))

GType                   udisks_linux_drive_object_get_type      (void) G_GNUC_CONST;
UDisksLinuxDriveObject *udisks_linux_drive_object_new           (UDisksDaemon             *daemon,
                                                                 UDisksLinuxDevice        *device);
void                    udisks_linux_drive_object_uevent        (UDisksLinuxDriveObject   *object,
                                                                 const gchar              *action,
                                                                 UDisksLinuxDevice        *device);
UDisksDaemon           *udisks_linux_drive_object_get_daemon    (UDisksLinuxDriveObject   *object);
GList                  *udisks_linux_drive_object_get_devices   (UDisksLinuxDriveObject   *object);
UDisksLinuxDevice      *udisks_linux_drive_object_get_device    (UDisksLinuxDriveObject   *object,
                                                                 gboolean                  get_hw);
UDisksLinuxBlockObject *udisks_linux_drive_object_get_block     (UDisksLinuxDriveObject   *object,
                                                                 gboolean                  get_hw);

GList                  *udisks_linux_drive_object_get_siblings  (UDisksLinuxDriveObject   *object);

gboolean                udisks_linux_drive_object_housekeeping  (UDisksLinuxDriveObject   *object,
                                                                 guint                     secs_since_last,
                                                                 GCancellable             *cancellable,
                                                                 GError                  **error);

gboolean                udisks_linux_drive_object_is_not_in_use (UDisksLinuxDriveObject   *object,
                                                                 GCancellable             *cancellable,
                                                                 GError                  **error);

gboolean                udisks_linux_drive_object_should_include_device (GUdevClient        *client,
                                                                         UDisksLinuxDevice  *device,
                                                                         gchar             **out_vpd);


G_END_DECLS

#endif /* __UDISKS_LINUX_DRIVE_OBJECT_H__ */
