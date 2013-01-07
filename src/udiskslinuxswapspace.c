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

#include "udiskslogging.h"
#include "udiskslinuxswapspace.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksmountmonitor.h"
#include "udiskslinuxdevice.h"

/**
 * SECTION:udiskslinuxswapspace
 * @title: UDisksLinuxSwapspace
 * @short_description: Linux implementation of #UDisksSwapspace
 *
 * This type provides an implementation of the #UDisksSwapspace interface
 * on Linux.
 */

typedef struct _UDisksLinuxSwapspaceClass   UDisksLinuxSwapspaceClass;

/**
 * UDisksLinuxSwapspace:
 *
 * The #UDisksLinuxSwapspace structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxSwapspace
{
  UDisksSwapspaceSkeleton parent_instance;
};

struct _UDisksLinuxSwapspaceClass
{
  UDisksSwapspaceSkeletonClass parent_class;
};

static void swapspace_iface_init (UDisksSwapspaceIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxSwapspace, udisks_linux_swapspace, UDISKS_TYPE_SWAPSPACE_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_SWAPSPACE, swapspace_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_swapspace_init (UDisksLinuxSwapspace *swapspace)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (swapspace),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_swapspace_class_init (UDisksLinuxSwapspaceClass *klass)
{
}

/**
 * udisks_linux_swapspace_new:
 *
 * Creates a new #UDisksLinuxSwapspace instance.
 *
 * Returns: A new #UDisksLinuxSwapspace. Free with g_object_unref().
 */
UDisksSwapspace *
udisks_linux_swapspace_new (void)
{
  return UDISKS_SWAPSPACE (g_object_new (UDISKS_TYPE_LINUX_SWAPSPACE,
                                          NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_swapspace_update:
 * @swapspace: A #UDisksLinuxSwapspace.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_swapspace_update (UDisksLinuxSwapspace   *swapspace,
                               UDisksLinuxBlockObject *object)
{
  UDisksMountMonitor *mount_monitor;
  UDisksLinuxDevice *device;
  UDisksMountType mount_type;
  gboolean active;

  mount_monitor = udisks_daemon_get_mount_monitor (udisks_linux_block_object_get_daemon (object));
  device = udisks_linux_block_object_get_device (object);

  active = FALSE;
  if (udisks_mount_monitor_is_dev_in_use (mount_monitor, g_udev_device_get_device_number (device->udev_device), &mount_type) &&
      mount_type == UDISKS_MOUNT_TYPE_SWAP)
    active = TRUE;
  udisks_swapspace_set_active (UDISKS_SWAPSPACE (swapspace), active);

  g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */


static void
swapspace_start_on_job_completed (UDisksJob   *job,
                                  gboolean     success,
                                  const gchar *message,
                                  gpointer     user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (user_data);
  UDisksSwapspace *swapspace;
  swapspace = UDISKS_SWAPSPACE (g_dbus_method_invocation_get_user_data (invocation));
  if (success)
    udisks_swapspace_complete_start (swapspace, invocation);
  else
    g_dbus_method_invocation_return_error (invocation,
                                           UDISKS_ERROR,
                                           UDISKS_ERROR_FAILED,
                                           "Error activating swap: %s",
                                           message);
}

static gboolean
handle_start (UDisksSwapspace        *swapspace,
              GDBusMethodInvocation  *invocation,
              GVariant               *options)
{
  UDisksObject *object;
  UDisksDaemon *daemon;
  UDisksBlock *block;
  UDisksBaseJob *job;
  GError *error;
  gchar *escaped_device = NULL;
  uid_t caller_uid;
  gid_t caller_gid;

  error = NULL;
  object = udisks_daemon_util_dup_object (swapspace, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  block = udisks_object_peek_block (object);

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.manage-swapspace",
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

  escaped_device = udisks_daemon_util_escape_and_quote (udisks_block_get_device (block));

  job = udisks_daemon_launch_spawned_job (daemon,
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
swapspace_stop_on_job_completed (UDisksJob   *job,
                                 gboolean     success,
                                 const gchar *message,
                                  gpointer     user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (user_data);
  UDisksSwapspace *swapspace;
  swapspace = UDISKS_SWAPSPACE (g_dbus_method_invocation_get_user_data (invocation));
  if (success)
    udisks_swapspace_complete_start (swapspace, invocation);
  else
    g_dbus_method_invocation_return_error (invocation,
                                           UDISKS_ERROR,
                                           UDISKS_ERROR_FAILED,
                                           "Error deactivating swap: %s",
                                           message);
}

static gboolean
handle_stop (UDisksSwapspace        *swapspace,
             GDBusMethodInvocation  *invocation,
             GVariant               *options)
{
  UDisksObject *object;
  UDisksDaemon *daemon;
  UDisksBlock *block;
  UDisksBaseJob *job;
  uid_t caller_uid;
  gid_t caller_gid;
  gchar *escaped_device = NULL;
  GError *error = NULL;

  object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (swapspace)));
  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  block = udisks_object_peek_block (object);

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.manage-swapspace",
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

  escaped_device = udisks_daemon_util_escape_and_quote (udisks_block_get_device (block));

  job = udisks_daemon_launch_spawned_job (daemon,
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
swapspace_iface_init (UDisksSwapspaceIface *iface)
{
  iface->handle_start  = handle_start;
  iface->handle_stop   = handle_stop;
}
