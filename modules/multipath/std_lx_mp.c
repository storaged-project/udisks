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

/*
 * Handle these dbus interfaces:
 *      org.storaged.Storaged.Multipath
 *      org.storaged.Storaged.Multipath.PathGroup
 *      org.storaged.Storaged.Multipath.PathGroup.Path
 */

#include <sys/types.h>

#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <libdmmp/libdmmp.h>

#include <src/storagedlogging.h>
#include <src/storagedlinuxdriveobject.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedbasejob.h>
#include <src/storagedsimplejob.h>
#include <src/storagedthreadedjob.h>
#include <src/storagedlinuxdevice.h>
#include <modules/storagedmoduleobject.h>

#include "mp_types.h"
#include "mp_generated.h"

typedef struct _StoragedLinuxMultipathClass
StoragedLinuxMultipathClass;

struct _StoragedLinuxMultipath
{
  StoragedMultipathSkeleton parent_instance;
};

struct _StoragedLinuxMultipathClass
{
  StoragedMultipathSkeletonClass parent_class;
};

static void
std_lx_mp_iface_init (StoragedMultipathIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (StoragedLinuxMultipath, std_lx_mp,
   STORAGED_TYPE_MULTIPATH_SKELETON,
   G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MULTIPATH,
                          std_lx_mp_iface_init));

static void
std_lx_mp_finalize (GObject *object)
{
  storaged_debug ("Multipath: std_lx_mp_finalize ()");

  if (G_OBJECT_CLASS (std_lx_mp_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (std_lx_mp_parent_class)->finalize (object);
}

static void
std_lx_mp_init (StoragedLinuxMultipath *std_lx_mp)
{
  storaged_debug ("Multipath: std_lx_mp_init");
  return;
}

static void
std_lx_mp_class_init (StoragedLinuxMultipathClass *class)
{
  GObjectClass *gobject_class;

  storaged_debug ("Multipath: std_lx_mp_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = std_lx_mp_finalize;
}

static void
std_lx_mp_iface_init (StoragedMultipathIface *iface)
{
  storaged_debug ("Multipath: std_lx_mp_iface_init");
}

StoragedLinuxMultipath *
std_lx_mp_new (struct dmmp_mpath *mpath)
{
  StoragedLinuxMultipath *std_lx_mp = NULL;

  std_lx_mp = g_object_new (STORAGED_TYPE_LINUX_MULTIPATH, NULL);
  std_lx_mp_update (std_lx_mp, mpath);

  return std_lx_mp;
}

gboolean
std_lx_mp_update (StoragedLinuxMultipath *std_lx_mp, struct dmmp_mpath *mpath)
{
  //TODO: Check whether changed.
  StoragedMultipath *std_mp = NULL;

  storaged_debug ("Multipath: std_lx_mp_update()");

  std_mp = STORAGED_MULTIPATH (std_lx_mp);

  storaged_multipath_set_name (std_mp, dmmp_mpath_name_get (mpath));
  storaged_multipath_set_wwid (std_mp, dmmp_mpath_wwid_get (mpath));

  return FALSE;
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
