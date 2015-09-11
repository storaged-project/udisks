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
#include <storaged/storaged.h>
#include <gudev/gudev.h>
#include <sys/types.h>
#include <src/storagedlogging.h>

#include "lsm_generated.h"

G_BEGIN_DECLS

#define LSM_MODULE_NAME "lsm"

struct _StoragedLinuxDriveLSM;
typedef struct _StoragedLinuxDriveLSM StoragedLinuxDriveLSM;

#define STORAGED_TYPE_LINUX_DRIVE_LSM (storaged_linux_drive_lsm_get_type ())
#define STORAGED_LINUX_DRIVE_LSM(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_DRIVE_LSM, \
                               StoragedLinuxDriveLSM))
#define STORAGED_IS_LINUX_DRIVE_LSM(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_DRIVE_LSM))

GType storaged_linux_drive_lsm_get_type (void) G_GNUC_CONST;
StoragedLinuxDriveLSM *storaged_linux_drive_lsm_new (void);
gboolean
storaged_linux_drive_lsm_update (StoragedLinuxDriveLSM *std_lx_drv_lsm,
                                 StoragedLinuxDriveObject *st_lx_drv_obj);

struct _StoragedLinuxManagerLSM;
typedef struct _StoragedLinuxManagerLSM StoragedLinuxManagerLSM;

#define STORAGED_TYPE_LINUX_MANAGER_LSM \
  (storaged_linux_manager_lsm_get_type ())
#define STORAGED_LINUX_MANAGER_LSM(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_MANAGER_LSM, \
                               StoragedLinuxManagerLSM))
#define STORAGED_IS_LINUX_MANAGER_LSM(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_MANAGER))

GType storaged_linux_manager_lsm_get_type (void) G_GNUC_CONST;
StoragedLinuxManagerLSM *storaged_linux_manager_lsm_new (void);

G_END_DECLS

#endif /* __LSM_TYPES_H__ */
