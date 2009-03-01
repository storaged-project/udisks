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

#ifndef __DEVKIT_DISKS_DEVICE_H__
#define __DEVKIT_DISKS_DEVICE_H__

#include <dbus/dbus-glib.h>
#include <polkit-dbus/polkit-dbus.h>
#include <devkit-gobject/devkit-gobject.h>

#include "devkit-disks-types.h"

G_BEGIN_DECLS

#define DEVKIT_DISKS_TYPE_DEVICE         (devkit_disks_device_get_type ())
#define DEVKIT_DISKS_DEVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), DEVKIT_DISKS_TYPE_DEVICE, DevkitDisksDevice))
#define DEVKIT_DISKS_DEVICE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), DEVKIT_DISKS_TYPE_DEVICE, DevkitDisksDeviceClass))
#define DEVKIT_DISKS_IS_DEVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), DEVKIT_DISKS_TYPE_DEVICE))
#define DEVKIT_DISKS_IS_DEVICE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), DEVKIT_DISKS_TYPE_DEVICE))
#define DEVKIT_DISKS_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DEVKIT_DISKS_TYPE_DEVICE, DevkitDisksDeviceClass))

typedef struct DevkitDisksDeviceClass   DevkitDisksDeviceClass;
typedef struct DevkitDisksDevicePrivate DevkitDisksDevicePrivate;

struct DevkitDisksDevice
{
        GObject                   parent;
        DevkitDisksDevicePrivate *priv;
};

struct DevkitDisksDeviceClass
{
        GObjectClass parent_class;
};

GType              devkit_disks_device_get_type              (void) G_GNUC_CONST;

DevkitDisksDevice *devkit_disks_device_new                   (DevkitDisksDaemon *daemon,
                                                              DevkitDevice      *d);

gboolean           devkit_disks_device_changed               (DevkitDisksDevice *device,
                                                              DevkitDevice      *d,
                                                              gboolean           synthesized);

void               devkit_disks_device_removed               (DevkitDisksDevice *device);

/* local methods */

const char        *devkit_disks_device_local_get_object_path     (DevkitDisksDevice *device);
const char        *devkit_disks_device_local_get_native_path     (DevkitDisksDevice *device);

const char        *devkit_disks_device_local_get_device_file     (DevkitDisksDevice *device);
const char        *devkit_disks_device_local_get_mount_path      (DevkitDisksDevice *device);

gboolean           devkit_disks_device_local_is_busy             (DevkitDisksDevice *device);
gboolean           devkit_disks_device_local_partitions_are_busy (DevkitDisksDevice *device);

/* exported methods */

gboolean devkit_disks_device_job_cancel (DevkitDisksDevice     *device,
                                         DBusGMethodInvocation *context);

gboolean devkit_disks_device_filesystem_mount (DevkitDisksDevice     *device,
                                               const char            *filesystem_type,
                                               char                 **options,
                                               DBusGMethodInvocation *context);

gboolean devkit_disks_device_filesystem_unmount (DevkitDisksDevice     *device,
                                                 char                 **options,
                                                 DBusGMethodInvocation *context);

gboolean devkit_disks_device_filesystem_list_open_files (DevkitDisksDevice     *device,
                                                         DBusGMethodInvocation *context);

gboolean devkit_disks_device_drive_eject (DevkitDisksDevice     *device,
                                          char                 **options,
                                          DBusGMethodInvocation *context);

gboolean devkit_disks_device_filesystem_check (DevkitDisksDevice     *device,
                                               char                 **options,
                                               DBusGMethodInvocation *context);

gboolean devkit_disks_device_erase (DevkitDisksDevice     *device,
                                    char                 **options,
                                    DBusGMethodInvocation *context);

gboolean devkit_disks_device_filesystem_create (DevkitDisksDevice     *device,
                                                const char            *fstype,
                                                char                 **options,
                                                DBusGMethodInvocation *context);

gboolean devkit_disks_device_partition_delete (DevkitDisksDevice     *device,
                                               char                 **options,
                                               DBusGMethodInvocation *context);

gboolean devkit_disks_device_partition_create (DevkitDisksDevice     *device,
                                               guint64                offset,
                                               guint64                size,
                                               const char            *type,
                                               const char            *label,
                                               char                 **flags,
                                               char                 **options,
                                               const char            *fstype,
                                               char                 **fsoptions,
                                               DBusGMethodInvocation *context);

gboolean devkit_disks_device_partition_modify (DevkitDisksDevice     *device,
                                               const char            *type,
                                               const char            *label,
                                               char                 **flags,
                                               DBusGMethodInvocation *context);

gboolean devkit_disks_device_partition_table_create (DevkitDisksDevice     *device,
                                                     const char            *scheme,
                                                     char                 **options,
                                                     DBusGMethodInvocation *context);

gboolean devkit_disks_device_luks_unlock (DevkitDisksDevice     *device,
                                          const char            *secret,
                                          char                 **options,
                                          DBusGMethodInvocation *context);

gboolean devkit_disks_device_luks_lock (DevkitDisksDevice     *device,
                                        char                 **options,
                                        DBusGMethodInvocation *context);

gboolean devkit_disks_device_luks_change_passphrase (DevkitDisksDevice     *device,
                                                     const char            *old_secret,
                                                     const char            *new_secret,
                                                     DBusGMethodInvocation *context);

gboolean devkit_disks_device_filesystem_set_label (DevkitDisksDevice     *device,
                                                   const char            *new_label,
                                                   DBusGMethodInvocation *context);

gboolean devkit_disks_device_drive_smart_refresh_data (DevkitDisksDevice     *device,
                                                       char                 **options,
                                                       DBusGMethodInvocation *context);

gboolean devkit_disks_device_drive_smart_get_historical_data (DevkitDisksDevice     *device,
                                                              guint64                from,
                                                              guint64                to,
                                                              DBusGMethodInvocation *context);

gboolean devkit_disks_device_drive_smart_initiate_selftest (DevkitDisksDevice     *device,
                                                            const char            *test,
                                                            gboolean               captive,
                                                            DBusGMethodInvocation *context);

gboolean devkit_disks_device_linux_md_stop (DevkitDisksDevice     *device,
                                            char                 **options,
                                            DBusGMethodInvocation *context);

gboolean devkit_disks_device_linux_md_add_component (DevkitDisksDevice     *device,
                                                     char                  *component,
                                                     char                 **options,
                                                     DBusGMethodInvocation *context);

gboolean devkit_disks_device_linux_md_remove_component (DevkitDisksDevice     *device,
                                                        char                  *component,
                                                        char                 **options,
                                                        DBusGMethodInvocation *context);

gboolean devkit_disks_device_drive_inhibit_polling (DevkitDisksDevice     *device,
                                                    char                 **options,
                                                    DBusGMethodInvocation *context);

gboolean devkit_disks_device_drive_uninhibit_polling (DevkitDisksDevice     *device,
                                                      char                  *cookie,
                                                      DBusGMethodInvocation *context);

gboolean devkit_disks_device_drive_poll_media (DevkitDisksDevice     *device,
                                               DBusGMethodInvocation *context);

G_END_DECLS

#endif /* __DEVKIT_DISKS_DEVICE_H__ */
