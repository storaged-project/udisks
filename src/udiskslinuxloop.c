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

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/loop.h>

#include <glib/gstdio.h>

#include "udiskslogging.h"
#include "udiskslinuxloop.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxloop
 * @title: UDisksLinuxLoop
 * @short_description: Linux implementation of #UDisksLoop
 *
 * This type provides an implementation of the #UDisksLoop
 * interface on Linux.
 */

typedef struct _UDisksLinuxLoopClass   UDisksLinuxLoopClass;

/**
 * UDisksLinuxLoop:
 *
 * The #UDisksLinuxLoop structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxLoop
{
  UDisksLoopSkeleton parent_instance;
};

struct _UDisksLinuxLoopClass
{
  UDisksLoopSkeletonClass parent_class;
};

static void loop_iface_init (UDisksLoopIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxLoop, udisks_linux_loop, UDISKS_TYPE_LOOP_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_LOOP, loop_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_loop_init (UDisksLinuxLoop *loop)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (loop),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_loop_class_init (UDisksLinuxLoopClass *klass)
{
}

/**
 * udisks_linux_loop_new:
 *
 * Creates a new #UDisksLinuxLoop instance.
 *
 * Returns: A new #UDisksLinuxLoop. Free with g_object_unref().
 */
UDisksLoop *
udisks_linux_loop_new (void)
{
  return UDISKS_LOOP (g_object_new (UDISKS_TYPE_LINUX_LOOP,
                                    NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_loop_update:
 * @loop: A #UDisksLinuxLoop.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_loop_update (UDisksLinuxLoop        *loop,
                          UDisksLinuxBlockObject *object)
{
  GUdevDevice *device;
  device = udisks_linux_block_object_get_device (object);
  if (g_str_has_prefix (g_udev_device_get_name (device), "loop"))
    {
      gchar *filename;
      gchar *backing_file;
      GError *error;
      filename = g_strconcat (g_udev_device_get_sysfs_path (device), "/loop/backing_file", NULL);
      error = NULL;
      if (!g_file_get_contents (filename,
                                &backing_file,
                                NULL,
                                &error))
        {
          /* ENOENT is not unexpected */
          if (!(error->domain == G_FILE_ERROR && error->code == G_FILE_ERROR_NOENT))
            {
              udisks_warning ("Error loading %s: %s (%s, %d)",
                              filename,
                              error->message,
                              g_quark_to_string (error->domain),
                              error->code);
            }
          g_error_free (error);
          udisks_loop_set_backing_file (UDISKS_LOOP (loop), "");
        }
      else
        {
          /* TODO: validate UTF-8 */
          g_strstrip (backing_file);
          udisks_loop_set_backing_file (UDISKS_LOOP (loop), backing_file);
          g_free (backing_file);
        }
      g_free (filename);
    }
  else
    {
      udisks_loop_set_backing_file (UDISKS_LOOP (loop), "");
    }
  udisks_loop_set_autoclear (UDISKS_LOOP (loop),
                             g_udev_device_get_sysfs_attr_as_boolean (device, "loop/autoclear"));
  g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_delete (UDisksLoop             *loop,
               GDBusMethodInvocation  *invocation,
               GVariant               *options)
{
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDaemon *daemon;
  UDisksCleanup *cleanup;
  gchar *error_message;
  gchar *escaped_device;
  GError *error;
  uid_t caller_uid;
  uid_t setup_by_uid;

  object = NULL;
  daemon = NULL;
  error_message = NULL;
  escaped_device = NULL;

  error = NULL;
  object = udisks_daemon_util_dup_object (loop, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = udisks_object_peek_block (object);
  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  cleanup = udisks_daemon_get_cleanup (daemon);

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon, invocation, NULL, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  if (!udisks_cleanup_has_loop (cleanup,
                                udisks_block_get_device (block),
                                &setup_by_uid))
    {
      setup_by_uid = -1;
    }

  if (caller_uid != setup_by_uid)
    {
      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        object,
                                                        "org.freedesktop.udisks2.loop-delete-others",
                                                        options,
                                                        /* Translators: Shown in authentication dialog when the user
                                                         * requests deleting a loop device previously set up by
                                                         * another user.
                                                         *
                                                         * Do not translate $(udisks2.device), it's a placeholder and
                                                         * will be replaced by the name of the drive/device in question
                                                         */
                                                        N_("Authentication is required to delete the loop device $(udisks2.device)"),
                                                        invocation))
        goto out;
    }

  escaped_device = udisks_daemon_util_escape_and_quote (udisks_block_get_device (block));

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              NULL, /* UDisksObject */
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "losetup -d %s",
                                              escaped_device))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error deleting %s: %s",
                                             udisks_block_get_device (block),
                                             error_message);
      goto out;
    }

  udisks_notice ("Deleted loop device %s (was backed by %s)",
                 udisks_block_get_device (block),
                 udisks_loop_get_backing_file (loop));

  udisks_loop_complete_delete (loop, invocation);

 out:
  g_free (escaped_device);
  g_free (error_message);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
