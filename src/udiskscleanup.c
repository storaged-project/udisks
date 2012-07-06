/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2011 David Zeuthen <zeuthen@gmail.com>
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

#include <glib/gstdio.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/loop.h>

#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udiskspersistentstore.h"
#include "udisksmount.h"
#include "udisksmountmonitor.h"
#include "udiskslogging.h"
#include "udiskslinuxprovider.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskscleanup
 * @title: UDisksCleanup
 * @short_description: Object used for cleaning up after device removal
 *
 * This type is used for cleaning up when devices set up via the
 * udisks interfaces are removed while still in use - for example, a
 * USB stick being yanked. The #UDisksPersistentStore type is used to
 * record this information to ensure that it exists across daemon
 * restarts and OS reboots.
 *
 * The following files are used:
 * <table frame='all'>
 *   <title>Persistent information used for cleanup</title>
 *   <tgroup cols='2' align='left' colsep='1' rowsep='1'>
 *     <thead>
 *       <row>
 *         <entry>File</entry>
 *         <entry>Usage</entry>
 *       </row>
 *     </thead>
 *     <tbody>
 *       <row>
 *         <entry><filename>/run/udisks2/mounted-fs</filename></entry>
 *         <entry>
 *           A serialized 'a{sa{sv}}' #GVariant mapping from the
 *           mount point (e.g. <filename>/media/EOS_DIGITAL</filename>) into a set of details.
 *           Known details include
 *           <literal>block-device</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-UINT64:CAPS">'t'</link>) that is the #dev_t
 *           for the mounted device,
 *           <literal>mounted-by-uid</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-UINT32:CAPS">'u'</link>) that is the #uid_t
 *           of the user who mounted the device, and
 *           <literal>fstab-mount</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-BOOLEAN:CAPS">'b'</link>) that is %TRUE
 *           if the device was mounted via an entry in /etc/fstab.
 *         </entry>
 *       </row>
 *       <row>
 *         <entry><filename>/run/udisks2/unlocked-luks</filename></entry>
 *         <entry>
 *           A serialized 'a{ta{sv}}' #GVariant mapping from the
 *           #dev_t of the clear-text device (e.g. <filename>/dev/dm-0</filename>) into a set of details.
 *           Known details include
 *           <literal>crypto-device</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-UINT64:CAPS">'t'</link>) that is the #dev_t
 *           for the crypto-text device,
 *           <literal>dm-uuid</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-ARRAY:CAPS">'ay'</link>) that is the device mapper UUID
 *           for the clear-text device and
 *           <literal>unlocked-by-uid</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-UINT32:CAPS">'u'</link>) that is the #uid_t
 *           of the user who unlocked the device.
 *         </entry>
 *       </row>
 *       <row>
 *         <entry><filename>/run/udisks2/loop</filename></entry>
 *         <entry>
 *           A serialized 'a{sa{sv}}' #GVariant mapping from the
 *           loop device name (e.g. <filename>/dev/loop0</filename>) into a set of details.
 *           Known details include
 *           <literal>backing-file</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-ARRAY:CAPS">'ay'</link>) for the name of the backing file and
 *           <literal>backing-file-device</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-UINT64:CAPS">'t'</link>) for the #dev_t
 *           for of the device holding the backing file (or 0 if unknown) and
 *           <literal>setup-by-uid</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-UINT32:CAPS">'u'</link>) that is the #uid_t
 *           of the user who set up the loop device.
 *         </entry>
 *       </row>
 *     </tbody>
 *   </tgroup>
 * </table>
 * Cleaning up is implemented by running a thread (to ensure that
 * actions are serialized) that checks all data in the files mentioned
 * above and cleans up the entry in question by e.g. unmounting a
 * filesystem, removing a mount point or tearing down a device-mapper
 * device when needed. The clean-up thread itself needs to be manually
 * kicked using e.g. udisks_cleanup_check() from suitable places in
 * the #UDisksDaemon and #UDisksProvider implementations.
 *
 * Since cleaning up is only necessary when a device has been removed
 * without having been properly stopped or shut down, the fact that it
 * was cleaned up is logged to ensure that the information is brought
 * to the attention of the system administrator.
 */

