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
#include <udisks/udisks.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define LSM_MODULE_NAME "lsm"
#define LSM_POLICY_ACTION_ID "org.freedesktop.udisks2.lsm.manage-led"

struct _UDisksLinuxModuleLSM;
typedef struct _UDisksLinuxModuleLSM UDisksLinuxModuleLSM;

struct _UDisksLinuxDriveLSM;
typedef struct _UDisksLinuxDriveLSM UDisksLinuxDriveLSM;

struct _UDisksLinuxDriveLSMLocal;
typedef struct _UDisksLinuxDriveLSMLocal UDisksLinuxDriveLSMLocal;

G_END_DECLS

#endif /* __LSM_TYPES_H__ */
