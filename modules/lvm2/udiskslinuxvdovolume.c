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
#include "udiskslinuxlogicalvolume.h"
#include "udiskslinuxlogicalvolumeobject.h"
#include "udiskslinuxvolumegroup.h"
#include "udiskslinuxvolumegroupobject.h"
#include "udiskslinuxmodulelvm2.h"

#include "udiskslvm2daemonutil.h"
#include "jobhelpers.h"

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

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxVDOVolume, udisks_linux_vdo_volume, UDISKS_TYPE_VDO_VOLUME_SKELETON,
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

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (iface));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
common_setup (UDisksLinuxLogicalVolumeObject *object,
              GDBusMethodInvocation          *invocation,
              GVariant                       *options,
              const gchar                    *auth_err_msg,
              UDisksLinuxModuleLVM2         **module,
              UDisksDaemon                  **daemon,
              uid_t                          *out_uid)
{
  gboolean rc = FALSE;
  GError *error = NULL;

  *module = udisks_linux_logical_volume_object_get_module (object);
  *daemon = udisks_module_get_daemon (UDISKS_MODULE (*module));

  if (!udisks_daemon_util_get_caller_uid_sync (*daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               out_uid,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (*daemon,
                                     UDISKS_OBJECT (object),
                                     LVM2_POLICY_ACTION_ID,
                                     options,
                                     auth_err_msg,
                                     invocation);
  rc = TRUE;
 out:
  return rc;
}

static gboolean
_set_compression_deduplication (UDisksVDOVolume       *_volume,
                                GDBusMethodInvocation *invocation,
                                gboolean               enable,
                                gboolean               compression,
                                gboolean               deduplication,
                                GVariant              *options)
{
  GError *error = NULL;
  UDisksLinuxVDOVolume *volume = UDISKS_LINUX_VDO_VOLUME (_volume);
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksLinuxModuleLVM2 *module = NULL;
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

  if (!common_setup (object, invocation, options,
                     N_("Authentication is required to set deduplication/compression on a VDO volume"),
                     &module, &daemon, &caller_uid))
    goto out;

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

static gboolean
_vdo_resize (UDisksLinuxLogicalVolumeObject *object,
             GDBusMethodInvocation          *invocation,
             guint64                         new_size,
             GVariant                       *options)
{
  GError *error = NULL;
  UDisksLinuxModuleLVM2 *module = NULL;
  UDisksDaemon *daemon = NULL;
  UDisksLinuxVolumeGroupObject *group_object = NULL;
  uid_t caller_uid;
  LVJobData data;
  gboolean success = FALSE;

  if (!common_setup (object, invocation, options,
                     N_("Authentication is required to resize a VDO volume"),
                     &module, &daemon, &caller_uid))
    goto out;

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  data.vg_name = udisks_linux_volume_group_object_get_name (group_object);
  data.lv_name = udisks_linux_logical_volume_object_get_name (object);
  data.new_lv_size = new_size;

  data.resize_fs = FALSE;
  data.force = FALSE;
  g_variant_lookup (options, "resize_fsys", "b", &(data.resize_fs));
  g_variant_lookup (options, "force", "b", &(data.force));

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               UDISKS_OBJECT (object),
                                               "lvm-lvol-resize",
                                               caller_uid,
                                               lvresize_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error resizing VDO volume: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  success = TRUE;

 out:
  return success;
}

static gboolean
handle_resize_logical (UDisksVDOVolume       *_volume,
                       GDBusMethodInvocation *invocation,
                       guint64                new_size,
                       GVariant              *options)
{
  UDisksLinuxLogicalVolumeObject *object = NULL;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (_volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (_vdo_resize (object, invocation, new_size, options))
    udisks_vdo_volume_complete_resize_logical (_volume, invocation);

out:
  g_clear_object (&object);
  return TRUE;
}

static gboolean
handle_resize_physical (UDisksVDOVolume       *_volume,
                        GDBusMethodInvocation *invocation,
                        guint64                new_size,
                        GVariant              *options)
{
  const gchar *pool_path = NULL;
  UDisksObject *pool_object = NULL;
  UDisksLinuxLogicalVolumeObject *object = NULL;
  GError *error = NULL;
  UDisksLinuxModuleLVM2 *module;
  UDisksDaemon *daemon = NULL;

  object = udisks_daemon_util_dup_object (_volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  pool_path = udisks_vdo_volume_get_vdo_pool (_volume);
  if (pool_path == NULL || g_strcmp0 (pool_path, "/") == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Failed to get VDO pool path.");
      goto out;
    }

  module = udisks_linux_logical_volume_object_get_module (object);
  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  pool_object = udisks_daemon_find_object (daemon, pool_path);
  if (pool_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Failed to get VDO pool object.");
      goto out;
    }

  if (_vdo_resize (UDISKS_LINUX_LOGICAL_VOLUME_OBJECT (pool_object),
                   invocation, new_size, options))
    udisks_vdo_volume_complete_resize_physical (_volume, invocation);

out:
  g_clear_object (&object);
  g_clear_object (&pool_object);
  return TRUE;
}

static void
stats_add_element (const gchar *key, const gchar *value, GVariantBuilder *builder)
{
  g_variant_builder_add (builder, "{ss}", key, value);
}

static gboolean
handle_get_statistics (UDisksVDOVolume       *_volume,
                       GDBusMethodInvocation *invocation,
                       GVariant              *options)
{
  const gchar *pool_path = NULL;
  UDisksObject *pool_object = NULL;
  UDisksLinuxLogicalVolumeObject *object = NULL;
  UDisksLinuxVolumeGroupObject *group_object = NULL;
  GError *error = NULL;
  UDisksLinuxModuleLVM2 *module;
  UDisksDaemon *daemon = NULL;
  GHashTable *stats = NULL;
  GVariantBuilder builder;
  const gchar *pool_name = NULL;
  const gchar *vg_name = NULL;

  object = udisks_daemon_util_dup_object (_volume, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  group_object = udisks_linux_logical_volume_object_get_volume_group (object);
  vg_name = udisks_linux_volume_group_object_get_name (group_object);

  pool_path = udisks_vdo_volume_get_vdo_pool (_volume);
  if (pool_path == NULL || g_strcmp0 (pool_path, "/") == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Failed to get VDO pool path.");
      goto out;
    }

  module = udisks_linux_logical_volume_object_get_module (object);
  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  pool_object = udisks_daemon_find_object (daemon, pool_path);
  if (pool_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Failed to get VDO pool object.");
      goto out;
    }

  pool_name = udisks_linux_logical_volume_object_get_name (UDISKS_LINUX_LOGICAL_VOLUME_OBJECT (pool_object));

  stats = bd_lvm_vdo_get_stats_full (vg_name, pool_name, &error);
  if (stats == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error retrieving volume statistics: %s",
                                             error->message);
      g_error_free (error);
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  g_hash_table_foreach (stats, (GHFunc) stats_add_element, &builder);

  udisks_vdo_volume_complete_get_statistics (_volume, invocation, g_variant_builder_end (&builder));
  g_hash_table_destroy (stats);

out:
  g_clear_object (&object);
  g_clear_object (&pool_object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
vdo_volume_iface_init (UDisksVDOVolumeIface *iface)
{
  iface->handle_enable_compression = handle_enable_compression;
  iface->handle_enable_deduplication = handle_enable_deduplication;
  iface->handle_resize_logical = handle_resize_logical;
  iface->handle_resize_physical = handle_resize_physical;
  iface->handle_get_statistics = handle_get_statistics;
}
