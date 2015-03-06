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

#ifndef __STORAGED_LINUX_DRIVE_OBJECT_H__
#define __STORAGED_LINUX_DRIVE_OBJECT_H__

#include "storageddaemontypes.h"
#include <gudev/gudev.h>

G_BEGIN_DECLS

#define STORAGED_TYPE_LINUX_DRIVE_OBJECT  (storaged_linux_drive_object_get_type ())
#define STORAGED_LINUX_DRIVE_OBJECT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_DRIVE_OBJECT, StoragedLinuxDriveObject))
#define STORAGED_IS_LINUX_DRIVE_OBJECT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_DRIVE_OBJECT))

GType                     storaged_linux_drive_object_get_type      (void) G_GNUC_CONST;
StoragedLinuxDriveObject *storaged_linux_drive_object_new           (StoragedDaemon             *daemon,
                                                                     StoragedLinuxDevice        *device);
void                      storaged_linux_drive_object_uevent        (StoragedLinuxDriveObject   *object,
                                                                     const gchar                *action,
                                                                     StoragedLinuxDevice        *device);
StoragedDaemon           *storaged_linux_drive_object_get_daemon    (StoragedLinuxDriveObject   *object);
GList                    *storaged_linux_drive_object_get_devices   (StoragedLinuxDriveObject   *object);
StoragedLinuxDevice      *storaged_linux_drive_object_get_device    (StoragedLinuxDriveObject   *object,
                                                                     gboolean                    get_hw);
StoragedLinuxBlockObject *storaged_linux_drive_object_get_block     (StoragedLinuxDriveObject   *object,
                                                                     gboolean                    get_hw);

GList                    *storaged_linux_drive_object_get_siblings  (StoragedLinuxDriveObject   *object);

gboolean                  storaged_linux_drive_object_housekeeping  (StoragedLinuxDriveObject   *object,
                                                                     guint                       secs_since_last,
                                                                     GCancellable               *cancellable,
                                                                     GError                    **error);

gboolean                  storaged_linux_drive_object_is_not_in_use (StoragedLinuxDriveObject   *object,
                                                                     GCancellable               *cancellable,
                                                                     GError                    **error);

gboolean                  storaged_linux_drive_object_should_include_device (GUdevClient          *client,
                                                                             StoragedLinuxDevice  *device,
                                                                             gchar               **out_vpd);


G_END_DECLS

#endif /* __STORAGED_LINUX_DRIVE_OBJECT_H__ */
