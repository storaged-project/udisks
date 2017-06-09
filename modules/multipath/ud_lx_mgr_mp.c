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

#include <sys/types.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <src/udiskslogging.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxdevice.h>

#include <libdmmp/libdmmp.h>

#include "mp_types.h"

typedef struct _UDisksLinuxManagerMultipathClass
UDisksLinuxManagerMultipathClass;

struct _UDisksLinuxManagerMultipath
{
  UDisksManagerMultipathSkeleton parent_instance;
};

struct _UDisksLinuxManagerMultipathClass
{
  UDisksManagerMultipathSkeletonClass parent_class;
};

static void
ud_lx_mgr_mp_iface_init (UDisksManagerMultipathIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerMultipath, ud_lx_mgr_mp,
                         UDISKS_TYPE_MANAGER_MULTIPATH_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_MULTIPATH,
                                                ud_lx_mgr_mp_iface_init));

static void
ud_lx_mgr_mp_init (UDisksLinuxManagerMultipath *manager)
{
  g_dbus_interface_skeleton_set_flags
    (G_DBUS_INTERFACE_SKELETON (manager),
     G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
ud_lx_mgr_mp_class_init (UDisksLinuxManagerMultipathClass *class)
{
}

UDisksLinuxManagerMultipath *
ud_lx_mgr_mp_new (void)
{
  return UDISKS_LINUX_MANAGER_MULTIPATH
    (g_object_new (UDISKS_TYPE_LINUX_MANAGER_MULTIPATH, NULL));
}

static gboolean
handle_get_all_multipaths (UDisksManagerMultipath *ud_mgr_mp_obj,
                           GDBusMethodInvocation  *invocation)
{
  struct dmmp_context *dmmp_ctx = NULL;
  struct dmmp_mpath **dmmp_mps = NULL;
  uint32_t dmmp_mp_count = 0;
  int mp_rc = 0;
  const gchar **mp_obj_paths = NULL;
  uint32_t i = 0;
  const char *mp_name = NULL;
  const char *wwid = NULL;

  dmmp_ctx = dmmp_context_new ();
  if (dmmp_ctx == NULL)
    {
      g_dbus_method_invocation_return_error
        (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
         "Out of memory");
      goto fail;
    }

  mp_rc = dmmp_mpath_array_get (dmmp_ctx, &dmmp_mps, &dmmp_mp_count);
  if (mp_rc != DMMP_OK)
    {
      g_dbus_method_invocation_return_error
        (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
         "Failed to retrieve mpath list %d", mp_rc);
      goto fail;
    }

  mp_obj_paths = (const gchar **) malloc
    (sizeof(gchar *) * (dmmp_mp_count + 1));
  if (mp_obj_paths == NULL)
    {
      g_dbus_method_invocation_return_error
        (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
         "Out of memory");
      goto fail;
    }

  for (i = 0; i <= dmmp_mp_count; ++i)
    mp_obj_paths[i] = NULL;

  for (i = 0; i < dmmp_mp_count; ++i)
    {
      mp_name = dmmp_mpath_name_get(dmmp_mps[i]);
      wwid = dmmp_mpath_wwid_get(dmmp_mps[i]);
      if ((mp_name != NULL) && (wwid != NULL))
        mp_obj_paths[i] = ud_lx_mp_obj_path_gen(mp_name, wwid);
      else
        {
          g_dbus_method_invocation_return_error
            (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
             "BUG: libdmmp provide us a mpath with NULL name or wwid");
          goto fail;
        }
    }

  udisks_manager_multipath_complete_get_all_multipaths
    (ud_mgr_mp_obj, invocation, mp_obj_paths);

  return TRUE;

fail:
  dmmp_context_free (dmmp_ctx);
  dmmp_mpath_array_free (dmmp_mps, dmmp_mp_count);
  if (mp_obj_paths != NULL)
    for (i = 0; i < dmmp_mp_count; ++i)
      g_free ((gchar *) mp_obj_paths[i]);
  g_free ((gchar **) mp_obj_paths);
  return FALSE;
}

static void
ud_lx_mgr_mp_iface_init (UDisksManagerMultipathIface *iface)
{
  iface->handle_get_all_multipaths = handle_get_all_multipaths;
}
