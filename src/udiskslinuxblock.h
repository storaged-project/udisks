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

#ifndef __UDISKS_LINUX_BLOCK_H__
#define __UDISKS_LINUX_BLOCK_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_BLOCK  (udisks_linux_block_get_type ())
#define UDISKS_LINUX_BLOCK(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_BLOCK, UDisksLinuxBlock))
#define UDISKS_IS_LINUX_BLOCK(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_BLOCK))

GType        udisks_linux_block_get_type (void) G_GNUC_CONST;
UDisksBlock *udisks_linux_block_new      (void);
void         udisks_linux_block_update   (UDisksLinuxBlock       *block,
                                          UDisksLinuxBlockObject *object);

void         udisks_linux_block_handle_format (UDisksBlock            *block,
                                               GDBusMethodInvocation  *invocation,
                                               const gchar            *type,
                                               GVariant               *options,
                                               void                  (*complete)(gpointer user_data),
                                               gpointer                complete_user_data);

gchar       *udisks_linux_get_parent_for_tracking (UDisksDaemon *daemon,
                                                   const gchar    *path,
                                                   const gchar   **uuid_ret);

GVariant    *udisks_linux_find_child_configuration (UDisksDaemon *daemon,
                                                    const gchar    *uuid);

gboolean     udisks_linux_remove_configuration (GVariant       *configuration,
                                                GError        **error);

gboolean     udisks_linux_block_teardown (UDisksBlock            *block,
                                          GDBusMethodInvocation  *invocation,
                                          GVariant               *options,
                                          GError                **error);

gboolean     udisks_linux_block_is_luks (UDisksBlock *block);

gboolean     udisks_linux_block_is_tcrypt (UDisksBlock *block);

gboolean     udisks_linux_block_is_unknown_crypto (UDisksBlock *block);

void         udisks_linux_block_encrypted_lock (UDisksBlock *block);
void         udisks_linux_block_encrypted_unlock (UDisksBlock *block);

G_END_DECLS

#endif /* __UDISKS_LINUX_BLOCK_H__ */
