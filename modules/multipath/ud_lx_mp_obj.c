/*
 * Copyright (C) 2015 - 2017 Gris Ge <fge@redhat.com>
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
 *      org.freedesktop.UDisks2.Multipath
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

#include <src/udiskslogging.h>
#include <src/udiskslinuxdriveobject.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udisksbasejob.h>
#include <src/udiskssimplejob.h>
#include <src/udisksthreadedjob.h>
#include <src/udiskslinuxdevice.h>
#include <modules/udisksmoduleobject.h>

#include "mp_types.h"
#include "mp_generated.h"

#define __OLD_PATH                  "old"
#define __NEW_PATH                  "new"

typedef struct _UDisksLinuxMultipathObjectClass
UDisksLinuxMultipathObjectClass;

struct _UDisksLinuxMultipathObject
{
  UDisksObjectSkeleton parent_instance;
  UDisksLinuxMultipath *ud_lx_mp;
  GDBusObjectManagerServer *dbus_mgr;
  GHashTable *path_hash;
  const gchar *mp_obj_path;
  const gchar *blk_obj_path;
  const gchar *drv_obj_path;
};

struct _UDisksLinuxMultipathObjectClass
{
  UDisksObjectSkeletonClass parent_class;
};

static void
ud_lx_mp_obj_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (UDisksLinuxMultipathObject, ud_lx_mp_obj,
   UDISKS_TYPE_OBJECT_SKELETON,
   G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT,
                          ud_lx_mp_obj_iface_init));

static void
ud_lx_mp_obj_finalize (GObject *object)
{
  UDisksLinuxMultipathObject *ud_lx_mp_obj = NULL;

  udisks_debug ("Multipath: ud_lx_mp_obj_finalize()");

  if ((object != NULL) && (UDISKS_IS_LINUX_MULTIPATH_OBJECT (object)))
    {
      ud_lx_mp_obj = UDISKS_LINUX_MULTIPATH_OBJECT (object);
      udisks_debug ("Multipath: ud_lx_mp_obj_finalize(): %s",
                    ud_lx_mp_obj->mp_obj_path);

      if (ud_lx_mp_obj->ud_lx_mp != NULL)
        g_object_unref (ud_lx_mp_obj->ud_lx_mp);

      if (ud_lx_mp_obj->path_hash != NULL)
        g_hash_table_unref (ud_lx_mp_obj->path_hash);

      g_free ((gpointer) ud_lx_mp_obj->mp_obj_path);
      g_free ((gpointer) ud_lx_mp_obj->blk_obj_path);
      g_free ((gpointer) ud_lx_mp_obj->drv_obj_path);
    }

  if (G_OBJECT_CLASS (ud_lx_mp_obj_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (ud_lx_mp_obj_parent_class)->finalize (object);
}

static void
ud_lx_mp_obj_init (UDisksLinuxMultipathObject *ud_lx_mp_obj)
{
  udisks_debug ("Multipath: ud_lx_mp_obj_init()");
  ud_lx_mp_obj->ud_lx_mp = NULL;
  ud_lx_mp_obj->dbus_mgr = NULL;
  ud_lx_mp_obj->path_hash = NULL;
  ud_lx_mp_obj->mp_obj_path = NULL;
  ud_lx_mp_obj->blk_obj_path = NULL;
  ud_lx_mp_obj->drv_obj_path = NULL;
  return;
}

static void
ud_lx_mp_obj_class_init (UDisksLinuxMultipathObjectClass *class)
{
  GObjectClass *gobject_class;

  udisks_debug ("Multipath: ud_lx_mp_obj_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = ud_lx_mp_obj_finalize;
}

static void
ud_lx_mp_obj_iface_init (UDisksModuleObjectIface *iface)
{
  udisks_debug ("Multipath: ud_lx_mp_obj_iface_init");
}

UDisksLinuxMultipathObject *
ud_lx_mp_obj_new (GDBusObjectManagerServer *dbus_mgr,
                  struct dmmp_mpath *mpath)
{
  const char *mp_name = NULL;
  const char *wwid = NULL;
  const gchar *mp_obj_path = NULL;
  UDisksLinuxMultipath *ud_lx_mp = NULL;
  UDisksLinuxMultipathObject *ud_lx_mp_obj = NULL;

  udisks_debug ("Multipath: ud_lx_mp_obj_new()");

  if (mpath == NULL)
    /* We should never got NULL mpath, just in case of future clumsy patches */
    return FALSE;

  mp_name = dmmp_mpath_name_get (mpath);
  wwid = dmmp_mpath_wwid_get (mpath);

  if ((mp_name == NULL) || (wwid == NULL))
    return NULL;

  mp_obj_path = ud_lx_mp_obj_path_gen(mp_name, wwid);
  ud_lx_mp = ud_lx_mp_new (mpath, mp_obj_path);

  ud_lx_mp_obj = g_object_new (UDISKS_TYPE_LINUX_MULTIPATH_OBJECT, NULL);

  ud_lx_mp_obj->ud_lx_mp = ud_lx_mp;
  ud_lx_mp_obj->dbus_mgr = dbus_mgr;
  ud_lx_mp_obj->mp_obj_path = mp_obj_path;
  ud_lx_mp_obj->path_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, NULL);

  g_dbus_object_skeleton_set_object_path
    (G_DBUS_OBJECT_SKELETON (ud_lx_mp_obj), mp_obj_path);

  g_dbus_object_skeleton_add_interface
      (G_DBUS_OBJECT_SKELETON (ud_lx_mp_obj),
       G_DBUS_INTERFACE_SKELETON (ud_lx_mp_obj->ud_lx_mp));
  g_dbus_object_manager_server_export (ud_lx_mp_obj->dbus_mgr,
                                       G_DBUS_OBJECT_SKELETON (ud_lx_mp_obj));

  udisks_debug ("Multipath: ud_lx_mp_obj_new(): Exporting %s", mp_obj_path);

  ud_lx_mp_obj_update (ud_lx_mp_obj, mpath);

  return ud_lx_mp_obj;
}

