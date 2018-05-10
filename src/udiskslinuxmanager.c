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

#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <blockdev/loop.h>
#include <blockdev/fs.h>
#include <blockdev/mdraid.h>

#include "udiskslogging.h"
#include "udiskslinuxmanager.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksstate.h"
#include "udiskslinuxblockobject.h"
#include "udiskslinuxdevice.h"
#include "udisksmodulemanager.h"
#include "udiskslinuxfsinfo.h"
#include "udiskssimplejob.h"

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

  GMutex lock;

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
  UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (object);

  g_mutex_clear (&(manager->lock));

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
  g_mutex_init (&(manager->lock));
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);

  udisks_manager_set_supported_filesystems (UDISKS_MANAGER (manager),
                                            get_supported_filesystems ());
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

static UDisksObject *
wait_for_loop_object (UDisksDaemon *daemon,
                      gpointer      user_data)
{
  WaitForLoopData *data = user_data;
  UDisksObject *ret = NULL;
  UDisksObject *object = NULL;
  UDisksBlock *block;
  UDisksLoop *loop;
  UDisksLinuxDevice *device = NULL;
  GDir *dir;

  /* First see if we have the right loop object */
  object = udisks_daemon_find_block_by_device_file (daemon, data->loop_device);
  if (object == NULL)
    goto out;
  block = udisks_object_peek_block (object);
  loop = udisks_object_peek_loop (object);
  if (block == NULL || loop == NULL)
    goto out;
  if (g_strcmp0 (udisks_loop_get_backing_file (loop), data->path) != 0)
    goto out;

  /* We also need to wait for all partitions to be in place in case
   * the loop device is partitioned... we can do it like this because
   * we are guaranteed that partitions are in sysfs when receiving the
   * uevent for the main block device...
   */
  device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));
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
              UDisksObject *partition_object;
              sysfs_path = g_strconcat (g_udev_device_get_sysfs_path (device->udev_device), "/", name, NULL);
              partition_object = udisks_daemon_find_block_by_sysfs_path (daemon, sysfs_path);
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
  gchar *loop_device = NULL;
  const gchar *loop_name = NULL;
  UDisksObject *loop_object = NULL;
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
  if (!udisks_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* Check if the user is authorized to create a loop device */
  if (!udisks_daemon_util_check_authorization_sync (manager->daemon,
                                                    NULL,
                                                    "org.freedesktop.udisks2.loop-setup",
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
  g_variant_lookup (options, "no-part-scan", "b", &option_no_part_scan);

  /* it's not a problem if fstat fails... for example, this can happen if the user
   * passes a fd to a file on the GVfs fuse mount
   */
  if (fstat (fd, &fd_statbuf) == 0)
    fd_statbuf_valid = TRUE;

  error = NULL;
  if (!bd_loop_setup_from_fd (fd,
                              option_offset,
                              option_size,
                              option_read_only,
                              !option_no_part_scan,
                              &loop_name,
                              &error))
    {
      g_prefix_error (&error, "Error creating loop device: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  loop_device = g_strdup_printf ("/dev/%s", loop_name);

  /* Update the udisks loop state file (/run/udisks2/loop) with information
   * about the new loop device created by us.
   */
  udisks_state_add_loop (udisks_daemon_get_state (manager->daemon),
                        loop_device,
                        path,
                        fd_statbuf_valid ? fd_statbuf.st_dev : 0,
                        caller_uid);

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
  g_free ((gpointer) loop_name);
  if (fd != -1)
    close (fd);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  gint md_num;
} WaitForArrayData;

static UDisksObject *
wait_for_array_object (UDisksDaemon *daemon,
                       gpointer      user_data)
{
  const gchar *raid_device_file = user_data;
  UDisksObject *object = NULL;
  UDisksBlock *block = NULL;
  gchar *mdraid_objpath = NULL;
  UDisksObject *ret = NULL;

  /* First see if we have the right array object */
  object = udisks_daemon_find_block_by_device_file (daemon, raid_device_file);
  if (object == NULL)
    goto out;

  block = udisks_object_get_block (object);
  if (block == NULL)
    goto out;

  mdraid_objpath = udisks_block_dup_mdraid (block);
  if (g_strcmp0 (mdraid_objpath, "/") == 0)
    goto out;

  ret = udisks_daemon_find_object (daemon, mdraid_objpath);

 out:
  g_free (mdraid_objpath);
  g_clear_object (&block);
  g_clear_object (&object);
  return ret;
}

static const gchar *raid_level_whitelist[] = {"raid0", "raid1", "raid4", "raid5", "raid6", "raid10", NULL};

static gboolean
handle_mdraid_create (UDisksManager         *_object,
                      GDBusMethodInvocation *invocation,
                      const gchar *const    *arg_blocks,
                      const gchar           *arg_level,
                      const gchar           *arg_name,
                      guint64                arg_chunk,
                      GVariant              *arg_options)
{
  UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (_object);
  UDisksObject *array_object = NULL;
  uid_t caller_uid;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;
  guint num_devices = 0;
  GList *blocks = NULL;
  GList *l;
  guint n;
  gchar *array_name = NULL;
  gchar *raid_device_file = NULL;
  gchar *raid_node = NULL;
  struct stat statbuf;
  dev_t raid_device_num;
  UDisksBaseJob *job = NULL;
  const gchar **disks = NULL;
  guint disks_top = 0;
  gboolean success = FALSE;

  if (!udisks_daemon_util_get_caller_uid_sync (manager->daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               NULL, NULL,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      success = FALSE;
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * attempts to start a RAID Array.
   */
  /* TODO: variables */
  message = N_("Authentication is required to create a RAID array");
  action_id = "org.freedesktop.udisks2.manage-md-raid";
  if (!udisks_daemon_util_check_authorization_sync (manager->daemon,
                                                    NULL,
                                                    action_id,
                                                    arg_options,
                                                    message,
                                                    invocation))
    {
      success = FALSE;
      goto out;
    }

  /* Authentication checked -- lets create the job */
  job = udisks_daemon_launch_simple_job (manager->daemon,
                                         NULL,
                                         "mdraid-create",
                                         caller_uid,
                                         NULL);

  if (job == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to create a job object");
      success = FALSE;
      goto out;
    }

  /* validate level */
  for (n = 0; raid_level_whitelist[n] != NULL; n++)
    {
      if (g_strcmp0 (raid_level_whitelist[n], arg_level) == 0)
        break;
    }
  if (raid_level_whitelist[n] == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Unsupported RAID level %s", arg_level);
      success = FALSE;
      goto out;
    }

  /* validate chunk (TODO: check that it's a power of 2) */
  if ((arg_chunk & 0x0fff) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Chunk %" G_GUINT64_FORMAT " is not a multiple of 4KiB", arg_chunk);
      success = FALSE;
      goto out;
    }

  /* validate chunk for raid1 */
  if (g_strcmp0 (arg_level, "raid1") == 0 && arg_chunk != 0)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Chunk must be zero for level 'raid1'");
      success = FALSE;
      goto out;
    }

  /* validate name */
  if (strlen (arg_name) > 32)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Name cannot be longer than 32 characters");
      success = FALSE;
      goto out;
    }

  num_devices = g_strv_length ((gchar **) arg_blocks);

  /* validate number of devices */
  if (num_devices < 2)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Must have at least two devices");
      success = FALSE;
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
      UDisksObject *object = NULL;
      UDisksBlock *block = NULL;
      gchar *device_file = NULL;
      int fd;

      object = udisks_daemon_find_object (manager->daemon, arg_blocks[n]);
      if (object == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Invalid object path %s at index %u",
                                                 arg_blocks[n], n);
          success = FALSE;
          goto out;
        }

      block = udisks_object_get_block (object);
      if (block == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Object path %s for index %u is not a block device",
                                                 arg_blocks[n], n);
          success = FALSE;
          goto out;
        }

      device_file = udisks_block_dup_device (block);
      fd = open (device_file, O_RDWR | O_EXCL);
      if (fd < 0)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error opening device %s: %m",
                                                 device_file);
          g_free (device_file);
          success = FALSE;
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
      UDisksBlock *block = UDISKS_BLOCK (l->data);
      if (!bd_fs_wipe (udisks_block_get_device (block), TRUE, &error))
        {
          /* no signature to remove, ignore */
          if (g_error_matches (error, BD_FS_ERROR, BD_FS_ERROR_NOFS))
            g_clear_error (&error);
          else
            {
              g_prefix_error (&error,
                              "Error wiping device %s to be used in the RAID array:",
                              udisks_block_get_device (block));
              g_dbus_method_invocation_take_error (invocation, error);
              success = FALSE;
              goto out;
            }
        }
    }

  /* we have name from the user */
  if (strlen (arg_name) > 0)
      array_name = g_strdup (arg_name);
  /* we don't have name, get next 'free' /dev/mdX device */
  else
    {
      array_name = udisks_daemon_util_get_free_mdraid_device ();
      if (array_name == NULL)
        {
          g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                 "Unable to find free MD device");
          success = FALSE;
          goto out;
        }
    }

  /* names of members as gchar** for libblockdev */
  disks = g_new0 (const gchar*, g_list_length (blocks) + 1);
  for (l = blocks; l != NULL; l = l->next)
    {
      UDisksBlock *block = UDISKS_BLOCK (l->data);
      disks[disks_top++] = udisks_block_dup_device (block);
    }
  disks[disks_top] = NULL;

  if (!bd_md_create (array_name, arg_level, disks, 0, NULL, FALSE, arg_chunk, NULL, &error))
    {
      g_prefix_error (&error, "Error creating RAID array:");
      g_dbus_method_invocation_take_error (invocation, error);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      success = FALSE;
      goto out;
    }

  /* User specified name of the array, we need to get the md node */
  if (strlen (arg_name) > 0)
    {
      raid_node = bd_md_node_from_name (array_name, &error);
      if (!raid_node)
        {
          g_prefix_error (&error, "Failed to get md node for array %s", array_name);
          g_dbus_method_invocation_take_error (invocation, error);
          success = FALSE;
          goto out;
        }
      raid_device_file = g_strdup_printf ("/dev/%s", raid_node);
    }

  else
    raid_device_file = g_strdup (array_name);

  /* ... then, sit and wait for raid array object to show up */
  array_object = udisks_daemon_wait_for_object_sync (manager->daemon,
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
      success = FALSE;
      goto out;
    }

  if (stat (raid_device_file, &statbuf) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error calling stat(2) on %s: %m",
                                             raid_device_file);
      success = FALSE;
      goto out;
    }
  if (!S_ISBLK (statbuf.st_mode))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Device file %s is not a block device",
                                             raid_device_file);
      success = FALSE;
      goto out;
    }
  raid_device_num = statbuf.st_rdev;

  /* update the mdraid file */
  udisks_state_add_mdraid (udisks_daemon_get_state (manager->daemon),
                           raid_device_num,
                           caller_uid);

  /* ... wipe the created RAID array */
  if (!bd_fs_wipe (raid_device_file, TRUE, &error))
    {
      if (g_error_matches (error, BD_FS_ERROR, BD_FS_ERROR_NOFS))
        g_clear_error (&error);
      else
        {
          g_prefix_error (&error, "Error wiping raid device %s:", raid_device_file);
          g_dbus_method_invocation_take_error (invocation, error);
          success = FALSE;
          goto out;
        }
    }

  /* ... finally trigger uevents on the members - we want this so the
   * udev database is updated for them with e.g. ID_FS_TYPE. Ideally
   * mdadm(8) or whatever thing is writing out the RAID metadata would
   * ensure this, but that's not how things currently work :-/
   */
  for (l = blocks; l != NULL; l = l->next)
    {
      UDisksBlock *block = UDISKS_BLOCK (l->data);
      UDisksObject *object_for_block;
      object_for_block = udisks_daemon_util_dup_object (block, &error);
      if (object_for_block == NULL)
        {
          g_dbus_method_invocation_return_gerror (invocation, error);
          g_clear_error (&error);
          success = FALSE;
          goto out;
        }
      udisks_linux_block_object_trigger_uevent (UDISKS_LINUX_BLOCK_OBJECT (object_for_block));
      g_object_unref (object_for_block);
    }

  /* ... and, we're done! */
  udisks_manager_complete_mdraid_create (_object,
                                         invocation,
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (array_object)));

  success = TRUE;

 out:

  if (job != NULL)
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), success, NULL);
    }

  g_strfreev ((gchar **) disks);
  g_free (raid_device_file);
  g_free (raid_node);
  g_free (array_name);
  g_list_free_full (blocks, g_object_unref);
  g_clear_object (&array_object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
load_modules (UDisksDaemon *daemon)
{
  UDisksModuleManager *module_manager;

  g_return_if_fail (UDISKS_IS_DAEMON (daemon));

  module_manager = udisks_daemon_get_module_manager (daemon);
  udisks_module_manager_load_modules (module_manager);
}

static gboolean
handle_enable_modules (UDisksManager *object,
                       GDBusMethodInvocation *invocation,
                       gboolean arg_enable)
{
  UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (object);

  if (! arg_enable)
    {
      /* TODO: implement proper module unloading */
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                                     "Invalid value \"FALSE\"");
      return TRUE;
    }

  if (! udisks_daemon_get_disable_modules (manager->daemon))
    load_modules (manager->daemon);

  udisks_manager_complete_enable_modules (object, invocation);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static gboolean
handle_can_format (UDisksManager         *object,
                   GDBusMethodInvocation *invocation,
                   const gchar           *type)
{
  gchar *required_utility = NULL;
  gchar *binary_path = NULL;

  if (g_strcmp0 (type, "swap") == 0)
    required_utility = g_strdup ("mkswap");
  else if (g_strcmp0 (type, "empty") == 0)
    required_utility = g_strdup ("wipefs");
  else
    {
      const gchar **supported_fs = get_supported_filesystems ();
      for (gsize i = 0; supported_fs[i] != NULL; i++)
        {
          if (g_strcmp0 (type, supported_fs[i]) == 0)
            {
            required_utility = g_strconcat ("mkfs.", type, NULL);
            break;
            }
        }
    }

  if (required_utility == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Creation of filesystem type %s is not supported",
                                             type);
      return TRUE;
    }

  binary_path = g_find_program_in_path (required_utility);
  udisks_manager_complete_can_format (object,
                                      invocation,
                                      g_variant_new ("(bs)", binary_path != NULL,
                                                     binary_path != NULL ? "" : required_utility));
  g_free (binary_path);
  g_free (required_utility);

  return TRUE;
}

