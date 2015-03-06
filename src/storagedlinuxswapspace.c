/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#include <glib/gstdio.h>

#include "storagedlogging.h"
#include "storagedlinuxswapspace.h"
#include "storagedlinuxblockobject.h"
#include "storageddaemon.h"
#include "storageddaemonutil.h"
#include "storagedmountmonitor.h"
#include "storagedlinuxdevice.h"

/**
 * SECTION:storagedlinuxswapspace
 * @title: StoragedLinuxSwapspace
 * @short_description: Linux implementation of #StoragedSwapspace
 *
 * This type provides an implementation of the #StoragedSwapspace interface
 * on Linux.
 */

typedef struct _StoragedLinuxSwapspaceClass   StoragedLinuxSwapspaceClass;

/**
 * StoragedLinuxSwapspace:
 *
 * The #StoragedLinuxSwapspace structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxSwapspace
{
  StoragedSwapspaceSkeleton parent_instance;
};

struct _StoragedLinuxSwapspaceClass
{
  StoragedSwapspaceSkeletonClass parent_class;
};

static void swapspace_iface_init (StoragedSwapspaceIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxSwapspace, storaged_linux_swapspace, STORAGED_TYPE_SWAPSPACE_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_SWAPSPACE, swapspace_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_swapspace_init (StoragedLinuxSwapspace *swapspace)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (swapspace),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_swapspace_class_init (StoragedLinuxSwapspaceClass *klass)
{
}

/**
 * storaged_linux_swapspace_new:
 *
 * Creates a new #StoragedLinuxSwapspace instance.
 *
 * Returns: A new #StoragedLinuxSwapspace. Free with g_object_unref().
 */
StoragedSwapspace *
storaged_linux_swapspace_new (void)
{
  return STORAGED_SWAPSPACE (g_object_new (STORAGED_TYPE_LINUX_SWAPSPACE,
                                          NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_swapspace_update:
 * @swapspace: A #StoragedLinuxSwapspace.
 * @object: The enclosing #StoragedLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_swapspace_update (StoragedLinuxSwapspace   *swapspace,
                                 StoragedLinuxBlockObject *object)
{
  StoragedMountMonitor *mount_monitor;
  StoragedLinuxDevice *device;
  StoragedMountType mount_type;
  gboolean active;

  mount_monitor = storaged_daemon_get_mount_monitor (storaged_linux_block_object_get_daemon (object));
  device = storaged_linux_block_object_get_device (object);

  active = FALSE;
  if (storaged_mount_monitor_is_dev_in_use (mount_monitor, g_udev_device_get_device_number (device->udev_device), &mount_type) &&
      mount_type == STORAGED_MOUNT_TYPE_SWAP)
    active = TRUE;
  storaged_swapspace_set_active (STORAGED_SWAPSPACE (swapspace), active);

  g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */


static void
swapspace_start_on_job_completed (StoragedJob   *job,
                                  gboolean       success,
                                  const gchar   *message,
                                  gpointer       user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (user_data);
  StoragedSwapspace *swapspace;
  swapspace = STORAGED_SWAPSPACE (g_dbus_method_invocation_get_user_data (invocation));
  if (success)
    storaged_swapspace_complete_start (swapspace, invocation);
  else
    g_dbus_method_invocation_return_error (invocation,
                                           STORAGED_ERROR,
                                           STORAGED_ERROR_FAILED,
                                           "Error activating swap: %s",
                                           message);
}

static gboolean
handle_start (StoragedSwapspace        *swapspace,
              GDBusMethodInvocation    *invocation,
              GVariant                 *options)
{
  StoragedObject *object;
  StoragedDaemon *daemon;
  StoragedBlock *block;
  StoragedBaseJob *job;
  GError *error;
  gchar *escaped_device = NULL;
  uid_t caller_uid;
  gid_t caller_gid;

  error = NULL;
  object = storaged_daemon_util_dup_object (swapspace, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  block = storaged_object_peek_block (object);

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &caller_gid,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      object,
                                                      "org.storaged.Storaged.manage-swapspace",
                                                      options,
                                                      /* Translators: Shown in authentication dialog when the user
                                                       * requests activating a swap device.
                                                       *
                                                       * Do not translate $(drive), it's a placeholder and
                                                       * will be replaced by the name of the drive/device in question
                                                       */
                                                      N_("Authentication is required to activate swapspace on $(drive)"),
                                                      invocation))
    goto out;

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (block));

  job = storaged_daemon_launch_spawned_job (daemon,
                                            object,
                                            "swapspace-start", caller_uid,
                                            NULL, /* cancellable */
                                            0,    /* uid_t run_as_uid */
                                            0,    /* uid_t run_as_euid */
                                            NULL, /* input_string */
                                            "swapon %s",
                                            escaped_device);
  g_signal_connect (job,
                    "completed",
                    G_CALLBACK (swapspace_start_on_job_completed),
                    invocation);

 out:
  g_free (escaped_device);
  g_clear_object (&object);
  return TRUE;
}

static void
swapspace_stop_on_job_completed (StoragedJob   *job,
                                 gboolean       success,
                                 const gchar   *message,
                                 gpointer       user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (user_data);
  StoragedSwapspace *swapspace;
  swapspace = STORAGED_SWAPSPACE (g_dbus_method_invocation_get_user_data (invocation));
  if (success)
    storaged_swapspace_complete_start (swapspace, invocation);
  else
    g_dbus_method_invocation_return_error (invocation,
                                           STORAGED_ERROR,
                                           STORAGED_ERROR_FAILED,
                                           "Error deactivating swap: %s",
                                           message);
}

static gboolean
handle_stop (StoragedSwapspace        *swapspace,
             GDBusMethodInvocation    *invocation,
             GVariant                 *options)
{
  StoragedObject *object;
  StoragedDaemon *daemon;
  StoragedBlock *block;
  StoragedBaseJob *job;
  uid_t caller_uid;
  gid_t caller_gid;
  gchar *escaped_device = NULL;
  GError *error = NULL;

  object = STORAGED_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (swapspace)));
  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  block = storaged_object_peek_block (object);

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &caller_gid,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Now, check that the user is actually authorized to stop the swap space.
   *
   * TODO: want nicer authentication message + special treatment if the
   * uid that locked the device (e.g. w/o -others).
   */
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      object,
                                                      "org.storaged.Storaged.manage-swapspace",
                                                      options,
                                                      /* Translators: Shown in authentication dialog when the user
                                                       * requests deactivating a swap device.
                                                       *
                                                       * Do not translate $(drive), it's a placeholder and
                                                       * will be replaced by the name of the drive/device in question
                                                       */
                                                      N_("Authentication is required to deactivate swapspace on $(drive)"),
                                                      invocation))
    goto out;

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (block));

  job = storaged_daemon_launch_spawned_job (daemon,
                                            object,
                                            "swapspace-stop", caller_uid,
                                            NULL, /* cancellable */
                                            0,    /* uid_t run_as_uid */
                                            0,    /* uid_t run_as_euid */
                                            NULL, /* input_string */
                                            "swapoff %s",
                                            escaped_device);
  g_signal_connect (job,
                    "completed",
                    G_CALLBACK (swapspace_stop_on_job_completed),
                    invocation);

 out:
  g_free (escaped_device);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
swapspace_iface_init (StoragedSwapspaceIface *iface)
{
  iface->handle_start  = handle_start;
  iface->handle_stop   = handle_stop;
}
