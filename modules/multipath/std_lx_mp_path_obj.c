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
 * This file handle the dbus object of
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
#include <stdint.h>
#include <inttypes.h>
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

typedef struct _StoragedLinuxMultipathPathGroupPathObjectClass
StoragedLinuxMultipathPathGroupPathObjectClass;

struct _StoragedLinuxMultipathPathGroupPathObject
{
  StoragedObjectSkeleton parent_instance;
  StoragedLinuxMultipathPathGroupPath *std_lx_mp_path;
  GDBusObjectManagerServer *dbus_mgr;
};

struct _StoragedLinuxMultipathPathGroupPathObjectClass
{
  StoragedObjectSkeletonClass parent_class;
};

static void
std_lx_mp_path_obj_iface_init (StoragedModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (StoragedLinuxMultipathPathGroupPathObject, std_lx_mp_path_obj,
   STORAGED_TYPE_OBJECT_SKELETON,
   G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MODULE_OBJECT,
                          std_lx_mp_path_obj_iface_init));

static void
std_lx_mp_path_obj_finalize (GObject *_object)
{
  StoragedLinuxMultipathPathGroupPathObject *object =
      STORAGED_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT (_object);

  storaged_debug ("Multipath: std_lx_mp_path_obj_finalize ()");

  if (object->std_lx_mp_path != NULL)
    g_object_unref (object->std_lx_mp_path);

  if (G_OBJECT_CLASS (std_lx_mp_path_obj_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (std_lx_mp_path_obj_parent_class)->finalize (_object);
}

static void
std_lx_mp_path_obj_init
  (StoragedLinuxMultipathPathGroupPathObject *std_lx_mp_path_obj)
{
  storaged_debug ("Multipath: std_lx_mp_path_obj_init");
  return;
}

static void
std_lx_mp_path_obj_class_init
  (StoragedLinuxMultipathPathGroupPathObjectClass *class)
{
  GObjectClass *gobject_class;

  storaged_debug ("Multipath: std_lx_mp_path_obj_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = std_lx_mp_path_obj_finalize;
}

static void
std_lx_mp_path_obj_iface_init (StoragedModuleObjectIface *iface)
{
  storaged_debug ("Multipath: std_lx_mp_path_obj_iface_init");
}

StoragedLinuxMultipathPathGroupPathObject *
std_lx_mp_path_obj_new (GDBusObjectManagerServer *dbus_mgr,
                        struct dmmp_path *mp_path,
                        const char *mp_pg_obj_path)
{
  const char *mp_path_obj_path = NULL;
  const char *blk_name = NULL;
  StoragedLinuxMultipathPathGroupPath *std_lx_mp_path = NULL;
  StoragedLinuxMultipathPathGroupPathObject *std_lx_mp_path_obj = NULL;
  StoragedLinuxBlockObject *std_lx_blk_obj = NULL;

  storaged_debug ("Multipath: std_lx_mp_path_obj_new");

  blk_name = dmmp_path_blk_name_get (mp_path);

  if (blk_name == NULL)
    return NULL;

  std_lx_mp_path = std_lx_mp_path_new (mp_path);

  std_lx_mp_path_obj = g_object_new
    (STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT, NULL);

  std_lx_mp_path_obj->std_lx_mp_path = std_lx_mp_path;
  std_lx_mp_path_obj->dbus_mgr = dbus_mgr;

  mp_path_obj_path = std_lx_mp_path_obj_path_gen (mp_pg_obj_path, blk_name);

  g_dbus_object_skeleton_set_object_path
    (G_DBUS_OBJECT_SKELETON (std_lx_mp_path_obj), mp_path_obj_path);

  g_dbus_object_skeleton_add_interface
      (G_DBUS_OBJECT_SKELETON (std_lx_mp_path_obj),
       G_DBUS_INTERFACE_SKELETON (std_lx_mp_path_obj->std_lx_mp_path));

  g_dbus_object_manager_server_export
    (std_lx_mp_path_obj->dbus_mgr,
     G_DBUS_OBJECT_SKELETON (std_lx_mp_path_obj));

  std_lx_mp_path_update (std_lx_mp_path_obj->std_lx_mp_path, mp_path);

  g_free ((gpointer) mp_path_obj_path);

  /* When new multipath was created, its slave disks block will not be
   * trigger with udev event. Manually do so.
   */

  /* TODO(Gris Ge): Create a object path generate in
   * storagedlinuxblockobject.h
   */
  std_lx_blk_obj = storaged_linux_block_object_get
    (G_DBUS_OBJECT_MANAGER (std_lx_mp_path_obj->dbus_mgr),
     blk_name);
  if (std_lx_blk_obj == NULL)
    goto out;

  storaged_linux_block_object_uevent
    (std_lx_blk_obj, MP_MODULE_UDEV_ACTION_ADD,
     storaged_linux_block_object_get_device (std_lx_blk_obj));

out:
  if (std_lx_blk_obj != NULL)
    g_object_unref (std_lx_blk_obj);

  return std_lx_mp_path_obj;
}

gboolean
std_lx_mp_path_obj_update
    (StoragedLinuxMultipathPathGroupPathObject *std_lx_mp_path_obj,
     struct dmmp_path *mp_path)
{
  g_assert (std_lx_mp_path_obj != NULL);
  if (mp_path == NULL)
    {
      g_dbus_object_skeleton_remove_interface
          (G_DBUS_OBJECT_SKELETON (std_lx_mp_path_obj),
           G_DBUS_INTERFACE_SKELETON (std_lx_mp_path_obj->std_lx_mp_path));
      g_dbus_object_manager_server_unexport
        (std_lx_mp_path_obj->dbus_mgr,
         g_dbus_object_get_object_path ((GDBusObject *) (std_lx_mp_path_obj)));
      g_object_unref (std_lx_mp_path_obj->std_lx_mp_path);
      std_lx_mp_path_obj->std_lx_mp_path = NULL;
      return TRUE;
    }

  return std_lx_mp_path_update (std_lx_mp_path_obj->std_lx_mp_path, mp_path);
}

void
std_lx_mp_path_obj_set_block
  (StoragedLinuxMultipathPathGroupPathObject *std_lx_mp_path_obj,
   const char *blk_obj_path)
{
  storaged_debug ("std_lx_mp_path_obj_set_block()");
  storaged_multipath_path_group_path_set_block
    (STORAGED_MULTIPATH_PATH_GROUP_PATH (std_lx_mp_path_obj->std_lx_mp_path),
     blk_obj_path);
}

const char *
std_lx_mp_path_obj_path_gen (const char *mp_pg_obj_path, const char *blk_name)
{
  if ((mp_pg_obj_path == NULL) || (blk_name == NULL))
    return NULL;
  return g_strdup_printf ("%s/path_%s", mp_pg_obj_path, blk_name);
}

StoragedLinuxMultipathPathGroupPathObject *
std_lx_mp_path_obj_get (GDBusObjectManager *dbus_mgr,
                        const char *mp_path_obj_path)
{
  GDBusObject *dbus_obj = NULL;

  dbus_obj = g_dbus_object_manager_get_object (dbus_mgr, mp_path_obj_path);
  if (dbus_obj == NULL)
    return NULL;

  return STORAGED_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT
    (G_DBUS_OBJECT_SKELETON (dbus_obj));
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
