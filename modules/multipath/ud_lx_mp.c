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
 * Handle this interface:
 *      org.freedesktop.UDisks2.Multipath
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

typedef struct _UDisksLinuxMultipathClass
UDisksLinuxMultipathClass;

struct _UDisksLinuxMultipath
{
  UDisksMultipathSkeleton parent_instance;
  const gchar **path_obj_paths;
  size_t all_path_count;
  const gchar *name;
  const gchar *wwid;
};

struct _UDisksLinuxMultipathClass
{
  UDisksMultipathSkeletonClass parent_class;
};

static void
ud_lx_mp_iface_init (UDisksMultipathIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (UDisksLinuxMultipath, ud_lx_mp,
   UDISKS_TYPE_MULTIPATH_SKELETON,
   G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MULTIPATH,
                          ud_lx_mp_iface_init));

static void
ud_lx_mp_finalize (GObject *object)
{
  UDisksLinuxMultipath *ud_lx_mp = NULL;
  size_t i = 0;
  udisks_debug ("Multipath: ud_lx_mp_finalize ()");

  if ((object != NULL) && (UDISKS_IS_LINUX_MULTIPATH (object)))
    {
      ud_lx_mp = UDISKS_LINUX_MULTIPATH (object);
      if (ud_lx_mp->path_obj_paths != NULL)
        for (i = 0; i < ud_lx_mp->all_path_count; ++i)
          g_free ((gpointer) ud_lx_mp->path_obj_paths[i]);
      g_free ((gpointer) ud_lx_mp->path_obj_paths);
      g_free ((gpointer) ud_lx_mp->name);
      g_free ((gpointer) ud_lx_mp->wwid);
    }

  if (G_OBJECT_CLASS (ud_lx_mp_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (ud_lx_mp_parent_class)->finalize (object);

}

static void
ud_lx_mp_init (UDisksLinuxMultipath *ud_lx_mp)
{
  udisks_debug ("Multipath: ud_lx_mp_init");
  ud_lx_mp->path_obj_paths = NULL;
  ud_lx_mp->all_path_count = 0;
  ud_lx_mp->name = NULL;
  ud_lx_mp->wwid = NULL;
  return;
}

static void
ud_lx_mp_class_init (UDisksLinuxMultipathClass *class)
{
  GObjectClass *gobject_class;

  udisks_debug ("Multipath: ud_lx_mp_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = ud_lx_mp_finalize;
}

static void
ud_lx_mp_iface_init (UDisksMultipathIface *iface)
{
  udisks_debug ("Multipath: ud_lx_mp_iface_init");
}

UDisksLinuxMultipath *
ud_lx_mp_new (struct dmmp_mpath *mpath, const gchar *mp_obj_path)
{
  UDisksLinuxMultipath *ud_lx_mp = NULL;

  ud_lx_mp = g_object_new (UDISKS_TYPE_LINUX_MULTIPATH, NULL);
  ud_lx_mp_update (ud_lx_mp, mpath, mp_obj_path);

  return ud_lx_mp;
}

gboolean
ud_lx_mp_update (UDisksLinuxMultipath *ud_lx_mp, struct dmmp_mpath *mpath,
                 const gchar *mp_obj_path)
{
  UDisksMultipath *ud_mp = NULL;
  gboolean changed = FALSE;
  struct dmmp_path_group **pgs = NULL;
  uint32_t pg_count = 0;
  struct dmmp_path **ps = NULL;
  uint32_t p_count = 0;
  const gchar **path_obj_paths = NULL;
  size_t all_path_count = 0;
  uint32_t i = 0;
  uint32_t j = 0;
  size_t l = 0;

  udisks_debug ("Multipath: ud_lx_mp_update()");

  ud_mp = UDISKS_MULTIPATH (ud_lx_mp);

  ud_lx_mp->name = g_strdup(dmmp_mpath_name_get (mpath));
  ud_lx_mp->wwid = g_strdup(dmmp_mpath_wwid_get (mpath));

  udisks_multipath_set_name (ud_mp, ud_lx_mp->name);
  udisks_multipath_set_wwid (ud_mp, ud_lx_mp->wwid);

  dmmp_path_group_array_get(mpath, &pgs, &pg_count);

  for (i = 0; i < pg_count; ++i)
    {
      dmmp_path_array_get (pgs[i], &ps, &p_count);
      all_path_count += p_count;
    }

  if (all_path_count == 0)
      return changed;
  path_obj_paths = (const gchar **) calloc
    (all_path_count + 1, sizeof(gchar *));

  if (path_obj_paths == NULL)
    goto fail;

  for (i = 0; i < pg_count; ++i)
    {
      dmmp_path_array_get (pgs[i], &ps, &p_count);
      for (j = 0; j < p_count; ++j)
        path_obj_paths[l++] =
          ud_lx_mp_path_obj_path_gen(mp_obj_path,
                                     dmmp_path_blk_name_get(ps[j]));
    }

  udisks_multipath_set_paths(ud_mp, path_obj_paths);
  ud_lx_mp->path_obj_paths = path_obj_paths;
  ud_lx_mp->all_path_count = all_path_count;

  return changed;

fail:
  if (path_obj_paths != NULL)
    for (i = 0; i < all_path_count; ++i)
      g_free ((gchar *) path_obj_paths[i]);
  g_free ((gchar **) path_obj_paths);

  return changed;
}
