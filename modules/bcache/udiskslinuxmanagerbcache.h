/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Dominika Hodovska <dhodovsk@redhat.com>
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

#ifndef __UDISKS_LINUX_MANAGER_BCACHE_H__
#define __UDISKS_LINUX_MANAGER_BCACHE_H__

#include <src/udisksdaemontypes.h>

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_MANAGER_BCACHE            (udisks_linux_manager_bcache_get_type ())
#define UDISKS_LINUX_MANAGER_BCACHE(o)              (G_TYPE_CHECK_INSTANCE_CAST  ((o), UDISKS_TYPE_LINUX_MANAGER_BCACHE, UDisksLinuxManagerBcache))
#define UDISKS_IS_LINUX_MANAGER_BCACHE(o)           (G_TYPE_CHECK_INSTANCE_TYPE  ((o), UDISKS_TYPE_LINUX_MANAGER_BCACHE))
#define UDISKS_LINUX_MANAGER_BCACHE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), UDISKS_TYPE_LINUX_MANAGER_BCACHE, UDisksLinuxManagerBcacheClass))
#define UDISKS_IS_LINUX_MANAGER_BCACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), UDISKS_TYPE_LINUX_MANAGER_BCACHE))
#define UDISKS_LINUX_MANAGER_BCACHE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), UDISKS_TYPE_LINUX_MANAGER_BCACHE, UDisksLinuxManagerBcacheClass))

typedef struct _UDisksLinuxManagerBcache        UDisksLinuxManagerBcache;
typedef struct _UDisksLinuxManagerBcacheClass   UDisksLinuxManagerBcacheClass;

GType                      udisks_linux_manager_bcache_get_type     (void) G_GNUC_CONST;
UDisksLinuxManagerBcache  *udisks_linux_manager_bcache_new          (UDisksDaemon              *daemon);
UDisksDaemon              *udisks_linux_manager_bcache_get_daemon   (UDisksLinuxManagerBcache  *manager);

G_END_DECLS

#endif /* __UDISKS_LINUX_MANAGER_BCACHE_H__ */
