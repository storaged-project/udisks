/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2019 Vojtech Trefny <vtrefny@redhat.com>
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
#include <fcntl.h>
#include <glib/gi18n-lib.h>

#include <blockdev/lvm.h>

#include <src/udiskslogging.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udisksdaemon.h>
#include <src/udisksstate.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslinuxblock.h>

#include "udiskslinuxvdovolume.h"
#include "udiskslinuxlogicalvolumeobject.h"
#include "udiskslinuxvolumegroup.h"
#include "udiskslinuxvolumegroupobject.h"

#include "udiskslvm2daemonutil.h"
#include "udiskslvm2dbusutil.h"
#include "udiskslvm2util.h"
#include "jobhelpers.h"
#include "udisks-lvm2-generated.h"

/**
 * SECTION:udisklinuxvdovolume
 * @title: UDisksLinuxVDOVolume
 * @short_description: Linux implementation of #UDisksVDOVolume
 *
 * This type provides an implementation of the #UDisksVDOVolume
 * interface on Linux.
 */

typedef struct _UDisksLinuxVDOVolumeClass   UDisksLinuxVDOVolumeClass;

/**
 * UDisksLinuxVDOVolume:
 *
 * The #UDisksLinuxVDOVolume structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxVDOVolume
{
  UDisksVDOVolumeSkeleton parent_instance;
};

struct _UDisksLinuxVDOVolumeClass
{
  UDisksVDOVolumeSkeletonClass parent_class;
};

static void vdo_volume_iface_init (UDisksVDOVolumeIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxVDOVolume, udisks_linux_vdo_volume,
                         UDISKS_TYPE_VDO_VOLUME_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_VDO_VOLUME, vdo_volume_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_vdo_volume_init (UDisksLinuxVDOVolume *vdo_volume)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (vdo_volume),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_vdo_volume_constructed (GObject *_object)
{
  if (G_OBJECT_CLASS (udisks_linux_vdo_volume_parent_class)->constructed != NULL)
      G_OBJECT_CLASS (udisks_linux_vdo_volume_parent_class)->constructed (_object);
}

static void
udisks_linux_vdo_volume_finalize (GObject *_object)
{
  if (G_OBJECT_CLASS (udisks_linux_vdo_volume_parent_class)->finalize != NULL)
      G_OBJECT_CLASS (udisks_linux_vdo_volume_parent_class)->finalize (_object);
}

static void
udisks_linux_vdo_volume_class_init (UDisksLinuxVDOVolumeClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize    = udisks_linux_vdo_volume_finalize;
  gobject_class->constructed = udisks_linux_vdo_volume_constructed;
}

/**
 * udisks_linux_vdo_volume_new:
 *
 * Creates a new #UDisksLinuxVDOVolume instance.
 *
 * Returns: A new #UDisksLinuxVDOVolume. Free with g_object_unref().
 */
