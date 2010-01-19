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

#ifndef __DAEMON_H__
#define __DAEMON_H__

#include <gudev/gudev.h>
#include <polkit/polkit.h>
#include <dbus/dbus-glib.h>

#include "types.h"

G_BEGIN_DECLS

#define TYPE_DAEMON         (daemon_get_type ())
#define DAEMON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_DAEMON, Daemon))
#define DAEMON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_DAEMON, DaemonClass))
#define IS_DAEMON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_DAEMON))
#define IS_DAEMON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_DAEMON))
#define DAEMON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_DAEMON, DaemonClass))

typedef struct DaemonClass DaemonClass;
typedef struct DaemonPrivate DaemonPrivate;

struct Daemon
{
  GObject parent;
  DaemonPrivate *priv;
};

struct DaemonClass
{
  GObjectClass parent_class;
};

typedef enum
{
  ERROR_FAILED,
  ERROR_PERMISSION_DENIED,
  ERROR_BUSY,
  ERROR_CANCELLED,
  ERROR_INHIBITED,
  ERROR_INVALID_OPTION,
  ERROR_NOT_SUPPORTED,
  ERROR_ATA_SMART_WOULD_WAKEUP,
  ERROR_FILESYSTEM_DRIVER_MISSING,
  ERROR_FILESYSTEM_TOOLS_MISSING,
  NUM_ERRORS
} Error;

#define ERROR error_quark ()

GType error_get_type (void);
#define TYPE_ERROR (error_get_type ())
GQuark error_quark (void);

GType daemon_get_type (void) G_GNUC_CONST;
Daemon * daemon_new (void);

/* local methods */

GList *daemon_local_get_all_devices (Daemon *daemon);

Device * daemon_local_find_by_native_path (Daemon *daemon,
                                           const char *native_path);

Device * daemon_local_find_by_object_path (Daemon *daemon,
                                           const char *object_path);

Device * daemon_local_find_by_device_file (Daemon *daemon,
                                           const char *device_file);

Device * daemon_local_find_by_dev (Daemon *daemon,
                                   dev_t dev);

Adapter * daemon_local_find_enclosing_adapter (Daemon *daemon,
                                               const gchar *native_path);

Expander * daemon_local_find_enclosing_expander (Daemon *daemon,
                                                 const gchar *native_path);

GList * daemon_local_find_enclosing_ports (Daemon *daemon,
                                           const gchar *native_path);

typedef void (*CheckAuthCallback) (Daemon *daemon,
                                   Device *device,
                                   DBusGMethodInvocation *context,
                                   const gchar *action_id,
                                   guint num_user_data,
                                   gpointer *user_data_elements);

/* num_user_data param is followed by @num_user_data (gpointer, GDestroyNotify) pairs.. */
void daemon_local_check_auth (Daemon *daemon,
                              Device *device,
                              const gchar *action_id,
                              const gchar *operation,
                              gboolean allow_user_interaction,
                              CheckAuthCallback check_auth_callback,
                              DBusGMethodInvocation *context,
                              guint num_user_data,
                              ...);

/* TODO: probably use G_GNUC_WARN_UNUSED_RESULT here and fix up callers */
gboolean daemon_local_get_uid (Daemon *daemon,
                               uid_t *out_uid,
                               DBusGMethodInvocation *context);

void daemon_local_synthesize_changed_on_all_devices (Daemon *daemon);

void daemon_local_synthesize_changed (Daemon *daemon, Device *device);

void daemon_local_update_poller (Daemon *daemon);

void daemon_local_update_spindown (Daemon *daemon);

gboolean daemon_local_has_polling_inhibitors (Daemon *daemon);

gboolean daemon_local_is_inhibited (Daemon *daemon);

MountMonitor * daemon_local_get_mount_monitor (Daemon *daemon);

typedef struct
{
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
} Filesystem;

const Filesystem *daemon_local_get_fs_details (Daemon *daemon,
                                               const gchar *filesystem_id);

/* exported methods */

gboolean daemon_enumerate_adapters (Daemon *daemon,
                                    DBusGMethodInvocation *context);

