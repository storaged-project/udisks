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

#ifndef __UDISKS_LINUX_FILESYSTEM_BTRFS_H__
#define __UDISKS_LINUX_FILESYSTEM_BTRFS_H__

#include <src/udisksdaemontypes.h>
#include "udisksbtrfstypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_FILESYSTEM_BTRFS            (udisks_linux_filesystem_btrfs_get_type ())
#define UDISKS_LINUX_FILESYSTEM_BTRFS(o)              (G_TYPE_CHECK_INSTANCE_CAST  ((o), UDISKS_TYPE_LINUX_FILESYSTEM_BTRFS, UDisksLinuxFilesystemBTRFS))
#define UDISKS_IS_LINUX_FILESYSTEM_BTRFS(o)           (G_TYPE_CHECK_INSTANCE_TYPE  ((o), UDISKS_TYPE_LINUX_FILESYSTEM_BTRFS))
#define UDISKS_LINUX_FILESYSTEM_BTRFS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), UDISKS_TYPE_LINUX_FILESYSTEM_BTRFS, UDisksLinuxFilesystemBTRFSClass))
#define UDISKS_IS_LINUX_FILESYSTEM_BTRFS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), UDISKS_TYPE_LINUX_FILESYSTEM_BTRFS))
#define UDISKS_LINUX_FILESYSTEM_BTRFS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), UDISKS_TYPE_LINUX_FILESYSTEM_BTRFS, UDisksLinuxFilesystemBTRFSClass))
#define UDISKS_LINUX_FILESYSTEM_BTRFS_GET_PRIVATE(o)  ((UDisksLinuxFilesystemBTRFSPrivate *)((UDISKS_LINUX_FILESYSTEM_BTRFS (o))->priv))

GType                       udisks_linux_filesystem_btrfs_get_type   (void) G_GNUC_CONST;
UDisksLinuxFilesystemBTRFS *udisks_linux_filesystem_btrfs_new        (void);
gboolean                    udisks_linux_filesystem_btrfs_update     (UDisksLinuxFilesystemBTRFS *l_fs_btrfs,
                                                                      UDisksLinuxBlockObject     *object);
UDisksDaemon               *udisks_linux_filesystem_btrfs_get_daemon (UDisksLinuxFilesystemBTRFS *l_fs_btrfs);

G_END_DECLS

#endif /* __UDISKS_LINUX_FILESYSTEM_BTRFS_H__ */
