/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2018 Tomas Bzatek <tbzatek@redhat.com>
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
 */

#include "config.h"

#include <glib/gi18n.h>

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslogging.h>
#include <src/udiskssimplejob.h>
#include <blockdev/vdo.h>

#include "udiskslinuxblockvdo.h"
#include "udisks-vdo-generated.h"

/**
 * SECTION:udiskslinuxblockvdo
 * @title: UDisksLinuxBlockVDO
 * @short_description: Linux implementation of #UDisksBlockVDO
 *
 * This type provides an implementation of #UDisksBlockVDO interface
 * on Linux.
 */

/**
 * UDisksLinuxBlockVDO:
 *
 * The #UDisksLinuxBlockVDO structure contains only private data and
 * should be only accessed using provided API.
 */
struct _UDisksLinuxBlockVDO {
  UDisksBlockVDOSkeleton parent_instance;

  UDisksDaemon *daemon;
};

struct _UDisksLinuxBlockVDOClass {
  UDisksBlockVDOSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  N_PROPERTIES
};

static void udisks_linux_block_vdo_iface_init (UDisksBlockVDOIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxBlockVDO, udisks_linux_block_vdo,
                         UDISKS_TYPE_BLOCK_VDO_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_BLOCK_VDO,
                                                udisks_linux_block_vdo_iface_init));

static void
udisks_linux_block_vdo_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  UDisksLinuxBlockVDO *l_block_vdo = UDISKS_LINUX_BLOCK_VDO (object);

  switch (property_id)
    {
      case PROP_DAEMON:
        g_value_set_object (value, udisks_linux_block_vdo_get_daemon (l_block_vdo));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
udisks_linux_block_vdo_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  UDisksLinuxBlockVDO *l_block_vdo = UDISKS_LINUX_BLOCK_VDO (object);

  switch (property_id)
    {
      case PROP_DAEMON:
        g_assert (l_block_vdo->daemon == NULL);
        /* We don't take a reference to the daemon. */
        l_block_vdo->daemon = g_value_get_object (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
udisks_linux_block_vdo_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_block_vdo_parent_class))
    G_OBJECT_CLASS (udisks_linux_block_vdo_parent_class)->dispose (object);
}

static void
udisks_linux_block_vdo_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_block_vdo_parent_class))
    G_OBJECT_CLASS (udisks_linux_block_vdo_parent_class)->finalize (object);
}

static void
udisks_linux_block_vdo_class_init (UDisksLinuxBlockVDOClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_block_vdo_get_property;
  gobject_class->set_property = udisks_linux_block_vdo_set_property;
  gobject_class->dispose = udisks_linux_block_vdo_dispose;
  gobject_class->finalize = udisks_linux_block_vdo_finalize;

  /**
   * UDisksLinuxManager:daemon
   *
   * The #UDisksDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
udisks_linux_block_vdo_init (UDisksLinuxBlockVDO *l_block_vdo)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (l_block_vdo),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_block_vdo_new:
 *
 * Creates a new #UDisksLinuxBlockVDO instance.
 *
 * Returns: A new #UDisksLinuxBlockVDO. Free with g_object_unref().
 */
UDisksLinuxBlockVDO *
udisks_linux_block_vdo_new (void)
{
  return UDISKS_LINUX_BLOCK_VDO (g_object_new (UDISKS_TYPE_LINUX_BLOCK_VDO, NULL));
}

/**
 * udisks_linux_block_vdo_get_daemon:
 * @vdo_block: A #UDisksLinuxBlockVDO.
 *
 * Gets the daemon used by @vdo_block.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @vdo_block.
 */
UDisksDaemon *
udisks_linux_block_vdo_get_daemon (UDisksLinuxBlockVDO *vdo_block)
{
  GError *error = NULL;
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon = NULL;

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_VDO (vdo_block), NULL);

  object = udisks_daemon_util_dup_object (vdo_block, &error);
  if (object)
    {
      daemon = udisks_linux_block_object_get_daemon (object);
      g_clear_object (&object);
    }
  else
    {
      udisks_critical ("%s", error->message);
      g_clear_error (&error);
    }

  return daemon;
}

