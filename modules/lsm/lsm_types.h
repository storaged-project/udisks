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

#ifndef __LSM_TYPES_H__
#define __LSM_TYPES_H__

#include <gio/gio.h>
#include <polkit/polkit.h>
#include <udisks/udisks.h>
#include <gudev/gudev.h>
#include <sys/types.h>
#include <src/udiskslogging.h>

#include "lsm_generated.h"

G_BEGIN_DECLS

#define LSM_MODULE_NAME "lsm"

struct _UDisksLinuxDriveLSM;
typedef struct _UDisksLinuxDriveLSM UDisksLinuxDriveLSM;

#define UDISKS_TYPE_LINUX_DRIVE_LSM (udisks_linux_drive_lsm_get_type ())
#define UDISKS_LINUX_DRIVE_LSM(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_DRIVE_LSM, \
                               UDisksLinuxDriveLSM))
#define UDISKS_IS_LINUX_DRIVE_LSM(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_DRIVE_LSM))

GType udisks_linux_drive_lsm_get_type (void) G_GNUC_CONST;
UDisksLinuxDriveLSM *udisks_linux_drive_lsm_new (void);
gboolean
udisks_linux_drive_lsm_update (UDisksLinuxDriveLSM *std_lx_drv_lsm,
                               UDisksLinuxDriveObject *st_lx_drv_obj);

struct _UDisksLinuxManagerLSM;
typedef struct _UDisksLinuxManagerLSM UDisksLinuxManagerLSM;

#define UDISKS_TYPE_LINUX_MANAGER_LSM \
  (udisks_linux_manager_lsm_get_type ())
#define UDISKS_LINUX_MANAGER_LSM(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_MANAGER_LSM, \
                               UDisksLinuxManagerLSM))
#define UDISKS_IS_LINUX_MANAGER_LSM(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_MANAGER))

GType udisks_linux_manager_lsm_get_type (void) G_GNUC_CONST;
UDisksLinuxManagerLSM *udisks_linux_manager_lsm_new (void);

struct _UDisksLinuxDriveLsmLocal;
typedef struct _UDisksLinuxDriveLsmLocal UDisksLinuxDriveLsmLocal;

GType udisks_linux_drive_lsm_local_get_type (void) G_GNUC_CONST;
#define UDISKS_TYPE_LINUX_DRIVE_LSM_LOCAL \
  (udisks_linux_drive_lsm_local_get_type ())
#define UDISKS_LINUX_DRIVE_LSM_LOCAL(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_DRIVE_LSM_LOCAL, \
                               UDisksLinuxDriveLsmLocal))
#define UDISKS_IS_LINUX_DRIVE_LSM_LOCAL(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_DRIVE_LSM_LOCAL))

UDisksLinuxDriveLsmLocal *
udisks_linux_drive_lsm_local_new (void);

gboolean
udisks_linux_drive_lsm_local_update (UDisksLinuxDriveLsmLocal *lsm_local,
                                     UDisksLinuxDriveObject *ud_lx_drv_obj);

G_END_DECLS

#endif /* __LSM_TYPES_H__ */
