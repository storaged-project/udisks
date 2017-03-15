/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2013 Marius Vollmer <marius.vollmer@redhat.com>
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

#ifndef __UDISKS_LVM2_DAEMON_UTIL_H__
#define __UDISKS_LVM2_DAEMON_UTIL_H__

#include <src/udisksdaemontypes.h>
#include "udiskslvm2types.h"

G_BEGIN_DECLS

gboolean udisks_daemon_util_lvm2_block_is_unused (UDisksBlock  *block,
                                                  GError      **error);

gboolean udisks_daemon_util_lvm2_wipe_block      (UDisksDaemon  *daemon,
                                                  UDisksBlock   *block,
                                                  GError       **error);

gboolean udisks_daemon_util_lvm2_name_is_reserved (const gchar *name);

UDisksLinuxVolumeGroupObject * udisks_daemon_util_lvm2_find_volume_group_object (UDisksDaemon *daemon,
                                                                                 const gchar  *name);

void udisks_daemon_util_lvm2_trigger_udev (const gchar *device_file);

G_END_DECLS

#endif /* __UDISKS_LVM2_DAEMON_UTIL_H__ */
