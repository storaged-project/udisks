/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2011 Martin Pitt <martin.pitt@ubuntu.com>
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

#ifndef __UDISKS_LINUX_FSINFO_H__
#define __UDISKS_LINUX_FSINFO_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct
{
  const gchar *fstype;
  const gchar *command_change_label; /* should have $DEVICE and $LABEL */
  const gchar *command_clear_label; /* should have $DEVICE; if NULL, call command_change_label with $LABEL == '' */
  /* TODO: use flags or bitfields */
  gboolean     supports_online_label_rename;
  gboolean     supports_owners;
  const gchar *command_create_fs;  /* should have $DEVICE and $LABEL */
  const gchar *command_validate_create_fs;  /* should have $DEVICE and $LABEL */
  const gchar *option_no_discard;
} FSInfo;

const FSInfo  *get_fs_info (const gchar *fstype);
const gchar  **get_supported_filesystems (void);

G_END_DECLS

#endif /* __UDISKS_LINUX_FSINFO_H__ */
