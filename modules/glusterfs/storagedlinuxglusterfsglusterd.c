/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Samikshan Bairagya <sbairagy@redhat.com>
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

#include "config.h"
#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <mntent.h>

#include <glib/gstdio.h>

#include <src/storagedlogging.h>
#include <src/storagedlinuxprovider.h>
#include <src/storageddaemon.h>
#include <src/storagedstate.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlinuxblock.h>
#include <src/storagedlinuxblockobject.h>

#include "storagedlinuxglusterfsglusterd.h"

/**
 * SECTION:storagedlinuxglusterfsglusterd
 * @title: StoragedLinuxGlusterFSGlusterd
 * @short_description: Linux implementation of #StoragedGlusterFSGlusterd
 *
 * This type provides an implementation of the #StoragedGlusterFSGlusterd interface
 * on Linux.
 */

typedef struct _StoragedLinuxGlusterFSGlusterdClass StoragedLinuxGlusterFSGlusterdClass;

/**
 * StoragedLinuxGlusterFSGlusterd:
 *
 * The #StoragedLinuxGlusterFSGlusterd structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxGlusterFSGlusterd
{
  StoragedGlusterFSGlusterdSkeleton parent_instance;
};

struct _StoragedLinuxGlusterFSGlusterdClass
{
  StoragedGlusterFSGlusterdSkeletonClass parent_class;
};

static void storaged_linux_glusterfs_glusterd_iface_init (StoragedGlusterFSGlusterdIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxGlusterFSGlusterd, storaged_linux_glusterfs_glusterd, STORAGED_TYPE_GLUSTERFS_GLUSTERD_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_GLUSTERFS_GLUSTERD, storaged_linux_glusterfs_glusterd_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_glusterfs_glusterd_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_glusterfs_glusterd_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_glusterd_parent_class)->finalize (object);
}

static void
storaged_linux_glusterfs_glusterd_init (StoragedLinuxGlusterFSGlusterd *gfs_glusterd)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (gfs_glusterd),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_glusterfs_glusterd_class_init (StoragedLinuxGlusterFSGlusterdClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = storaged_linux_glusterfs_glusterd_finalize;
}

/**
 * storaged_linux_glusterfs_glusterd_new:
 *
 * Creates a new #StoragedLinuxGlusterFSGlusterd instance.
 *
 * Returns: A new #StoragedLinuxGlusterFSGlusterd. Free with g_object_unref().
 */
StoragedGlusterFSGlusterd *
storaged_linux_glusterfs_glusterd_new (void)
{
  return STORAGED_GLUSTERFS_GLUSTERD (g_object_new (STORAGED_TYPE_LINUX_GLUSTERFS_GLUSTERD,
                                                    NULL));
}

/**
 * storaged_linux_glusterfs_glusterd_update:
 * @gfs_glusterd: A #StoragedLinuxGlusterFSGlusterd.
 * @object: The enclosing #StoragedLinuxGlusterFSGlusterdObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_glusterfs_glusterd_update (StoragedLinuxGlusterFSGlusterd *gfs_glusterd,
                                          GVariant                       *info)
{
  StoragedGlusterFSGlusterd *iface = STORAGED_GLUSTERFS_GLUSTERD (gfs_glusterd);
  const gchar *load_state;
  const gchar *active_state;

  if (g_variant_lookup (info, "LoadState", "s", &load_state))
    storaged_glusterfs_glusterd_set_load_state (iface, load_state);

  if (g_variant_lookup (info, "ActiveState", "s", &active_state))
    storaged_glusterfs_glusterd_set_active_state (iface, active_state);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_glusterfs_glusterd_iface_init (StoragedGlusterFSGlusterdIface *iface)
{}

