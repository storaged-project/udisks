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

typedef struct _StoragedLinuxMultipathObjectClass
StoragedLinuxMultipathObjectClass;

struct _StoragedLinuxMultipathObject
{
  StoragedObjectSkeleton parent_instance;
  StoragedLinuxMultipath *std_lx_mp;
  GDBusObjectManagerServer *dbus_mgr;
  GHashTable *pg_obj_hash;
};

struct _StoragedLinuxMultipathObjectClass
{
  StoragedObjectSkeletonClass parent_class;
};

static void
std_lx_mp_obj_iface_init (StoragedModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (StoragedLinuxMultipathObject, std_lx_mp_obj,
   STORAGED_TYPE_OBJECT_SKELETON,
   G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MODULE_OBJECT,
                          std_lx_mp_obj_iface_init));

static void
std_lx_mp_obj_finalize (GObject *object)
{
  StoragedLinuxMultipathObject *std_lx_mp_obj = STORAGED_LINUX_MULTIPATH_OBJECT
    (object);

  storaged_debug ("Multipath: std_lx_mp_obj_finalize ()");

  if (std_lx_mp_obj->std_lx_mp != NULL)
    g_object_unref (std_lx_mp_obj->std_lx_mp);

  if (std_lx_mp_obj->pg_obj_hash != NULL)
    g_hash_table_unref (std_lx_mp_obj->pg_obj_hash);

  if (G_OBJECT_CLASS (std_lx_mp_obj_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (std_lx_mp_obj_parent_class)->finalize (object);
}

static void
std_lx_mp_obj_init (StoragedLinuxMultipathObject *std_lx_mp_obj)
{
  storaged_debug ("Multipath: std_lx_mp_obj_init");
  std_lx_mp_obj->std_lx_mp = NULL;
  std_lx_mp_obj->dbus_mgr = NULL;
  std_lx_mp_obj->pg_obj_hash =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  return;
}

static void
std_lx_mp_obj_class_init (StoragedLinuxMultipathObjectClass *class)
{
  GObjectClass *gobject_class;

  storaged_debug ("Multipath: std_lx_mp_obj_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = std_lx_mp_obj_finalize;
}

static void
std_lx_mp_obj_iface_init (StoragedModuleObjectIface *iface)
{
  storaged_debug ("Multipath: std_lx_mp_obj_iface_init");
}

StoragedLinuxMultipathObject *
std_lx_mp_obj_new (GDBusObjectManagerServer *dbus_mgr,
                   struct dmmp_mpath *mpath)
{
  const char *mp_name = NULL;
  const char *wwid = NULL;
  const char *mp_obj_path = NULL;
  StoragedLinuxMultipath *std_lx_mp = NULL;
  StoragedLinuxMultipathObject *std_lx_mp_obj = NULL;
  StoragedLinuxMultipathPathGroupObject *std_lx_mp_pg_obj = NULL;
  struct dmmp_path_group **dmmp_pgs = NULL;
  uint32_t dmmp_pg_id = 0;
  uint32_t dmmp_pg_count = 0;
  uint32_t i = 0;

  storaged_debug ("Multipath: std_lx_mp_obj_new");

  mp_name = dmmp_mpath_name_get (mpath);
  wwid = dmmp_mpath_wwid_get (mpath);

  if ((mp_name == NULL) || (wwid == NULL))
    return NULL;

  std_lx_mp = std_lx_mp_new (mpath);

  std_lx_mp_obj = g_object_new (STORAGED_TYPE_LINUX_MULTIPATH_OBJECT, NULL);

  std_lx_mp_obj->std_lx_mp = std_lx_mp;
  std_lx_mp_obj->dbus_mgr = dbus_mgr;

  mp_obj_path = std_lx_mp_obj_path_gen(mp_name, wwid);

  g_dbus_object_skeleton_set_object_path
    (G_DBUS_OBJECT_SKELETON (std_lx_mp_obj), mp_obj_path);

  g_dbus_object_skeleton_add_interface
      (G_DBUS_OBJECT_SKELETON (std_lx_mp_obj),
       G_DBUS_INTERFACE_SKELETON (std_lx_mp_obj->std_lx_mp));
  g_dbus_object_manager_server_export (std_lx_mp_obj->dbus_mgr,
                                       G_DBUS_OBJECT_SKELETON (std_lx_mp_obj));

  /* Genreate org.storaged.Storaged.Multipath.PathGroup dbus object  */
  dmmp_path_group_array_get(mpath, &dmmp_pgs, &dmmp_pg_count);

  for (i = 0; i < dmmp_pg_count; ++i)
    {
      dmmp_pg_id = dmmp_path_group_id_get (dmmp_pgs[i]);
      if (dmmp_pg_id == 0)
        continue;

      std_lx_mp_pg_obj = std_lx_mp_pg_obj_new (dbus_mgr, dmmp_pgs[i],
                                               mp_obj_path);
      g_hash_table_insert (std_lx_mp_obj->pg_obj_hash,
                           g_strdup_printf("%" PRIu32 "", dmmp_pg_id),
                           std_lx_mp_pg_obj);
    }

  g_free ((gpointer) mp_obj_path);

  return std_lx_mp_obj;
}

gboolean
std_lx_mp_obj_update (StoragedLinuxMultipathObject *std_lx_mp_obj,
                      struct dmmp_mpath *mpath)
{
  StoragedLinuxMultipathPathGroupObject *std_lx_mp_pg_obj = NULL;
  GHashTableIter iter;
  gpointer key, value;
  struct dmmp_path_group **dmmp_pgs = NULL;
  uint32_t i = 0;
  uint32_t dmmp_pg_id = 0;
  uint32_t dmmp_pg_count = 0;
  char *pg_id_str = NULL;
  const gchar *mp_obj_path = NULL;

  g_assert (std_lx_mp_obj != NULL);

  if (mpath == NULL)
    {
      g_dbus_object_skeleton_remove_interface
          (G_DBUS_OBJECT_SKELETON (std_lx_mp_obj),
           G_DBUS_INTERFACE_SKELETON (std_lx_mp_obj->std_lx_mp));
      g_dbus_object_manager_server_unexport
        (std_lx_mp_obj->dbus_mgr,
         g_dbus_object_get_object_path ((GDBusObject *) (std_lx_mp_obj)));
      g_object_unref (std_lx_mp_obj->std_lx_mp);
      std_lx_mp_obj->std_lx_mp = NULL;

      g_hash_table_iter_init (&iter, std_lx_mp_obj->pg_obj_hash);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          std_lx_mp_pg_obj =
            STORAGED_LINUX_MULTIPATH_PATH_GROUP_OBJECT (value);
          std_lx_mp_pg_obj_update (std_lx_mp_pg_obj, NULL);
        }
      g_hash_table_unref (std_lx_mp_obj->pg_obj_hash);
      std_lx_mp_obj->pg_obj_hash = NULL;
      return TRUE;
    }

  mp_obj_path = g_dbus_object_get_object_path ((GDBusObject *)
                                               (std_lx_mp_obj));

  dmmp_path_group_array_get (mpath, &dmmp_pgs, &dmmp_pg_count);

  for (i = 0; i < dmmp_pg_count; ++i)
    {
      dmmp_pg_id = dmmp_path_group_id_get (dmmp_pgs[i]);
      if (dmmp_pg_id == 0)
        continue;
      pg_id_str = g_strdup_printf ("%" PRIu32 "", dmmp_pg_id);

      std_lx_mp_pg_obj = g_hash_table_lookup (std_lx_mp_obj->pg_obj_hash,
                                              pg_id_str);
      g_free (pg_id_str);

      if (std_lx_mp_obj == NULL)
        {
          /* New PathGroup created */
          std_lx_mp_pg_obj = std_lx_mp_pg_obj_new (std_lx_mp_obj->dbus_mgr,
                                                   dmmp_pgs[i], mp_obj_path);
          g_hash_table_insert (std_lx_mp_obj->pg_obj_hash,
                               g_strdup_printf("%" PRIu32 "", dmmp_pg_id),
                               std_lx_mp_pg_obj);
        }
      else
        {
          /* Exist PathGroup found. Update properties */
          std_lx_mp_pg_obj_update (std_lx_mp_pg_obj, dmmp_pgs[i]);
        }

      /* TODO(Gris Ge): Handle PathGroup deletion */
    }


  return std_lx_mp_update (std_lx_mp_obj->std_lx_mp, mpath);
}

void std_lx_mp_obj_set_block (StoragedLinuxMultipathObject *std_lx_mp_obj,
                              const char *blk_obj_path)
{
  storaged_debug ("std_lx_mp_obj_set_block() %s", blk_obj_path);
  storaged_multipath_set_block
    (STORAGED_MULTIPATH (std_lx_mp_obj->std_lx_mp), blk_obj_path);
}

const char *
std_lx_mp_obj_path_gen (const char *mp_name, const char *wwid)
{
  if ((mp_name == NULL) || (wwid == NULL))
    return NULL;

  return g_strdup_printf ("/org/storaged/Storaged/Multipath/%s_%s",
                          mp_name, wwid);
}

StoragedLinuxMultipathObject *
std_lx_mp_obj_get (GDBusObjectManager *dbus_mgr, const char *mp_obj_path)
{
  GDBusObject *dbus_obj = NULL;

  dbus_obj = g_dbus_object_manager_get_object (dbus_mgr, mp_obj_path);
  if (dbus_obj == NULL)
    return NULL;

  return STORAGED_LINUX_MULTIPATH_OBJECT (G_DBUS_OBJECT_SKELETON (dbus_obj));
}

StoragedLinuxMultipathPathGroupObject *
std_lx_mp_pg_obj_search (StoragedLinuxMultipathObject *std_lx_mp_obj,
                         uint32_t pg_id)
{
  gchar *pg_id_str = NULL;
  gpointer *tmp_obj = NULL;

  if ((std_lx_mp_obj == NULL) ||
      (! STORAGED_IS_LINUX_MULTIPATH_OBJECT (std_lx_mp_obj)) ||
      (pg_id == 0) ||
      (std_lx_mp_obj->pg_obj_hash == NULL))
    return NULL;

  pg_id_str = g_strdup_printf ("%" PRIu32 "", pg_id);

  tmp_obj = g_hash_table_lookup(std_lx_mp_obj->pg_obj_hash, pg_id_str);

  g_free (pg_id_str);

  if (tmp_obj == NULL)
    return NULL;

  return STORAGED_LINUX_MULTIPATH_PATH_GROUP_OBJECT (tmp_obj);
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
