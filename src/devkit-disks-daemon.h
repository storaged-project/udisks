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

#ifndef __DEVKIT_DISKS_DAEMON_H__
#define __DEVKIT_DISKS_DAEMON_H__

#include <gudev/gudev.h>
#include <polkit/polkit.h>
#include <dbus/dbus-glib.h>

#include "devkit-disks-types.h"

G_BEGIN_DECLS

#define DEVKIT_DISKS_TYPE_DAEMON         (devkit_disks_daemon_get_type ())
#define DEVKIT_DISKS_DAEMON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), DEVKIT_DISKS_TYPE_DAEMON, DevkitDisksDaemon))
#define DEVKIT_DISKS_DAEMON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), DEVKIT_DISKS_TYPE_DAEMON, DevkitDisksDaemonClass))
#define DEVKIT_DISKS_IS_DAEMON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), DEVKIT_DISKS_TYPE_DAEMON))
#define DEVKIT_DISKS_IS_DAEMON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), DEVKIT_DISKS_TYPE_DAEMON))
#define DEVKIT_DISKS_DAEMON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DEVKIT_DISKS_TYPE_DAEMON, DevkitDisksDaemonClass))

typedef struct DevkitDisksDaemonClass   DevkitDisksDaemonClass;
typedef struct DevkitDisksDaemonPrivate DevkitDisksDaemonPrivate;

struct DevkitDisksDaemon
{
        GObject        parent;
        DevkitDisksDaemonPrivate *priv;
};

struct DevkitDisksDaemonClass
{
        GObjectClass   parent_class;
};

typedef enum
{
        DEVKIT_DISKS_ERROR_FAILED,
        DEVKIT_DISKS_ERROR_PERMISSION_DENIED,
        DEVKIT_DISKS_ERROR_BUSY,
        DEVKIT_DISKS_ERROR_CANCELLED,
        DEVKIT_DISKS_ERROR_INHIBITED,
        DEVKIT_DISKS_ERROR_INVALID_OPTION,
        DEVKIT_DISKS_ERROR_NOT_SUPPORTED,
        DEVKIT_DISKS_ERROR_ATA_SMART_WOULD_WAKEUP,
        DEVKIT_DISKS_NUM_ERRORS
} DevkitDisksError;

#define DEVKIT_DISKS_ERROR devkit_disks_error_quark ()

GType devkit_disks_error_get_type (void);
#define DEVKIT_DISKS_TYPE_ERROR (devkit_disks_error_get_type ())
GQuark             devkit_disks_error_quark         (void);

GType              devkit_disks_daemon_get_type            (void) G_GNUC_CONST;
DevkitDisksDaemon *devkit_disks_daemon_new                 (void);

/* local methods */

GList             *devkit_disks_daemon_local_get_all_devices     (DevkitDisksDaemon       *daemon);

DevkitDisksDevice *devkit_disks_daemon_local_find_by_native_path (DevkitDisksDaemon       *daemon,
                                                                  const char              *native_path);

DevkitDisksDevice *devkit_disks_daemon_local_find_by_object_path (DevkitDisksDaemon       *daemon,
                                                                  const char              *object_path);

DevkitDisksDevice *devkit_disks_daemon_local_find_by_device_file (DevkitDisksDaemon       *daemon,
                                                                  const char              *device_file);

DevkitDisksDevice *devkit_disks_daemon_local_find_by_dev         (DevkitDisksDaemon       *daemon,
                                                                  dev_t                    dev);

DevkitDisksAtaSmartDb *devkit_disks_daemon_local_get_ata_smart_db (DevkitDisksDaemon       *daemon);


typedef void (*DevkitDisksCheckAuthCallback) (DevkitDisksDaemon     *daemon,
                                              DevkitDisksDevice     *device,
                                              DBusGMethodInvocation *context,
                                              const gchar           *action_id,
                                              guint                  num_user_data,
                                              gpointer              *user_data_elements);

