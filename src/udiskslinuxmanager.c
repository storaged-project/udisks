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
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <linux/loop.h>

#include "udiskslogging.h"
#include "udiskslinuxmanager.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udiskscleanup.h"

/**
 * SECTION:udiskslinuxmanager
 * @title: UDisksLinuxManager
 * @short_description: Linux implementation of #UDisksManager
 *
 * This type provides an implementation of the #UDisksManager
 * interface on Linux.
 */

typedef struct _UDisksLinuxManagerClass   UDisksLinuxManagerClass;

/**
 * UDisksLinuxManager:
 *
 * The #UDisksLinuxManager structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxManager
{
  UDisksManagerSkeleton parent_instance;

  UDisksDaemon *daemon;
};

struct _UDisksLinuxManagerClass
{
  UDisksManagerSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON
};

static void manager_iface_init (UDisksManagerIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManager, udisks_linux_manager, UDISKS_TYPE_MANAGER_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER, manager_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_manager_finalize (GObject *object)
{
  /* UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (object); */

  G_OBJECT_CLASS (udisks_linux_manager_parent_class)->finalize (object);
}

static void
udisks_linux_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_manager_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (manager->daemon == NULL);
      /* we don't take a reference to the daemon */
      manager->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_init (UDisksLinuxManager *manager)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_manager_class_init (UDisksLinuxManagerClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_manager_finalize;
  gobject_class->set_property = udisks_linux_manager_set_property;
  gobject_class->get_property = udisks_linux_manager_get_property;

  /**
   * UDisksLinuxManager:daemon:
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

/**
 * udisks_linux_manager_new:
 * @daemon: A #UDisksDaemon.
 *
 * Creates a new #UDisksLinuxManager instance.
 *
 * Returns: A new #UDisksLinuxManager. Free with g_object_unref().
 */
UDisksManager *
udisks_linux_manager_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_MANAGER (g_object_new (UDISKS_TYPE_LINUX_MANAGER,
                                       "daemon", daemon,
                                       "version", PACKAGE_VERSION,
                                       NULL));
}

/**
 * udisks_linux_manager_get_daemon:
 * @manager: A #UDisksLinuxManager.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @manager.
 */
UDisksDaemon *
udisks_linux_manager_get_daemon (UDisksLinuxManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER (manager), NULL);
  return manager->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  const gchar *loop_device;
  const gchar *path;
} WaitForLoopData;