static gboolean
handle_can_resize (UDisksManager         *object,
                   GDBusMethodInvocation *invocation,
                   const gchar           *type)
{
  GError *error = NULL;
  gchar *required_utility = NULL;
  BDFsResizeFlags mode;
  gboolean ret;

  ret = bd_fs_can_resize (type, &mode, &required_utility, &error);

  if (error != NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  udisks_manager_complete_can_resize (object,
                                      invocation,
                                      g_variant_new ("(bts)", ret, (guint64) mode,
                                                     ret ? "" : required_utility));

  g_free (required_utility);

  return TRUE;
}

static gboolean
handle_can_check (UDisksManager         *object,
                  GDBusMethodInvocation *invocation,
                  const gchar           *type)
{
  GError *error = NULL;
  gchar *required_utility = NULL;
  gboolean ret;

  ret = bd_fs_can_check (type, &required_utility, &error);

  if (error != NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  udisks_manager_complete_can_check (object,
                                     invocation,
                                     g_variant_new ("(bs)", ret,
                                                    ret ? "" : required_utility));

  g_free (required_utility);

  return TRUE;
}

static gboolean
handle_can_repair (UDisksManager         *object,
                   GDBusMethodInvocation *invocation,
                   const gchar           *type)
{
  GError *error = NULL;
  gchar *required_utility = NULL;
  gboolean ret;

  ret = bd_fs_can_repair (type, &required_utility, &error);

  if (error != NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE;
    }

  udisks_manager_complete_can_repair (object,
                                      invocation,
                                      g_variant_new ("(bs)", ret,
                                                     ret ? "" : required_utility));

  g_free (required_utility);

  return TRUE;
}


static GSList*
get_block_objects (UDisksManager *manager,
                   guint         *num_blocks)
{
  UDisksLinuxManager *linux_manager = UDISKS_LINUX_MANAGER (manager);
  GDBusObjectManagerServer *object_manager = NULL;
  GList *objects = NULL;
  GList *objects_p = NULL;
  GSList *ret = NULL;

  object_manager = udisks_daemon_get_object_manager (linux_manager->daemon);
  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));

  for (objects_p = objects; objects_p != NULL; objects_p = objects_p->next)
    {
      UDisksObject *object = UDISKS_OBJECT (objects_p->data);
      UDisksBlock *block;

      block = udisks_object_get_block (object);
      if (block != NULL)
        {
          ret = g_slist_prepend (ret, block);
          (*num_blocks)++;
        }
    }

  g_list_free_full (objects, g_object_unref);
  ret = g_slist_reverse (ret);
  return ret;
}