/**
 * UDisksCleanup:
 *
 * The #UDisksCleanup structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksCleanup
{
  GObject parent_instance;

  GMutex lock;

  UDisksDaemon *daemon;
  UDisksPersistentStore *persistent_store;

  GThread *thread;
  GMainContext *context;
  GMainLoop *loop;
};

typedef struct _UDisksCleanupClass UDisksCleanupClass;

struct _UDisksCleanupClass
{
  GObjectClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON
};

static void udisks_cleanup_check_in_thread (UDisksCleanup *cleanup);

static void udisks_cleanup_check_mounted_fs (UDisksCleanup  *cleanup,
                                             GArray         *devs_to_clean);

static void udisks_cleanup_check_unlocked_luks (UDisksCleanup *cleanup,
                                                gboolean       check_only,
                                                GArray        *devs_to_clean);

static void udisks_cleanup_check_loop (UDisksCleanup *cleanup,
                                       gboolean       check_only,
                                       GArray        *devs_to_clean);

G_DEFINE_TYPE (UDisksCleanup, udisks_cleanup, G_TYPE_OBJECT);


static void
udisks_cleanup_init (UDisksCleanup *cleanup)
{
  g_mutex_init (&cleanup->lock);
}

static void
udisks_cleanup_finalize (GObject *object)
{
  UDisksCleanup *cleanup = UDISKS_CLEANUP (object);

  g_mutex_clear (&cleanup->lock);

  G_OBJECT_CLASS (udisks_cleanup_parent_class)->finalize (object);
}

static void
udisks_cleanup_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  UDisksCleanup *cleanup = UDISKS_CLEANUP (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_cleanup_get_daemon (cleanup));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_cleanup_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  UDisksCleanup *cleanup = UDISKS_CLEANUP (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (cleanup->daemon == NULL);
      /* we don't take a reference to the daemon */
      cleanup->daemon = g_value_get_object (value);
      g_assert (cleanup->daemon != NULL);
      cleanup->persistent_store = udisks_daemon_get_persistent_store (cleanup->daemon);
      g_assert (cleanup->persistent_store != NULL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_cleanup_class_init (UDisksCleanupClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = udisks_cleanup_finalize;
  gobject_class->set_property = udisks_cleanup_set_property;
  gobject_class->get_property = udisks_cleanup_get_property;

  /**
   * UDisksCleanup:daemon:
   *
   * The #UDisksDaemon object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon object",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_cleanup_new:
 * @daemon: A #UDisksDaemon.
 *
 * Creates a new #UDisksCleanup object.
 *
 * Returns: A #UDisksCleanup that should be freed with g_object_unref().
 */
UDisksCleanup *
udisks_cleanup_new (UDisksDaemon *daemon)
{
  return UDISKS_CLEANUP (g_object_new (UDISKS_TYPE_CLEANUP,
                                       "daemon", daemon,
                                       NULL));
}

static gpointer
udisks_cleanup_thread_func (gpointer user_data)
{
  UDisksCleanup *cleanup = UDISKS_CLEANUP (user_data);

  udisks_info ("Entering cleanup thread");

  g_main_loop_run (cleanup->loop);

  cleanup->thread = NULL;
  g_main_loop_unref (cleanup->loop);
  cleanup->loop = NULL;
  g_main_context_unref (cleanup->context);
  cleanup->context = NULL;
  g_object_unref (cleanup);

  udisks_info ("Exiting cleanup thread");
  return NULL;
}


/**
 * udisks_cleanup_start:
 * @cleanup: A #UDisksCleanup.
 *
 * Starts the clean-up thread.
 *
 * The clean-up thread will hold a reference to @cleanup for as long
 * as it's running - use udisks_cleanup_stop() to stop it.
 */
void
udisks_cleanup_start (UDisksCleanup *cleanup)
{
  g_return_if_fail (UDISKS_IS_CLEANUP (cleanup));
  g_return_if_fail (cleanup->thread == NULL);

  cleanup->context = g_main_context_new ();
  cleanup->loop = g_main_loop_new (cleanup->context, FALSE);
  cleanup->thread = g_thread_new ("cleanup",
                                  udisks_cleanup_thread_func,
                                  g_object_ref (cleanup));
}

/**
 * udisks_cleanup_stop:
 * @cleanup: A #UDisksCleanup.
 *
 * Stops the clean-up thread. Blocks the calling thread until it has stopped.
 */
void
udisks_cleanup_stop (UDisksCleanup *cleanup)
{
  GThread *thread;

  g_return_if_fail (UDISKS_IS_CLEANUP (cleanup));
  g_return_if_fail (cleanup->thread != NULL);

  thread = cleanup->thread;
  g_main_loop_quit (cleanup->loop);
  g_thread_join (thread);
}

static gboolean
udisks_cleanup_check_func (gpointer user_data)
{
  UDisksCleanup *cleanup = UDISKS_CLEANUP (user_data);
  udisks_cleanup_check_in_thread (cleanup);
  return FALSE;
}

/**
 * udisks_cleanup_check:
 * @cleanup: A #UDisksCleanup.
 *
 * Causes the clean-up thread for @cleanup to check if anything should be cleaned up.
 *
 * This can be called from any thread and will not block the calling thread.
 */
void
udisks_cleanup_check (UDisksCleanup *cleanup)
{
  g_return_if_fail (UDISKS_IS_CLEANUP (cleanup));
  g_return_if_fail (cleanup->thread != NULL);

  g_main_context_invoke (cleanup->context,
                         udisks_cleanup_check_func,
                         cleanup);
}

/**
 * udisks_cleanup_get_daemon:
 * @cleanup: A #UDisksCleanup.
 *
 * Gets the daemon used by @cleanup.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @cleanup.
 */
UDisksDaemon *
udisks_cleanup_get_daemon (UDisksCleanup *cleanup)
{
  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), NULL);
  return cleanup->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

/* must be called from cleanup thread */
static void
udisks_cleanup_check_in_thread (UDisksCleanup *cleanup)
{
  GArray *devs_to_clean;

  g_mutex_lock (&cleanup->lock);

  /* We have to do a two-stage clean-up since fake block devices
   * can't be stopped if they are in use
   */

  udisks_info ("Cleanup check start");

  /* First go through all block devices we might tear down
   * but only check + record devices marked for cleaning
   */
  devs_to_clean = g_array_new (FALSE, FALSE, sizeof (dev_t));
  udisks_cleanup_check_unlocked_luks (cleanup,
                                      TRUE, /* check_only */
                                      devs_to_clean);
  udisks_cleanup_check_loop (cleanup,
                             TRUE, /* check_only */
                             devs_to_clean);

  /* Then go through all mounted filesystems and pass the
   * devices that we intend to clean...
   */
  udisks_cleanup_check_mounted_fs (cleanup, devs_to_clean);

  /* Then go through all block devices and clear them up
   * ... for real this time
   */
  udisks_cleanup_check_unlocked_luks (cleanup,
                                      FALSE, /* check_only */
                                      NULL);
  udisks_cleanup_check_loop (cleanup,
                             FALSE, /* check_only */
                             NULL);

  g_array_unref (devs_to_clean);

  udisks_info ("Cleanup check end");

  g_mutex_unlock (&cleanup->lock);
}

/* ---------------------------------------------------------------------------------------------------- */

static GVariant *
lookup_asv (GVariant    *asv,
            const gchar *key)
{
  GVariantIter iter;
  const gchar *iter_key;
  GVariant *value;
  GVariant *ret;

  ret = NULL;

  g_variant_iter_init (&iter, asv);
  while (g_variant_iter_next (&iter,
                              "{&s@v}",
                              &iter_key,
                              &value))
    {
      if (g_strcmp0 (key, iter_key) == 0)
        {
          ret = g_variant_get_variant (value);
          g_variant_unref (value);
          goto out;
        }
      g_variant_unref (value);
    }

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
trigger_change_uevent (const gchar *sysfs_path)
{
  gchar* path = NULL;
  gint fd = -1;

  g_return_if_fail (sysfs_path != NULL);

  path = g_strconcat (sysfs_path, "/uevent", NULL);
  fd = open (path, O_WRONLY);
  if (fd < 0)
    {
      udisks_warning ("Error opening %s: %m", path);
      goto out;
    }

  if (write (fd, "change", sizeof "change" - 1) != sizeof "change" - 1)
    {
      udisks_warning ("Error writing 'change' to file %s: %m", path);
      goto out;
    }

 out:
  if (fd >= 0)
    close (fd);
  g_free (path);
}

/* returns TRUE if the entry should be kept */
static gboolean
udisks_cleanup_check_mounted_fs_entry (UDisksCleanup  *cleanup,
                                       GVariant       *value,
                                       GArray         *devs_to_clean)
{
  const gchar *mount_point;
  GVariant *details;
  GVariant *block_device_value;
  dev_t block_device;
  GVariant *fstab_mount_value;
  gboolean fstab_mount;
  gboolean keep;
  gchar *s;
  GList *mounts;
  GList *l;
  gboolean is_mounted;
  gboolean device_exists;
  gboolean device_to_be_cleaned;
  gboolean attempt_no_cleanup;
  UDisksMountMonitor *monitor;
  GUdevClient *udev_client;
  GUdevDevice *udev_device;
  guint n;
  gchar *change_sysfs_path = NULL;

  keep = FALSE;
  is_mounted = FALSE;
  device_exists = FALSE;
  device_to_be_cleaned = FALSE;
  attempt_no_cleanup = FALSE;
  block_device_value = NULL;
  fstab_mount_value = NULL;
  fstab_mount = FALSE;
  details = NULL;

  monitor = udisks_daemon_get_mount_monitor (cleanup->daemon);
  udev_client = udisks_linux_provider_get_udev_client (udisks_daemon_get_linux_provider (cleanup->daemon));

  g_variant_get (value,
                 "{&s@a{sv}}",
                 &mount_point,
                 &details);

  block_device_value = lookup_asv (details, "block-device");
  if (block_device_value == NULL)
    {
      s = g_variant_print (value, TRUE);
      udisks_error ("mounted-fs entry %s is invalid: no block-device key/value pair", s);
      g_free (s);
      attempt_no_cleanup = FALSE;
      goto out;
    }
  block_device = g_variant_get_uint64 (block_device_value);

  fstab_mount_value = lookup_asv (details, "fstab-mount");
  if (fstab_mount_value == NULL)
    {
      s = g_variant_print (value, TRUE);
      udisks_error ("mounted-fs entry %s is invalid: no fstab-mount key/value pair", s);
      g_free (s);
      attempt_no_cleanup = FALSE;
      goto out;
    }
  fstab_mount = g_variant_get_boolean (fstab_mount_value);

  /* udisks_debug ("Validating mounted-fs entry for mount point %s", mount_point); */

  /* Figure out if still mounted */
  mounts = udisks_mount_monitor_get_mounts_for_dev (monitor, block_device);
  for (l = mounts; l != NULL; l = l->next)
    {
      UDisksMount *mount = UDISKS_MOUNT (l->data);
      if (udisks_mount_get_mount_type (mount) == UDISKS_MOUNT_TYPE_FILESYSTEM &&
          g_strcmp0 (udisks_mount_get_mount_path (mount), mount_point) == 0)
        {
          is_mounted = TRUE;
          break;
        }
    }
  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);

  /* Figure out if block device still exists */
  udev_device = g_udev_client_query_by_device_number (udev_client,
                                                      G_UDEV_DEVICE_TYPE_BLOCK,
                                                      block_device);
  if (udev_device != NULL)
    {
      /* If media is pulled from a device with removable media (say,
       * /dev/sdc being a CF reader connected via USB) and a device
       * (say, /dev/sdc1) on the media is mounted, the kernel won't
       * necessarily send 'remove' uevent for /dev/sdc1 even though
       * media removal was detected (we will get a 'change' uevent
       * though).
       *
       * Therefore, we need to sanity-check the device - it appears
       * that it's good enough to just check the 'size' sysfs
       * attribute of the device (or its enclosing device if a
       * partition)
       *
       * Additionally, if we conclude that the device is not valid
       * (e.g. still there but size of device or its enclosing device
       * is 0), we also need to poke the kernel (via a 'change'
       * uevent) to make the device go away. We do that after
       * unmounting the device.
       */

      /* if umounting, issue 'change' event on the device after unmounting it */
      change_sysfs_path = g_strdup (g_udev_device_get_sysfs_path (udev_device));

      if (g_udev_device_get_sysfs_attr_as_uint64 (udev_device, "size") > 0)
        {
          /* for partition, also check enclosing device */
          if (g_strcmp0 (g_udev_device_get_devtype (udev_device), "partition") == 0)
            {
              GUdevDevice *udev_device_disk;
              udev_device_disk = g_udev_device_get_parent_with_subsystem (udev_device, "block", "disk");
              if (udev_device_disk != NULL)
                {
                  if (g_udev_device_get_sysfs_attr_as_uint64 (udev_device_disk, "size") > 0)
                    {
                      device_exists = TRUE;
                    }
                  /* if unmounting, issue 'change' uevent on the enclosing device after unmounting the device */
                  g_free (change_sysfs_path);
                  change_sysfs_path = g_strdup (g_udev_device_get_sysfs_path (udev_device_disk));
                  g_object_unref (udev_device_disk);
                }
            }
          else
            {
              device_exists = TRUE;
            }
        }
      g_object_unref (udev_device);
    }

  /* Figure out if the device is about to be cleaned up */
  for (n = 0; n < devs_to_clean->len; n++)
    {
      dev_t dev_to_clean = g_array_index (devs_to_clean, dev_t, n);
      if (dev_to_clean == block_device)
        {
          device_to_be_cleaned = TRUE;
          break;
        }
    }

  if (is_mounted && device_exists && !device_to_be_cleaned)
    keep = TRUE;

 out:

  if (!keep && !attempt_no_cleanup)
    {
      if (!device_exists)
        {
          udisks_notice ("Cleaning up mount point %s (device %d:%d no longer exist)",
                         mount_point, major (block_device), minor (block_device));
        }
      else if (device_to_be_cleaned)
        {
          udisks_notice ("Cleaning up mount point %s (device %d:%d is about to be cleaned up)",
                         mount_point, major (block_device), minor (block_device));
        }
      else if (!is_mounted)
        {
          udisks_notice ("Cleaning up mount point %s (device %d:%d is not mounted)",
                         mount_point, major (block_device), minor (block_device));
        }

      if (is_mounted)
        {
          gchar *escaped_mount_point;
          gchar *error_message;

          error_message = NULL;
          escaped_mount_point = udisks_daemon_util_escape_and_quote (mount_point);
          /* right now -l is the only way to "force unmount" file systems... */
          if (!udisks_daemon_launch_spawned_job_sync (cleanup->daemon,
                                                      NULL, /* UDisksObject */
                                                      "cleanup", 0, /* StartedByUID */
                                                      NULL, /* GCancellable */
                                                      0,    /* uid_t run_as_uid */
                                                      0,    /* uid_t run_as_euid */
                                                      NULL, /* gint *out_status */
                                                      &error_message,
                                                      NULL,  /* input_string */
                                                      "umount -l %s",
                                                      escaped_mount_point))
            {
              udisks_error ("Error cleaning up mount point %s: Error unmounting: %s",
                            mount_point, error_message);
              g_free (escaped_mount_point);
              g_free (error_message);
              /* keep the entry so we can clean it up later */
              keep = TRUE;
              goto out2;
            }
          g_free (escaped_mount_point);
          g_free (error_message);

          /* just unmounting the device does not make the kernel revalidate media
           * so we issue a 'change' uevent to request that
           */
          if (change_sysfs_path != NULL)
            {
              trigger_change_uevent (change_sysfs_path);
            }
        }

      /* remove directory */
      if (!fstab_mount)
        {
          if (g_file_test (mount_point, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
            {
              if (g_rmdir (mount_point) != 0)
                {
                  udisks_error ("Error cleaning up mount point %s: Error removing directory: %m",
                                mount_point);
                  /* keep the entry so we can clean it up later */
                  keep = TRUE;
                  goto out2;
                }
            }
        }
    }

 out2:
  if (fstab_mount_value != NULL)
    g_variant_unref (fstab_mount_value);
  if (block_device_value != NULL)
    g_variant_unref (block_device_value);
  if (details != NULL)
    g_variant_unref (details);

  g_free (change_sysfs_path);

  return keep;
}

/* called with mutex->lock held */
static void
udisks_cleanup_check_mounted_fs (UDisksCleanup *cleanup,
                                 GArray        *devs_to_clean)
{
  gboolean changed;
  GVariant *value;
  GVariant *new_value;
  GVariantBuilder builder;
  GError *error;

  changed = FALSE;

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "mounted-fs",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting mounted-fs: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* check valid entries */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          if (udisks_cleanup_check_mounted_fs_entry (cleanup, child, devs_to_clean))
            g_variant_builder_add_value (&builder, child);
          else
            changed = TRUE;
          g_variant_unref (child);
        }
      g_variant_unref (value);
    }

  new_value = g_variant_builder_end (&builder);

  /* save new entries */
  if (changed)
    {
      error = NULL;
      if (!udisks_persistent_store_set (cleanup->persistent_store,
                                        UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                        "mounted-fs",
                                        G_VARIANT_TYPE ("a{sa{sv}}"),
                                        new_value, /* consumes new_value */
                                        &error))
        {
          udisks_warning ("Error setting mounted-fs: %s (%s, %d)",
                          error->message,
                          g_quark_to_string (error->domain),
                          error->code);
          g_error_free (error);
          goto out;
        }
    }
  else
    {
      g_variant_unref (new_value);
    }

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_cleanup_add_mounted_fs:
 * @cleanup: A #UDisksCleanup.
 * @block_device: The block device.
 * @mount_point: The mount point.
 * @uid: The user id of the process requesting the device to be mounted.
 * @fstab_mount: %TRUE if the device was mounted via /etc/fstab.
 *
 * Adds a new entry to the
 * <filename>/run/udisks2/mounted-fs</filename> file.
 */
void
udisks_cleanup_add_mounted_fs (UDisksCleanup  *cleanup,
                               const gchar    *mount_point,
                               dev_t           block_device,
                               uid_t           uid,
                               gboolean        fstab_mount)
{
  GVariant *value;
  GVariant *new_value;
  GVariant *details_value;
  GVariantBuilder builder;
  GVariantBuilder details_builder;
  GError *error;

  g_return_if_fail (UDISKS_IS_CLEANUP (cleanup));
  g_return_if_fail (mount_point != NULL);

  g_mutex_lock (&cleanup->lock);

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "mounted-fs",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting mounted-fs: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* start by including existing entries */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;

      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          const gchar *entry_mount_point;
          g_variant_get (child, "{&s@a{sv}}", &entry_mount_point, NULL);
          /* Skip/remove stale entries */
          if (g_strcmp0 (entry_mount_point, mount_point) == 0)
            {
              udisks_warning ("Removing stale entry for mount point `%s' in /run/udisks2/mounted-fs file",
                              entry_mount_point);
            }
          else
            {
              g_variant_builder_add_value (&builder, child);
            }
          g_variant_unref (child);
        }
      g_variant_unref (value);
    }

  /* build the details */
  g_variant_builder_init (&details_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&details_builder,
                         "{sv}",
                         "block-device",
                         g_variant_new_uint64 (block_device));
  g_variant_builder_add (&details_builder,
                         "{sv}",
                         "mounted-by-uid",
                         g_variant_new_uint32 (uid));
  g_variant_builder_add (&details_builder,
                         "{sv}",
                         "fstab-mount",
                         g_variant_new_boolean (fstab_mount));
  details_value = g_variant_builder_end (&details_builder);

  /* finally add the new entry */
  g_variant_builder_add (&builder,
                         "{s@a{sv}}",
                         mount_point,
                         details_value); /* consumes details_value */
  new_value = g_variant_builder_end (&builder);

  /* save new entries */
  error = NULL;
  if (!udisks_persistent_store_set (cleanup->persistent_store,
                                    UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                    "mounted-fs",
                                    G_VARIANT_TYPE ("a{sa{sv}}"),
                                    new_value, /* consumes new_value */
                                    &error))
    {
      udisks_warning ("Error setting mounted-fs: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_mutex_unlock (&cleanup->lock);
}

/**
 * udisks_cleanup_find_mounted_fs:
 * @cleanup: A #UDisksCleanup.
 * @block_device: The block device.
 * @out_uid: Return location for the user id who mounted the device or %NULL.
 * @out_fstab_mount: Return location for whether the device was a fstab mount or %NULL.
 *
 * Gets the mount point for @block_device, if it exists in the
 * <filename>/run/udisks2/mounted-fs</filename> file.
 *
 * Returns: The mount point for @block_device or %NULL if not found.
 */
gchar *
udisks_cleanup_find_mounted_fs (UDisksCleanup   *cleanup,
                                dev_t            block_device,
                                uid_t           *out_uid,
                                gboolean        *out_fstab_mount)
{
  gchar *ret;
  GVariant *value;
  GError *error;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), NULL);

  g_mutex_lock (&cleanup->lock);

  ret = NULL;
  value = NULL;

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "mounted-fs",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting mounted-fs: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* look through list */
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          const gchar *mount_point;
          GVariant *details;
          GVariant *block_device_value;

          g_variant_get (child,
                         "{&s@a{sv}}",
                         &mount_point,
                         &details);

          block_device_value = lookup_asv (details, "block-device");
          if (block_device_value != NULL)
            {
              dev_t iter_block_device;
              iter_block_device = g_variant_get_uint64 (block_device_value);
              if (iter_block_device == block_device)
                {
                  ret = g_strdup (mount_point);
                  if (out_uid != NULL)
                    {
                      GVariant *lookup_value;
                      lookup_value = lookup_asv (details, "mounted-by-uid");
                      *out_uid = 0;
                      if (lookup_value != NULL)
                        {
                          *out_uid = g_variant_get_uint32 (lookup_value);
                          g_variant_unref (lookup_value);
                        }
                    }
                  if (out_fstab_mount != NULL)
                    {
                      GVariant *lookup_value;
                      lookup_value = lookup_asv (details, "fstab-mount");
                      *out_fstab_mount = FALSE;
                      if (lookup_value != NULL)
                        {
                          *out_fstab_mount = g_variant_get_boolean (lookup_value);
                          g_variant_unref (lookup_value);
                        }
                    }
                  g_variant_unref (block_device_value);
                  g_variant_unref (details);
                  g_variant_unref (child);
                  goto out;
                }
              g_variant_unref (block_device_value);
            }
          g_variant_unref (details);
          g_variant_unref (child);
        }
    }

 out:
  if (value != NULL)
    g_variant_unref (value);
  g_mutex_unlock (&cleanup->lock);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* returns TRUE if the entry should be kept */
