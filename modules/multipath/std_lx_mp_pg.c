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
 *      org.storaged.Storaged.MultipathPathGroup.PathGroup
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

typedef struct _StoragedLinuxMultipathPathGroupClass
StoragedLinuxMultipathPathGroupClass;

struct _StoragedLinuxMultipathPathGroup
{
  StoragedMultipathPathGroupSkeleton parent_instance;
};

struct _StoragedLinuxMultipathPathGroupClass
{
  StoragedMultipathPathGroupSkeletonClass parent_class;
};

static void
std_lx_mp_pg_iface_init (StoragedMultipathPathGroupIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (StoragedLinuxMultipathPathGroup, std_lx_mp_pg,
   STORAGED_TYPE_MULTIPATH_PATH_GROUP_SKELETON,
   G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MULTIPATH_PATH_GROUP,
                          std_lx_mp_pg_iface_init));

static void
std_lx_mp_pg_finalize (GObject *object)
{
  storaged_debug ("Multipath: std_lx_mp_pg_finalize ()");

  if (G_OBJECT_CLASS (std_lx_mp_pg_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (std_lx_mp_pg_parent_class)->finalize (object);
}

static void
std_lx_mp_pg_init (StoragedLinuxMultipathPathGroup *std_lx_mp_pg)
{
  storaged_debug ("Multipath: std_lx_mp_pg_init");
  return;
}

static void
std_lx_mp_pg_class_init (StoragedLinuxMultipathPathGroupClass *class)
{
  GObjectClass *gobject_class;

  storaged_debug ("Multipath: std_lx_mp_pg_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = std_lx_mp_pg_finalize;
}

static void
std_lx_mp_pg_iface_init (StoragedMultipathPathGroupIface *iface)
{
  storaged_debug ("Multipath: std_lx_mp_pg_iface_init");
}

StoragedLinuxMultipathPathGroup *
std_lx_mp_pg_new (struct dmmp_path_group *mp_pg)
{
  StoragedLinuxMultipathPathGroup * std_lx_mp_pg = NULL;
  std_lx_mp_pg = g_object_new (STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP, NULL);

  std_lx_mp_pg_update (std_lx_mp_pg, mp_pg);

  return std_lx_mp_pg;
}

gboolean
std_lx_mp_pg_update (StoragedLinuxMultipathPathGroup *std_lx_mp_pg,
                     struct dmmp_path_group *mp_pg)
{
  StoragedMultipathPathGroup *std_mp_pg = NULL;
  uint32_t status;

  storaged_debug ("Multipath: std_lx_mp_pg_update()");

  std_mp_pg = STORAGED_MULTIPATH_PATH_GROUP (std_lx_mp_pg);

  storaged_multipath_path_group_set_id
    (std_mp_pg, dmmp_path_group_id_get (mp_pg));
  storaged_multipath_path_group_set_priority
    (std_mp_pg, dmmp_path_group_priority_get (mp_pg));

  status = dmmp_path_group_status_get (mp_pg);
  storaged_multipath_path_group_set_status
    (std_mp_pg, dmmp_path_group_status_str (status));

  storaged_multipath_path_group_set_selector
    (std_mp_pg, dmmp_path_group_selector_get (mp_pg));

  return TRUE; //TODO(Gris Ge)
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