static gboolean
handle_get_block_devices (UDisksManager         *object,
                          GDBusMethodInvocation *invocation,
                          GVariant              *arg_options)
{
  GSList *blocks = NULL;
  GSList *blocks_p = NULL;
  const gchar **block_paths = NULL;
  guint num_blocks = 0;
  guint i = 0;

  blocks = get_block_objects (object, &num_blocks);
  block_paths = g_new0 (const gchar *, num_blocks + 1);

  for (i = 0,blocks_p = blocks; blocks_p != NULL; blocks_p = blocks_p->next, i++)
      block_paths[i] = g_dbus_object_get_object_path (g_dbus_interface_get_object (G_DBUS_INTERFACE (blocks_p->data)));

  udisks_manager_complete_get_block_devices  (object,
                                              invocation,
                                              block_paths);

  g_slist_free_full (blocks, g_object_unref);

  return TRUE;  /* returning TRUE means that we handled the method invocation */
}

static gboolean
compare_paths (UDisksManager         *object,
               UDisksBlock           *block,
               gchar                 *path)
{
  const gchar *const *symlinks = NULL;

  if (g_strcmp0 (udisks_block_get_device (block), path) == 0)
    return TRUE;

  symlinks = udisks_block_get_symlinks (block);
  if (symlinks != NULL)
    {
      for (guint i = 0; symlinks[i] != NULL; i++)
        if (g_strcmp0 (symlinks[i], path) == 0)
          return TRUE;
    }

  return FALSE;
}