/* Get info of the volume and set object properties */
static gboolean
do_refresh (UDisksBlockVDO *block_vdo,
            const gchar    *vdo_name,
            GError        **error)
{
  BDVDOInfo *bd_info;

  bd_info = bd_vdo_info (vdo_name, error);
  if (bd_info == NULL)
    {
      return FALSE;
    }

  /* Update the interface */
  udisks_block_vdo_set_active (block_vdo, bd_info->active);
  udisks_block_vdo_set_compression (block_vdo, bd_info->compression);
  udisks_block_vdo_set_deduplication (block_vdo, bd_info->deduplication);
  udisks_block_vdo_set_index_memory (block_vdo, bd_info->index_memory);
  udisks_block_vdo_set_logical_size (block_vdo, bd_info->logical_size);
  udisks_block_vdo_set_name (block_vdo, bd_info->name);
  udisks_block_vdo_set_physical_size (block_vdo, bd_info->physical_size);
  udisks_block_vdo_set_write_policy (block_vdo,
                                     bd_vdo_get_write_policy_str (bd_info->write_policy, NULL));

  bd_vdo_info_free (bd_info);

  return TRUE;
}

/**
 * udisks_linux_block_vdo_update:
 * @vdo_block: A #UDisksLinuxBlockVDO.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if the configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_block_vdo_update (UDisksLinuxBlockVDO    *l_block_vdo,
                               UDisksLinuxBlockObject *object)
{
  UDisksBlockVDO *block_vdo = UDISKS_BLOCK_VDO (l_block_vdo);
  UDisksLinuxDevice *device;
  GError *error = NULL;
  const gchar *dm_name;

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_VDO (l_block_vdo), FALSE);
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  device = udisks_linux_block_object_get_device (object);
  dm_name = g_udev_device_get_property (device->udev_device, "DM_NAME");

  if (dm_name == NULL)
    {
      udisks_critical ("Can't get DM_NAME attribute for the VDO volume");
      g_object_unref (device);
      return FALSE;
    }

  if (! do_refresh (block_vdo, dm_name, &error))
    {
      udisks_critical ("Can't get VDO volume info for %s: %s (%s, %d)",
                       dm_name,
                       error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      g_object_unref (device);
      return FALSE;
    }

  g_object_unref (device);

  return TRUE;
}

static gboolean
check_pk_auth (UDisksBlockVDO        *block_vdo,
               GDBusMethodInvocation *invocation,
               GVariant              *arg_options,
               const gchar           *polkit_message,
               const gchar           *job_operation,
               UDisksBaseJob        **job)
{
  UDisksLinuxBlockVDO *l_block_vdo = UDISKS_LINUX_BLOCK_VDO (block_vdo);
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon;
  GError *error = NULL;
  uid_t caller_uid;

  daemon = udisks_linux_block_vdo_get_daemon (l_block_vdo);

  if (! udisks_daemon_util_get_caller_uid_sync (daemon,
                                                invocation,
                                                NULL /* GCancellable */,
                                                &caller_uid,
                                                NULL, NULL,
                                                &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return FALSE;
    }

  object = udisks_daemon_util_dup_object (l_block_vdo, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return FALSE;
    }

  /* Policy check */
  if (! udisks_daemon_util_check_authorization_sync (daemon,
                                                     UDISKS_OBJECT (object),
                                                     "org.freedesktop.udisks2.vdo.manage-vdo",
                                                     arg_options,
                                                     polkit_message,
                                                     invocation))
    {
      g_object_unref (object);
      return FALSE;
    }

  /* want to create a job */
  if (job && job_operation)
    {
      *job = udisks_daemon_launch_simple_job (daemon,
                                              UDISKS_OBJECT (object),
                                              job_operation,
                                              caller_uid,
                                              NULL /* cancellable */);
      g_warn_if_fail (UDISKS_IS_SIMPLE_JOB (*job));
      if (*job != NULL)
        /* tie the "object" lifecycle to the job and unref it when the job is finished */
        g_object_set_data_full (G_OBJECT (*job), job_operation, g_object_ref (object), g_object_unref);
    }
  g_object_unref (object);

  return TRUE;
}