static gboolean
wait_for_loop_object (UDisksDaemon *daemon,
                      UDisksObject *object,
                      gpointer      user_data)
{
  WaitForLoopData *data = user_data;
  UDisksBlock *block;
  UDisksLoop *loop;
  gboolean ret;

  ret = FALSE;
  block = udisks_object_peek_block (object);
  loop = udisks_object_peek_loop (object);
  if (block == NULL || loop == NULL)
    goto out;

  if (g_strcmp0 (udisks_block_get_device (block), data->loop_device) != 0)
    goto out;

  if (g_strcmp0 (udisks_loop_get_backing_file (loop), data->path) != 0)
    goto out;

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_loop_setup (UDisksManager          *object,
                   GDBusMethodInvocation  *invocation,
                   GUnixFDList            *fd_list,
                   GVariant               *fd_index,
                   GVariant               *options)
{
  UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (object);
  GError *error;
  gint fd_num;
  gint fd = -1;
  gchar proc_path[64];
  gchar path[8192];
  ssize_t path_len;
  gint loop_fd = -1;
  gint loop_control_fd = -1;
  gint allocated_loop_number = -1;
  gchar *loop_device = NULL;
  struct loop_info64 li64;
  UDisksObject *loop_object = NULL;
  gboolean option_read_only = FALSE;
  guint64 option_offset = 0;
  guint64 option_size = 0;
  uid_t caller_uid;
  struct stat fd_statbuf;
  gboolean fd_statbuf_valid = FALSE;
  WaitForLoopData wait_data;

  /* we need the uid of the caller for the loop file */
  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Check if the user is authorized to create a loop device */
  if (!udisks_daemon_util_check_authorization_sync (manager->daemon,
                                                    NULL,
                                                    "org.freedesktop.udisks2.loop-setup",
                                                    options,
                                                    N_("Authentication is required to set up a loop device"),
                                                    invocation))
    goto out;

  fd_num = g_variant_get_handle (fd_index);
  if (fd_list == NULL || fd_num >= g_unix_fd_list_get_length (fd_list))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Expected to use fd at index %d, but message has only %d fds",
                                             fd_num,
                                             fd_list == NULL ? 0 : g_unix_fd_list_get_length (fd_list));
      goto out;
    }
  error = NULL;
  fd = g_unix_fd_list_get (fd_list, fd_num, &error);
  if (fd == -1)
    {
      g_prefix_error (&error, "Error getting file descriptor %d from message: ", fd_num);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  snprintf (proc_path, sizeof (proc_path), "/proc/%d/fd/%d", getpid (), fd);
  path_len = readlink (proc_path, path, sizeof (path) - 1);
  if (path_len < 1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error determing path: %m");
      goto out;
    }
  path[path_len] = '\0';

  g_variant_lookup (options, "read-only", "b", &option_read_only);
  g_variant_lookup (options, "offset", "t", &option_offset);
  g_variant_lookup (options, "size", "t", &option_size);

  /* it's not a problem if fstat fails... for example, this can happen if the user
   * passes a fd to a file on the GVfs fuse mount
   */
  if (fstat (fd, &fd_statbuf) == 0)
    fd_statbuf_valid = TRUE;

  loop_control_fd = open ("/dev/loop-control", O_RDWR);
  if (loop_control_fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error opening /dev/loop-control: %m");
      goto out;
    }

  allocated_loop_number = ioctl (loop_control_fd, LOOP_CTL_GET_FREE);
  if (allocated_loop_number < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error allocating free loop device: %m");
      goto out;
    }

  loop_device = g_strdup_printf ("/dev/loop%d", allocated_loop_number);
  loop_fd = open (loop_device, option_read_only ? O_RDONLY : O_RDWR);
  if (loop_fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Cannot open %s: %m", loop_device);
      goto out;
    }

  memset (&li64, '\0', sizeof (li64));
  strncpy ((char *) li64.lo_file_name, path, LO_NAME_SIZE);
  if (option_read_only)
    li64.lo_flags |= LO_FLAGS_READ_ONLY;
  /* TODO: we could have an option 'no-part-scan' but I don't think that's right */
  li64.lo_flags |= 8; /* Use LO_FLAGS_PARTSCAN when 3.2 has been out for a while */
  li64.lo_offset = option_offset;
  li64.lo_sizelimit = option_size;
  if (ioctl (loop_fd, LOOP_SET_FD, fd) < 0 || ioctl (loop_fd, LOOP_SET_STATUS64, &li64) < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error setting up loop device %s: %m",
                                             loop_device);
      goto out;
    }

  /* Determine the resulting object */
  error = NULL;
  wait_data.loop_device = loop_device;
  wait_data.path = path;
  loop_object = udisks_daemon_wait_for_object_sync (manager->daemon,
                                                    wait_for_loop_object,
                                                    &wait_data,
                                                    NULL,
                                                    10, /* timeout_seconds */
                                                    &error);
  if (loop_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for loop object after creating %s",
                      loop_device);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* update the loop file */
  udisks_cleanup_add_loop (udisks_daemon_get_cleanup (manager->daemon),
                           loop_device,
                           path,
                           fd_statbuf_valid ? fd_statbuf.st_dev : 0,
                           caller_uid);

  udisks_notice ("Set up loop device %s (backed by %s)",
                 loop_device,
                 path);

  udisks_manager_complete_loop_setup (object,
                                      invocation,
                                      NULL, /* fd_list */
                                      g_dbus_object_get_object_path (G_DBUS_OBJECT (loop_object)));

 out:
  if (loop_object != NULL)
    g_object_unref (loop_object);
  g_free (loop_device);
  if (loop_control_fd != -1)
    close (loop_control_fd);
  if (loop_fd != -1)
    close (loop_fd);
  if (fd != -1)
    close (fd);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static void
manager_iface_init (UDisksManagerIface *iface)
{
  iface->handle_loop_setup = handle_loop_setup;
}
