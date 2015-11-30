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

#include "storagedlinuxglusterfsvolume.h"

/**
 * SECTION:storagedlinuxglusterfsvolume
 * @title: StoragedLinuxGlusterFSVolume
 * @short_description: Linux implementation of #StoragedGlusterFSVolume
 *
 * This type provides an implementation of the #StoragedGlusterFSVolume interface
 * on Linux.
 */

typedef struct _StoragedLinuxGlusterFSVolumeClass   StoragedLinuxGlusterFSVolumeClass;

/**
 * StoragedLinuxGlusterFSVolume:
 *
 * The #StoragedLinuxGlusterFSVolume structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxGlusterFSVolume
{
  StoragedGlusterFSVolumeSkeleton parent_instance;
};

struct _StoragedLinuxGlusterFSVolumeClass
{
  StoragedGlusterFSVolumeSkeletonClass parent_class;
};

static void glusterfs_volume_iface_init (StoragedGlusterFSVolumeIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxGlusterFSVolume, storaged_linux_glusterfs_volume, STORAGED_TYPE_GLUSTERFS_VOLUME_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_GLUSTERFS_VOLUME, glusterfs_volume_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_glusterfs_volume_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_glusterfs_volume_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_glusterfs_volume_parent_class)->finalize (object);
}

static void
storaged_linux_glusterfs_volume_init (StoragedLinuxGlusterFSVolume *gfs_volume)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (gfs_volume),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_glusterfs_volume_class_init (StoragedLinuxGlusterFSVolumeClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = storaged_linux_glusterfs_volume_finalize;
}

/**
 * storaged_linux_glusterfs_volume_new:
 *
 * Creates a new #StoragedLinuxGlusterFSVolume instance.
 *
 * Returns: A new #StoragedLinuxGlusterFSVolume. Free with g_object_unref().
 */
StoragedGlusterFSVolume *
storaged_linux_glusterfs_volume_new (void)
{
  return STORAGED_GLUSTERFS_VOLUME (g_object_new (STORAGED_TYPE_LINUX_GLUSTERFS_VOLUME,
                                                  NULL));
}

/**
 * storaged_linux_glusterfs_volume_update:
 * @gfs_volume: A #StoragedLinuxGlusterFSVolume.
 * @object: The enclosing #StoragedLinuxGlusterFSVolumeObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_glusterfs_volume_update (StoragedLinuxGlusterFSVolume *gfs_volume,
                                        GVariant                     *info)
{
  StoragedGlusterFSVolume *iface = STORAGED_GLUSTERFS_VOLUME (gfs_volume);
  const gchar *str;
  guint num;

  if (g_variant_lookup (info, "name", "&s", &str)) {
    storaged_glusterfs_volume_set_name (iface, str);
  }

  if (g_variant_lookup (info, "id", "&s", &str))
    storaged_glusterfs_volume_set_id (iface, str);

  if (g_variant_lookup (info, "status", "u", &num))
    storaged_glusterfs_volume_set_status (iface, num);

  if (g_variant_lookup (info, "brickCount", "u", &num))
    storaged_glusterfs_volume_set_brickcount (iface, num);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_add_brick (StoragedGlusterFSVolume   *_group,
                  GDBusMethodInvocation     *invocation,
                  const gchar               *arg_brick_path)
{
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
glusterfs_volume_iface_init (StoragedGlusterFSVolumeIface *iface)
{
  iface->handle_add_brick = handle_add_brick;
}

