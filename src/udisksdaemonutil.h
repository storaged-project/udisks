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

#ifndef __UDISKS_DAEMON_UTIL_H__
#define __UDISKS_DAEMON_UTIL_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

gchar *udisks_decode_udev_string (const gchar *str);

void udisks_safe_append_to_object_path (GString      *str,
                                        const gchar  *s);

guint64 udisks_daemon_util_block_get_size (GUdevDevice *device,
                                           gboolean    *out_media_available,
                                           gboolean    *out_media_change_detected);

gchar *udisks_daemon_util_resolve_link (const gchar *path,
                                        const gchar *name);

gchar **udisks_daemon_util_resolve_links (const gchar *path,
                                          const gchar *dir_name);

gboolean udisks_daemon_util_setup_by_user (UDisksDaemon *daemon,
                                           UDisksObject *object,
                                           uid_t         user);

gboolean udisks_daemon_util_on_same_seat (UDisksDaemon          *daemon,
                                          UDisksObject          *object,
                                          pid_t                  process);

gboolean udisks_daemon_util_check_authorization_sync (UDisksDaemon          *daemon,
                                                      UDisksObject          *object,
                                                      const gchar           *action_id,
                                                      GVariant              *options,
                                                      const gchar           *message,
                                                      GDBusMethodInvocation *invocation);

gboolean udisks_daemon_util_get_caller_uid_sync (UDisksDaemon            *daemon,
                                                 GDBusMethodInvocation   *invocation,
                                                 GCancellable            *cancellable,
                                                 uid_t                   *out_uid,
                                                 gid_t                   *out_gid,
                                                 gchar                  **out_user_name,
                                                 GError                 **error);

gboolean udisks_daemon_util_get_caller_pid_sync (UDisksDaemon            *daemon,
                                                 GDBusMethodInvocation   *invocation,
                                                 GCancellable            *cancellable,
                                                 pid_t                   *out_pid,
                                                 GError                 **error);

gpointer  udisks_daemon_util_dup_object (gpointer   interface_,
                                         GError   **error);

gchar *udisks_daemon_util_escape (const gchar *str);
gchar *udisks_daemon_util_escape_and_quote (const gchar *str);

gchar *udisks_daemon_util_hexdump (gconstpointer data, gsize len);
void udisks_daemon_util_hexdump_debug (gconstpointer data, gsize len);

gboolean udisks_daemon_util_file_set_contents (const gchar  *filename,
                                               const gchar  *contents,
                                               gssize        contents_len,
                                               gint          mode_for_new_file,
                                               GError      **error);

UDisksInhibitCookie *udisks_daemon_util_inhibit_system_sync   (const gchar          *reason);
void                 udisks_daemon_util_uninhibit_system_sync (UDisksInhibitCookie  *cookie);

gboolean udisks_daemon_util_on_same_seat (UDisksDaemon          *daemon,
                                          UDisksObject          *object,
                                          pid_t                  process);

gchar *udisks_daemon_util_get_free_mdraid_device (void);

guint16 udisks_ata_identify_get_word (const guchar *identify_data, guint word_number);

G_END_DECLS

#endif /* __UDISKS_DAEMON_UTIL_H__ */