gboolean
ud_lx_mp_obj_update (UDisksLinuxMultipathObject *ud_lx_mp_obj,
                     struct dmmp_mpath *mpath)
{
  UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj = NULL;
  struct dmmp_path_group **pgs = NULL;
  uint32_t i = 0;
  uint32_t j = 0;
  uint32_t pg_count = 0;
  struct dmmp_path **ps = NULL;
  uint32_t p_count = 0;
  const char *blk_name = NULL;
  GHashTableIter iter;
  gpointer key, value;
  const char *mp_path_obj_path = NULL;
  UDisksLinuxDevice *ud_lx_dev = NULL;
  UDisksLinuxBlockObject *ud_lx_blk_obj = NULL;
  const gchar *blk_obj_path = NULL;
  GDBusObject *obj = NULL;

  if (ud_lx_mp_obj == NULL)
    /* We should never got NULL mpath, just in case of future clumsy patches */
    return FALSE;

  if (mpath == NULL)
    /* We should never got NULL mpath, just in case of future clumsy patches */
    return FALSE;

  udisks_debug ("Multipath: ud_lx_mp_obj_update(): %s",
                dmmp_mpath_name_get (mpath));

  dmmp_path_group_array_get (mpath, &pgs, &pg_count);

  g_hash_table_iter_init (&iter, ud_lx_mp_obj->path_hash);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      udisks_debug ("Multipath: ud_lx_mp_obj_update(): Old path %s",
                    (gchar *) key);
      g_hash_table_replace (ud_lx_mp_obj->path_hash,
                            g_strdup ((gchar *) key), (gpointer) __OLD_PATH);
    }

  for (i = 0; i < pg_count; ++i)
    {
      dmmp_path_array_get (pgs[i], &ps, &p_count);
      for (j = 0; j < p_count; ++j)
        {
          blk_name = dmmp_path_blk_name_get (ps[j]);
          if (blk_name == NULL)
            continue;

          mp_path_obj_path = ud_lx_mp_path_obj_path_gen
            (ud_lx_mp_obj->mp_obj_path, blk_name);


          ud_lx_mp_path_obj = ud_lx_mp_path_obj_get
            (ud_lx_mp_obj->dbus_mgr, mp_path_obj_path);
          if (ud_lx_mp_path_obj == NULL)
            {
              udisks_debug ("Multipath: ud_lx_mp_obj_update(): Create %s",
                            mp_path_obj_path);
              /* New Path created */
              ud_lx_mp_path_obj = ud_lx_mp_path_obj_new
                (ud_lx_mp_obj->dbus_mgr, ps[j], ud_lx_mp_obj->mp_obj_path);
            }
          else
            {
              udisks_debug ("Multipath: ud_lx_mp_obj_update(): Updating %s",
                            mp_path_obj_path);
              /* Exist Path found. Update properties */
              ud_lx_mp_path_obj_update (ud_lx_mp_path_obj, ps[j],
                                        ud_lx_mp_obj->mp_obj_path);
            }
          g_free ((gpointer) mp_path_obj_path);
          /* dbus object manager toke a reference of path object */
          g_object_unref (ud_lx_mp_path_obj);
          g_hash_table_replace (ud_lx_mp_obj->path_hash,
                                g_strdup (blk_name), (gpointer) __NEW_PATH);
        }
    }

  /* Handle removed path */
  g_hash_table_iter_init (&iter, ud_lx_mp_obj->path_hash);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      udisks_debug ("Multipath: ud_lx_mp_obj_update(): path %s",
                    (gchar *) key);
      if (g_strcmp0 ((gchar *) value, __OLD_PATH) == 0)
        {
          mp_path_obj_path = ud_lx_mp_path_obj_path_gen
            (ud_lx_mp_obj->mp_obj_path, (gchar *) key);
          udisks_debug ("Multipath: ud_lx_mp_obj_update(): Unexporting %s",
                        mp_path_obj_path);
          ud_lx_mp_path_obj_unexport (ud_lx_mp_obj->dbus_mgr,
                                      mp_path_obj_path);
          g_free ((gpointer) mp_path_obj_path);
          g_hash_table_iter_remove (&iter);
        }
    }

  /* When new mpath was creating(every time it add a path into mpath, it
   * trigger a "change" uevent on dm-XX), the block sdX will not get any uevent
   * which caused the missing org.freedesktop.UDisks2.Block.Multipath interface
   * on sdX blocks. We manually trigger so.
   */
  g_hash_table_iter_init (&iter, ud_lx_mp_obj->path_hash);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      blk_obj_path = udisks_linux_block_object_path_gen ((gchar *) key);
      obj = g_dbus_object_manager_get_object
        (G_DBUS_OBJECT_MANAGER (ud_lx_mp_obj->dbus_mgr), blk_obj_path);
      g_free ((gchar *) blk_obj_path);

      if (obj == NULL)
        continue;

      if (UDISKS_IS_LINUX_MULTIPATH_PATH_OBJECT (G_DBUS_OBJECT_SKELETON (obj)))
        continue;

      ud_lx_blk_obj = UDISKS_LINUX_BLOCK_OBJECT
        (G_DBUS_OBJECT_SKELETON (obj));

      ud_lx_dev = udisks_linux_block_object_get_device (ud_lx_blk_obj);
      if (ud_lx_dev != NULL)
        {
          udisks_linux_block_object_uevent
            (ud_lx_blk_obj, MP_MODULE_UDEV_ACTION_ADD, ud_lx_dev);
          g_object_unref (ud_lx_dev);
        }
      g_object_unref (ud_lx_blk_obj);
    }

  return ud_lx_mp_update (ud_lx_mp_obj->ud_lx_mp, mpath,
                          ud_lx_mp_obj->mp_obj_path);
}

