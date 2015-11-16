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

/* Note: This is inspired by modules/dummy/dummylinuxblock.c  */

#include <sys/types.h>

#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <inttypes.h>
#include <libdmmp/libdmmp.h>

#include <src/storagedlogging.h>
#include <src/storagedlinuxblockobject.h>
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

typedef struct _StoragedLinuxBlockMultipathClass
StoragedLinuxBlockMultipathClass;

struct _StoragedLinuxBlockMultipath
{
  StoragedBlockMultipathSkeleton parent_instance;
  gboolean is_inited;

};

struct _StoragedLinuxBlockMultipathClass
{
  StoragedBlockMultipathSkeletonClass parent_class;
};

static uint32_t _dmmp_pg_id_of_path (struct dmmp_mpath *dmmp_mp,
                                     const char *blk_name);

static void
std_lx_blk_mp_iface_init (StoragedBlockMultipathIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (StoragedLinuxBlockMultipath, std_lx_blk_mp,
   STORAGED_TYPE_BLOCK_MULTIPATH_SKELETON,
   G_IMPLEMENT_INTERFACE (STORAGED_TYPE_BLOCK_MULTIPATH,
                          std_lx_blk_mp_iface_init));

static void
std_lx_blk_mp_finalize (GObject *object)
{
  storaged_debug ("Multipath: std_lx_blk_mp_finalize ()");

  if (G_OBJECT_CLASS (std_lx_blk_mp_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (std_lx_blk_mp_parent_class)->finalize (object);
}


static void
std_lx_blk_mp_init (StoragedLinuxBlockMultipath *std_lx_blk_mp)
{
  storaged_debug ("Multipath: std_lx_blk_mp_init");
  std_lx_blk_mp->is_inited = FALSE;

  return;
}

static void
std_lx_blk_mp_class_init (StoragedLinuxBlockMultipathClass *class)
{
  GObjectClass *gobject_class;

  storaged_debug ("Multipath: std_lx_blk_mp_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = std_lx_blk_mp_finalize;
}

static void
std_lx_blk_mp_iface_init (StoragedBlockMultipathIface *iface)
{
  storaged_debug ("Multipath: std_lx_blk_mp_iface_init");
}

static void
_update_std_lx_blk_mp_dm (StoragedLinuxBlockObject *std_lx_blk_obj,
                          StoragedLinuxBlockMultipath *std_lx_blk_mp,
                          GDBusObjectManager *dbus_mgr,
                          const char *mp_name, const char *wwid,
                          gboolean is_dm, const char *blk_name)
{
  StoragedBlockMultipath *std_blk_mp = NULL;
  StoragedLinuxMultipathObject *std_lx_mp_obj = NULL;
  const gchar *mp_obj_path = NULL;
  const gchar *blk_obj_path = NULL;

  if ((mp_name == NULL) || (wwid == NULL) || (blk_name == NULL))
    goto out;

  std_blk_mp = STORAGED_BLOCK_MULTIPATH (std_lx_blk_mp);

  mp_obj_path = std_lx_mp_obj_path_gen (mp_name, wwid);
  std_lx_mp_obj = std_lx_mp_obj_get (dbus_mgr, mp_obj_path);

  if (std_lx_mp_obj == NULL)
    goto out;

  storaged_block_multipath_set_multipath (std_blk_mp, mp_obj_path);

  std_lx_blk_mp->is_inited = TRUE;

  if (is_dm == FALSE)
    goto out;

  /* At early stage, std_lx_blk_obj might not have object path yet.
   * Becase object path is generated after storaged_linux_block_object_uevent
   */
  blk_obj_path = storaged_linux_block_object_path_gen (blk_name);

  storaged_debug ("blk_obj_path: %s\n", blk_obj_path);
  std_lx_mp_obj_set_block (std_lx_mp_obj, blk_obj_path);

out:
  g_free ((gpointer) mp_obj_path);
  g_free ((gchar *) blk_obj_path);
  if (std_lx_mp_obj != NULL)
    g_object_unref (std_lx_mp_obj);
}


static void
_update_std_lx_blk_mp_slave (StoragedLinuxBlockObject *std_lx_blk_obj,
                             StoragedLinuxBlockMultipath *std_lx_blk_mp,
                             GDBusObjectManager *dbus_mgr,
                             const char *mp_name, const char *wwid,
                             uint32_t pg_id, const char *blk_name)
{
  const gchar *mp_obj_path = NULL;
  const gchar *mp_pg_obj_path = NULL;
  const gchar *mp_path_obj_path = NULL;
  const gchar *blk_obj_path = NULL;
  StoragedBlockMultipath *std_blk_mp = NULL;
  StoragedLinuxMultipathPathGroupPathObject *std_lx_mp_path_obj = NULL;

  if ((mp_name == NULL) || (wwid == NULL) || (pg_id == 0) ||
      (blk_name == NULL))
    goto out;

  std_blk_mp = STORAGED_BLOCK_MULTIPATH (std_lx_blk_mp);

  mp_obj_path = std_lx_mp_obj_path_gen (mp_name, wwid);
  mp_pg_obj_path = std_lx_mp_pg_obj_path_gen (mp_obj_path, pg_id);
  mp_path_obj_path = std_lx_mp_path_obj_path_gen (mp_pg_obj_path, blk_name);

  if (mp_path_obj_path == NULL)
    goto out;

  std_lx_mp_path_obj = std_lx_mp_path_obj_get (dbus_mgr, mp_path_obj_path);

  if (std_lx_mp_path_obj == NULL)
    goto out;

  storaged_block_multipath_set_path (std_blk_mp, mp_path_obj_path);

  /* At early stage, std_lx_blk_obj might not have object path yet.
   * Becase object path is generated after storaged_linux_block_object_uevent
   */
  blk_obj_path = storaged_linux_block_object_path_gen (blk_name);

  std_lx_mp_path_obj_set_block (std_lx_mp_path_obj, blk_obj_path);

out:
  g_free ((gchar *) mp_obj_path);
  g_free ((gchar *) mp_pg_obj_path);
  g_free ((gchar *) mp_path_obj_path);
  g_free ((gchar *) blk_obj_path);
  if (std_lx_mp_path_obj != NULL)
    g_object_unref (std_lx_mp_path_obj);
}

StoragedLinuxBlockMultipath *std_lx_blk_mp_new (void)
{
  storaged_debug ("Multipath: std_lx_blk_mp_new");
  return STORAGED_LINUX_BLOCK_MULTIPATH
    (g_object_new (STORAGED_TYPE_BLOCK_MULTIPATH, NULL));
}

static uint32_t
_dmmp_pg_id_of_path (struct dmmp_mpath *dmmp_mp, const char *blk_name)
{
  uint32_t i = 0;
  uint32_t j = 0;
  struct dmmp_path_group **dmmp_pgs = NULL;
  uint32_t dmmp_pg_count =0;
  struct dmmp_path **dmmp_ps = NULL;
  uint32_t dmmp_p_count =0;

  dmmp_path_group_array_get (dmmp_mp, &dmmp_pgs, &dmmp_pg_count);

  for (; i < dmmp_pg_count; ++i)
    {
      dmmp_path_array_get (dmmp_pgs[i], &dmmp_ps, &dmmp_p_count);
      for (; j < dmmp_p_count; ++j)
        {
          if ( g_strcmp0 (blk_name, dmmp_path_blk_name_get (dmmp_ps[j])) == 0)
            return dmmp_path_pg_id_get (dmmp_ps[j]);
        }
    }
  return DMMP_PATH_GROUP_ID_UNKNOWN;
}

gboolean
std_lx_blk_mp_update (StoragedLinuxBlockMultipath *std_lx_blk_mp,
                      StoragedLinuxBlockObject *std_lx_blk_obj,
                      const gchar *uevent_action, struct dmmp_mpath *mpath)
{
  //TODO(Gris Ge): Find a way to save duplicate call of
  //               dmmp_mpath_get_by_name()
  gboolean rc = FALSE;
  StoragedLinuxDevice *std_lx_dev = NULL;
  StoragedDaemon *daemon = NULL;
  GDBusObjectManager *dbus_mgr = NULL;
  const gchar *blk_name = NULL;
  const gchar *dm_name = NULL;

  storaged_debug ("Multipath: std_lx_blk_mp_update");
  if ((strcmp (uevent_action, MP_MODULE_UDEV_ACTION_ADD) == 0) &&
      (std_lx_blk_mp->is_inited == TRUE))
    goto out;

  daemon = storaged_linux_block_object_get_daemon (std_lx_blk_obj);

  dbus_mgr =
    G_DBUS_OBJECT_MANAGER (storaged_daemon_get_object_manager (daemon));

  std_lx_dev = storaged_linux_block_object_get_device (std_lx_blk_obj);

  blk_name = g_udev_device_get_name (std_lx_dev->udev_device);

  if (blk_name == NULL)
    goto out;


  dm_name = g_udev_device_get_property (std_lx_dev->udev_device, "DM_NAME");

  /* Updating these values:
   *   org.storaged.Storaged.Multipath.PathGroup.Multipath
   *   org.storaged.Storaged.Block.Multipath.Multipath
   */
  _update_std_lx_blk_mp_dm (std_lx_blk_obj, std_lx_blk_mp, dbus_mgr,
                            dmmp_mpath_name_get(mpath),
                            dmmp_mpath_wwid_get(mpath),
                            dm_name != NULL, blk_name);

  if (dm_name != NULL)
    goto out;

  /* Is multipath slave, updating these values:
   *   org.storaged.Storaged.Multipath.PathGroup.Path
   *   org.storaged.Storaged.Block.Multipath.Path
   */
  _update_std_lx_blk_mp_slave(std_lx_blk_obj, std_lx_blk_mp, dbus_mgr,
                              dmmp_mpath_name_get(mpath),
                              dmmp_mpath_wwid_get(mpath),
                              _dmmp_pg_id_of_path (mpath, blk_name),
                              blk_name);

out:

  if (std_lx_dev != NULL)
    g_object_unref (std_lx_dev);
  return rc;
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
