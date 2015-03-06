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

#include "storagedlogging.h"
#include "storagedlinuxloop.h"
#include "storagedlinuxblockobject.h"
#include "storageddaemon.h"
#include "storagedstate.h"
#include "storageddaemonutil.h"
#include "storagedlinuxdevice.h"

/**
 * SECTION:storagedlinuxloop
 * @title: StoragedLinuxLoop
 * @short_description: Linux implementation of #StoragedLoop
 *
 * This type provides an implementation of the #StoragedLoop
 * interface on Linux.
 */

typedef struct _StoragedLinuxLoopClass   StoragedLinuxLoopClass;

/**
 * StoragedLinuxLoop:
 *
 * The #StoragedLinuxLoop structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxLoop
{
  StoragedLoopSkeleton parent_instance;
};

struct _StoragedLinuxLoopClass
{
  StoragedLoopSkeletonClass parent_class;
};

static void loop_iface_init (StoragedLoopIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxLoop, storaged_linux_loop, STORAGED_TYPE_LOOP_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_LOOP, loop_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_loop_init (StoragedLinuxLoop *loop)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (loop),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_loop_class_init (StoragedLinuxLoopClass *klass)
{
}

/**
 * storaged_linux_loop_new:
 *
 * Creates a new #StoragedLinuxLoop instance.
 *
 * Returns: A new #StoragedLinuxLoop. Free with g_object_unref().
 */
StoragedLoop *
storaged_linux_loop_new (void)
{
  return STORAGED_LOOP (g_object_new (STORAGED_TYPE_LINUX_LOOP,
                                      NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_loop_update:
 * @loop: A #StoragedLinuxLoop.
 * @object: The enclosing #StoragedLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_loop_update (StoragedLinuxLoop        *loop,
                            StoragedLinuxBlockObject *object)
{
  StoragedDaemon *daemon;
  StoragedState *state;
  StoragedLinuxDevice *device;
  uid_t setup_by_uid;

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  state = storaged_daemon_get_state (daemon);

  device = storaged_linux_block_object_get_device (object);
  if (g_str_has_prefix (g_udev_device_get_name (device->udev_device), "loop"))
    {
      gchar *filename;
      gchar *backing_file;
      GError *error;
      filename = g_strconcat (g_udev_device_get_sysfs_path (device->udev_device), "/loop/backing_file", NULL);
      error = NULL;
      if (!g_file_get_contents (filename,
                                &backing_file,
                                NULL,
                                &error))
        {
          /* ENOENT is not unexpected */
          if (!(error->domain == G_FILE_ERROR && error->code == G_FILE_ERROR_NOENT))
            {
              storaged_warning ("Error loading %s: %s (%s, %d)",
                                filename,
                                error->message,
                                g_quark_to_string (error->domain),
                                error->code);
            }
          g_error_free (error);
          storaged_loop_set_backing_file (STORAGED_LOOP (loop), "");
        }
      else
        {
          /* TODO: validate UTF-8 */
          g_strstrip (backing_file);
          storaged_loop_set_backing_file (STORAGED_LOOP (loop), backing_file);
          g_free (backing_file);
        }
      g_free (filename);
    }
  else
    {
      storaged_loop_set_backing_file (STORAGED_LOOP (loop), "");
    }
  storaged_loop_set_autoclear (STORAGED_LOOP (loop),
                               g_udev_device_get_sysfs_attr_as_boolean (device->udev_device, "loop/autoclear"));

  setup_by_uid = 0;
  if (state != NULL)
    {
      storaged_state_has_loop (state,
                               g_udev_device_get_device_file (device->udev_device),
                               &setup_by_uid);
    }
  storaged_loop_set_setup_by_uid (STORAGED_LOOP (loop), setup_by_uid);

  g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_delete (StoragedLoop             *loop,
               GDBusMethodInvocation    *invocation,
               GVariant                 *options)
{
  StoragedObject *object;
  StoragedBlock *block;
  StoragedDaemon *daemon;
  StoragedState *state;
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
  object = storaged_daemon_util_dup_object (loop, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = storaged_object_peek_block (object);
  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  state = storaged_daemon_get_state (daemon);

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon, invocation, NULL, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  if (!storaged_state_has_loop (state,
                                storaged_block_get_device (block),
                                &setup_by_uid))
    {
      setup_by_uid = -1;
    }

  if (caller_uid != setup_by_uid)
    {
      if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                          object,
                                                          "org.storaged.Storaged.loop-delete-others",
                                                          options,
                                                          /* Translators: Shown in authentication dialog when the user
                                                           * requests deleting a loop device previously set up by
                                                           * another user.
                                                           *
                                                           * Do not translate $(drive), it's a placeholder and
                                                           * will be replaced by the name of the drive/device in question
                                                           */
                                                          N_("Authentication is required to delete the loop device $(drive)"),
                                                          invocation))
        goto out;
    }

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (block));

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                NULL, /* StoragedObject */
                                                "loop-setup", caller_uid,
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
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error deleting %s: %s",
                                             storaged_block_get_device (block),
                                             error_message);
      goto out;
    }

  storaged_notice ("Deleted loop device %s (was backed by %s)",
                   storaged_block_get_device (block),
                   storaged_loop_get_backing_file (loop));

  storaged_loop_complete_delete (loop, invocation);

 out:
  g_free (escaped_device);
  g_free (error_message);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