static gboolean
udisks_cleanup_check_unlocked_luks_entry (UDisksCleanup  *cleanup,
                                          GVariant       *value,
                                          gboolean        check_only,
                                          GArray         *devs_to_clean)
{
  guint64 cleartext_device;
  GVariant *details;
  GVariant *crypto_device_value;
  dev_t crypto_device;
  GVariant *dm_uuid_value;
  const gchar *dm_uuid;
  gchar *device_file_cleartext;
  gboolean keep;
  gchar *s;
  gboolean is_unlocked;
  gboolean crypto_device_exists;
  gboolean attempt_no_cleanup;
  GUdevClient *udev_client;
  GUdevDevice *udev_cleartext_device;
  GUdevDevice *udev_crypto_device;

  keep = FALSE;
  is_unlocked = FALSE;
  crypto_device_exists = FALSE;
  attempt_no_cleanup = FALSE;
  device_file_cleartext = NULL;
  crypto_device_value = NULL;
  dm_uuid_value = NULL;
  details = NULL;

  udev_client = udisks_linux_provider_get_udev_client (udisks_daemon_get_linux_provider (cleanup->daemon));

  g_variant_get (value,
                 "{t@a{sv}}",
                 &cleartext_device,
                 &details);

  crypto_device_value = lookup_asv (details, "crypto-device");
  if (crypto_device_value == NULL)
    {
      s = g_variant_print (value, TRUE);
      udisks_error ("unlocked-luks entry %s is invalid: no crypto-device key/value pair", s);
      g_free (s);
      attempt_no_cleanup = TRUE;
      goto out;
    }
  crypto_device = g_variant_get_uint64 (crypto_device_value);

  dm_uuid_value = lookup_asv (details, "dm-uuid");
  if (dm_uuid_value == NULL)
    {
      s = g_variant_print (value, TRUE);
      udisks_error ("unlocked-luks entry %s is invalid: no dm-uuid key/value pair", s);
      g_free (s);
      attempt_no_cleanup = TRUE;
      goto out;
    }
  dm_uuid = g_variant_get_bytestring (dm_uuid_value);

  /*udisks_debug ("Validating luks entry for device %d:%d (backed by %d:%d) with uuid %s",
                major (cleartext_device), minor (cleartext_device),
                major (crypto_device), minor (crypto_device), dm_uuid);*/

  udev_cleartext_device = g_udev_client_query_by_device_number (udev_client,
                                                                G_UDEV_DEVICE_TYPE_BLOCK,
                                                                cleartext_device);
  if (udev_cleartext_device != NULL)
    {
      const gchar *current_dm_uuid;
      device_file_cleartext = g_strdup (g_udev_device_get_device_file (udev_cleartext_device));
      current_dm_uuid = g_udev_device_get_sysfs_attr (udev_cleartext_device, "dm/uuid");
      /* if the UUID doesn't match, then the dm device might have been reused... */
      if (g_strcmp0 (current_dm_uuid, dm_uuid) != 0)
        {
          s = g_variant_print (value, TRUE);
          udisks_warning ("Removing unlocked-luks entry %s because %s now has another dm-uuid %s",
                          s, device_file_cleartext,
                          current_dm_uuid != NULL ? current_dm_uuid : "(NULL)");
          g_free (s);
          attempt_no_cleanup = TRUE;
        }
      else
        {
          is_unlocked = TRUE;
        }
      g_object_unref (udev_cleartext_device);
    }

  udev_crypto_device = g_udev_client_query_by_device_number (udev_client,
                                                             G_UDEV_DEVICE_TYPE_BLOCK,
                                                             crypto_device);
  if (udev_crypto_device != NULL)
    {
      crypto_device_exists = TRUE;
      g_object_unref (udev_crypto_device);
    }

  /* OK, entry is valid - keep it around */
  if (is_unlocked && crypto_device_exists)
    keep = TRUE;

 out:

  if (check_only && !keep)
    {
      dev_t cleartext_device_dev_t = cleartext_device; /* !@#!$# array type */
      g_array_append_val (devs_to_clean, cleartext_device_dev_t);
      keep = TRUE;
      goto out2;
    }

  if (!keep && !attempt_no_cleanup)
    {
      if (is_unlocked)
        {
          gchar *escaped_device_file;
          gchar *error_message;

          udisks_notice ("Cleaning up LUKS device %s (backing device %d:%d no longer exist)",
                         device_file_cleartext,
                         major (crypto_device), minor (crypto_device));

          error_message = NULL;
          escaped_device_file = udisks_daemon_util_escape_and_quote (device_file_cleartext);
          if (!udisks_daemon_launch_spawned_job_sync (cleanup->daemon,
                                                      NULL, /* UDisksObject */
                                                      "cleanup", 0, /* StartedByUID */
                                                      NULL, /* GCancellable */
                                                      0,    /* uid_t run_as_uid */
                                                      0,    /* uid_t run_as_euid */
                                                      NULL, /* gint *out_status */
                                                      &error_message,
                                                      NULL,  /* input_string */
                                                      "cryptsetup luksClose %s",
                                                      escaped_device_file))
            {
              udisks_error ("Error cleaning up LUKS device %s: %s",
                            device_file_cleartext, error_message);
              g_free (escaped_device_file);
              g_free (error_message);
              /* keep the entry so we can clean it up later */
              keep = TRUE;
              goto out2;
            }
          g_free (escaped_device_file);
          g_free (error_message);
        }
      else
        {
          udisks_notice ("LUKS device %d:%d was manually removed",
                         major (cleartext_device), minor (cleartext_device));
        }
    }

 out2:
  g_free (device_file_cleartext);
  if (crypto_device_value != NULL)
    g_variant_unref (crypto_device_value);
  if (dm_uuid_value != NULL)
    g_variant_unref (dm_uuid_value);
  if (details != NULL)
    g_variant_unref (details);
  return keep;
}

