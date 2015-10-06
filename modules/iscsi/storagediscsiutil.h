/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Peter Hatina <phatina@redhat.com>
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
 */

#ifndef __STORAGED_ISCSI_UTIL_H__
#define __STORAGED_ISCSI_UTIL_H__

#include <glib.h>

#include <src/storageddaemon.h>

typedef enum
{
  ACTION_LOGIN,
  ACTION_LOGOUT
} libiscsi_login_action;

struct libiscsi_context;
struct libiscsi_node;

extern const gchar      *iscsi_policy_action_id;

gint                     iscsi_login (StoragedDaemon   *daemon,
                                      const gchar      *name,
                                      const gint        tpgt,
                                      const gchar      *address,
                                      const gint        port,
                                      const gchar      *iface,
                                      const gchar      *username,
                                      const gchar      *password,
                                      const gchar      *reverse_username,
                                      const gchar      *reverse_password,
                                      gchar           **errorstr);

gint                     iscsi_logout (StoragedDaemon  *daemon,
                                       const gchar     *name,
                                       const gint       tpgt,
                                       const gchar     *address,
                                       const gint       port,
                                       const gchar     *iface,
                                       gchar          **errorstr);

gint      iscsi_discover_send_targets (StoragedDaemon  *daemon,
                                       const gchar     *address,
                                       const guint16    port,
                                       const gchar     *username,
                                       const gchar     *password,
                                       const gchar     *reverse_username,
                                       const gchar     *reverse_password,
                                       GVariant       **nodes,
                                       gint            *nodes_cnt,
                                       gchar          **errorstr);

GVariant *iscsi_libiscsi_nodes_to_gvariant (const struct libiscsi_node  *nodes,
                                            const gint                   nodes_cnt);
void      iscsi_libiscsi_nodes_free        (const struct libiscsi_node  *nodes);

StoragedError iscsi_error_to_storaged_error (const gint err);

#endif /* __STORAGED_ISCSI_UTIL_H__ */
