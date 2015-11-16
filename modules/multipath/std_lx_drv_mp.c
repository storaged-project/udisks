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

/* Note: This is inspired by modules/dummy/dummylinuxdrive.c  */

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

static gboolean
_fill_std_lx_drv_mp (StoragedDaemon *daemon,
                     StoragedLinuxDriveMultipath *std_lx_drv_mp,
                     struct dmmp_mpath *mpath,
                     const gchar *blk_name);

typedef struct _StoragedLinuxDriveMultipathClass
StoragedLinuxDriveMultipathClass;

struct _StoragedLinuxDriveMultipath
{
  StoragedDriveMultipathSkeleton parent_instance;
  StoragedLinuxMultipathObject *std_lx_mp_obj;
  gboolean is_inited;
};

struct _StoragedLinuxDriveMultipathClass
{
  StoragedDriveMultipathSkeletonClass parent_class;
};

static void
std_lx_drv_mp_iface_init (StoragedDriveMultipathIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (StoragedLinuxDriveMultipath, std_lx_drv_mp,
   STORAGED_TYPE_DRIVE_MULTIPATH_SKELETON,
   G_IMPLEMENT_INTERFACE (STORAGED_TYPE_DRIVE_MULTIPATH,
                          std_lx_drv_mp_iface_init));

static void
std_lx_drv_mp_finalize (GObject *object)
{
  StoragedLinuxDriveMultipath *std_lx_drv_mp = NULL;

  storaged_debug ("Multipath: std_lx_drv_mp_finalize ()");

  if ((object != NULL) && (STORAGED_IS_LINUX_DRIVE_MULTIPATH (object)))
    {
      std_lx_drv_mp = STORAGED_LINUX_DRIVE_MULTIPATH (object);
      std_lx_mp_obj_update (std_lx_drv_mp->std_lx_mp_obj, NULL);
      g_object_unref (std_lx_drv_mp->std_lx_mp_obj);
    }

  if (G_OBJECT_CLASS (std_lx_drv_mp_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (std_lx_drv_mp_parent_class)->finalize (object);
}


static void
std_lx_drv_mp_init (StoragedLinuxDriveMultipath *std_lx_drv_mp)
{
  storaged_debug ("Multipath: std_lx_drv_mp_init");
  std_lx_drv_mp->is_inited = FALSE;
  std_lx_drv_mp->std_lx_mp_obj = NULL;
  return;
}

static void
std_lx_drv_mp_class_init (StoragedLinuxDriveMultipathClass *class)
{
  GObjectClass *gobject_class;

  storaged_debug ("Multipath: std_lx_drv_mp_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = std_lx_drv_mp_finalize;
}

static void
std_lx_drv_mp_iface_init (StoragedDriveMultipathIface *iface)
{
  storaged_debug ("Multipath: std_lx_drv_mp_iface_init");
}

StoragedLinuxDriveMultipath *
std_lx_drv_mp_new (void)
{
  storaged_debug ("Multipath: std_lx_drv_mp_new");
  return STORAGED_LINUX_DRIVE_MULTIPATH
    (g_object_new (STORAGED_TYPE_DRIVE_MULTIPATH, NULL));
}

gboolean
std_lx_drv_mp_update (StoragedLinuxDriveMultipath *std_lx_drv_mp,
                      StoragedLinuxDriveObject *std_lx_drv_obj,
                      const gchar *uevent_action, struct dmmp_mpath *mpath)
{
  gboolean rc = FALSE;
  StoragedLinuxDevice *mp_dev = NULL;
  StoragedDaemon *daemon = NULL;
  const char *blk_name = NULL;

  if ((strcmp (uevent_action, MP_MODULE_UDEV_ACTION_ADD) == 0) &&
      (std_lx_drv_mp->is_inited == TRUE))
    goto out;

  daemon = storaged_linux_drive_object_get_daemon (std_lx_drv_obj);

  mp_dev = storaged_linux_drive_object_get_mp_device (std_lx_drv_obj);
  if (mp_dev == NULL)
    goto out;

  std_lx_drv_mp->is_inited = TRUE;

  blk_name = g_udev_device_get_name (mp_dev->udev_device);
  if (blk_name == NULL)
    goto out;

  _fill_std_lx_drv_mp (daemon, std_lx_drv_mp, mpath, blk_name);

  rc = TRUE;

out:
  if (mp_dev != NULL)
    g_object_unref (mp_dev);

  return rc;
}


/*
 * Create/Update org.storaged.Storaged.Multipath object and it's
 * sub-interfaces. Return TRUE if data changed.
 */
static gboolean
_fill_std_lx_drv_mp (StoragedDaemon *daemon,
                     StoragedLinuxDriveMultipath *std_lx_drv_mp,
                     struct dmmp_mpath *mpath,
                     const gchar * blk_name)
{
  GDBusObjectManagerServer *dbus_mgr = NULL;
  StoragedLinuxBlockObject *std_lx_blk_obj = NULL;

  storaged_debug ("Multipath: _fill_std_lx_drv_mp ()");

  if (std_lx_drv_mp->std_lx_mp_obj != NULL)
    {
      if (mpath == NULL)
        {
          storaged_drive_multipath_set_multipath
              (STORAGED_DRIVE_MULTIPATH (std_lx_drv_mp), "/");
          std_lx_mp_obj_update (std_lx_drv_mp->std_lx_mp_obj, mpath);
          g_object_unref (std_lx_drv_mp->std_lx_mp_obj);
          std_lx_drv_mp->std_lx_mp_obj = NULL;
          std_lx_drv_mp->is_inited = FALSE;
          return TRUE;
        }

      return std_lx_mp_obj_update (std_lx_drv_mp->std_lx_mp_obj, mpath);
    }

  dbus_mgr = storaged_daemon_get_object_manager (daemon);
  std_lx_drv_mp->std_lx_mp_obj = std_lx_mp_obj_new (dbus_mgr, mpath);

  storaged_drive_multipath_set_multipath
      (STORAGED_DRIVE_MULTIPATH (std_lx_drv_mp),
       g_dbus_object_get_object_path
         ((GDBusObject *) std_lx_drv_mp->std_lx_mp_obj));

  /* When new multipath was created, the dm block will not be
   * trigger with udev event, maybe a bug, anyway, manually trigger so.
   * TODO(Gris Ge): need a recheck
   */
  std_lx_blk_obj = storaged_linux_block_object_get
    (G_DBUS_OBJECT_MANAGER (dbus_mgr), blk_name);

  if (std_lx_blk_obj != NULL)
    {
      storaged_linux_block_object_uevent
        (std_lx_blk_obj, MP_MODULE_UDEV_ACTION_ADD,
         storaged_linux_block_object_get_device (std_lx_blk_obj));

      g_object_unref (std_lx_blk_obj);
    }

  return TRUE;
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