/* called with mutex->lock held */
static void
udisks_cleanup_check_unlocked_luks (UDisksCleanup *cleanup,
                                    gboolean       check_only,
                                    GArray        *devs_to_clean)
{
  gboolean changed;
  GVariant *value;
  GVariant *new_value;
  GVariantBuilder builder;
  GError *error;

  changed = FALSE;

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "unlocked-luks",
                                       G_VARIANT_TYPE ("a{ta{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting unlocked-luks: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* check valid entries */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ta{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          if (udisks_cleanup_check_unlocked_luks_entry (cleanup, child, check_only, devs_to_clean))
            g_variant_builder_add_value (&builder, child);
          else
            changed = TRUE;
          g_variant_unref (child);
        }
      g_variant_unref (value);
    }

  new_value = g_variant_builder_end (&builder);

  /* save new entries */
  if (changed)
    {
      error = NULL;
      if (!udisks_persistent_store_set (cleanup->persistent_store,
                                        UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                        "unlocked-luks",
                                        G_VARIANT_TYPE ("a{ta{sv}}"),
                                        new_value, /* consumes new_value */
                                        &error))
        {
          udisks_warning ("Error setting unlocked-luks: %s (%s, %d)",
                          error->message,
                          g_quark_to_string (error->domain),
                          error->code);
          g_error_free (error);
          goto out;
        }
    }
  else
    {
      g_variant_unref (new_value);
    }

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_cleanup_add_unlocked_luks:
 * @cleanup: A #UDisksCleanup.
 * @cleartext_device: The clear-text device.
 * @crypto_device: The crypto device.
 * @dm_uuid: The UUID of the unlocked dm device.
 * @uid: The user id of the process requesting the device to be unlocked.
 *
 * Adds a new entry to the
 * <filename>/run/udisks2/unlocked-luks</filename> file.
 */