/* num_user_data param is followed by @num_user_data (gpointer, GDestroyNotify) pairs.. */
void               devkit_disks_daemon_local_check_auth          (DevkitDisksDaemon            *daemon,
                                                                  DevkitDisksDevice            *device,
                                                                  const gchar                  *action_id,
                                                                  const gchar                  *operation,
                                                                  DevkitDisksCheckAuthCallback  check_auth_callback,
                                                                  DBusGMethodInvocation        *context,
                                                                  guint                         num_user_data,
                                                                  ...);


/* TODO: probably use G_GNUC_WARN_UNUSED_RESULT here and fix up callers */
gboolean           devkit_disks_daemon_local_get_uid               (DevkitDisksDaemon     *daemon,
                                                                    uid_t                 *out_uid,
                                                                    DBusGMethodInvocation *context);

void               devkit_disks_daemon_local_synthesize_changed_on_all_devices (DevkitDisksDaemon *daemon);

void               devkit_disks_daemon_local_synthesize_changed  (DevkitDisksDaemon       *daemon,
                                                                  DevkitDisksDevice       *device);

void               devkit_disks_daemon_local_update_poller       (DevkitDisksDaemon       *daemon);

void               devkit_disks_daemon_local_update_spindown     (DevkitDisksDaemon       *daemon);

gboolean           devkit_disks_daemon_local_has_polling_inhibitors (DevkitDisksDaemon       *daemon);

gboolean           devkit_disks_daemon_local_has_inhibitors (DevkitDisksDaemon       *daemon);

DevkitDisksMountMonitor *devkit_disks_daemon_local_get_mount_monitor (DevkitDisksDaemon *daemon);

typedef struct {
        const char *id;
        const char *name;
        gboolean supports_unix_owners;
        gboolean can_mount;
        gboolean can_create;
        guint max_label_len;
        gboolean supports_label_rename;
        gboolean supports_online_label_rename;
        gboolean supports_fsck;
        gboolean supports_online_fsck;
        gboolean supports_resize_enlarge;
        gboolean supports_online_resize_enlarge;
        gboolean supports_resize_shrink;
        gboolean supports_online_resize_shrink;
} DevkitDisksFilesystem;

const DevkitDisksFilesystem *devkit_disks_daemon_local_get_fs_details (DevkitDisksDaemon  *daemon,
                                                                       const gchar        *filesystem_id);

/* exported methods */

gboolean devkit_disks_daemon_enumerate_devices (DevkitDisksDaemon     *daemon,
                                                DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_enumerate_device_files (DevkitDisksDaemon     *daemon,
                                                     DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_find_device_by_device_file (DevkitDisksDaemon     *daemon,
                                                         const char            *device_file,
                                                         DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_find_device_by_major_minor (DevkitDisksDaemon     *daemon,
                                                         gint64                  major,
                                                         gint64                  minor,
                                                         DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_linux_md_start (DevkitDisksDaemon     *daemon,
                                             GPtrArray             *components,
                                             char                 **options,
                                             DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_drive_inhibit_all_polling (DevkitDisksDaemon     *daemon,
                                                        char                 **options,
                                                        DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_drive_uninhibit_all_polling (DevkitDisksDaemon     *daemon,
                                                          char                  *cookie,
                                                          DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_inhibit (DevkitDisksDaemon     *daemon,
                                      DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_uninhibit (DevkitDisksDaemon     *daemon,
                                        char                  *cookie,
                                        DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_drive_set_all_spindown_timeouts (DevkitDisksDaemon     *daemon,
                                                              int                    timeout_seconds,
                                                              char                 **options,
                                                              DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_drive_unset_all_spindown_timeouts (DevkitDisksDaemon     *daemon,
                                                                char                  *cookie,
                                                                DBusGMethodInvocation *context);

G_END_DECLS

#endif /* __DEVKIT_DISKS_DAEMON_H__ */
