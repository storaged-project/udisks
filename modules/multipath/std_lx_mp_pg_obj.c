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

typedef struct _StoragedLinuxMultipathPathGroupObjectClass
StoragedLinuxMultipathPathGroupObjectClass;

struct _StoragedLinuxMultipathPathGroupObject
{
  StoragedObjectSkeleton parent_instance;
  StoragedLinuxMultipathPathGroup *std_lx_mp_pg;
  GDBusObjectManagerServer *dbus_mgr;
  GHashTable *path_obj_hash;
};

struct _StoragedLinuxMultipathPathGroupObjectClass
{
  StoragedObjectSkeletonClass parent_class;
};

static void
std_lx_mp_pg_obj_iface_init (StoragedModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (StoragedLinuxMultipathPathGroupObject, std_lx_mp_pg_obj,
   STORAGED_TYPE_OBJECT_SKELETON,
   G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MODULE_OBJECT,
                          std_lx_mp_pg_obj_iface_init));

static void
std_lx_mp_pg_obj_finalize (GObject *object)
{
  StoragedLinuxMultipathPathGroupObject *std_lx_mp_pg_obj =
    STORAGED_LINUX_MULTIPATH_PATH_GROUP_OBJECT (object);

  storaged_debug ("Multipath: std_lx_mp_pg_obj_finalize ()");

  if (std_lx_mp_pg_obj->std_lx_mp_pg != NULL)
    g_object_unref (std_lx_mp_pg_obj->std_lx_mp_pg);

  if (std_lx_mp_pg_obj->path_obj_hash != NULL)
    g_hash_table_unref (std_lx_mp_pg_obj->path_obj_hash);

  if (G_OBJECT_CLASS (std_lx_mp_pg_obj_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (std_lx_mp_pg_obj_parent_class)->finalize (object);
}

static void
std_lx_mp_pg_obj_init (StoragedLinuxMultipathPathGroupObject *std_lx_mp_pg_obj)
{
  storaged_debug ("Multipath: std_lx_mp_pg_obj_init");
  std_lx_mp_pg_obj->dbus_mgr = NULL;
  std_lx_mp_pg_obj->std_lx_mp_pg = NULL;
  std_lx_mp_pg_obj->path_obj_hash =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  return;
}

static void
std_lx_mp_pg_obj_class_init (StoragedLinuxMultipathPathGroupObjectClass *class)
{
  GObjectClass *gobject_class;

  storaged_debug ("Multipath: std_lx_mp_pg_obj_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = std_lx_mp_pg_obj_finalize;
}

static void
std_lx_mp_pg_obj_iface_init (StoragedModuleObjectIface *iface)
{
  storaged_debug ("Multipath: std_lx_mp_pg_obj_iface_init");
}

StoragedLinuxMultipathPathGroupObject *
std_lx_mp_pg_obj_new (GDBusObjectManagerServer *dbus_mgr,
                      struct dmmp_path_group *mp_pg, const char *mp_obj_path)
{
  const char *mp_pg_obj_path = NULL;
  StoragedLinuxMultipathPathGroup *std_lx_mp_pg = NULL;
  StoragedLinuxMultipathPathGroupObject *std_lx_mp_pg_obj = NULL;
  StoragedLinuxMultipathPathGroupPathObject *std_lx_mp_path_obj = NULL;
  struct dmmp_path **mp_paths = NULL;
  uint32_t mp_path_count = 0;
  uint32_t i = 0;
  const char *blk_name = NULL;
  uint32_t pg_id = 0;

  storaged_debug ("Multipath: std_lx_mp_pg_obj_new");

  pg_id = dmmp_path_group_id_get (mp_pg);
  if (pg_id == 0)
    return NULL;

  std_lx_mp_pg = std_lx_mp_pg_new (mp_pg);

  std_lx_mp_pg_obj =
    g_object_new (STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_OBJECT, NULL);

  std_lx_mp_pg_obj->std_lx_mp_pg = std_lx_mp_pg;
  std_lx_mp_pg_obj->dbus_mgr = dbus_mgr;

  mp_pg_obj_path = std_lx_mp_pg_obj_path_gen (mp_obj_path, pg_id);

  g_dbus_object_skeleton_set_object_path
    (G_DBUS_OBJECT_SKELETON (std_lx_mp_pg_obj), mp_pg_obj_path);

  g_dbus_object_skeleton_add_interface
      (G_DBUS_OBJECT_SKELETON (std_lx_mp_pg_obj),
       G_DBUS_INTERFACE_SKELETON (std_lx_mp_pg_obj->std_lx_mp_pg));
  g_dbus_object_manager_server_export
    (std_lx_mp_pg_obj->dbus_mgr, G_DBUS_OBJECT_SKELETON (std_lx_mp_pg_obj));

  dmmp_path_array_get (mp_pg, &mp_paths, &mp_path_count);

  for (i = 0; i < mp_path_count; ++i)
    {
      blk_name = dmmp_path_blk_name_get (mp_paths[i]);
      if (blk_name == NULL)
        continue;
      std_lx_mp_path_obj =
        std_lx_mp_path_obj_new(dbus_mgr, mp_paths[i], mp_pg_obj_path);

      g_hash_table_insert(std_lx_mp_pg_obj->path_obj_hash,
                          g_strdup (blk_name), std_lx_mp_path_obj);
    }

  g_free ((gpointer) mp_pg_obj_path);

  return std_lx_mp_pg_obj;
}

gboolean
std_lx_mp_pg_obj_update
  (StoragedLinuxMultipathPathGroupObject *std_lx_mp_pg_obj,
   struct dmmp_path_group *mp_pg)
{
  StoragedLinuxMultipathPathGroupPathObject *std_lx_mp_path_obj = NULL;
  GHashTableIter iter;
  gpointer key, value;
  struct dmmp_path **mp_paths = NULL;
  uint32_t mp_path_count = 0;
  uint32_t i = 0;
  const gchar *mp_pg_obj_path = NULL;
  const char *blk_name = NULL;

  g_assert (std_lx_mp_pg_obj != NULL);
  if (mp_pg == NULL)
    {
      g_dbus_object_skeleton_remove_interface
          (G_DBUS_OBJECT_SKELETON (std_lx_mp_pg_obj),
           G_DBUS_INTERFACE_SKELETON (std_lx_mp_pg_obj->std_lx_mp_pg));
      g_dbus_object_manager_server_unexport
        (std_lx_mp_pg_obj->dbus_mgr,
         g_dbus_object_get_object_path ((GDBusObject *) (std_lx_mp_pg_obj)));
      g_object_unref (std_lx_mp_pg_obj->std_lx_mp_pg);
      std_lx_mp_pg_obj->std_lx_mp_pg = NULL;

      g_hash_table_iter_init (&iter, std_lx_mp_pg_obj->path_obj_hash);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          std_lx_mp_path_obj =
            STORAGED_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT (value);
          std_lx_mp_path_obj_update (std_lx_mp_path_obj, NULL);
        }
      g_hash_table_unref (std_lx_mp_pg_obj->path_obj_hash);
      std_lx_mp_pg_obj->path_obj_hash = NULL;

      return TRUE;
    }

  mp_pg_obj_path = g_dbus_object_get_object_path
    ((GDBusObject *) (std_lx_mp_pg_obj));

  dmmp_path_array_get (mp_pg, &mp_paths, &mp_path_count);

  for (i = 0; i < mp_path_count; ++i)
    {
      blk_name = dmmp_path_blk_name_get (mp_paths[i]);
      if (blk_name == NULL)
        continue;

      std_lx_mp_path_obj = g_hash_table_lookup
        (std_lx_mp_pg_obj->path_obj_hash, blk_name);

      if (std_lx_mp_path_obj == NULL)
        {
          /* New path added */
          std_lx_mp_path_obj =
            std_lx_mp_path_obj_new(std_lx_mp_pg_obj->dbus_mgr, mp_paths[i],
                                   mp_pg_obj_path);

          g_hash_table_insert(std_lx_mp_pg_obj->path_obj_hash,
                              g_strdup (blk_name), std_lx_mp_path_obj);
        }
      else
        {
          /* Update existing path */
          std_lx_mp_path_obj_update (std_lx_mp_path_obj, mp_paths[i]);
        }
      /* TODO(Gris Ge): Handle path deletion */
    }

  return std_lx_mp_pg_update (std_lx_mp_pg_obj->std_lx_mp_pg, mp_pg);
}

const char *
std_lx_mp_pg_obj_path_gen (const char *mp_obj_path, uint32_t pg_id)
{
  if ((mp_obj_path == NULL) || (pg_id == 0))
    return NULL;
  return g_strdup_printf ("%s/path_group_%" PRIu32 "", mp_obj_path, pg_id);
}

StoragedLinuxMultipathPathGroupPathObject *
std_lx_mp_path_obj_search (StoragedLinuxMultipathPathGroupObject
                           *std_lx_mp_pg_obj, const char *blk_name)
{
  gpointer *tmp_obj = NULL;

  if ((std_lx_mp_pg_obj == NULL) ||
      (! STORAGED_IS_LINUX_MULTIPATH_PATH_GROUP_OBJECT (std_lx_mp_pg_obj)) ||
      (blk_name == NULL) ||
      (std_lx_mp_pg_obj->path_obj_hash == NULL))
    return NULL;

  tmp_obj = g_hash_table_lookup(std_lx_mp_pg_obj->path_obj_hash, blk_name);

  if (tmp_obj == NULL)
    return NULL;

  return STORAGED_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT (tmp_obj);
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
