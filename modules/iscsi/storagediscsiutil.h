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

extern const gchar      *iscsi_policy_action_id;

gint                     iscsi_perform_login_action (StoragedDaemon        *daemon,
                                                     libiscsi_login_action  action,
                                                     const gchar           *name,
                                                     const gint             tpgt,
                                                     const gchar           *address,
                                                     const gint             port,
                                                     const gchar           *iface,
                                                     gchar                **errorstr);

#endif /* __STORAGED_ISCSI_UTIL_H__ */
