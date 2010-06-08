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

#ifndef __LINUX_DEVICE_H__
#define __LINUX_DEVICE_H__

#include "types.h"
#include <gudev/gudev.h>

G_BEGIN_DECLS

#define TYPE_LINUX_DEVICE         (linux_device_get_type ())
#define LINUX_DEVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_LINUX_DEVICE, LinuxDevice))
#define LINUX_DEVICE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_DEVICE, LinuxDeviceClass))
#define IS_LINUX_DEVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_LINUX_DEVICE))
#define IS_LINUX_DEVICE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_LINUX_DEVICE))
#define LINUX_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_LINUX_DEVICE, LinuxDeviceClass))

typedef struct _LinuxDeviceClass   LinuxDeviceClass;
typedef struct _LinuxDevicePrivate LinuxDevicePrivate;

struct _LinuxDevice
{
  DeviceStub parent;
  LinuxDevicePrivate *priv;
};

struct _LinuxDeviceClass
{
  DeviceStubClass parent_class;
};

GType        linux_device_get_type        (void) G_GNUC_CONST;
LinuxDevice *linux_device_new             (GUdevDevice  *udev_device);
GUdevDevice *linux_device_get_udev_device (LinuxDevice  *device);
void         linux_device_set_udev_device (LinuxDevice  *device,
                                           GUdevDevice  *udev_device);
void         linux_device_update          (LinuxDevice  *device);
gboolean     linux_device_get_visible     (LinuxDevice  *device);
const gchar *linux_device_get_object_path (LinuxDevice  *device);

G_END_DECLS

#endif /* __LINUX_DEVICE_H__ */