loop_set_autoclear (StoragedLinuxDevice  *device,
                    gboolean              value,
                    GError              **error)
{
  gboolean ret = FALSE;
  struct loop_info64 li64;
  gint fd = -1;
  const gchar *device_file = NULL;
  gint sysfs_autoclear_fd;
  gchar *sysfs_autoclear_path = NULL;

  g_return_val_if_fail (STORAGED_IS_LINUX_DEVICE (device), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* try writing to the loop/autoclear sysfs file - this may not work
   * since it currently (May 2012) depends on a patch not yet applied
   * upstream (it'll fail in open(2))
   */
  sysfs_autoclear_path = g_strconcat (g_udev_device_get_sysfs_path (device->udev_device), "/loop/autoclear", NULL);
  sysfs_autoclear_fd = open (sysfs_autoclear_path, O_WRONLY);
  if (sysfs_autoclear_fd > 0)
    {
      gchar strval[2] = {'0', 0};
      if (value)
        strval[0] = '1';
      if (write (sysfs_autoclear_fd, strval, 1) != 1)
        {
          storaged_warning ("Error writing '1' to file %s: %m", sysfs_autoclear_path);
        }
      else
        {
          ret = TRUE;
          close (sysfs_autoclear_fd);
          g_free (sysfs_autoclear_path);
          goto out;
        }
      close (sysfs_autoclear_fd);
    }
  g_free (sysfs_autoclear_path);

  /* if that didn't work, do LO_GET_STATUS, then LO_SET_STATUS */
  device_file = g_udev_device_get_device_file (device->udev_device);
  fd = open (device_file, O_RDWR);
  if (fd == -1)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error opening loop device %s: %m",
                   device_file);
      goto out;
    }

  memset (&li64, '\0', sizeof (li64));
  if (ioctl (fd, LOOP_GET_STATUS64, &li64) < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error getting status for loop device %s: %m",
                   device_file);
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
                   device_file);
      goto out;
    }

  ret = TRUE;

 out:
  if (fd != -1 )
    {
      if (close (fd) != 0)
        storaged_warning ("close(2) on loop fd %d for device %s failed: %m", fd, device_file);
    }
  return ret;
}

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_set_autoclear (StoragedLoop             *loop,
                      GDBusMethodInvocation    *invocation,
                      gboolean                  arg_value,
                      GVariant                 *options)
{
  StoragedObject *object = NULL;
  StoragedDaemon *daemon = NULL;
  StoragedLinuxDevice *device = NULL;
  GError *error = NULL;
  uid_t caller_uid = -1;

  error = NULL;
  object = storaged_daemon_util_dup_object (loop, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon, invocation, NULL, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                          object,
                                                          "org.storaged.Storaged.loop-modify-others",
                                                          options,
                                                          /* Translators: Shown in authentication dialog when the user
                                                           * requests changing autoclear on a loop device set up by
                                                           * another user.
                                                           *
                                                           * Do not translate $(drive), it's a placeholder and
                                                           * will be replaced by the name of the drive/device in question
                                                           */
                                                          N_("Authentication is required to modify the loop device $(drive)"),
                                                          invocation))
        goto out;
    }

  device = storaged_linux_block_object_get_device (STORAGED_LINUX_BLOCK_OBJECT (object));
  error = NULL;
  if (!loop_set_autoclear (device,
                           arg_value,
                           &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* specutatively update our local value so a change signal is emitted before we return... */
  storaged_loop_set_autoclear (STORAGED_LOOP (loop), arg_value);
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (loop));

  /* ... but make sure we update the property value from sysfs */
  storaged_linux_block_object_trigger_uevent (STORAGED_LINUX_BLOCK_OBJECT (object));

  /* TODO: would be better to have something like trigger_uevent_and_wait_for_it_sync() */

  storaged_loop_complete_set_autoclear (loop, invocation);

 out:
  g_clear_object (&device);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
loop_iface_init (StoragedLoopIface *iface)
{
  iface->handle_delete        = handle_delete;
  iface->handle_set_autoclear = handle_set_autoclear;
}