loop_set_autoclear (const gchar  *device,
                    gboolean      value,
                    GError      **error)
{
  gboolean ret = FALSE;
  struct loop_info64 li64;
  gint fd = -1;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  fd = open (device, O_RDWR);
  if (fd == -1)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error opening loop device %s: %m",
                   device);
      goto out;
    }

  memset (&li64, '\0', sizeof (li64));
  if (ioctl (fd, LOOP_GET_STATUS64, &li64) < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error getting status for loop device %s: %m",
                   device);
      goto out;
    }

  if (value)
    li64.lo_flags |= LO_FLAGS_AUTOCLEAR;
  else
    li64.lo_flags &= (~LO_FLAGS_AUTOCLEAR);

  if (ioctl (fd, LOOP_SET_STATUS64, &li64) < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error setting status for loop device %s: %m",
                   device);
      goto out;
    }

  ret = TRUE;

 out:
  if (fd != -1 )
    {
      if (close (fd) != 0)
        udisks_warning ("close(2) on loop fd %d for device %s failed: %m", fd, device);
    }
  return ret;
}

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_set_autoclear (UDisksLoop             *loop,
                      GDBusMethodInvocation  *invocation,
                      gboolean                arg_value,
                      GVariant               *options)
{
  UDisksObject *object = NULL;
  UDisksBlock *block = NULL;
  UDisksDaemon *daemon = NULL;
  GError *error = NULL;
  uid_t caller_uid = -1;

  error = NULL;
  object = udisks_daemon_util_dup_object (loop, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = udisks_object_peek_block (object);
  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon, invocation, NULL, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  if (!udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        object,
                                                        "org.freedesktop.udisks2.loop-modify-others",
                                                        options,
                                                        /* Translators: Shown in authentication dialog when the user
                                                         * requests changing autoclear on a loop device set up by
                                                         * another user.
                                                         *
                                                         * Do not translate $(udisks2.device), it's a placeholder and
                                                         * will be replaced by the name of the drive/device in question
                                                         */
                                                        N_("Authentication is required to modify the loop device $(udisks2.device)"),
                                                        invocation))
        goto out;
    }

  error = NULL;
  if (!loop_set_autoclear (udisks_block_get_device (block),
                           arg_value,
                           &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* specutatively update our local value so a change signal is emitted before we return... */
  udisks_loop_set_autoclear (UDISKS_LOOP (loop), arg_value);
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (loop));

  /* ... but make sure we update the property value from sysfs */
  udisks_linux_block_object_trigger_uevent (UDISKS_LINUX_BLOCK_OBJECT (object));

  /* TODO: would be better to have something like trigger_uevent_and_wait_for_it_sync() */

  udisks_loop_complete_set_autoclear (loop, invocation);

 out:
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
loop_iface_init (UDisksLoopIface *iface)
{
  iface->handle_delete        = handle_delete;
  iface->handle_set_autoclear = handle_set_autoclear;
}
