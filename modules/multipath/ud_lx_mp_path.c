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
 * Handle these dbus interfaces:
 *      org.freedesktop.UDisks2.Multipath.Path
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

#define _STD_LX_BLK_DBUS_OBJ_PATH_PREFIX \
  "/org.freedesktop.UDisks2/block_devices"

typedef struct _UDisksLinuxMultipathPathClass
UDisksLinuxMultipathPathClass;

struct _UDisksLinuxMultipathPath
{
  UDisksMultipathPathSkeleton parent_instance;
  const gchar *blk_name;
  const gchar *mp_obj_path;
};

struct _UDisksLinuxMultipathPathClass
{
  UDisksMultipathPathSkeletonClass parent_class;
};

static void
ud_lx_mp_path_iface_init (UDisksMultipathPathIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (UDisksLinuxMultipathPath, ud_lx_mp_path,
   UDISKS_TYPE_MULTIPATH_PATH_SKELETON,
   G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MULTIPATH_PATH,
                          ud_lx_mp_path_iface_init));

static void
ud_lx_mp_path_finalize (GObject *object)
{
  UDisksLinuxMultipathPath *ud_lx_mp_path = NULL;
  udisks_debug ("Multipath: ud_lx_mp_path_finalize ()");

  if ((object != NULL) && (UDISKS_IS_LINUX_MULTIPATH_PATH (object)))
    {
      ud_lx_mp_path = UDISKS_LINUX_MULTIPATH_PATH (object);
      g_free ((gpointer) ud_lx_mp_path->blk_name);
      g_free ((gpointer) ud_lx_mp_path->mp_obj_path);
    }

  if (G_OBJECT_CLASS (ud_lx_mp_path_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (ud_lx_mp_path_parent_class)->finalize (object);
}

static void
ud_lx_mp_path_init (UDisksLinuxMultipathPath *ud_lx_mp_path)
{
  udisks_debug ("Multipath: ud_lx_mp_path_init");
  ud_lx_mp_path->blk_name = NULL;
  ud_lx_mp_path->mp_obj_path = NULL;
  return;
}

static void
ud_lx_mp_path_class_init (UDisksLinuxMultipathPathClass *class)
{
  GObjectClass *gobject_class;

  udisks_debug ("Multipath: ud_lx_mp_path_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = ud_lx_mp_path_finalize;
}

static void
ud_lx_mp_path_iface_init (UDisksMultipathPathIface *iface)
{
  udisks_debug ("Multipath: ud_lx_mp_path_iface_init");
}

UDisksLinuxMultipathPath *
ud_lx_mp_path_new (void)
{
  UDisksLinuxMultipathPath * ud_lx_mp_path = NULL;
  ud_lx_mp_path = g_object_new (UDISKS_TYPE_LINUX_MULTIPATH_PATH, NULL);

  return ud_lx_mp_path;
}

gboolean
ud_lx_mp_path_update (UDisksLinuxMultipathPath *ud_lx_mp_path,
                      struct dmmp_path *mp_path,
                      const gchar *mp_obj_path)
{
  UDisksMultipathPath *ud_mp_path = NULL;
  uint32_t status;
  const char *blk_name = NULL;

  udisks_debug ("Multipath: ud_lx_mp_path_update()");

  if (mp_path == NULL)
    return FALSE;

  ud_mp_path = UDISKS_MULTIPATH_PATH (ud_lx_mp_path);

  blk_name = dmmp_path_blk_name_get (mp_path);
  if (blk_name == NULL)
    return FALSE;

  ud_lx_mp_path->blk_name = g_strdup (blk_name);

  udisks_multipath_path_set_name
    (ud_mp_path, ud_lx_mp_path->blk_name);

  status = dmmp_path_status_get (mp_path);
  udisks_multipath_path_set_status
    (ud_mp_path, dmmp_path_status_str (status));

  ud_lx_mp_path->mp_obj_path = g_strdup (mp_obj_path);

  udisks_multipath_path_set_multipath
    (ud_mp_path, ud_lx_mp_path->mp_obj_path);

  return TRUE;
}