UDisksVDOVolume *
udisks_linux_vdo_volume_new (void)
{
  return UDISKS_VDO_VOLUME (g_object_new (UDISKS_TYPE_LINUX_VDO_VOLUME,
                                          NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

void
udisks_linux_vdo_volume_update (UDisksLinuxVDOVolume         *vdo_volume,
                                UDisksLinuxVolumeGroupObject *group_object,
                                BDLVMLVdata                  *lv_info,
                                BDLVMVDOPooldata             *vdo_info)
{
  UDisksVDOVolume *iface;
  UDisksLinuxLogicalVolumeObject *pool_object = NULL;
  const gchar *value = NULL;
  const char *pool_objpath;
  GError *loc_error = NULL;

  iface = UDISKS_VDO_VOLUME (vdo_volume);

  pool_objpath = "/";
  if (lv_info->pool_lv)
    {
      pool_object = udisks_linux_volume_group_object_find_logical_volume_object (group_object, lv_info->pool_lv);
      if (pool_object)
          pool_objpath = g_dbus_object_get_object_path (G_DBUS_OBJECT (pool_object));
    }
  udisks_vdo_volume_set_vdo_pool (iface, pool_objpath);

  value = bd_lvm_get_vdo_operating_mode_str (vdo_info->operating_mode, &loc_error);
  if (!value)
    {
      g_clear_error (&loc_error);
      udisks_vdo_volume_set_operating_mode (iface, "");
    }
  else
      udisks_vdo_volume_set_operating_mode (iface, value);

  value = bd_lvm_get_vdo_compression_state_str (vdo_info->compression_state, &loc_error);
  if (!value)
    {
      g_clear_error (&loc_error);
      udisks_vdo_volume_set_compression_state (iface, "");
    }
  else
      udisks_vdo_volume_set_compression_state (iface, value);

  value = bd_lvm_get_vdo_index_state_str (vdo_info->index_state, &loc_error);
  if (!value)
    {
      g_clear_error (&loc_error);
      udisks_vdo_volume_set_index_state (iface, "");
    }
  else
      udisks_vdo_volume_set_index_state (iface, value);

  udisks_vdo_volume_set_used_size (iface, vdo_info->used_size);

  udisks_vdo_volume_set_compression (iface, vdo_info->compression);
  udisks_vdo_volume_set_deduplication (iface, vdo_info->deduplication);
}

static gboolean _set_compression_deduplication (UDisksVDOVolume       *_volume,
                                                GDBusMethodInvocation *invocation,
                                                gboolean               enable,
                                                gboolean               compression,
                                                gboolean               deduplication,
                                                GVariant              *options)
{
    GError *error = NULL;
    UDisksLinuxVDOVolume *volume = UDISKS_LINUX_VDO_VOLUME (_volume);
    UDisksLinuxLogicalVolumeObject *object = NULL;
    UDisksDaemon *daemon = NULL;
    uid_t caller_uid;
    UDisksLinuxVolumeGroupObject *group_object = NULL;
    LVJobData data;

    object = udisks_daemon_util_dup_object (volume, &error);
    if (object == NULL)
      {
        g_dbus_method_invocation_take_error (invocation, error);
        goto out;
      }

    daemon = udisks_linux_logical_volume_object_get_daemon (object);

    if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &error))
      {
        g_dbus_method_invocation_return_gerror (invocation, error);
        g_clear_error (&error);
        goto out;
      }

    /* Policy check. */
    UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       UDISKS_OBJECT (object),
                                       lvm2_policy_action_id,
                                       options,
                                       N_("Authentication is required to set deduplication/compression on a VDO volume"),
                                       invocation);

    group_object = udisks_linux_logical_volume_object_get_volume_group (object);
    data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
    data.lv_name = udisks_linux_logical_volume_object_get_name (object);
    if (compression)
        data.compression = enable;
    else if (deduplication)
        data.deduplication = enable;

    if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                                 UDISKS_OBJECT (object),
                                                 "lvm-vdo-dedup-comp",
                                                 caller_uid,
                                                 compression ? lv_vdo_compression_job_func : lv_vdo_deduplication_job_func,
                                                 &data,
                                                 NULL, /* user_data_free_func */
                                                 NULL, /* GCancellable */
                                                 &error))
      {
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Error setting deduplication/compression on the VDO volume: %s",
                                               error->message);
        g_clear_error (&error);
        goto out;
      }

    if (compression)
        udisks_vdo_volume_complete_enable_compression (_volume, invocation);
    else if (deduplication)
        udisks_vdo_volume_complete_enable_deduplication (_volume, invocation);

  out:
    g_clear_object (&object);
    return TRUE;
}

static gboolean
handle_enable_compression (UDisksVDOVolume       *_volume,
                           GDBusMethodInvocation *invocation,
                           gboolean               enable,
                           GVariant              *options)
{
    return _set_compression_deduplication (_volume, invocation, enable,
                                           TRUE /* compression */, FALSE /* deduplication */, options);
}

static gboolean
handle_enable_deduplication (UDisksVDOVolume       *_volume,
                             GDBusMethodInvocation *invocation,
                             gboolean               enable,
                             GVariant              *options)
{
    return _set_compression_deduplication (_volume, invocation, enable,
                                           FALSE /* compression */, TRUE /* deduplication */, options);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
vdo_volume_iface_init (UDisksVDOVolumeIface *iface)
{
  iface->handle_enable_compression = handle_enable_compression;
  iface->handle_enable_deduplication = handle_enable_deduplication;
}
