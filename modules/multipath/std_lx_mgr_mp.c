/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 - 2016 Gris Ge <fge@redhat.com>
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

#include <sys/types.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <src/storagedlogging.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>

#include "mp_types.h"

typedef struct _StoragedLinuxManagerMultipathClass
StoragedLinuxManagerMultipathClass;

struct _StoragedLinuxManagerMultipath
{
  StoragedManagerMultipathSkeleton parent_instance;
};

struct _StoragedLinuxManagerMultipathClass
{
  StoragedManagerMultipathSkeletonClass parent_class;
};

static void
std_lx_mgr_mp_iface_init (StoragedManagerMultipathIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxManagerMultipath, std_lx_mgr_mp,
                         STORAGED_TYPE_MANAGER_MULTIPATH_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MANAGER_MULTIPATH,
                                                std_lx_mgr_mp_iface_init));

static void
std_lx_mgr_mp_init (StoragedLinuxManagerMultipath *manager)
{
}

static void
std_lx_mgr_mp_class_init (StoragedLinuxManagerMultipathClass *class)
{
}

StoragedLinuxManagerMultipath *
std_lx_mgr_mp_new (void)
{
  return STORAGED_LINUX_MANAGER_MULTIPATH
    (g_object_new (STORAGED_TYPE_LINUX_MANAGER_MULTIPATH, NULL));
}

static void
std_lx_mgr_mp_iface_init (StoragedManagerMultipathIface *iface)
{
  // Empty but need to force storaged do coldplug refresh all interface.
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
