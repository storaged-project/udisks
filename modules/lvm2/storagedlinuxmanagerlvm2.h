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

#ifndef __STORAGED_LINUX_MANAGER_LVM2_H__
#define __STORAGED_LINUX_MANAGER_LVM2_H__

#include <src/storageddaemontypes.h>
#include "storagedlvm2types.h"
#include "storaged-lvm2-generated.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_LINUX_MANAGER_LVM2  (storaged_linux_manager_lvm2_get_type ())
#define STORAGED_LINUX_MANAGER_LVM2(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_MANAGER_LVM2, StoragedLinuxManagerLVM2))
#define STORAGED_IS_LINUX_MANAGER_LVM2(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_MANAGER_LVM2))

GType                      storaged_linux_manager_lvm2_get_type           (void) G_GNUC_CONST;
StoragedLinuxManagerLVM2  *storaged_linux_manager_lvm2_new                (StoragedDaemon           *daemon);
StoragedDaemon            *storaged_linux_manager_lvm2_get_daemon         (StoragedLinuxManagerLVM2 *manager);

G_END_DECLS

#endif /* __STORAGED_LINUX_MANAGER_LVM2_H__ */
