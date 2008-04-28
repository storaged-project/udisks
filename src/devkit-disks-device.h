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

#include <glib-object.h>
#include <polkit-dbus/polkit-dbus.h>
#include <devkit-gobject.h>

#include "devkit-disks-daemon.h"

G_BEGIN_DECLS

#define DEVKIT_TYPE_DISKS_DEVICE         (devkit_disks_device_get_type ())
#define DEVKIT_DISKS_DEVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), DEVKIT_TYPE_DISKS_DEVICE, DevkitDisksDevice))
#define DEVKIT_DISKS_DEVICE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), DEVKIT_TYPE_DISKS_DEVICE, DevkitDisksDeviceClass))
#define DEVKIT_IS_DISKS_DEVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), DEVKIT_TYPE_DISKS_DEVICE))
#define DEVKIT_IS_DISKS_DEVICE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), DEVKIT_TYPE_DISKS_DEVICE))
#define DEVKIT_DISKS_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DEVKIT_TYPE_DISKS_DEVICE, DevkitDisksDeviceClass))

typedef struct DevkitDisksDevicePrivate DevkitDisksDevicePrivate;

struct DevkitDisksDevice
{
        GObject        parent;
        DevkitDisksDevicePrivate *priv;
};

typedef struct
{
        GObjectClass   parent_class;
} DevkitDisksDeviceClass;

typedef enum
{
        DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_SUPPORTED,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTABLE,
        DEVKIT_DISKS_DEVICE_ERROR_MOUNTED,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTED,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTED_BY_DK,
        DEVKIT_DISKS_DEVICE_ERROR_FSTAB_ENTRY,
        DEVKIT_DISKS_DEVICE_ERROR_MOUNT_OPTION_NOT_ALLOWED,
        DEVKIT_DISKS_DEVICE_ERROR_FILESYSTEM_BUSY,
        DEVKIT_DISKS_DEVICE_ERROR_CANNOT_REMOUNT,
        DEVKIT_DISKS_DEVICE_ERROR_UNMOUNT_OPTION_NOT_ALLOWED,
        DEVKIT_DISKS_DEVICE_ERROR_NO_JOB_IN_PROGRESS,
        DEVKIT_DISKS_DEVICE_ERROR_JOB_ALREADY_IN_PROGRESS,
        DEVKIT_DISKS_DEVICE_ERROR_JOB_CANNOT_BE_CANCELLED,
        DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_PARTITION,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_PARTITIONED,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_CRYPTO,
        DEVKIT_DISKS_DEVICE_ERROR_CRYPTO_ALREADY_UNLOCKED,
        DEVKIT_DISKS_DEVICE_ERROR_CRYPTO_NOT_UNLOCKED,
        DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_DRIVE,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_SMART_CAPABLE,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD,
        DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD_COMPONENT,
        DEVKIT_DISKS_DEVICE_ERROR_NO_SUCH_DEVICE,
        DEVKIT_DISKS_DEVICE_NUM_ERRORS
} DevkitDisksDeviceError;

#define DEVKIT_DISKS_DEVICE_ERROR devkit_disks_device_error_quark ()

GType devkit_disks_device_error_get_type (void);
#define DEVKIT_DISKS_DEVICE_TYPE_ERROR (devkit_disks_device_error_get_type ())

GQuark             devkit_disks_device_error_quark           (void);
GType              devkit_disks_device_get_type              (void);

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

void               devkit_disks_device_local_set_mounted         (DevkitDisksDevice *device,
                                                                  const char        *mount_path,
                                                                  gboolean           emit_changed_signal);
void               devkit_disks_device_local_set_unmounted       (DevkitDisksDevice *device,
                                                                  gboolean           emit_changed_signal);

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

gboolean devkit_disks_device_encrypted_unlock (DevkitDisksDevice     *device,
                                               const char            *secret,
                                               char                 **options,
                                               DBusGMethodInvocation *context);

gboolean devkit_disks_device_encrypted_lock (DevkitDisksDevice     *device,
                                             char                 **options,
                                             DBusGMethodInvocation *context);

gboolean devkit_disks_device_encrypted_change_passphrase (DevkitDisksDevice     *device,
                                                          const char            *old_secret,
                                                          const char            *new_secret,
                                                          DBusGMethodInvocation *context);

gboolean devkit_disks_device_filesystem_set_label (DevkitDisksDevice     *device,
                                                   const char            *new_label,
                                                   DBusGMethodInvocation *context);

gboolean devkit_disks_device_smart_retrieve_data (DevkitDisksDevice     *device,
                                                  DBusGMethodInvocation *context);

gboolean devkit_disks_device_smart_initiate_selftest (DevkitDisksDevice     *device,
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

G_END_DECLS

#endif /* __DEVKIT_DISKS_DEVICE_H__ */
