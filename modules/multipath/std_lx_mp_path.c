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
 *      org.storaged.Storaged.MultipathPathGroupPath.PathGroupPath.Path
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

#define _STD_LX_BLK_DBUS_OBJ_PATH_PREFIX \
  "/org/storaged/Storaged/block_devices"

typedef struct _StoragedLinuxMultipathPathGroupPathClass
StoragedLinuxMultipathPathGroupPathClass;

struct _StoragedLinuxMultipathPathGroupPath
{
  StoragedMultipathPathGroupPathSkeleton parent_instance;
};

struct _StoragedLinuxMultipathPathGroupPathClass
{
  StoragedMultipathPathGroupPathSkeletonClass parent_class;
};

static void
std_lx_mp_path_iface_init (StoragedMultipathPathGroupPathIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (StoragedLinuxMultipathPathGroupPath, std_lx_mp_path,
   STORAGED_TYPE_MULTIPATH_PATH_GROUP_PATH_SKELETON,
   G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MULTIPATH_PATH_GROUP_PATH,
                          std_lx_mp_path_iface_init));

static void
std_lx_mp_path_finalize (GObject *object)
{
  storaged_debug ("MultipathPathGroupPath: std_lx_mp_path_finalize ()");

  if (G_OBJECT_CLASS (std_lx_mp_path_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (std_lx_mp_path_parent_class)->finalize (object);
}

static void
std_lx_mp_path_init (StoragedLinuxMultipathPathGroupPath *std_lx_mp_path)
{
  storaged_debug ("MultipathPathGroupPath: std_lx_mp_path_init");
  return;
}

static void
std_lx_mp_path_class_init (StoragedLinuxMultipathPathGroupPathClass *class)
{
  GObjectClass *gobject_class;

  storaged_debug ("MultipathPathGroupPath: std_lx_mp_path_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = std_lx_mp_path_finalize;
}

static void
std_lx_mp_path_iface_init (StoragedMultipathPathGroupPathIface *iface)
{
  storaged_debug ("MultipathPathGroupPath: std_lx_mp_path_iface_init");
}

StoragedLinuxMultipathPathGroupPath *
std_lx_mp_path_new (struct dmmp_path *mp_path)
{
  StoragedLinuxMultipathPathGroupPath * std_lx_mp_path = NULL;
  std_lx_mp_path = g_object_new (STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_PATH,
                                 NULL);

  std_lx_mp_path_update (std_lx_mp_path, mp_path);

  return std_lx_mp_path;
}

gboolean
std_lx_mp_path_update (StoragedLinuxMultipathPathGroupPath *std_lx_mp_path,
                       struct dmmp_path *mp_path)
{
  StoragedMultipathPathGroupPath *std_mp_path = NULL;
  uint32_t status;
  const char *blk_obj_path = NULL;
  const char *blk_name = NULL;

  storaged_debug ("MultipathPathGroupPath: std_lx_mp_path_update()");

  std_mp_path = STORAGED_MULTIPATH_PATH_GROUP_PATH (std_lx_mp_path);

  blk_name = dmmp_path_blk_name_get (mp_path);
  if (blk_name == NULL)
      return FALSE;

  blk_obj_path = g_strdup_printf ("%s/%s", _STD_LX_BLK_DBUS_OBJ_PATH_PREFIX,
                                  blk_name);

  storaged_multipath_path_group_path_set_block
    (std_mp_path, blk_obj_path);

  storaged_multipath_path_group_path_set_name
    (std_mp_path, blk_name);

  status = dmmp_path_status_get (mp_path);
  storaged_multipath_path_group_path_set_status
    (std_mp_path, dmmp_path_status_str (status));

  return TRUE; //TODO(Gris Ge)
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
