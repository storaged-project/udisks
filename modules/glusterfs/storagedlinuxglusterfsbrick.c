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

#include "storagedlinuxglusterfsbrick.h"

/**
 * SECTION:storagedlinuxglusterfsbrick
 * @title: StoragedLinuxGlusterFSBrick
 * @short_description: Linux implementation of #StoragedGlusterFSBrick
 *
 * This type provides an implementation of the #StoragedGlusterFSBrick interface
 * on Linux.
 */

typedef struct _StoragedLinuxGlusterFSBrickClass   StoragedLinuxGlusterFSBrickClass;

/**
 * StoragedLinuxGlusterFSBrick:
 *
 * The #StoragedLinuxGlusterFSBrick structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxGlusterFSBrick
{
  StoragedGlusterFSBrickSkeleton parent_instance;
};

struct _StoragedLinuxGlusterFSBrickClass
{
  StoragedGlusterFSBrickSkeletonClass parent_class;
};

static void glusterfs_brick_iface_init (StoragedGlusterFSBrickIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxGlusterFSBrick, storaged_linux_glusterfs_brick, STORAGED_TYPE_GLUSTERFS_BRICK_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_GLUSTERFS_BRICK, glusterfs_brick_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_glusterfs_brick_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_glusterfs_brick_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_brick_parent_class)->finalize (object);
}

static void
storaged_linux_glusterfs_brick_init (StoragedLinuxGlusterFSBrick *gfs_brick)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (gfs_brick),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_glusterfs_brick_class_init (StoragedLinuxGlusterFSBrickClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = storaged_linux_glusterfs_brick_finalize;
}

/**
 * storaged_linux_glusterfs_brick_new:
 *
 * Creates a new #StoragedLinuxGlusterFSBrick instance.
 *
 * Returns: A new #StoragedLinuxGlusterFSBrick. Free with g_object_unref().
 */
StoragedGlusterFSBrick *
storaged_linux_glusterfs_brick_new (void)
{
  return STORAGED_GLUSTERFS_BRICK (g_object_new (STORAGED_TYPE_LINUX_GLUSTERFS_BRICK,
                                                 NULL));
}

/**
 * storaged_linux_glusterfs_brick_update:
 * @gfs_brick: A #StoragedLinuxGlusterFSBrick.
 * @object: The enclosing #StoragedLinuxGlusterFSBrickObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_glusterfs_brick_update (StoragedLinuxGlusterFSBrick        *gfs_brick,
                                       StoragedLinuxGlusterFSVolumeObject *volume_object,
                                       GVariant                           *brick_info)
{
  StoragedGlusterFSBrick *iface = STORAGED_GLUSTERFS_BRICK (gfs_brick);
  const gchar *str;
  guint num;

  if (g_variant_lookup (brick_info, "name", "&s", &str))
    storaged_glusterfs_brick_set_name (iface, str);

  if (g_variant_lookup (brick_info, "hostUuid", "&s", &str))
    storaged_glusterfs_brick_set_host_uuid (iface, str);

  storaged_glusterfs_brick_set_volume (iface, g_dbus_object_get_object_path (G_DBUS_OBJECT (volume_object)));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
glusterfs_brick_iface_init (StoragedGlusterFSBrickIface *iface)
{
  ;
}

