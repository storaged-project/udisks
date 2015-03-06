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

#ifndef __STORAGED_DAEMON_H__
#define __STORAGED_DAEMON_H__

#include "storageddaemontypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_DAEMON         (storaged_daemon_get_type ())
#define STORAGED_DAEMON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_DAEMON, StoragedDaemon))
#define STORAGED_IS_DAEMON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_DAEMON))

GType                     storaged_daemon_get_type             (void) G_GNUC_CONST;
StoragedDaemon           *storaged_daemon_new                  (GDBusConnection *connection,
                                                                gboolean         disable_modules,
                                                                gboolean         force_load_modules);
GDBusConnection          *storaged_daemon_get_connection        (StoragedDaemon    *daemon);
GDBusObjectManagerServer *storaged_daemon_get_object_manager    (StoragedDaemon    *daemon);
StoragedMountMonitor     *storaged_daemon_get_mount_monitor     (StoragedDaemon    *daemon);
StoragedFstabMonitor     *storaged_daemon_get_fstab_monitor     (StoragedDaemon    *daemon);
StoragedCrypttabMonitor  *storaged_daemon_get_crypttab_monitor  (StoragedDaemon    *daemon);
StoragedLinuxProvider    *storaged_daemon_get_linux_provider    (StoragedDaemon    *daemon);
PolkitAuthority          *storaged_daemon_get_authority         (StoragedDaemon    *daemon);
StoragedState            *storaged_daemon_get_state             (StoragedDaemon    *daemon);
StoragedModuleManager    *storaged_daemon_get_module_manager    (StoragedDaemon    *daemon);
gboolean                  storaged_daemon_get_disable_modules   (StoragedDaemon    *daemon);
gboolean                  storaged_daemon_get_force_load_modules(StoragedDaemon    *daemon);

/**
 * StoragedDaemonWaitFunc:
 * @daemon: A #StoragedDaemon.
 * @user_data: The #gpointer passed to storaged_daemon_wait_for_object_sync().
 *
 * Type for callback function used with storaged_daemon_wait_for_object_sync().
 *
 * Returns: (transfer full): %NULL if the object to wait for was not found, otherwise a full reference to a #StoragedObject.
 */
typedef StoragedObject *(*StoragedDaemonWaitFunc) (StoragedDaemon *daemon,
                                                   gpointer      user_data);

StoragedObject           *storaged_daemon_wait_for_object_sync  (StoragedDaemon         *daemon,
                                                                 StoragedDaemonWaitFunc  wait_func,
                                                                 gpointer                user_data,
                                                                 GDestroyNotify          user_data_free_func,
                                                                 guint                   timeout_seconds,
                                                                 GError                **error);

GList                    *storaged_daemon_get_objects           (StoragedDaemon         *daemon);

StoragedObject           *storaged_daemon_find_block            (StoragedDaemon         *daemon,
                                                                 dev_t                   block_device_number);

StoragedObject             *storaged_daemon_find_block_by_device_file (StoragedDaemon *daemon,
                                                                       const gchar    *device_file);

StoragedObject             *storaged_daemon_find_block_by_sysfs_path (StoragedDaemon *daemon,
                                                                      const gchar    *sysfs_path);

StoragedObject             *storaged_daemon_find_object           (StoragedDaemon         *daemon,
                                                                   const gchar            *object_path);

StoragedBaseJob            *storaged_daemon_launch_simple_job     (StoragedDaemon    *daemon,
                                                                   StoragedObject    *object,
                                                                   const gchar       *job_operation,
                                                                   uid_t              job_started_by_uid,
                                                                   GCancellable      *cancellable);
StoragedBaseJob            *storaged_daemon_launch_spawned_job    (StoragedDaemon    *daemon,
                                                                   StoragedObject    *object,
                                                                   const gchar       *job_operation,
                                                                   uid_t              job_started_by_uid,
                                                                   GCancellable      *cancellable,
                                                                   uid_t              run_as_uid,
                                                                   uid_t              run_as_euid,
                                                                   const gchar       *input_string,
                                                                   const gchar       *command_line_format,
                                                                   ...) G_GNUC_PRINTF (9, 10);
gboolean                  storaged_daemon_launch_spawned_job_sync (StoragedDaemon    *daemon,
                                                                   StoragedObject    *object,
                                                                   const gchar       *job_operation,
                                                                   uid_t              job_started_by_uid,
                                                                   GCancellable      *cancellable,
                                                                   uid_t              run_as_uid,
                                                                   uid_t              run_as_euid,
                                                                   gint              *out_status,
                                                                   gchar            **out_message,
                                                                   const gchar       *input_string,
                                                                   const gchar       *command_line_format,
                                                                   ...) G_GNUC_PRINTF (11, 12);
StoragedBaseJob            *storaged_daemon_launch_threaded_job   (StoragedDaemon         *daemon,
                                                                   StoragedObject         *object,
                                                                   const gchar            *job_operation,
                                                                   uid_t                   job_started_by_uid,
                                                                   StoragedThreadedJobFunc job_func,
                                                                   gpointer                user_data,
                                                                   GDestroyNotify          user_data_free_func,
                                                                   GCancellable           *cancellable);

G_END_DECLS

#endif /* __STORAGED_DAEMON_H__ */
