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

#include <sys/types.h>

#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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

typedef struct _UDisksLinuxDriveMultipathClass
UDisksLinuxDriveMultipathClass;

struct _UDisksLinuxDriveMultipath
{
  UDisksDriveMultipathSkeleton parent_instance;
  const gchar *mp_obj_path;
  UDisksDaemon *daemon;
};

struct _UDisksLinuxDriveMultipathClass
{
  UDisksDriveMultipathSkeletonClass parent_class;
};

static void
ud_lx_drv_mp_iface_init (UDisksDriveMultipathIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (UDisksLinuxDriveMultipath, ud_lx_drv_mp,
   UDISKS_TYPE_DRIVE_MULTIPATH_SKELETON,
   G_IMPLEMENT_INTERFACE (UDISKS_TYPE_DRIVE_MULTIPATH,
                          ud_lx_drv_mp_iface_init));

static void
ud_lx_drv_mp_finalize (GObject *object)
{
  UDisksLinuxDriveMultipath *ud_lx_drv_mp = NULL;

  udisks_debug ("Multipath: ud_lx_drv_mp_finalize()");
  if ((object != NULL) && (UDISKS_IS_LINUX_DRIVE_MULTIPATH (object)))
    {
      ud_lx_drv_mp = UDISKS_LINUX_DRIVE_MULTIPATH (object);
      g_free((gpointer) ud_lx_drv_mp->mp_obj_path);
    }

  if (G_OBJECT_CLASS (ud_lx_drv_mp_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (ud_lx_drv_mp_parent_class)->finalize (object);
}


static void
ud_lx_drv_mp_init (UDisksLinuxDriveMultipath *ud_lx_drv_mp)
{
  udisks_debug ("Multipath: ud_lx_drv_mp_init");
  ud_lx_drv_mp->mp_obj_path = NULL;
  ud_lx_drv_mp->daemon = NULL;
  return;
}

static void
ud_lx_drv_mp_class_init (UDisksLinuxDriveMultipathClass *class)
{
  GObjectClass *gobject_class;

  udisks_debug ("Multipath: ud_lx_drv_mp_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = ud_lx_drv_mp_finalize;
}

static void
ud_lx_drv_mp_iface_init (UDisksDriveMultipathIface *iface)
{
  udisks_debug ("Multipath: ud_lx_drv_mp_iface_init");
}

UDisksLinuxDriveMultipath *
ud_lx_drv_mp_new (void)
{
  udisks_debug ("Multipath: ud_lx_drv_mp_new");
  return UDISKS_LINUX_DRIVE_MULTIPATH
    (g_object_new (UDISKS_TYPE_DRIVE_MULTIPATH, NULL));
}

gboolean
ud_lx_drv_mp_update (UDisksLinuxDriveMultipath *ud_lx_drv_mp,
                     UDisksLinuxDriveObject *ud_lx_drv_obj,
                     const gchar *uevent_action, struct dmmp_mpath *mpath)
{
  gboolean rc = FALSE;
  const gchar *mp_obj_path = NULL;
  const char *wwid = NULL;
  const char *mp_name = NULL;
  UDisksLinuxMultipathObject *ud_lx_mp_obj = NULL;
  GDBusObjectManagerServer *dbus_mgr = NULL;
  const gchar *drv_obj_path = NULL;

  if (mpath == NULL)
    return rc;

  mp_name = dmmp_mpath_name_get (mpath);
  wwid = dmmp_mpath_wwid_get (mpath);

  udisks_debug ("Multipath: ud_lx_drv_mp_update(): %s %s", mp_name, wwid);

  mp_obj_path = ud_lx_mp_obj_path_gen(mp_name, wwid);

  udisks_drive_multipath_set_multipath
      (UDISKS_DRIVE_MULTIPATH (ud_lx_drv_mp), mp_obj_path);

  rc = TRUE;

  ud_lx_drv_mp->mp_obj_path = mp_obj_path;
  ud_lx_drv_mp->daemon = udisks_linux_drive_object_get_daemon (ud_lx_drv_obj);

  dbus_mgr = udisks_daemon_get_object_manager (ud_lx_drv_mp->daemon);
  ud_lx_mp_obj = ud_lx_mp_obj_get (dbus_mgr, mp_obj_path);
  if (ud_lx_mp_obj != NULL)
    {
      drv_obj_path = g_dbus_object_get_object_path
                      (G_DBUS_OBJECT (ud_lx_drv_obj));
      udisks_debug ("Multipath: ud_lx_drv_mp_update(): "
                    "Setting %s drive property: %s", mp_obj_path,
                    drv_obj_path);
      ud_lx_mp_obj_set_drive (ud_lx_mp_obj, drv_obj_path);
      g_object_unref (ud_lx_mp_obj);
    }

  return rc;
}