void
udisks_cleanup_add_unlocked_luks (UDisksCleanup  *cleanup,
                                  dev_t           cleartext_device,
                                  dev_t           crypto_device,
                                  const gchar    *dm_uuid,
                                  uid_t           uid)
{
  GVariant *value;
  GVariant *new_value;
  GVariant *details_value;
  GVariantBuilder builder;
  GVariantBuilder details_builder;
  GError *error;

  g_return_if_fail (UDISKS_IS_CLEANUP (cleanup));
  g_return_if_fail (dm_uuid != NULL);

  g_mutex_lock (&cleanup->lock);

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "unlocked-luks",
                                       G_VARIANT_TYPE ("a{ta{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting unlocked-luks: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* start by including existing entries */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ta{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          guint64 entry_cleartext_device;
          g_variant_get (child, "{t@a{sv}}", &entry_cleartext_device, NULL);
          /* Skip/remove stale entries */
          if ((dev_t) entry_cleartext_device == cleartext_device)
            {
              udisks_warning ("Removing stale entry for cleartext device %d:%d in /run/udisks2/unlocked-luks file",
                              (gint) major (entry_cleartext_device),
                              (gint) minor (entry_cleartext_device));
            }
          else
            {
              g_variant_builder_add_value (&builder, child);
            }
          g_variant_unref (child);
        }
      g_variant_unref (value);
    }

  /* build the details */
  g_variant_builder_init (&details_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&details_builder,
                         "{sv}",
                         "crypto-device",
                         g_variant_new_uint64 (crypto_device));
  g_variant_builder_add (&details_builder,
                         "{sv}",
                         "dm-uuid",
                         g_variant_new_bytestring (dm_uuid));
  g_variant_builder_add (&details_builder,
                         "{sv}",
                         "unlocked-by-uid",
                         g_variant_new_uint32 (uid));
  details_value = g_variant_builder_end (&details_builder);

  /* finally add the new entry */
  g_variant_builder_add (&builder,
                         "{t@a{sv}}",
                         (guint64) cleartext_device,
                         details_value); /* consumes details_value */
  new_value = g_variant_builder_end (&builder);

  /* save new entries */
  error = NULL;
  if (!udisks_persistent_store_set (cleanup->persistent_store,
                                    UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                    "unlocked-luks",
                                    G_VARIANT_TYPE ("a{ta{sv}}"),
                                    new_value, /* consumes new_value */
                                    &error))
    {
      udisks_warning ("Error setting unlocked-luks: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_mutex_unlock (&cleanup->lock);
}

/**
 * udisks_cleanup_find_unlocked_luks:
 * @cleanup: A #UDisksCleanup.
 * @crypto_device: The block device.
 * @out_uid: Return location for the user id who mounted the device or %NULL.
 *
 * Gets the clear-text device for @crypto_device, if it exists in the
 * <filename>/run/udisks2/unlocked-luks</filename> file.
 *
 * Returns: The cleartext device for @crypto_device or 0 if not found.
 */
dev_t
udisks_cleanup_find_unlocked_luks (UDisksCleanup   *cleanup,
                                   dev_t            crypto_device,
                                   uid_t           *out_uid)
{
  dev_t ret;
  GVariant *value;
  GError *error;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), 0);

  g_mutex_lock (&cleanup->lock);

  ret = 0;
  value = NULL;

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "unlocked-luks",
                                       G_VARIANT_TYPE ("a{ta{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting unlocked-luks: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* look through list */
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          guint64 cleartext_device;
          GVariant *details;
          GVariant *crypto_device_value;

          g_variant_get (child,
                         "{t@a{sv}}",
                         &cleartext_device,
                         &details);

          crypto_device_value = lookup_asv (details, "crypto-device");
          if (crypto_device_value != NULL)
            {
              dev_t iter_crypto_device;
              iter_crypto_device = g_variant_get_uint64 (crypto_device_value);
              if (iter_crypto_device == crypto_device)
                {
                  ret = cleartext_device;
                  if (out_uid != NULL)
                    {
                      GVariant *lookup_value;
                      lookup_value = lookup_asv (details, "unlocked-by-uid");
                      *out_uid = 0;
                      if (lookup_value != NULL)
                        {
                          *out_uid = g_variant_get_uint32 (lookup_value);
                          g_variant_unref (lookup_value);
                        }
                    }
                  g_variant_unref (crypto_device_value);
                  g_variant_unref (details);
                  g_variant_unref (child);
                  goto out;
                }
              g_variant_unref (crypto_device_value);
            }
          g_variant_unref (details);
          g_variant_unref (child);
        }
    }

 out:
  if (value != NULL)
    g_variant_unref (value);
  g_mutex_unlock (&cleanup->lock);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* returns TRUE if the entry should be kept */
