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
#include <blockdev/vdo.h>

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslogging.h>
#include <src/udiskssimplejob.h>

#include "udiskslinuxmanagervdo.h"
#include "udisks-vdo-generated.h"

/**
 * SECTION:udiskslinuxmanagervdo
 * @title: UDisksLinuxManagerVDO
 * @short_description: Linux implementation of #UDisksLinuxManagerVDO
 *
 * This type provides an implementation of the #UDisksLinuxManagerVDO
 * interface on Linux.
 */

/**
 * UDisksLinuxManagerVDO:
 *
 * The #UDisksLinuxManagerVDO structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxManagerVDO{
  UDisksManagerVDOSkeleton parent_instance;

  UDisksDaemon *daemon;
};

struct _UDisksLinuxManagerVDOClass {
  UDisksManagerVDOSkeletonClass parent_class;
};

static void udisks_linux_manager_vdo_iface_init (UDisksManagerVDOIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerVDO, udisks_linux_manager_vdo,
                         UDISKS_TYPE_MANAGER_VDO_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_VDO,
                                                udisks_linux_manager_vdo_iface_init));

enum
{
  PROP_0,
  PROP_DAEMON,
  N_PROPERTIES
};

static void
udisks_linux_manager_vdo_get_property (GObject *object, guint property_id,
                                       GValue *value, GParamSpec *pspec)
{
  UDisksLinuxManagerVDO *manager = UDISKS_LINUX_MANAGER_VDO (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_manager_vdo_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_vdo_set_property (GObject *object, guint property_id,
                                       const GValue *value, GParamSpec *pspec)
{
  UDisksLinuxManagerVDO *manager = UDISKS_LINUX_MANAGER_VDO (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_assert (manager->daemon == NULL);
      /* We don't take a reference to the daemon */
      manager->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_vdo_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_manager_vdo_parent_class))
      G_OBJECT_CLASS (udisks_linux_manager_vdo_parent_class)->dispose (object);
}

static void
udisks_linux_manager_vdo_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_manager_vdo_parent_class))
      G_OBJECT_CLASS (udisks_linux_manager_vdo_parent_class)->finalize (object);
}

static void
udisks_linux_manager_vdo_class_init (UDisksLinuxManagerVDOClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_manager_vdo_get_property;
  gobject_class->set_property = udisks_linux_manager_vdo_set_property;
  gobject_class->dispose = udisks_linux_manager_vdo_dispose;
  gobject_class->finalize = udisks_linux_manager_vdo_finalize;

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
udisks_linux_manager_vdo_init (UDisksLinuxManagerVDO *self)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_manager_vdo_new:
 * @daemon: A #UDisksDaemon.
 *
 * Creates a new #UDisksLinuxManagerVDO instance.
 *
 * Returns: A new #UDisksLinuxManagerVDO. Free with g_object_unref().
 */
UDisksLinuxManagerVDO *
udisks_linux_manager_vdo_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_LINUX_MANAGER_VDO(g_object_new (UDISKS_TYPE_LINUX_MANAGER_VDO,
                                                "daemon", daemon,
                                                NULL));
}

/**
 * udisks_linux_manager_vdo_get_daemon:
 * @manager: A #UDisksLinuxManagerVDO.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @manager.
 */
UDisksDaemon *
udisks_linux_manager_vdo_get_daemon (UDisksLinuxManagerVDO *manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER_VDO (manager), NULL);
  return manager->daemon;
}


static UDisksObject *
wait_for_vdo_object (UDisksDaemon *daemon,
                     gpointer      user_data)
{
  UDisksObject *object;
  UDisksBlock *block;
  const gchar *dm_name = (const gchar *) user_data;
  gchar *real_path;

  /* Rely on /dev/mapper/<name> for the moment until more reliable way is found */
  real_path = udisks_daemon_util_resolve_link ("/dev/mapper/", dm_name);
  if (real_path == NULL)
    return NULL;

  object = udisks_daemon_find_block_by_device_file (daemon, real_path);
  g_free (real_path);
  if (object == NULL)
    return NULL;

  block = udisks_object_peek_block (object);
  if (block == NULL)
    {
      g_clear_object (&object);
      return NULL;
    }

  return object;
}

