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
#include <stdint.h>
#include <inttypes.h>
#include <libdmmp/libdmmp.h>

#include <src/udiskslogging.h>
#include <src/udiskslinuxblockobject.h>
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

typedef struct _UDisksLinuxBlockMultipathClass
UDisksLinuxBlockMultipathClass;

struct _UDisksLinuxBlockMultipath
{
  UDisksBlockMultipathSkeleton parent_instance;
  const gchar *mp_obj_path;
  const gchar *mp_path_obj_path;
  const gchar *blk_name;
  UDisksDaemon *daemon;
};

struct _UDisksLinuxBlockMultipathClass
{
  UDisksBlockMultipathSkeletonClass parent_class;
};

static void
ud_lx_blk_mp_iface_init (UDisksBlockMultipathIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (UDisksLinuxBlockMultipath, ud_lx_blk_mp,
   UDISKS_TYPE_BLOCK_MULTIPATH_SKELETON,
   G_IMPLEMENT_INTERFACE (UDISKS_TYPE_BLOCK_MULTIPATH,
                          ud_lx_blk_mp_iface_init));

static void
ud_lx_blk_mp_finalize (GObject *object)
{
  UDisksLinuxBlockMultipath *ud_lx_blk_mp = NULL;
  GDBusObjectManagerServer *dbus_mgr = NULL;

  udisks_debug ("Multipath: ud_lx_blk_mp_finalize()");

  if ((object != NULL) && (UDISKS_IS_LINUX_BLOCK_MULTIPATH (object)))
    {
      ud_lx_blk_mp = UDISKS_LINUX_BLOCK_MULTIPATH (object);

      if (strncmp(ud_lx_blk_mp->blk_name, "dm-", strlen("dm-")) == 0)
        {
          dbus_mgr = udisks_daemon_get_object_manager (ud_lx_blk_mp->daemon);
          ud_lx_mp_obj_unexport (dbus_mgr, ud_lx_blk_mp->mp_obj_path);
        }

      g_free ((gpointer) ud_lx_blk_mp->mp_obj_path);
      g_free ((gpointer) ud_lx_blk_mp->blk_name);
      g_free ((gpointer) ud_lx_blk_mp->mp_path_obj_path);
    }


  if (G_OBJECT_CLASS (ud_lx_blk_mp_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (ud_lx_blk_mp_parent_class)->finalize (object);

  udisks_debug ("Multipath: ud_lx_blk_mp_finalize(): Done");
}


static void
ud_lx_blk_mp_init (UDisksLinuxBlockMultipath *ud_lx_blk_mp)
{
  udisks_debug ("Multipath: ud_lx_blk_mp_init");
  ud_lx_blk_mp->blk_name = NULL;
  ud_lx_blk_mp->mp_obj_path = NULL;
  ud_lx_blk_mp->mp_path_obj_path = NULL;
  ud_lx_blk_mp->daemon = NULL;
  return;
}

static void
ud_lx_blk_mp_class_init (UDisksLinuxBlockMultipathClass *class)
{
  GObjectClass *gobject_class;

  udisks_debug ("Multipath: ud_lx_blk_mp_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = ud_lx_blk_mp_finalize;
}

static void
ud_lx_blk_mp_iface_init (UDisksBlockMultipathIface *iface)
{
  udisks_debug ("Multipath: ud_lx_blk_mp_iface_init");
}


UDisksLinuxBlockMultipath *ud_lx_blk_mp_new (void)
{
  udisks_debug ("Multipath: ud_lx_blk_mp_new");
  return UDISKS_LINUX_BLOCK_MULTIPATH
    (g_object_new (UDISKS_TYPE_BLOCK_MULTIPATH, NULL));
}

gboolean
ud_lx_blk_mp_update (UDisksLinuxBlockMultipath *ud_lx_blk_mp,
                     UDisksLinuxBlockObject *ud_lx_blk_obj,
                     const gchar *uevent_action, struct dmmp_mpath *mpath,
                     const gchar *blk_name)
{
  gboolean rc = FALSE;
  const gchar *mp_obj_path = NULL;
  const gchar *mp_path_obj_path = NULL;
  const char *wwid = NULL;
  const char *mp_name = NULL;
  UDisksLinuxMultipathObject *ud_lx_mp_obj = NULL;
  UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj = NULL;
  UDisksDaemon *daemon = NULL;
  GDBusObjectManagerServer *dbus_mgr = NULL;
  const gchar *blk_obj_path = NULL;

  if (mpath == NULL)
    {
      udisks_warning ("Multipath: ud_lx_blk_mp_update() got NULL mpath");
      return rc;
    }

  if (blk_name == NULL)
    {
      udisks_warning ("Multipath: ud_lx_blk_mp_update() got NULL blk_name");
      return rc;
    }

  udisks_debug ("Multipath: ud_lx_blk_mp_update(): %s %s",
                dmmp_mpath_name_get (mpath), blk_name);

  mp_name = dmmp_mpath_name_get (mpath);
  wwid = dmmp_mpath_wwid_get (mpath);

  mp_obj_path = ud_lx_mp_obj_path_gen (mp_name, wwid);
  mp_path_obj_path = ud_lx_mp_path_obj_path_gen (mp_obj_path, blk_name);

  ud_lx_blk_mp->mp_obj_path = mp_obj_path;
  ud_lx_blk_mp->mp_path_obj_path = mp_path_obj_path;
  ud_lx_blk_mp->blk_name = g_strdup (blk_name);

  daemon = udisks_linux_block_object_get_daemon (ud_lx_blk_obj);
  ud_lx_blk_mp->daemon = daemon;

  udisks_block_multipath_set_multipath (UDISKS_BLOCK_MULTIPATH (ud_lx_blk_mp),
                                        mp_obj_path);

  dbus_mgr = udisks_daemon_get_object_manager (daemon);
  blk_obj_path = udisks_linux_block_object_path_gen (blk_name);

  if (strncmp(blk_name, "dm-", strlen("dm-")) == 0)
    {
      ud_lx_mp_obj = ud_lx_mp_obj_get (dbus_mgr, mp_obj_path);
      /* g_dbus_object_get_object_path() might return NULL, we
       * use udisks_linux_block_object_path_gen() instead
       */
      if (ud_lx_mp_obj != NULL)
        {
          ud_lx_mp_obj_set_block (ud_lx_mp_obj, blk_obj_path);
          g_object_unref (ud_lx_mp_obj);
        }
    }
  else
    {
      /* Is multipath slave path, updating these properties:
       *   org.freedesktop.UDisks2.Block.Multipath.Path
       *   org.freedesktop.UDisks2.Multipath.Path.Block
       */
      udisks_block_multipath_set_path (UDISKS_BLOCK_MULTIPATH (ud_lx_blk_mp),
                                       mp_path_obj_path);
      ud_lx_mp_path_obj = ud_lx_mp_path_obj_get (dbus_mgr, mp_path_obj_path);
      if (ud_lx_mp_path_obj != NULL)
        {
          ud_lx_mp_path_obj_set_block (ud_lx_mp_path_obj, blk_obj_path);
          g_object_unref (ud_lx_mp_path_obj);
        }
    }

  g_free ((gpointer) blk_obj_path);

  return rc;
}
