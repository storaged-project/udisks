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

#ifndef __DEVKIT_DISKS_MOUNT_FILE_H__
#define __DEVKIT_DISKS_MOUNT_FILE_H__

#include "devkit-disks-types.h"

G_BEGIN_DECLS

gboolean devkit_disks_mount_file_has_device  (const gchar  *device_file,
                                              uid_t        *mounted_by_uid,
                                              gboolean     *remove_dir_on_unmount);
void     devkit_disks_mount_file_add         (const gchar  *device_file,
                                              const gchar  *mount_path,
                                              uid_t         mounted_by_uid,
                                              gboolean      remove_dir_on_unmount);
void     devkit_disks_mount_file_remove      (const gchar  *device_file,
                                              const char   *mount_path);
void     devkit_disks_mount_file_clean_stale (GList        *existing_devices);

G_END_DECLS

#endif /* __DEVKIT_DISKS_MOUNT_FILE_H__ */

