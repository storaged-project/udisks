/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014 Tomas Bzatek <tbzatek@redhat.com>
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

#ifndef __UDISKS_LINUX_MANAGER_LVM2_H__
#define __UDISKS_LINUX_MANAGER_LVM2_H__

#include <src/udisksdaemontypes.h>
#include "udiskslvm2types.h"
#include "udisks-lvm2-generated.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_MANAGER_LVM2  (udisks_linux_manager_lvm2_get_type ())
#define UDISKS_LINUX_MANAGER_LVM2(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_MANAGER_LVM2, UDisksLinuxManagerLVM2))
#define UDISKS_IS_LINUX_MANAGER_LVM2(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_MANAGER_LVM2))

GType                    udisks_linux_manager_lvm2_get_type           (void) G_GNUC_CONST;
UDisksLinuxManagerLVM2  *udisks_linux_manager_lvm2_new                (UDisksDaemon           *daemon);
UDisksDaemon            *udisks_linux_manager_lvm2_get_daemon         (UDisksLinuxManagerLVM2 *manager);

G_END_DECLS

#endif /* __UDISKS_LINUX_MANAGER_LVM2_H__ */
