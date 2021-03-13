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

#ifndef __UDISKS_AUTHORIZATION_H__
#define __UDISKS_AUTHORIZATION_H__

#include "udisksdaemontypes.h"
#include <polkit/polkit.h>

G_BEGIN_DECLS

gboolean udisks_daemon_util_check_authorization_sync (UDisksDaemon          *daemon,
                                                      UDisksObject          *object,
                                                      const gchar           *action_id,
                                                      GVariant              *options,
                                                      const gchar           *message,
                                                      GDBusMethodInvocation *invocation);

gboolean udisks_daemon_util_check_authorization_sync_with_error (UDisksDaemon           *daemon,
                                                                 UDisksObject           *object,
                                                                 const gchar            *action_id,
                                                                 GVariant               *options,
                                                                 const gchar            *message,
                                                                 GDBusMethodInvocation  *invocation,
                                                                 GError                **error);

/* Utility macro for policy verification. */
#define UDISKS_DAEMON_CHECK_AUTHORIZATION(daemon,                   \
                                          object,                   \
                                          action_id,                \
                                          options,                  \
                                          message,                  \
                                          invocation)               \
  if (! udisks_daemon_util_check_authorization_sync ((daemon),      \
                                                     (object),      \
                                                     (action_id),   \
                                                     (options),     \
                                                     (message),     \
                                                     (invocation))) \
    { \
      goto out; \
    }

G_END_DECLS

#endif /* __UDISKS_AUTHORIZATION_H__ */