static gboolean
udisks_cleanup_check_loop_entry (UDisksCleanup  *cleanup,
                                 GVariant       *value,
                                 gboolean        check_only,
                                 GArray         *devs_to_clean)
{
  const gchar *loop_device;
  GVariant *details = NULL;
  gboolean keep = FALSE;
  GVariant *backing_file_value = NULL;
  const gchar *backing_file;
  GUdevClient *udev_client;
  GUdevDevice *device = NULL;
  const gchar *sysfs_backing_file;

  udev_client = udisks_linux_provider_get_udev_client (udisks_daemon_get_linux_provider (cleanup->daemon));

  g_variant_get (value,
                 "{&s@a{sv}}",
                 &loop_device,
                 &details);

  backing_file_value = lookup_asv (details, "backing-file");
  if (backing_file_value == NULL)
    {
      gchar *s;
      s = g_variant_print (value, TRUE);
      udisks_error ("loop entry %s is invalid: no backing-file key/value pair", s);
      g_free (s);
      goto out;
    }
  backing_file = g_variant_get_bytestring (backing_file_value);

  /* check the loop device is still set up */
  device = g_udev_client_query_by_device_file (udev_client, loop_device);
  if (device == NULL)
    {
      udisks_info ("no udev device for %s", loop_device);
      goto out;
    }
  if (g_udev_device_get_sysfs_attr (device, "loop/offset") == NULL)
    {
      udisks_info ("loop device %s is not setup  (no loop/offset sysfs file)", loop_device);
      goto out;
    }

  /* Check the loop device set up, is the one that _we_ set up
   *
   * Note that drivers/block/loop.c:loop_attr_backing_file_show() uses d_path()
   * on lo_file_name so in the event that the underlying fs was unmounted
   * (just 'umount -l /path/to/fs/holding/backing/file to try) it cuts
   * off the mount path.... in this case we simply just give up managing
   * the loop device
   */
  sysfs_backing_file = g_udev_device_get_sysfs_attr (device, "loop/backing_file");
  if (g_strcmp0 (sysfs_backing_file, backing_file) != 0)
    {
      udisks_notice ("unexpected name for %s - expected `%s' but got `%s'",
                     loop_device, backing_file, sysfs_backing_file);
      goto out;
    }

  /* OK, entry is valid - keep it around */
  keep = TRUE;

 out:

  if (check_only && !keep)
    {
      if (device != NULL)
        {
          dev_t dev_number = g_udev_device_get_device_number (device);
          g_array_append_val (devs_to_clean, dev_number);
        }
      keep = TRUE;
      goto out2;
    }

  if (!keep)
    {
      udisks_notice ("No longer watching loop device %s", loop_device);
    }

 out2:
  g_clear_object (&device);
  if (backing_file_value != NULL)
    g_variant_unref (backing_file_value);
  if (details != NULL)
    g_variant_unref (details);
  return keep;
}

