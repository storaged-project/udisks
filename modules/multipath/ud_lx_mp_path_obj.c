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
 *      org.freedesktop.UDisks2.Multipath.Path
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

typedef struct _UDisksLinuxMultipathPathObjectClass
UDisksLinuxMultipathPathObjectClass;

struct _UDisksLinuxMultipathPathObject
{
  UDisksObjectSkeleton parent_instance;
  UDisksLinuxMultipathPath *ud_lx_mp_path;
  const gchar *mp_obj_path;
  const gchar *mp_path_obj_path;
  const gchar *blk_obj_path;
  GDBusObjectManagerServer *dbus_mgr;
};

struct _UDisksLinuxMultipathPathObjectClass
{
  UDisksObjectSkeletonClass parent_class;
};

static void
ud_lx_mp_path_obj_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (UDisksLinuxMultipathPathObject, ud_lx_mp_path_obj,
   UDISKS_TYPE_OBJECT_SKELETON,
   G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT,
                          ud_lx_mp_path_obj_iface_init));

static void
ud_lx_mp_path_obj_finalize (GObject *object)
{
  UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj = NULL;

  udisks_debug ("Multipath: ud_lx_mp_path_obj_finalize()");

  if ((object != NULL) && (UDISKS_IS_LINUX_MULTIPATH_PATH_OBJECT (object)))
    {
      ud_lx_mp_path_obj = UDISKS_LINUX_MULTIPATH_PATH_OBJECT (object);
      udisks_debug ("Multipath: ud_lx_mp_path_obj_finalize(): %s",
                    ud_lx_mp_path_obj->mp_path_obj_path);
      if (ud_lx_mp_path_obj->ud_lx_mp_path != NULL)
        g_object_unref (ud_lx_mp_path_obj->ud_lx_mp_path);
      g_free ((gpointer) ud_lx_mp_path_obj->mp_path_obj_path);
      g_free ((gpointer) ud_lx_mp_path_obj->mp_obj_path);
      g_free ((gpointer) ud_lx_mp_path_obj->blk_obj_path);
    }

  if (G_OBJECT_CLASS (ud_lx_mp_path_obj_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (ud_lx_mp_path_obj_parent_class)->finalize (object);
}

static void
ud_lx_mp_path_obj_init
  (UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj)
{
  udisks_debug ("Multipath: ud_lx_mp_path_obj_init()");
  ud_lx_mp_path_obj->ud_lx_mp_path = NULL;
  ud_lx_mp_path_obj->mp_obj_path = NULL;
  ud_lx_mp_path_obj->mp_path_obj_path = NULL;
  ud_lx_mp_path_obj->blk_obj_path = NULL;
  ud_lx_mp_path_obj->dbus_mgr = NULL;
  return;
}

static void
ud_lx_mp_path_obj_class_init
  (UDisksLinuxMultipathPathObjectClass *class)
{
  GObjectClass *gobject_class;

  udisks_debug ("Multipath: ud_lx_mp_path_obj_class_init()");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = ud_lx_mp_path_obj_finalize;
}

static void
ud_lx_mp_path_obj_iface_init (UDisksModuleObjectIface *iface)
{
  udisks_debug ("Multipath: ud_lx_mp_path_obj_iface_init");
}

UDisksLinuxMultipathPathObject *
ud_lx_mp_path_obj_new (GDBusObjectManagerServer *dbus_mgr,
                       struct dmmp_path *mp_path,
                       const gchar *mp_obj_path)
{
  const gchar *mp_path_obj_path = NULL;
  const char *blk_name = NULL;
  UDisksLinuxMultipathPath *ud_lx_mp_path = NULL;
  UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj = NULL;

  udisks_debug ("Multipath: ud_lx_mp_path_obj_new()");

  blk_name = dmmp_path_blk_name_get (mp_path);

  if (blk_name == NULL)
    return NULL;

  ud_lx_mp_path = ud_lx_mp_path_new ();

  ud_lx_mp_path_obj = g_object_new
    (UDISKS_TYPE_LINUX_MULTIPATH_PATH_OBJECT, NULL);

  ud_lx_mp_path_obj->ud_lx_mp_path = ud_lx_mp_path;
  ud_lx_mp_path_obj->dbus_mgr = dbus_mgr;
  ud_lx_mp_path_obj->mp_obj_path = g_strdup (mp_obj_path);

  mp_path_obj_path = ud_lx_mp_path_obj_path_gen
    (ud_lx_mp_path_obj->mp_obj_path, blk_name);
  ud_lx_mp_path_obj->mp_path_obj_path = mp_path_obj_path;

  g_dbus_object_skeleton_set_object_path
    (G_DBUS_OBJECT_SKELETON (ud_lx_mp_path_obj), mp_path_obj_path);

  g_dbus_object_skeleton_add_interface
      (G_DBUS_OBJECT_SKELETON (ud_lx_mp_path_obj),
       G_DBUS_INTERFACE_SKELETON (ud_lx_mp_path_obj->ud_lx_mp_path));

  g_dbus_object_manager_server_export
    (ud_lx_mp_path_obj->dbus_mgr,
     G_DBUS_OBJECT_SKELETON (ud_lx_mp_path_obj));

  ud_lx_mp_path_obj_update (ud_lx_mp_path_obj, mp_path,
                            ud_lx_mp_path_obj->mp_obj_path);

  return ud_lx_mp_path_obj;
}

gboolean
ud_lx_mp_path_obj_update (UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj,
                          struct dmmp_path *mp_path, const gchar *mp_obj_path)
{
  udisks_debug ("Multipath: ud_lx_mp_path_obj_update()");

  if (ud_lx_mp_path_obj == NULL)
    return FALSE;

  if (mp_path == NULL)
    return FALSE;

  return ud_lx_mp_path_update (ud_lx_mp_path_obj->ud_lx_mp_path, mp_path,
                               mp_obj_path);
}

void
ud_lx_mp_path_obj_set_block
  (UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj,
   const gchar *blk_obj_path)
{
  udisks_debug ("ud_lx_mp_path_obj_set_block(): %s", blk_obj_path);
  ud_lx_mp_path_obj->blk_obj_path = g_strdup (blk_obj_path);
  udisks_multipath_path_set_block
    (UDISKS_MULTIPATH_PATH (ud_lx_mp_path_obj->ud_lx_mp_path),
     ud_lx_mp_path_obj->blk_obj_path);
}

const gchar *
ud_lx_mp_path_obj_path_gen (const gchar *mp_obj_path, const char *blk_name)
{
  if ((mp_obj_path == NULL) || (blk_name == NULL))
    return NULL;
  return g_strdup_printf ("%s/path_%s", mp_obj_path, blk_name);
}

UDisksLinuxMultipathPathObject *
ud_lx_mp_path_obj_get (GDBusObjectManagerServer *dbus_mgr,
                       const gchar *mp_path_obj_path)
{
  GDBusObject *dbus_obj = NULL;

  dbus_obj = g_dbus_object_manager_get_object
    (G_DBUS_OBJECT_MANAGER (dbus_mgr), mp_path_obj_path);

  if (dbus_obj == NULL)
    return NULL;

  return UDISKS_LINUX_MULTIPATH_PATH_OBJECT
    (G_DBUS_OBJECT_SKELETON (dbus_obj));
}

void
ud_lx_mp_path_obj_unexport (GDBusObjectManagerServer *dbus_mgr,
                            const gchar *mp_path_obj_path)
{
  g_dbus_object_manager_server_unexport (dbus_mgr, mp_path_obj_path);
}
