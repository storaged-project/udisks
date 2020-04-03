/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Gris Ge <fge@redhat.com>
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

#ifndef __LSM_LINUX_DRIVE_H__
#define __LSM_LINUX_DRIVE_H__

#include <gio/gio.h>
#include <udisks/udisks.h>
#include "lsm_types.h"
#include "udiskslinuxmodulelsm.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_DRIVE_LSM   (udisks_linux_drive_lsm_get_type ())
#define UDISKS_LINUX_DRIVE_LSM(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_DRIVE_LSM, UDisksLinuxDriveLSM))
#define UDISKS_IS_LINUX_DRIVE_LSM(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_DRIVE_LSM))

GType                 udisks_linux_drive_lsm_get_type (void) G_GNUC_CONST;
UDisksLinuxDriveLSM  *udisks_linux_drive_lsm_new      (UDisksLinuxModuleLSM   *module,
                                                       UDisksLinuxDriveObject *drive_object);
gboolean              udisks_linux_drive_lsm_update   (UDisksLinuxDriveLSM    *drive_lsm,
                                                       UDisksLinuxDriveObject *drive_object);

G_END_DECLS

#endif /* __LSM_LINUX_DRIVE_H__ */