static gboolean
handle_resolve_device (UDisksManager         *object,
                       GDBusMethodInvocation *invocation,
                       GVariant              *arg_devspec,
                       GVariant              *arg_options)
{

  gchar *devpath = NULL;
  gchar *devuuid = NULL;
  gchar *devlabel = NULL;

  GSList *blocks = NULL;
  GSList *blocks_p = NULL;
  guint num_blocks = 0;

  GSList *ret = NULL;
  GSList *ret_p = NULL;
  guint num_found = 0;
  const gchar **ret_paths = NULL;

  gboolean found = FALSE;
  guint i = 0;

  g_variant_lookup (arg_devspec, "path", "s", &devpath);
  g_variant_lookup (arg_devspec, "uuid", "s", &devuuid);
  g_variant_lookup (arg_devspec, "label", "s", &devlabel);

  blocks = get_block_objects (object, &num_blocks);

  for (blocks_p = blocks; blocks_p != NULL; blocks_p = blocks_p->next)
    {
      if (devpath != NULL)
          found = compare_paths (object, blocks_p->data, devpath);

      if (devuuid != NULL)
          found = g_strcmp0 (udisks_block_get_id_uuid (blocks_p->data), devuuid) == 0;
      if (devlabel != NULL)
          found = g_strcmp0 (udisks_block_get_id_label (blocks_p->data), devlabel) == 0;

      if (found)
        {
          ret = g_slist_prepend (ret, g_object_ref (blocks_p->data));
          num_found++;
        }
    }

    ret_paths = g_new0 (const gchar *, num_found + 1);
    for (i = 0,ret_p = ret; ret_p != NULL; ret_p = ret_p->next, i++)
      {
        ret_paths[i] = g_dbus_object_get_object_path (g_dbus_interface_get_object (G_DBUS_INTERFACE (ret_p->data)));
      }

    udisks_manager_complete_resolve_device (object,
                                            invocation,
                                            ret_paths);

    g_slist_free_full (blocks, g_object_unref);
    g_slist_free_full (ret, g_object_unref);

    return TRUE;  /* returning TRUE means that we handled the method invocation */

}

/* ---------------------------------------------------------------------------------------------------- */

static void
manager_iface_init (UDisksManagerIface *iface)
{
  iface->handle_loop_setup = handle_loop_setup;
  iface->handle_mdraid_create = handle_mdraid_create;
  iface->handle_enable_modules = handle_enable_modules;
  iface->handle_can_format = handle_can_format;
  iface->handle_can_resize = handle_can_resize;
  iface->handle_can_check = handle_can_check;
  iface->handle_can_repair = handle_can_repair;
  iface->handle_get_block_devices = handle_get_block_devices;
  iface->handle_resolve_device = handle_resolve_device;
}
