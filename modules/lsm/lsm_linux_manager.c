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

/* Note: This file is inspired by modules/dummy/dummylinuxmanager.c  */

#include "config.h"

#include <sys/types.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <src/udiskslogging.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxdevice.h>

#include "lsm_types.h"

typedef struct _UDisksLinuxManagerLSMClass UDisksLinuxManagerLSMClass;

struct _UDisksLinuxManagerLSM
{
  UDisksManagerLSMSkeleton parent_instance;
};

struct _UDisksLinuxManagerLSMClass
{
  UDisksManagerLSMSkeletonClass parent_class;
};

static void
udisks_linux_manager_lsm_iface_init (UDisksManagerLSMIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (UDisksLinuxManagerLSM, udisks_linux_manager_lsm,
   UDISKS_TYPE_MANAGER_LSM_SKELETON,
   G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_LSM,
                          udisks_linux_manager_lsm_iface_init));

static void
udisks_linux_manager_lsm_init (UDisksLinuxManagerLSM *manager)
{
}

static void
udisks_linux_manager_lsm_class_init (UDisksLinuxManagerLSMClass *class)
{
}

UDisksLinuxManagerLSM *
udisks_linux_manager_lsm_new (void)
{
  return UDISKS_LINUX_MANAGER_LSM
    (g_object_new (UDISKS_TYPE_LINUX_MANAGER_LSM, NULL));
}

static void
udisks_linux_manager_lsm_iface_init (UDisksManagerLSMIface *iface)
{
  // Empty but need to force udisks do coldplug refresh all interface.
}
