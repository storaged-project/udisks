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

#ifndef __MOUNT_FILE_H__
#define __MOUNT_FILE_H__

#include "devkit-disks-device.h"

G_BEGIN_DECLS

gboolean mounts_file_has_device  (DevkitDisksDevice *device,
                                  uid_t *mounted_by_uid,
                                  gboolean *remove_dir_on_unmount);
void     mounts_file_add         (DevkitDisksDevice *device,
                                  uid_t mounted_by_uid,
                                  gboolean remove_dir_on_unmount);
void     mounts_file_remove      (DevkitDisksDevice *device,
                                  const char *mount_path);
void     mounts_file_clean_stale (GList *existing_devices);

G_END_DECLS

#endif /* __MOUNT_FILE_H__ */

