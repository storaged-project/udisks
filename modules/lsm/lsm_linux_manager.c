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

#include <src/storagedlogging.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>

#include "lsm_types.h"

typedef struct _StoragedLinuxManagerLSMClass StoragedLinuxManagerLSMClass;

struct _StoragedLinuxManagerLSM
{
  StoragedManagerLSMSkeleton parent_instance;
};

struct _StoragedLinuxManagerLSMClass
{
  StoragedManagerLSMSkeletonClass parent_class;
};

static void
storaged_linux_manager_lsm_iface_init (StoragedManagerLSMIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (StoragedLinuxManagerLSM, storaged_linux_manager_lsm,
   STORAGED_TYPE_MANAGER_LSM_SKELETON,
   G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MANAGER_LSM,
                          storaged_linux_manager_lsm_iface_init));

static void
storaged_linux_manager_lsm_init (StoragedLinuxManagerLSM *manager)
{
}

static void
storaged_linux_manager_lsm_class_init (StoragedLinuxManagerLSMClass *class)
{
}

StoragedLinuxManagerLSM *
storaged_linux_manager_lsm_new (void)
{
  return STORAGED_LINUX_MANAGER_LSM
    (g_object_new (STORAGED_TYPE_LINUX_MANAGER_LSM, NULL));
}

static void
storaged_linux_manager_lsm_iface_init (StoragedManagerLSMIface *iface)
{
  // Empty but need to force storaged do coldplug refresh all interface.
}
