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

#include "storagedlogging.h"
#include "storagedlinuxmanager.h"
#include "storageddaemon.h"
#include "storageddaemonutil.h"
#include "storagedstate.h"
#include "storagedlinuxblockobject.h"
#include "storagedlinuxdevice.h"
#include "storagedmodulemanager.h"

/**
 * SECTION:storagedlinuxmanager
 * @title: StoragedLinuxManager
 * @short_description: Linux implementation of #StoragedManager
 *
 * This type provides an implementation of the #StoragedManager
 * interface on Linux.
 */

typedef struct _StoragedLinuxManagerClass   StoragedLinuxManagerClass;

/**
 * StoragedLinuxManager:
 *
 * The #StoragedLinuxManager structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxManager
{
  StoragedManagerSkeleton parent_instance;

  GMutex lock;

  StoragedDaemon *daemon;
};

struct _StoragedLinuxManagerClass
{
  StoragedManagerSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON
};

static void manager_iface_init (StoragedManagerIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxManager, storaged_linux_manager, STORAGED_TYPE_MANAGER_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MANAGER, manager_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_manager_finalize (GObject *object)
{
  StoragedLinuxManager *manager = STORAGED_LINUX_MANAGER (object);

  g_mutex_clear (&(manager->lock));

  G_OBJECT_CLASS (storaged_linux_manager_parent_class)->finalize (object);
}

static void
storaged_linux_manager_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  StoragedLinuxManager *manager = STORAGED_LINUX_MANAGER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_manager_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  StoragedLinuxManager *manager = STORAGED_LINUX_MANAGER (object);

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
storaged_linux_manager_init (StoragedLinuxManager *manager)
{
  g_mutex_init (&(manager->lock));
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_manager_class_init (StoragedLinuxManagerClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_linux_manager_finalize;
  gobject_class->set_property = storaged_linux_manager_set_property;
  gobject_class->get_property = storaged_linux_manager_get_property;

  /**
   * StoragedLinuxManager:daemon:
   *
   * The #StoragedDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * storaged_linux_manager_new:
 * @daemon: A #StoragedDaemon.
 *
 * Creates a new #StoragedLinuxManager instance.
 *
 * Returns: A new #StoragedLinuxManager. Free with g_object_unref().
 */
StoragedManager *
storaged_linux_manager_new (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_MANAGER (g_object_new (STORAGED_TYPE_LINUX_MANAGER,
                                       "daemon", daemon,
                                       "version", PACKAGE_VERSION,
                                       NULL));
}

/**
 * storaged_linux_manager_get_daemon:
 * @manager: A #StoragedLinuxManager.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @manager.
 */
StoragedDaemon *
storaged_linux_manager_get_daemon (StoragedLinuxManager *manager)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER (manager), NULL);
  return manager->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  const gchar *loop_device;
  const gchar *path;
} WaitForLoopData;