static void
udisks_cleanup_check_loop (UDisksCleanup *cleanup,
                           gboolean       check_only,
                           GArray        *devs_to_clean)
{
  gboolean changed;
  GVariant *value;
  GVariant *new_value;
  GVariantBuilder builder;
  GError *error;

  changed = FALSE;

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "loop",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting loop: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* check valid entries */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          if (udisks_cleanup_check_loop_entry (cleanup, child, check_only, devs_to_clean))
            g_variant_builder_add_value (&builder, child);
          else
            changed = TRUE;
          g_variant_unref (child);
        }
      g_variant_unref (value);
    }

  new_value = g_variant_builder_end (&builder);

  /* save new entries */
  if (changed)
    {
      error = NULL;
      if (!udisks_persistent_store_set (cleanup->persistent_store,
                                        UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                        "loop",
                                        G_VARIANT_TYPE ("a{sa{sv}}"),
                                        new_value, /* consumes new_value */
                                        &error))
        {
          udisks_warning ("Error setting loop: %s (%s, %d)",
                          error->message,
                          g_quark_to_string (error->domain),
                          error->code);
          g_error_free (error);
          goto out;
        }
    }
  else
    {
      g_variant_unref (new_value);
    }

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_cleanup_add_loop:
 * @cleanup: A #UDisksCleanup.
 * @device_file: The loop device file.
 * @backing_file: The backing file.
 * @backing_file_device: The #dev_t of the backing file or 0 if unknown.
 * @uid: The user id of the process requesting the loop device.
 *
 * Adds a new entry to the <filename>/run/udisks2/loop</filename>
 * file.
 */