void ud_lx_mp_obj_set_block (UDisksLinuxMultipathObject *ud_lx_mp_obj,
                             const gchar *blk_obj_path)
{
  udisks_debug ("ud_lx_mp_obj_set_block() %s", blk_obj_path);
  ud_lx_mp_obj->blk_obj_path = g_strdup (blk_obj_path);
  udisks_multipath_set_block
    (UDISKS_MULTIPATH (ud_lx_mp_obj->ud_lx_mp), ud_lx_mp_obj->blk_obj_path);
}

void ud_lx_mp_obj_set_drive (UDisksLinuxMultipathObject *ud_lx_mp_obj,
                             const gchar *drv_obj_path)
{
  udisks_debug ("ud_lx_mp_obj_set_drive() %s", drv_obj_path);
  ud_lx_mp_obj->drv_obj_path = g_strdup (drv_obj_path);
  udisks_multipath_set_drive
    (UDISKS_MULTIPATH (ud_lx_mp_obj->ud_lx_mp), ud_lx_mp_obj->drv_obj_path);
}

const gchar *
ud_lx_mp_obj_path_gen (const char *mp_name, const char *wwid)
{
  if ((mp_name == NULL) || (wwid == NULL))
    return NULL;

  return g_strdup_printf ("/org/freedesktop/UDisks2/Multipath/%s_%s",
                          mp_name, wwid);
}

UDisksLinuxMultipathObject *
ud_lx_mp_obj_get (GDBusObjectManagerServer *dbus_mgr, const gchar *mp_obj_path)
{
  GDBusObject *dbus_obj = NULL;

  dbus_obj = g_dbus_object_manager_get_object
    (G_DBUS_OBJECT_MANAGER (dbus_mgr), mp_obj_path);
  if (dbus_obj == NULL)
    return NULL;

  return UDISKS_LINUX_MULTIPATH_OBJECT (G_DBUS_OBJECT_SKELETON (dbus_obj));
}

void
ud_lx_mp_obj_unexport (GDBusObjectManagerServer *dbus_mgr,
                       const gchar *mp_obj_path)
{
  UDisksLinuxMultipathObject *ud_lx_mp_obj = NULL;
  GHashTableIter iter;
  gpointer key, value;
  const gchar *mp_path_obj_path = NULL;

  ud_lx_mp_obj = ud_lx_mp_obj_get (dbus_mgr, mp_obj_path);

  if (ud_lx_mp_obj == NULL)
    return;

  g_hash_table_iter_init (&iter, ud_lx_mp_obj->path_hash);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (key != NULL)
        {
          mp_path_obj_path = ud_lx_mp_path_obj_path_gen (mp_obj_path,
                                                         (gchar *) key);
          g_dbus_object_manager_server_unexport (dbus_mgr,
                                                 mp_path_obj_path);
          udisks_debug ("Multipath: ud_lx_mp_obj_unexport(): "
                        "unexported %s", mp_path_obj_path);
          g_free ((gpointer) mp_path_obj_path);
        }
    }
  g_object_unref (ud_lx_mp_obj);

  g_dbus_object_manager_server_unexport (dbus_mgr, mp_obj_path);
}