gboolean daemon_enumerate_expanders (Daemon *daemon,
                                     DBusGMethodInvocation *context);

gboolean daemon_enumerate_ports (Daemon *daemon,
                                 DBusGMethodInvocation *context);

gboolean daemon_enumerate_devices (Daemon *daemon,
                                   DBusGMethodInvocation *context);

gboolean daemon_enumerate_device_files (Daemon *daemon,
                                        DBusGMethodInvocation *context);

gboolean daemon_find_device_by_device_file (Daemon *daemon,
                                            const char *device_file,
                                            DBusGMethodInvocation *context);

gboolean daemon_find_device_by_major_minor (Daemon *daemon,
                                            gint64 major,
                                            gint64 minor,
                                            DBusGMethodInvocation *context);

gboolean daemon_linux_md_start (Daemon *daemon,
                                GPtrArray *components,
                                char **options,
                                DBusGMethodInvocation *context);

gboolean daemon_linux_md_create (Daemon *daemon,
                                 GPtrArray *components,
                                 char *level,
                                 guint64 stripe_size,
                                 char *name,
                                 char **options,
                                 DBusGMethodInvocation *context);

gboolean daemon_drive_inhibit_all_polling (Daemon *daemon,
                                           char **options,
                                           DBusGMethodInvocation *context);

gboolean daemon_drive_uninhibit_all_polling (Daemon *daemon,
                                             char *cookie,
                                             DBusGMethodInvocation *context);

gboolean daemon_inhibit (Daemon *daemon,
                         DBusGMethodInvocation *context);

gboolean daemon_uninhibit (Daemon *daemon,
                           char *cookie,
                           DBusGMethodInvocation *context);

gboolean daemon_drive_set_all_spindown_timeouts (Daemon *daemon,
                                                 int timeout_seconds,
                                                 char **options,
                                                 DBusGMethodInvocation *context);

gboolean daemon_drive_unset_all_spindown_timeouts (Daemon *daemon,
                                                   char *cookie,
                                                   DBusGMethodInvocation *context);

gboolean daemon_linux_lvm2_vg_start (Daemon *daemon,
                                     const gchar *uuid,
                                     char **options,
                                     DBusGMethodInvocation *context);

gboolean daemon_linux_lvm2_vg_stop (Daemon *daemon,
                                    const gchar *uuid,
                                    char **options,
                                    DBusGMethodInvocation *context);

gboolean daemon_linux_lvm2_lv_start (Daemon *daemon,
                                     const gchar *group_uuid,
                                     const gchar *uuid,
                                     char **options,
                                     DBusGMethodInvocation *context);

gboolean daemon_linux_lvm2_vg_set_name (Daemon *daemon,
                                        const gchar *uuid,
                                        const gchar *name,
                                        DBusGMethodInvocation *context);

gboolean daemon_linux_lvm2_lv_set_name (Daemon *daemon,
                                        const gchar *group_uuid,
                                        const gchar *uuid,
                                        const gchar *name,
                                        DBusGMethodInvocation *context);

gboolean daemon_linux_lvm2_lv_remove (Daemon *daemon,
                                      const gchar *group_uuid,
                                      const gchar *uuid,
                                      char **options,
                                      DBusGMethodInvocation *context);

gboolean daemon_linux_lvm2_lv_create (Daemon *daemon,
                                      const gchar *group_uuid,
                                      const gchar *name,
                                      guint64 size,
                                      guint num_stripes,
                                      guint64 stripe_size,
                                      guint num_mirrors,
                                      char **options,
                                      char *fstype,
                                      char **fsoptions,
                                      DBusGMethodInvocation *context);

gboolean daemon_linux_lvm2_vg_add_pv (Daemon *daemon,
                                      const gchar *uuid,
                                      const gchar *physical_volume_object_path,
                                      char **options,
                                      DBusGMethodInvocation *context);

gboolean daemon_linux_lvm2_vg_remove_pv (Daemon *daemon,
                                         const gchar *vg_uuid,
                                         const gchar *pv_uuid,
                                         char **options,
                                         DBusGMethodInvocation *context);

G_END_DECLS

#endif /* __DAEMON_H__ */