static gboolean
handle_change_write_policy (UDisksBlockVDO        *block_vdo,
                            GDBusMethodInvocation *invocation,
                            const gchar           *arg_write_policy,
                            GVariant              *arg_options)
{
  const gchar *dm_name;
  GError *error = NULL;
  BDVDOWritePolicy write_policy;

  if (! check_pk_auth (block_vdo, invocation, arg_options,
                       N_("Authentication is required to change the write policy of the VDO volume"),
                       NULL, NULL))
    return TRUE;

  write_policy = bd_vdo_get_write_policy_from_str (arg_write_policy, &error);
  if (error != NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  dm_name = udisks_block_vdo_get_name (block_vdo);
  if (! bd_vdo_change_write_policy (dm_name, write_policy, NULL, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error changing write policy: %s",
                                             error->message);
      g_error_free (error);
      /* Perform refresh anyway, without error checking */
      do_refresh (block_vdo, dm_name, NULL);
      return TRUE;
    }
  /* Perform refresh */
  if (! do_refresh (block_vdo, dm_name, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error getting info after changing write policy: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }
  udisks_block_vdo_complete_change_write_policy (block_vdo, invocation);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_deactivate (UDisksBlockVDO        *block_vdo,
                   GDBusMethodInvocation *invocation,
                   GVariant              *arg_options)
{
  const gchar *dm_name;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;

  if (! check_pk_auth (block_vdo, invocation, arg_options,
                       N_("Authentication is required to deactivate the VDO volume"),
                       "vdo-deactivate", &job))
    return TRUE;

  dm_name = udisks_block_vdo_get_name (block_vdo);
  if (! bd_vdo_deactivate (dm_name, NULL, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error deactivating volume: %s",
                                             error->message);
      g_error_free (error);
      /* Perform refresh anyway, without error checking */
      do_refresh (block_vdo, dm_name, NULL);
      return TRUE;
    }
  /* Perform refresh */
  if (! do_refresh (block_vdo, dm_name, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error getting info after deactivating the volume: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);
  udisks_block_vdo_complete_deactivate (block_vdo, invocation);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_enable_compression (UDisksBlockVDO        *block_vdo,
                           GDBusMethodInvocation *invocation,
                           gboolean               arg_enable,
                           GVariant              *arg_options)
{
  const gchar *dm_name;
  gboolean ret;
  GError *error = NULL;

  if (! check_pk_auth (block_vdo, invocation, arg_options,
                       arg_enable ?
                           N_("Authentication is required to enable compression on the VDO volume") :
                           N_("Authentication is required to disable compression on the VDO volume"),
                       NULL, NULL))
    return TRUE;

  dm_name = udisks_block_vdo_get_name (block_vdo);
  ret = arg_enable ?
          bd_vdo_enable_compression (dm_name, NULL, &error) :
          bd_vdo_disable_compression (dm_name, NULL, &error);
  if (! ret)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error changing compression state: %s",
                                             error->message);
      g_error_free (error);
      /* Perform refresh anyway, without error checking */
      do_refresh (block_vdo, dm_name, NULL);
      return TRUE;
    }
  /* Perform refresh */
  if (! do_refresh (block_vdo, dm_name, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error getting info after changing compression state: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }
  udisks_block_vdo_complete_enable_compression (block_vdo, invocation);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_enable_deduplication (UDisksBlockVDO        *block_vdo,
                             GDBusMethodInvocation *invocation,
                             gboolean               arg_enable,
                             GVariant              *arg_options)
{
  const gchar *dm_name;
  gboolean ret;
  GError *error = NULL;

  if (! check_pk_auth (block_vdo, invocation, arg_options,
                       arg_enable ?
                           N_("Authentication is required to enable deduplication on the VDO volume") :
                           N_("Authentication is required to disable deduplication on the VDO volume"),
                       NULL, NULL))
    return TRUE;

  dm_name = udisks_block_vdo_get_name (block_vdo);
  ret = arg_enable ?
          bd_vdo_enable_deduplication (dm_name, NULL, &error) :
          bd_vdo_disable_deduplication (dm_name, NULL, &error);
  if (! ret)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error changing deduplication: %s",
                                             error->message);
      g_error_free (error);
      /* Perform refresh anyway, without error checking */
      do_refresh (block_vdo, dm_name, NULL);
      return TRUE;
    }
  /* Perform refresh */
  if (! do_refresh (block_vdo, dm_name, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error getting info after changing deduplication: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }
  udisks_block_vdo_complete_enable_deduplication (block_vdo, invocation);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_grow_logical (UDisksBlockVDO        *block_vdo,
                     GDBusMethodInvocation *invocation,
                     guint64                arg_size,
                     GVariant              *arg_options)
{
  const gchar *dm_name;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;

  if (! check_pk_auth (block_vdo, invocation, arg_options,
                       N_("Authentication is required to grow the logical VDO volume size"),
                       "vdo-grow-logical", &job))
    return TRUE;

  dm_name = udisks_block_vdo_get_name (block_vdo);
  if (! bd_vdo_grow_logical (dm_name, arg_size, NULL, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error growing logical size of the volume: %s",
                                             error->message);
      g_error_free (error);
      /* Perform refresh anyway, without error checking */
      do_refresh (block_vdo, dm_name, NULL);
      return TRUE;
    }
  /* Perform refresh */
  if (! do_refresh (block_vdo, dm_name, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error getting info after growing logical size of the volume: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);
  udisks_block_vdo_complete_grow_logical (block_vdo, invocation);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_grow_physical (UDisksBlockVDO        *block_vdo,
                      GDBusMethodInvocation *invocation,
                      GVariant              *arg_options)
{
  const gchar *dm_name;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;

  if (! check_pk_auth (block_vdo, invocation, arg_options,
                       N_("Authentication is required to grow the physical VDO volume size"),
                       "vdo-grow-physical", &job))
    return TRUE;

  dm_name = udisks_block_vdo_get_name (block_vdo);
  if (! bd_vdo_grow_physical (dm_name, NULL, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error growing physical size of the volume: %s",
                                             error->message);
      g_error_free (error);
      /* Perform refresh anyway, without error checking */
      do_refresh (block_vdo, dm_name, NULL);
      return TRUE;
    }
  /* Perform refresh */
  if (! do_refresh (block_vdo, dm_name, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error getting info after growing physical size of the volume: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);
  udisks_block_vdo_complete_grow_physical (block_vdo, invocation);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_remove (UDisksBlockVDO        *block_vdo,
               GDBusMethodInvocation *invocation,
               gboolean               arg_force,
               GVariant              *arg_options)
{
  const gchar *dm_name;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;

  if (! check_pk_auth (block_vdo, invocation, arg_options,
                       N_("Authentication is required to remove the VDO volume"),
                       "vdo-remove", &job))
    return TRUE;

  dm_name = udisks_block_vdo_get_name (block_vdo);
  if (! bd_vdo_remove (dm_name, arg_force, NULL, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error removing volume: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);
  /* Assuming uevent generated that would trigger object refresh */
  udisks_block_vdo_complete_remove (block_vdo, invocation);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_stop (UDisksBlockVDO        *block_vdo,
             GDBusMethodInvocation *invocation,
             gboolean               arg_force,
             GVariant              *arg_options)
{
  const gchar *dm_name;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;

  if (! check_pk_auth (block_vdo, invocation, arg_options,
                       N_("Authentication is required to stop the VDO volume"),
                       "vdo-stop", &job))
    return TRUE;

  dm_name = udisks_block_vdo_get_name (block_vdo);
  if (! bd_vdo_stop (dm_name, arg_force, NULL, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error stopping volume: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);
  /* Assuming uevent generated that would trigger object refresh */
  udisks_block_vdo_complete_stop (block_vdo, invocation);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static void
stats_add_element (const gchar *key, const gchar *value, GVariantBuilder *builder)
{
  g_variant_builder_add (builder, "{ss}", key, value);
}

static gboolean
handle_get_statistics (UDisksBlockVDO        *block_vdo,
                       GDBusMethodInvocation *invocation,
                       GVariant              *arg_options)
{
  const gchar *dm_name;
  GHashTable *stats;
  GVariantBuilder builder;
  GError *error = NULL;

  dm_name = udisks_block_vdo_get_name (block_vdo);
  stats = bd_vdo_get_stats_full (dm_name, &error);
  if (stats == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error retrieving volume statistics: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  g_hash_table_foreach (stats, (GHFunc) stats_add_element, &builder);
  udisks_block_vdo_complete_get_statistics (block_vdo, invocation, g_variant_builder_end (&builder));
  g_hash_table_destroy (stats);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static void
udisks_linux_block_vdo_iface_init (UDisksBlockVDOIface *iface)
{
  iface->handle_change_write_policy = handle_change_write_policy;
  iface->handle_deactivate = handle_deactivate;
  iface->handle_enable_compression = handle_enable_compression;
  iface->handle_enable_deduplication = handle_enable_deduplication;
  iface->handle_grow_logical = handle_grow_logical;
  iface->handle_grow_physical = handle_grow_physical;
  iface->handle_remove = handle_remove;
  iface->handle_stop = handle_stop;
  iface->handle_get_statistics = handle_get_statistics;
}