static StoragedObject *
wait_for_loop_object (StoragedDaemon *daemon,
                      gpointer        user_data)
{
  WaitForLoopData *data = user_data;
  StoragedObject *ret = NULL;
  StoragedObject *object = NULL;
  StoragedBlock *block;
  StoragedLoop *loop;
  StoragedLinuxDevice *device = NULL;
  GDir *dir;

  /* First see if we have the right loop object */
  object = storaged_daemon_find_block_by_device_file (daemon, data->loop_device);
  if (object == NULL)
    goto out;
  block = storaged_object_peek_block (object);
  loop = storaged_object_peek_loop (object);
  if (block == NULL || loop == NULL)
    goto out;
  if (g_strcmp0 (storaged_loop_get_backing_file (loop), data->path) != 0)
    goto out;

  /* We also need to wait for all partitions to be in place in case
   * the loop device is partitioned... we can do it like this because
   * we are guaranteed that partitions are in sysfs when receiving the
   * uevent for the main block device...
   */
  device = storaged_linux_block_object_get_device (STORAGED_LINUX_BLOCK_OBJECT (object));
  if (device == NULL)
    goto out;

  dir = g_dir_open (g_udev_device_get_sysfs_path (device->udev_device), 0 /* flags */, NULL /* GError */);
  if (dir != NULL)
    {
      const gchar *name;
      const gchar *device_name;
      device_name = g_udev_device_get_name (device->udev_device);
      while ((name = g_dir_read_name (dir)) != NULL)
        {
          if (g_str_has_prefix (name, device_name))
            {
              gchar *sysfs_path;
              StoragedObject *partition_object;
              sysfs_path = g_strconcat (g_udev_device_get_sysfs_path (device->udev_device), "/", name, NULL);
              partition_object = storaged_daemon_find_block_by_sysfs_path (daemon, sysfs_path);
              if (partition_object == NULL)
                {
                  /* nope, not there, bail */
                  g_free (sysfs_path);
                  g_dir_close (dir);
                  goto out;
                }
              g_object_unref (partition_object);
              g_free (sysfs_path);
            }
        }
      g_dir_close (dir);
    }

  /* all, good return the loop object */
  ret = g_object_ref (object);

 out:
  g_clear_object (&object);
  g_clear_object (&device);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_loop_setup (StoragedManager          *object,
                   GDBusMethodInvocation    *invocation,
                   GUnixFDList              *fd_list,
                   GVariant                 *fd_index,
                   GVariant                 *options)
{
  StoragedLinuxManager *manager = STORAGED_LINUX_MANAGER (object);
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
  StoragedObject *loop_object = NULL;
  gboolean option_read_only = FALSE;
  gboolean option_no_part_scan = FALSE;
  guint64 option_offset = 0;
  guint64 option_size = 0;
  uid_t caller_uid;
  struct stat fd_statbuf;
  gboolean fd_statbuf_valid = FALSE;
  WaitForLoopData wait_data;

  /* we need the uid of the caller for the loop file */
  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Check if the user is authorized to create a loop device */
  if (!storaged_daemon_util_check_authorization_sync (manager->daemon,
                                                      NULL,
                                                      "org.storaged.Storaged.loop-setup",
                                                      options,
                                                      /* Translators: Shown in authentication dialog when the user
                                                       * requests setting up a loop device.
                                                       */
                                                      N_("Authentication is required to set up a loop device"),
                                                      invocation))
    goto out;

  fd_num = g_variant_get_handle (fd_index);
  if (fd_list == NULL || fd_num >= g_unix_fd_list_get_length (fd_list))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
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
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error determing path: %m");
      goto out;
    }
  path[path_len] = '\0';

  g_variant_lookup (options, "read-only", "b", &option_read_only);
  g_variant_lookup (options, "offset", "t", &option_offset);
  g_variant_lookup (options, "size", "t", &option_size);
  g_variant_lookup (options, "no-part-scan", "b", &option_no_part_scan);

  /* it's not a problem if fstat fails... for example, this can happen if the user
   * passes a fd to a file on the GVfs fuse mount
   */
  if (fstat (fd, &fd_statbuf) == 0)
    fd_statbuf_valid = TRUE;

  /* serialize access to /dev/loop-control */
  g_mutex_lock (&(manager->lock));

  loop_control_fd = open ("/dev/loop-control", O_RDWR);
  if (loop_control_fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error opening /dev/loop-control: %m");
      g_mutex_unlock (&(manager->lock));
      goto out;
    }

  allocated_loop_number = ioctl (loop_control_fd, LOOP_CTL_GET_FREE);
  if (allocated_loop_number < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error allocating free loop device: %m");
      g_mutex_unlock (&(manager->lock));
      goto out;
    }

  loop_device = g_strdup_printf ("/dev/loop%d", allocated_loop_number);
  loop_fd = open (loop_device, option_read_only ? O_RDONLY : O_RDWR);
  if (loop_fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Cannot open %s: %m", loop_device);
      g_mutex_unlock (&(manager->lock));
      goto out;
    }

  /* update the loop file - need to do this before getting the uevent for the device  */
  storaged_state_add_loop (storaged_daemon_get_state (manager->daemon),
                           loop_device,
                           path,
                           fd_statbuf_valid ? fd_statbuf.st_dev : 0,
                           caller_uid);

  memset (&li64, '\0', sizeof (li64));
  strncpy ((char *) li64.lo_file_name, path, LO_NAME_SIZE - 1);
  if (option_read_only)
    li64.lo_flags |= LO_FLAGS_READ_ONLY;
  if (!option_no_part_scan)
    li64.lo_flags |= 8; /* Use LO_FLAGS_PARTSCAN when 3.2 has been out for a while */
  li64.lo_offset = option_offset;
  li64.lo_sizelimit = option_size;
  if (ioctl (loop_fd, LOOP_SET_FD, fd) < 0 || ioctl (loop_fd, LOOP_SET_STATUS64, &li64) < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error setting up loop device %s: %m",
                                             loop_device);
      g_mutex_unlock (&(manager->lock));
      goto out;
    }
  g_mutex_unlock (&(manager->lock));

  /* Determine the resulting object */
  error = NULL;
  wait_data.loop_device = loop_device;
  wait_data.path = path;
  loop_object = storaged_daemon_wait_for_object_sync (manager->daemon,
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

  storaged_notice ("Set up loop device %s (backed by %s)",
                   loop_device,
                   path);

  storaged_manager_complete_loop_setup (object,
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

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  gint md_num;
} WaitForArrayData;

static StoragedObject *
wait_for_array_object (StoragedDaemon *daemon,
                       gpointer        user_data)
{
  const gchar *raid_device_file = user_data;
  StoragedObject *object = NULL;
  StoragedBlock *block = NULL;
  gchar *mdraid_objpath = NULL;
  StoragedObject *ret = NULL;

  /* First see if we have the right array object */
  object = storaged_daemon_find_block_by_device_file (daemon, raid_device_file);
  if (object == NULL)
    goto out;

  block = storaged_object_get_block (object);
  if (block == NULL)
    goto out;

  mdraid_objpath = storaged_block_dup_mdraid (block);
  if (g_strcmp0 (mdraid_objpath, "/") == 0)
    goto out;

  ret = storaged_daemon_find_object (daemon, mdraid_objpath);

 out:
  g_free (mdraid_objpath);
  g_clear_object (&block);
  g_clear_object (&object);
  return ret;
}

static const gchar *raid_level_whitelist[] = {"raid0", "raid1", "raid4", "raid5", "raid6", "raid10", NULL};

static gboolean
handle_mdraid_create (StoragedManager         *_object,
                      GDBusMethodInvocation   *invocation,
                      const gchar *const      *arg_blocks,
                      const gchar             *arg_level,
                      const gchar             *arg_name,
                      guint64                  arg_chunk,
                      GVariant                *arg_options)
{
  StoragedLinuxManager *manager = STORAGED_LINUX_MANAGER (_object);
  StoragedObject *array_object = NULL;
  uid_t caller_uid;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;
  guint num_devices = 0;
  GList *blocks = NULL;
  GList *l;
  guint n;
  gchar *escaped_name = NULL;
  GString *str = NULL;
  gint status;
  gchar *error_message = NULL;
  gchar *raid_device_file = NULL;
  struct stat statbuf;
  dev_t raid_device_num;

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * attempts to start a RAID Array.
   */
  /* TODO: variables */
  message = N_("Authentication is required to create a RAID array");
  action_id = "org.storaged.Storaged.manage-md-raid";
  if (!storaged_daemon_util_check_authorization_sync (manager->daemon,
                                                      NULL,
                                                      action_id,
                                                      arg_options,
                                                      message,
                                                      invocation))
    goto out;

  /* validate level */
  for (n = 0; raid_level_whitelist[n] != NULL; n++)
    {
      if (g_strcmp0 (raid_level_whitelist[n], arg_level) == 0)
        break;
    }
  if (raid_level_whitelist[n] == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Unsupported RAID level %s", arg_level);
      goto out;
    }

  /* validate chunk (TODO: check that it's a power of 2) */
  if ((arg_chunk & 0x0fff) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Chunk %" G_GUINT64_FORMAT " is not a multiple of 4KiB", arg_chunk);
      goto out;
    }

  /* validate name */
  if (g_strcmp0 (arg_level, "raid1") == 0 && arg_chunk != 0)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Chunk must be zero for level 'raid1'");
      goto out;
    }

  /* validate name */
  if (strlen (arg_name) > 32)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Name is invalid");
      goto out;
    }

  num_devices = g_strv_length ((gchar **) arg_blocks);

  /* validate number of devices */
  if (num_devices < 2)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Must have at least two devices");
      goto out;
    }

  /* Collect and validate block objects
   *
   * Also, check we can open the block devices at the same time - this
   * is to avoid start deleting half the block devices while the other
   * half is already in use.
   */
  for (n = 0; arg_blocks != NULL && arg_blocks[n] != NULL; n++)
    {
      StoragedObject *object = NULL;
      StoragedBlock *block = NULL;
      gchar *device_file = NULL;
      int fd;

      object = storaged_daemon_find_object (manager->daemon, arg_blocks[n]);
      if (object == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Invalid object path %s at index %u",
                                                 arg_blocks[n], n);
          goto out;
        }

      block = storaged_object_get_block (object);
      if (block == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Object path %s for index %u is not a block device",
                                                 arg_blocks[n], n);
          goto out;
        }

      device_file = storaged_block_dup_device (block);
      fd = open (device_file, O_RDWR | O_EXCL);
      if (fd < 0)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Error opening device %s: %m",
                                                 device_file);
          g_free (device_file);
          goto out;
        }
      close (fd);
      g_free (device_file);

      blocks = g_list_prepend (blocks, block); /* adopts ownership */
      g_object_unref (object);
    }
  blocks = g_list_reverse (blocks);

  /* wipe existing devices */
  for (l = blocks; l != NULL; l = l->next)
    {
      StoragedBlock *block = STORAGED_BLOCK (l->data);
      StoragedObject *object_for_block;
      gchar *escaped_device;
      object_for_block = storaged_daemon_util_dup_object (block, &error);
      if (object_for_block == NULL)
        {
          g_dbus_method_invocation_return_gerror (invocation, error);
          g_clear_error (&error);
          goto out;
        }
      escaped_device = storaged_daemon_util_escape (storaged_block_get_device (block));
      if (!storaged_daemon_launch_spawned_job_sync (manager->daemon,
                                                  object_for_block,
                                                  "format-erase", caller_uid,
                                                  NULL, /* cancellable */
                                                  0,    /* uid_t run_as_uid */
                                                  0,    /* uid_t run_as_euid */
                                                  &status,
                                                  &error_message,
                                                  NULL, /* input_string */
                                                  "wipefs -a \"%s\"",
                                                  escaped_device))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Error wiping device %s to be used in a RAID array: %s",
                                                 storaged_block_get_device (block),
                                                 error_message);
          g_free (error_message);
          g_object_unref (object_for_block);
          g_free (escaped_device);
          goto out;
        }
      g_object_unref (object_for_block);
      g_free (escaped_device);
    }

  /* Create the array... */
  escaped_name = storaged_daemon_util_escape (arg_name);
  str = g_string_new ("mdadm");
  raid_device_file = storaged_daemon_util_get_free_mdraid_device ();
  if (raid_device_file == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Unable to find free MD device");
      goto out;
    }
  g_string_append_printf (str, " --create %s", raid_device_file);
  g_string_append_printf (str, " --run");
  if (arg_chunk > 0)
    g_string_append_printf (str, " --chunk %" G_GUINT64_FORMAT, (guint64) (arg_chunk / 1024LL));
  g_string_append_printf (str, " --level %s", arg_level);
  if (strlen (arg_name) > 0)
    g_string_append_printf (str, " --name \"%s\"", escaped_name);
  g_string_append_printf (str, " --raid-devices %u", num_devices);
  for (l = blocks; l != NULL; l = l->next)
    {
      StoragedBlock *block = STORAGED_BLOCK (l->data);
      gchar *escaped_device;
      escaped_device = storaged_daemon_util_escape (storaged_block_get_device (block));
      g_string_append_printf (str, " \"%s\"", escaped_device);
      g_free (escaped_device);
    }

  if (!storaged_daemon_launch_spawned_job_sync (manager->daemon,
                                                NULL,
                                                "mdraid-create", caller_uid,
                                                NULL, /* cancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                &status,
                                                &error_message,
                                                NULL, /* input_string */
                                                "%s",
                                                str->str))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error creating RAID array: %s",
                                             error_message);
      g_free (error_message);
      goto out;
    }

  /* ... then, sit and wait for raid array object to show up */
  array_object = storaged_daemon_wait_for_object_sync (manager->daemon,
                                                       wait_for_array_object,
                                                       raid_device_file,
                                                       NULL,
                                                       10, /* timeout_seconds */
                                                       &error);
  if (array_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for array object after creating %s",
                      raid_device_file);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (stat (raid_device_file, &statbuf) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error calling stat(2) on %s: %m",
                                             raid_device_file);
      goto out;
    }
  if (!S_ISBLK (statbuf.st_mode))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Device file %s is not a block device",
                                             raid_device_file);
      goto out;
    }
  raid_device_num = statbuf.st_rdev;

  /* update the mdraid file */
  storaged_state_add_mdraid (storaged_daemon_get_state (manager->daemon),
                             raid_device_num,
                             caller_uid);

  /* ... wipe the created RAID array */
  if (!storaged_daemon_launch_spawned_job_sync (manager->daemon,
                                                array_object,
                                                "format-erase", caller_uid,
                                                NULL, /* cancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                &status,
                                                &error_message,
                                                NULL, /* input_string */
                                                "wipefs -a %s",
                                                raid_device_file))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error wiping raid device %s: %s",
                                             raid_device_file,
                                             error_message);
      goto out;
    }

  /* ... finally trigger uevents on the members - we want this so the
   * udev database is updated for them with e.g. ID_FS_TYPE. Ideally
   * mdadm(8) or whatever thing is writing out the RAID metadata would
   * ensure this, but that's not how things currently work :-/
   */
  for (l = blocks; l != NULL; l = l->next)
    {
      StoragedBlock *block = STORAGED_BLOCK (l->data);
      StoragedObject *object_for_block;
      object_for_block = storaged_daemon_util_dup_object (block, &error);
      if (object_for_block == NULL)
        {
          g_dbus_method_invocation_return_gerror (invocation, error);
          g_clear_error (&error);
          goto out;
        }
      storaged_linux_block_object_trigger_uevent (STORAGED_LINUX_BLOCK_OBJECT (object_for_block));
      g_object_unref (object_for_block);
    }

  /* ... and, we're done! */
  storaged_manager_complete_mdraid_create (_object,
                                           invocation,
                                           g_dbus_object_get_object_path (G_DBUS_OBJECT (array_object)));

 out:
  g_free (raid_device_file);
  if (str != NULL)
    g_string_free (str, TRUE);
  g_list_free_full (blocks, g_object_unref);
  g_free (escaped_name);
  g_clear_object (&array_object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
load_modules (StoragedDaemon *daemon)
{
  StoragedModuleManager *module_manager;

  g_return_if_fail (STORAGED_IS_DAEMON (daemon));

  module_manager = storaged_daemon_get_module_manager (daemon);
  storaged_module_manager_load_modules (module_manager);
}

static gboolean
handle_enable_modules (StoragedManager       *object,
                       GDBusMethodInvocation *invocation,
                       gboolean               arg_enable)
{
  StoragedLinuxManager *manager = STORAGED_LINUX_MANAGER (object);

  if (! arg_enable)
    {
      /* TODO: implement proper module unloading */
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                                     "Invalid value \"FALSE\"");
      return TRUE;
    }

  if (! storaged_daemon_get_disable_modules (manager->daemon))
    load_modules (manager->daemon);

  storaged_manager_complete_enable_modules (object, invocation);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
manager_iface_init (StoragedManagerIface *iface)
{
  iface->handle_loop_setup = handle_loop_setup;
  iface->handle_mdraid_create = handle_mdraid_create;
  iface->handle_enable_modules = handle_enable_modules;
}
