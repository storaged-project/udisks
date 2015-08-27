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

#ifndef __STORAGED_LINUX_MANAGER_BTRFS_H__
#define __STORAGED_LINUX_MANAGER_BTRFS_H__

#include <src/storageddaemontypes.h>
#include "storagedbtrfstypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_LINUX_MANAGER_BTRFS            (storaged_linux_manager_btrfs_get_type ())
#define STORAGED_LINUX_MANAGER_BTRFS(o)              (G_TYPE_CHECK_INSTANCE_CAST  ((o), STORAGED_TYPE_LINUX_MANAGER_BTRFS, StoragedLinuxManagerBTRFS))
#define STORAGED_IS_LINUX_MANAGER_BTRFS(o)           (G_TYPE_CHECK_INSTANCE_TYPE  ((o), STORAGED_TYPE_LINUX_MANAGER_BTRFS))
#define STORAGED_LINUX_MANAGER_BTRFS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), STORAGED_TYPE_LINUX_MANAGER_BTRFS, StoragedLinuxManagerBTRFSClass))
#define STORAGED_IS_LINUX_MANAGER_BTRFS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), STORAGED_TYPE_LINUX_MANAGER_BTRFS))
#define STORAGED_LINUX_MANAGER_BTRFS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), STORAGED_TYPE_LINUX_MANAGER_BTRFS, StoragedLinuxManagerBTRFSClass))

GType                      storaged_linux_manager_btrfs_get_type   (void) G_GNUC_CONST;
StoragedLinuxManagerBTRFS *storaged_linux_manager_btrfs_new        (StoragedDaemon *daemon);
StoragedDaemon            *storaged_linux_manager_btrfs_get_daemon (StoragedLinuxManagerBTRFS *manager);

G_END_DECLS

#endif /* __STORAGED_LINUX_MANAGER_BTRFS_H__ */