void
udisks_cleanup_add_loop (UDisksCleanup   *cleanup,
                         const gchar     *device_file,
                         const gchar     *backing_file,
                         dev_t            backing_file_device,
                         uid_t            uid)
{
  GVariant *value;
  GVariant *new_value;
  GVariant *details_value;
  GVariantBuilder builder;
  GVariantBuilder details_builder;
  GError *error;

  g_return_if_fail (UDISKS_IS_CLEANUP (cleanup));
  g_return_if_fail (device_file != NULL);
  g_return_if_fail (backing_file != NULL);

  g_mutex_lock (&cleanup->lock);

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "loop",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting loop: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* start by including existing entries */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          const gchar *entry_loop_device;
          g_variant_get (child, "{&s@a{sv}}", &entry_loop_device, NULL);
          /* Skip/remove stale entries */
          if (g_strcmp0 (entry_loop_device, device_file) == 0)
            {
              udisks_warning ("Removing stale entry for loop device `%s' in /run/udisks2/loop file",
                              entry_loop_device);
            }
          else
            {
              g_variant_builder_add_value (&builder, child);
            }
          g_variant_unref (child);
        }
      g_variant_unref (value);
    }

  /* build the details */
  g_variant_builder_init (&details_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&details_builder,
                         "{sv}",
                         "backing-file",
                         g_variant_new_bytestring (backing_file));
  g_variant_builder_add (&details_builder,
                         "{sv}",
                         "backing-file-device",
                         g_variant_new_uint64 (backing_file_device));
  g_variant_builder_add (&details_builder,
                         "{sv}",
                         "setup-by-uid",
                         g_variant_new_uint32 (uid));
  details_value = g_variant_builder_end (&details_builder);

  /* finally add the new entry */
  g_variant_builder_add (&builder,
                         "{s@a{sv}}",
                         device_file,
                         details_value); /* consumes details_value */
  new_value = g_variant_builder_end (&builder);

  /* save new entries */
  error = NULL;
  if (!udisks_persistent_store_set (cleanup->persistent_store,
                                    UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                    "loop",
                                    G_VARIANT_TYPE ("a{sa{sv}}"),
                                    new_value, /* consumes new_value */
                                    &error))
    {
      udisks_warning ("Error setting loop: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_mutex_unlock (&cleanup->lock);
}

/**
 * udisks_cleanup_has_loop:
 * @cleanup: A #UDisksCleanup
 * @device_file: A loop device file.
 * @out_uid: Return location for the user id who setup the loop device or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Checks if @device_file is set up via udisks.
 *
 * Returns: %TRUE if set up via udisks, otherwise %FALSE or if @error is set.
 */
gboolean
udisks_cleanup_has_loop (UDisksCleanup   *cleanup,
                         const gchar     *device_file,
                         uid_t           *out_uid)
{
  gboolean ret;
  GVariant *value;
  GError *error;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), FALSE);

  g_mutex_lock (&cleanup->lock);

  ret = 0;
  value = NULL;

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "loop",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting loop: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* look through list */
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          const gchar *iter_device_file;
          GVariant *details;

          g_variant_get (child,
                         "{&s@a{sv}}",
                         &iter_device_file,
                         &details);

          if (g_strcmp0 (iter_device_file, device_file) == 0)
            {
              ret = TRUE;
              if (out_uid != NULL)
                {
                  GVariant *lookup_value;
                  lookup_value = lookup_asv (details, "setup-by-uid");
                  *out_uid = 0;
                  if (lookup_value != NULL)
                    {
                      *out_uid = g_variant_get_uint32 (lookup_value);
                      g_variant_unref (lookup_value);
                    }
                }
              g_variant_unref (details);
              g_variant_unref (child);
              goto out;
            }
          g_variant_unref (details);
          g_variant_unref (child);
        }
    }

 out:
  if (value != NULL)
    g_variant_unref (value);
  g_mutex_unlock (&cleanup->lock);
  return ret;
}