static gboolean
handle_create_volume (UDisksManagerVDO      *manager,
                      GDBusMethodInvocation *invocation,
                      const gchar           *arg_name,
                      const gchar           *arg_device,
                      guint64                arg_logical_size,
                      guint64                arg_index_memory,
                      gboolean               arg_compression,
                      gboolean               arg_deduplication,
                      const gchar           *arg_write_policy,
                      GVariant              *arg_options)
{
  UDisksLinuxManagerVDO *l_manager = UDISKS_LINUX_MANAGER_VDO (manager);
  UDisksObject *block_object;
  UDisksObject *vdo_object;
  UDisksBlock *block;
  UDisksBaseJob *job;
  BDVDOWritePolicy write_policy;
  GError *error = NULL;
  gchar *dev;
  uid_t caller_uid;

  /* Policy check. */
  if (! udisks_daemon_util_check_authorization_sync (l_manager->daemon,
                                                     NULL,
                                                     "org.freedesktop.udisks2.vdo.manage-vdo",
                                                     arg_options,
                                                     N_("Authentication is required to create a new VDO volume"),
                                                     invocation))
    return TRUE;

  write_policy = bd_vdo_get_write_policy_from_str (arg_write_policy, &error);
  if (error != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error determining VDO write policy: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }

  if (! udisks_daemon_util_get_caller_uid_sync (l_manager->daemon,
                                                invocation,
                                                NULL /* GCancellable */,
                                                &caller_uid,
                                                NULL, NULL,
                                                &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  block_object = udisks_daemon_find_object (l_manager->daemon, arg_device);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Invalid object path %s",
                                             arg_device);
      return TRUE;
    }

  block = udisks_object_get_block (block_object);
  if (block == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Object path %s is not a block device",
                                             arg_device);
      g_object_unref (block_object);
      return TRUE;
    }

  job = udisks_daemon_launch_simple_job (l_manager->daemon,
                                         UDISKS_OBJECT (block_object),
                                         "vdo-create-volume",
                                         caller_uid,
                                         NULL /* cancellable */);
  g_warn_if_fail (job != NULL);

  dev = udisks_block_dup_device (block);
  g_object_unref (block);
  if (! bd_vdo_create (arg_name, dev, arg_logical_size, arg_index_memory, arg_compression, arg_deduplication, write_policy, NULL, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error creating new VDO volume: %s",
                                             error->message);
      g_error_free (error);
      g_object_unref (block_object);
      g_free (dev);
      return TRUE;
    }
  g_free (dev);

  /* Sit and wait for the VDO object to show up */
  vdo_object = udisks_daemon_wait_for_object_sync (l_manager->daemon,
                                                   wait_for_vdo_object,
                                                   (gpointer) arg_name,
                                                   NULL,
                                                   10, /* timeout_seconds */
                                                   &error);
  if (vdo_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for VDO object after creating %s: ",
                      arg_name);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_take_error (invocation, error);
      g_object_unref (block_object);
      return TRUE;
    }

  /* Complete the DBus call */
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);
  udisks_manager_vdo_complete_create_volume (manager, invocation,
                                             g_dbus_object_get_object_path (G_DBUS_OBJECT (vdo_object)));
  g_object_unref (vdo_object);
  g_object_unref (block_object);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_activate_volume_by_name (UDisksManagerVDO      *manager,
                                GDBusMethodInvocation *invocation,
                                const gchar           *arg_name,
                                GVariant              *arg_options)
{
  UDisksLinuxManagerVDO *l_manager = UDISKS_LINUX_MANAGER_VDO (manager);
  GError *error = NULL;

  /* Policy check. */
  if (! udisks_daemon_util_check_authorization_sync (l_manager->daemon,
                                                     NULL,
                                                     "org.freedesktop.udisks2.vdo.manage-vdo",
                                                     arg_options,
                                                     N_("Authentication is required to activate existing VDO volume"),
                                                     invocation))
    return TRUE;

  if (! bd_vdo_activate (arg_name, NULL, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error activating VDO volume: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }

  /* Complete the DBus call */
  udisks_manager_vdo_complete_activate_volume_by_name (manager, invocation);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_start_volume_by_name (UDisksManagerVDO      *manager,
                             GDBusMethodInvocation *invocation,
                             const gchar           *arg_name,
                             gboolean               arg_force_rebuild,
                             GVariant              *arg_options)
{
  UDisksLinuxManagerVDO *l_manager = UDISKS_LINUX_MANAGER_VDO (manager);
  UDisksObject *object;
  UDisksBaseJob *job;
  GError *error = NULL;
  uid_t caller_uid;

  /* Policy check. */
  if (! udisks_daemon_util_check_authorization_sync (l_manager->daemon,
                                                     NULL,
                                                     "org.freedesktop.udisks2.vdo.manage-vdo",
                                                     arg_options,
                                                     N_("Authentication is required to start VDO volume"),
                                                     invocation))
    return TRUE;

  if (! udisks_daemon_util_get_caller_uid_sync (l_manager->daemon,
                                                invocation,
                                                NULL /* GCancellable */,
                                                &caller_uid,
                                                NULL, NULL,
                                                &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  job = udisks_daemon_launch_simple_job (l_manager->daemon,
                                         NULL,
                                         "vdo-start-volume",
                                         caller_uid,
                                         NULL /* cancellable */);
  g_warn_if_fail (job != NULL);

  if (! bd_vdo_start (arg_name, arg_force_rebuild, NULL, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error starting volume: %s",
                                             error->message);
      g_error_free (error);
      return TRUE;
    }

  /* Sit and wait for the VDO object to show up */
  object = udisks_daemon_wait_for_object_sync (l_manager->daemon,
                                               wait_for_vdo_object,
                                               (gpointer) arg_name,
                                               NULL,
                                               10, /* timeout_seconds */
                                               &error);
  if (object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for VDO object after starting %s: ",
                      arg_name);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  /* Complete the DBus call */
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);
  udisks_manager_vdo_complete_start_volume_by_name (manager, invocation,
                                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  g_object_unref (object);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static void
udisks_linux_manager_vdo_iface_init (UDisksManagerVDOIface *iface)
{
  iface->handle_create_volume = handle_create_volume;
  iface->handle_activate_volume_by_name = handle_activate_volume_by_name;
  iface->handle_start_volume_by_name = handle_start_volume_by_name;
}
