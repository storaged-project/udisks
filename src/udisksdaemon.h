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

#ifndef __UDISKS_DAEMON_H__
#define __UDISKS_DAEMON_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_DAEMON         (udisks_daemon_get_type ())
#define UDISKS_DAEMON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_DAEMON, UDisksDaemon))
#define UDISKS_IS_DAEMON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_DAEMON))

GType                     udisks_daemon_get_type             (void) G_GNUC_CONST;
UDisksDaemon             *udisks_daemon_new                   (GDBusConnection *connection);
GDBusConnection          *udisks_daemon_get_connection        (UDisksDaemon    *daemon);
GDBusObjectManagerServer *udisks_daemon_get_object_manager    (UDisksDaemon    *daemon);
UDisksMountMonitor       *udisks_daemon_get_mount_monitor     (UDisksDaemon    *daemon);
UDisksLinuxProvider      *udisks_daemon_get_linux_provider    (UDisksDaemon    *daemon);
UDisksPersistentStore    *udisks_daemon_get_persistent_store  (UDisksDaemon    *daemon);
PolkitAuthority          *udisks_daemon_get_authority         (UDisksDaemon    *daemon);
UDisksCleanup            *udisks_daemon_get_cleanup           (UDisksDaemon    *daemon);

/**
 * UDisksDaemonWaitFunc:
 * @daemon: A #UDisksDaemon.
 * @object: A #UDisksObject to check.
 * @user_data: The #gpointer passed to udisks_daemon_wait_for_object_sync().
 *
 * Type for callback function used with udisks_daemon_wait_for_object_sync().
 *
 * Returns: %TRUE if the object is the one to wait for.
 */
typedef gboolean (*UDisksDaemonWaitFunc) (UDisksDaemon *daemon,
                                          UDisksObject *object,
                                          gpointer      user_data);

UDisksObject             *udisks_daemon_wait_for_object_sync  (UDisksDaemon         *daemon,
                                                               UDisksDaemonWaitFunc  wait_func,
                                                               gpointer              user_data,
                                                               GDestroyNotify        user_data_free_func,
                                                               guint                 timeout_seconds,
                                                               GError              **error);

UDisksBaseJob            *udisks_daemon_launch_simple_job     (UDisksDaemon    *daemon,
                                                               GCancellable    *cancellable);
UDisksBaseJob            *udisks_daemon_launch_spawned_job    (UDisksDaemon    *daemon,
                                                               GCancellable    *cancellable,
                                                               const gchar     *input_string,
                                                               const gchar     *command_line_format,
                                                               ...) G_GNUC_PRINTF (4, 5);
gboolean                  udisks_daemon_launch_spawned_job_sync (UDisksDaemon    *daemon,
                                                                 GCancellable    *cancellable,
                                                                 gchar          **out_message,
                                                                 const gchar     *input_string,
                                                                 const gchar     *command_line_format,
                                                                 ...) G_GNUC_PRINTF (5, 6);
UDisksBaseJob            *udisks_daemon_launch_threaded_job   (UDisksDaemon    *daemon,
                                                               UDisksThreadedJobFunc job_func,
                                                               gpointer         user_data,
                                                               GDestroyNotify   user_data_free_func,
                                                               GCancellable    *cancellable);

G_END_DECLS

#endif /* __UDISKS_DAEMON_H__ */
