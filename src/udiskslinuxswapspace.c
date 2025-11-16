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

#include <blockdev/swap.h>
#include <glib/gstdio.h>

#include "udiskslogging.h"
#include "udiskslinuxswapspace.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksmountmonitor.h"
#include "udiskslinuxdevice.h"
#include "udisksthreadedjob.h"
#include "udiskssimplejob.h"

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

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (swapspace));
  g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
start_job_func (UDisksThreadedJob  *job,
                GCancellable       *cancellable,
                gpointer            user_data,
                GError            **error)
{
  UDisksObject *object = UDISKS_OBJECT (user_data);
  UDisksBlock *block;
  gchar *device;
  gboolean ret = FALSE;

  block = udisks_object_get_block (object);
  device = udisks_block_dup_device (block);

  ret = bd_swap_swapon (device, -1, error);

  g_object_unref (block);
  g_free (device);
  return ret;
}

static gboolean
handle_start (UDisksSwapspace        *swapspace,
              GDBusMethodInvocation  *invocation,
              GVariant               *options)
{
  UDisksObject *object;
  UDisksDaemon *daemon;
  GError *error = NULL;
  uid_t caller_uid;

  object = udisks_daemon_util_dup_object (swapspace, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.manage-swapspace",
                                                    options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * requests activating a swap device.
                                                     *
                                                     * Do not translate $(device.name), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to activate swapspace on $(device.name)"),
                                                    invocation))
    goto out;

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               object,
                                               "swapspace-start",
                                               caller_uid,
                                               FALSE,
                                               start_job_func,
                                               object,
                                               NULL, /* user_data_free_func */
                                               NULL, /* cancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error activating swap: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  udisks_swapspace_complete_start (swapspace, invocation);

 out:
  g_clear_object (&object);
  return TRUE;
}


static gboolean
stop_job_func (UDisksThreadedJob  *job,
               GCancellable       *cancellable,
               gpointer            user_data,
               GError            **error)
{
  UDisksObject *object = UDISKS_OBJECT (user_data);
  UDisksBlock *block;
  gchar *device;
  gboolean ret = FALSE;

  block = udisks_object_get_block (object);
  device = udisks_block_dup_device (block);

  ret = bd_swap_swapoff (device, error);

  g_object_unref (block);
  g_free (device);
  return ret;
}

static gboolean
handle_stop (UDisksSwapspace        *swapspace,
             GDBusMethodInvocation  *invocation,
             GVariant               *options)
{
  UDisksObject *object;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (swapspace, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
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
                                                     * Do not translate $(device.name), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to deactivate swapspace on $(device.name)"),
                                                    invocation))
    goto out;

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               object,
                                               "swapspace-stop",
                                               caller_uid,
                                               FALSE,
                                               stop_job_func,
                                               object,
                                               NULL, /* user_data_free_func */
                                               NULL, /* cancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error deactivating swap: %s",
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  udisks_swapspace_complete_stop (swapspace, invocation);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_set_label (UDisksSwapspace        *swapspace,
                  GDBusMethodInvocation  *invocation,
                  const gchar            *label,
                  GVariant               *options)
{
  UDisksObject *object;
  UDisksDaemon *daemon;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;
  UDisksBlock *block = NULL;
  uid_t caller_uid;

  object = udisks_daemon_util_dup_object (swapspace, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  block = udisks_object_peek_block (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.manage-swapspace",
                                                    options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * requests setting label of a swap device.
                                                     *
                                                     * Do not translate $(device.name), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to set swapspace label on $(device.name)"),
                                                    invocation))
    goto out;

  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         "swapspace-modify",
                                         caller_uid,
                                         FALSE,
                                         NULL);
  if (job == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to create a job object");
      goto out;
    }

  if (!bd_swap_set_label (udisks_block_get_device (block),
                          label,
                          &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error taking setting label on %s: %s",
                                             udisks_block_get_device (block),
                                             error->message);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  udisks_swapspace_complete_set_label (swapspace, invocation);
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_set_uuid (UDisksSwapspace       *swapspace,
                 GDBusMethodInvocation *invocation,
                 const gchar           *arg_uuid,
                 GVariant              *arg_options)
{
  UDisksObject *object;
  UDisksDaemon *daemon;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;
  UDisksBlock *block = NULL;
  uid_t caller_uid;

  object = udisks_daemon_util_dup_object (swapspace, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  block = udisks_object_peek_block (object);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.manage-swapspace",
                                                    arg_options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * requests setting UUID of a swap device.
                                                     *
                                                     * Do not translate $(device.name), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to set swapspace UUID on $(device.name)"),
                                                    invocation))
    goto out;

  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         "swapspace-modify",
                                         caller_uid,
                                         FALSE,
                                         NULL);
  if (job == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to create a job object");
      goto out;
    }

  if (!bd_swap_set_uuid (udisks_block_get_device (block),
                         arg_uuid,
                         &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error taking setting UUID on %s: %s",
                                             udisks_block_get_device (block),
                                             error->message);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  udisks_swapspace_complete_set_uuid (swapspace, invocation);
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
swapspace_iface_init (UDisksSwapspaceIface *iface)
{
  iface->handle_start     = handle_start;
  iface->handle_stop      = handle_stop;
  iface->handle_set_label = handle_set_label;
  iface->handle_set_uuid  = handle_set_uuid;
}
