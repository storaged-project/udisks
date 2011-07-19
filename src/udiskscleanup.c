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

#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udiskspersistentstore.h"
#include "udisksmount.h"
#include "udisksmountmonitor.h"
#include "udiskslogging.h"
#include "udiskslinuxprovider.h"

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
 *         <entry><filename>/var/lib/udisks2/mounted-fs</filename></entry>
 *         <entry>
 *           A serialized 'a{sa{sv}}' #GVariant mapping from the
 *           mount point (e.g. <filename>/media/EOS_DIGITAL</filename>) into a set of details.
 *           Known details include
 *           <literal>block-device</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-UINT64:CAPS">'t'</link>) that is the #dev_t
 *           for the mounted device and
 *           <literal>mounted-by-uid</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-UINT64:CAPS">'u'</link>) that is the #uid_t
 *           of the user who mounted the device.
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
 *           (of type <link linkend="G-VARIANT-TYPE-UINT64:CAPS">'ay'</link>) that is the device mapper UUID
 *           for the clear-text device and
 *           <literal>unlocked-by-uid</literal>
 *           (of type <link linkend="G-VARIANT-TYPE-UINT64:CAPS">'u'</link>) that is the #uid_t
 *           of the user who unlocked the device.
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

  GMutex *lock;

  UDisksDaemon *daemon;
  UDisksPersistentStore *persistent_store;

  GHashTable *currently_unmounting;
  GHashTable *currently_locking;

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

G_DEFINE_TYPE (UDisksCleanup, udisks_cleanup, G_TYPE_OBJECT);


static void
udisks_cleanup_init (UDisksCleanup *cleanup)
{
  cleanup->lock = g_mutex_new ();
  cleanup->currently_unmounting = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  cleanup->currently_locking = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
}

static void
udisks_cleanup_finalize (GObject *object)
{
  UDisksCleanup *cleanup = UDISKS_CLEANUP (object);

  g_hash_table_unref (cleanup->currently_unmounting);
  g_hash_table_unref (cleanup->currently_locking);
  g_mutex_free (cleanup->lock);

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
  cleanup->thread = g_thread_create (udisks_cleanup_thread_func,
                                     g_object_ref (cleanup),
                                     TRUE, /* joinable */
                                     NULL);
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

  g_mutex_lock (cleanup->lock);

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

  g_array_unref (devs_to_clean);

  udisks_info ("Cleanup check end");

  g_mutex_unlock (cleanup->lock);
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

  keep = FALSE;
  is_mounted = FALSE;
  device_exists = FALSE;
  device_to_be_cleaned = FALSE;
  attempt_no_cleanup = FALSE;
  block_device_value = NULL;
  details = NULL;

  monitor = udisks_daemon_get_mount_monitor (cleanup->daemon);
  udev_client = udisks_linux_provider_get_udev_client (udisks_daemon_get_linux_provider (cleanup->daemon));

  g_variant_get (value,
                 "{&s@a{sv}}",
                 &mount_point,
                 &details);

  /* Don't consider entries being ignored (e.g. in the process of being unmounted) */
  if (g_hash_table_lookup (cleanup->currently_unmounting, mount_point) != NULL)
    {
      keep = TRUE;
      goto out;
    }

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
      device_exists = TRUE;
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
      g_assert (g_str_has_prefix (mount_point, "/media"));

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
          escaped_mount_point = g_strescape (mount_point, NULL);
          /* right now -l is the only way to "force unmount" file systems... */
          if (!udisks_daemon_launch_spawned_job_sync (cleanup->daemon,
                                                      NULL,  /* GCancellable */
                                                      &error_message,
                                                      NULL,  /* input_string */
                                                      "umount -l \"%s\"",
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
        }

      /* remove directory */
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

 out2:
  if (block_device_value != NULL)
    g_variant_unref (block_device_value);
  if (details != NULL)
    g_variant_unref (details);
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
                                       UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
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
                                        UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
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
 * @error: Return location for error or %NULL.
 *
 * Adds a new entry to the
 * <filename>/var/lib/udisks2/mounted-fs</filename> file.
 *
 * Returns: %TRUE if the entry was added, %FALSE if @error is set.
 */
gboolean
udisks_cleanup_add_mounted_fs (UDisksCleanup  *cleanup,
                               const gchar    *mount_point,
                               dev_t           block_device,
                               uid_t           uid,
                               GError        **error)
{
  gboolean ret;
  GVariant *value;
  GVariant *new_value;
  GVariant *details_value;
  GVariantBuilder builder;
  GVariantBuilder details_builder;
  GError *local_error;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), FALSE);
  g_return_val_if_fail (mount_point != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_mutex_lock (cleanup->lock);

  ret = FALSE;

  /* load existing entries */
  local_error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
                                       "mounted-fs",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &local_error);
  if (local_error != NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error getting mounted-fs: %s (%s, %d)",
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
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
          g_variant_builder_add_value (&builder, child);
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
  details_value = g_variant_builder_end (&details_builder);

  /* finally add the new entry */
  g_variant_builder_add (&builder,
                         "{s@a{sv}}",
                         mount_point,
                         details_value); /* consumes details_value */
  new_value = g_variant_builder_end (&builder);

  /* save new entries */
  local_error = NULL;
  if (!udisks_persistent_store_set (cleanup->persistent_store,
                                    UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
                                    "mounted-fs",
                                    G_VARIANT_TYPE ("a{sa{sv}}"),
                                    new_value, /* consumes new_value */
                                    &local_error))
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error setting mounted-fs: %s (%s, %d)",
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
      goto out;
    }

  ret = TRUE;

 out:
  g_mutex_unlock (cleanup->lock);
  return ret;
}

/**
 * udisks_cleanup_remove_mounted_fs:
 * @cleanup: A #UDisksCleanup.
 * @mount_point: The mount point.
 * @error: Return location for error or %NULL.
 *
 * Removes an entry previously added with udisks_cleanup_add_mounted_fs().
 *
 * Returns: %TRUE if the entry was removed, %FALSE if @error is set.
 */
gboolean
udisks_cleanup_remove_mounted_fs (UDisksCleanup   *cleanup,
                                  const gchar     *mount_point,
                                  GError         **error)
{
  gboolean ret;
  GVariant *value;
  GVariant *new_value;
  GVariantBuilder builder;
  GError *local_error;
  gboolean removed;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), FALSE);
  g_return_val_if_fail (mount_point != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_mutex_lock (cleanup->lock);

  ret = FALSE;
  removed = FALSE;

  /* load existing entries */
  local_error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
                                       "mounted-fs",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &local_error);
  if (local_error != NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error getting mounted-fs: %s (%s, %d)",
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
      goto out;
    }

  /* start by including existing entries except for the one we want to remove */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;

      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          const gchar *iter_mount_point;
          g_variant_get (child, "{s@a{sv}}", &iter_mount_point, NULL);
          if (g_strcmp0 (iter_mount_point, mount_point) == 0)
            {
              removed = TRUE;
            }
          else
            {
              g_variant_builder_add_value (&builder, child);
            }
          g_variant_unref (child);
        }
      g_variant_unref (value);
    }
  new_value = g_variant_builder_end (&builder);
  if (removed)
    {
      /* save new entries */
      local_error = NULL;
      if (!udisks_persistent_store_set (cleanup->persistent_store,
                                        UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
                                        "mounted-fs",
                                        G_VARIANT_TYPE ("a{sa{sv}}"),
                                        new_value, /* consumes new_value */
                                        &local_error))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Error setting mounted-fs: %s (%s, %d)",
                       local_error->message,
                       g_quark_to_string (local_error->domain),
                       local_error->code);
          g_error_free (local_error);
          goto out;
        }
      ret = TRUE;
    }
  else
    {
      g_variant_unref (new_value);
    }

 out:
  g_mutex_unlock (cleanup->lock);
  return ret;
}

/**
 * udisks_cleanup_find_mounted_fs:
 * @cleanup: A #UDisksCleanup.
 * @block_device: The block device.
 * @out_uid: Return location for the user id who mounted the device or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Gets the mount point for @block_device, if it exists in the
 * <filename>/var/lib/udisks2/mounted-fs</filename> file.
 *
 * Returns: The mount point for @block_device or %NULL if not found or
 * if @error is set.
 */
gchar *
udisks_cleanup_find_mounted_fs (UDisksCleanup   *cleanup,
                                dev_t            block_device,
                                uid_t           *out_uid,
                                GError         **error)
{
  gchar *ret;
  GVariant *value;
  GError *local_error;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  g_mutex_lock (cleanup->lock);

  ret = NULL;
  value = NULL;

  /* load existing entries */
  local_error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
                                       "mounted-fs",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &local_error);
  if (local_error != NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error getting mounted-fs: %s (%s, %d)",
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
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
                      GVariant *value;
                      value = lookup_asv (details, "mounted-by-uid");
                      *out_uid = 0;
                      if (value != NULL)
                        {
                          *out_uid = g_variant_get_uint32 (value);
                          g_variant_unref (value);
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
  g_mutex_unlock (cleanup->lock);
  return ret;
}

/**
 * udisks_cleanup_ignore_mounted_fs:
 * @cleanup: A #UDisksCleanup.
 * @mount_point: A mount point.
 *
 * Set @mount_point as currently being ignored. This ensures that the
 * entry for @mount_point won't get cleaned up by the cleanup routines
 * until udisks_cleanup_unignore_mounted_fs() is called.
 *
 * Returns: %TRUE if @mount_point was successfully ignored, %FALSE if
 * it was already ignored.
 */
gboolean
udisks_cleanup_ignore_mounted_fs (UDisksCleanup  *cleanup,
                                  const gchar    *mount_point)
{
  gboolean ret;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), FALSE);
  g_return_val_if_fail (mount_point != NULL, FALSE);

  g_mutex_lock (cleanup->lock);

  ret = FALSE;

  if (g_hash_table_lookup (cleanup->currently_unmounting, mount_point) != NULL)
    goto out;

  g_hash_table_insert (cleanup->currently_unmounting, g_strdup (mount_point), (gpointer) mount_point);

  ret = TRUE;

 out:
  g_mutex_unlock (cleanup->lock);
  return ret;
}

/**
 * udisks_cleanup_unignore_mounted_fs:
 * @cleanup: A #UDisksCleanup.
 * @mount_point: A mount point.
 *
 * Stops ignoring a mount point previously ignored using
 * udisks_cleanup_ignore_mounted_fs().
 */
void
udisks_cleanup_unignore_mounted_fs (UDisksCleanup  *cleanup,
                                    const gchar    *mount_point)
{
  g_return_if_fail (UDISKS_IS_CLEANUP (cleanup));
  g_return_if_fail (mount_point != NULL);

  g_mutex_lock (cleanup->lock);
  g_warn_if_fail (g_hash_table_remove (cleanup->currently_unmounting, mount_point));
  g_mutex_unlock (cleanup->lock);
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

  /* Don't consider entries being ignored (e.g. in the process of being locked) */
  if (g_hash_table_lookup (cleanup->currently_locking, &cleartext_device) != NULL)
    {
      keep = TRUE;
      goto out;
    }

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
          escaped_device_file = g_strescape (device_file_cleartext, NULL);
          /* right now -l is the only way to "force unmount" file systems... */
          if (!udisks_daemon_launch_spawned_job_sync (cleanup->daemon,
                                                      NULL,  /* GCancellable */
                                                      &error_message,
                                                      NULL,  /* input_string */
                                                      "cryptsetup luksClose \"%s\"",
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
 * @error: Return location for error or %NULL.
 *
 * Adds a new entry to the
 * <filename>/run/udisks2/unlocked-luks</filename> file.
 *
 * Returns: %TRUE if the entry was added, %FALSE if @error is set.
 */
gboolean
udisks_cleanup_add_unlocked_luks (UDisksCleanup  *cleanup,
                                  dev_t           cleartext_device,
                                  dev_t           crypto_device,
                                  const gchar    *dm_uuid,
                                  uid_t           uid,
                                  GError        **error)
{
  gboolean ret;
  GVariant *value;
  GVariant *new_value;
  GVariant *details_value;
  GVariantBuilder builder;
  GVariantBuilder details_builder;
  GError *local_error;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), FALSE);
  g_return_val_if_fail (dm_uuid != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_mutex_lock (cleanup->lock);

  ret = FALSE;

  /* load existing entries */
  local_error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "unlocked-luks",
                                       G_VARIANT_TYPE ("a{ta{sv}}"),
                                       &local_error);
  if (local_error != NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error getting unlocked-luks: %s (%s, %d)",
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
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
          g_variant_builder_add_value (&builder, child);
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
  local_error = NULL;
  if (!udisks_persistent_store_set (cleanup->persistent_store,
                                    UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                    "unlocked-luks",
                                    G_VARIANT_TYPE ("a{ta{sv}}"),
                                    new_value, /* consumes new_value */
                                    &local_error))
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error setting unlocked-luks: %s (%s, %d)",
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
      goto out;
    }

  ret = TRUE;

 out:
  g_mutex_unlock (cleanup->lock);
  return ret;
}

/**
 * udisks_cleanup_remove_unlocked_luks:
 * @cleanup: A #UDisksCleanup.
 * @cleartext_device: The mount point.
 * @error: Return location for error or %NULL.
 *
 * Removes an entry previously added with udisks_cleanup_add_unlocked_luks().
 *
 * Returns: %TRUE if the entry was removed, %FALSE if @error is set.
 */
gboolean
udisks_cleanup_remove_unlocked_luks (UDisksCleanup   *cleanup,
                                     dev_t            cleartext_device,
                                     GError         **error)
{
  gboolean ret;
  GVariant *value;
  GVariant *new_value;
  GVariantBuilder builder;
  GError *local_error;
  gboolean removed;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_mutex_lock (cleanup->lock);

  ret = FALSE;
  removed = FALSE;

  /* load existing entries */
  local_error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "unlocked-luks",
                                       G_VARIANT_TYPE ("a{ta{sv}}"),
                                       &local_error);
  if (local_error != NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error getting unlocked-luks: %s (%s, %d)",
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
      goto out;
    }

  /* start by including existing entries except for the one we want to remove */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ta{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;

      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          dev_t iter_cleartext_device;
          g_variant_get (child, "{t@a{sv}}", &iter_cleartext_device, NULL);
          if (iter_cleartext_device == cleartext_device)
            {
              removed = TRUE;
            }
          else
            {
              g_variant_builder_add_value (&builder, child);
            }
          g_variant_unref (child);
        }
      g_variant_unref (value);
    }
  new_value = g_variant_builder_end (&builder);
  if (removed)
    {
      /* save new entries */
      local_error = NULL;
      if (!udisks_persistent_store_set (cleanup->persistent_store,
                                        UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                        "unlocked-luks",
                                        G_VARIANT_TYPE ("a{ta{sv}}"),
                                        new_value, /* consumes new_value */
                                        &local_error))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Error setting unlocked-luks: %s (%s, %d)",
                       local_error->message,
                       g_quark_to_string (local_error->domain),
                       local_error->code);
          g_error_free (local_error);
          goto out;
        }
      ret = TRUE;
    }
  else
    {
      g_variant_unref (new_value);
    }

 out:
  g_mutex_unlock (cleanup->lock);
  return ret;
}

/**
 * udisks_cleanup_find_unlocked_luks:
 * @cleanup: A #UDisksCleanup.
 * @crypto_device: The block device.
 * @out_uid: Return location for the user id who mounted the device or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Gets the clear-text device for @crypto_device, if it exists in the
 * <filename>/run/udisks2/unlocked-luks</filename> file.
 *
 * Returns: The cleartext device for @crypto_device or 0 if not
 * found or if @error is set.
 */
dev_t
udisks_cleanup_find_unlocked_luks (UDisksCleanup   *cleanup,
                                   dev_t            crypto_device,
                                   uid_t           *out_uid,
                                   GError         **error)
{
  dev_t ret;
  GVariant *value;
  GError *local_error;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), 0);
  g_return_val_if_fail (error == NULL || *error == NULL, 0);

  g_mutex_lock (cleanup->lock);

  ret = 0;
  value = NULL;

  /* load existing entries */
  local_error = NULL;
  value = udisks_persistent_store_get (cleanup->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE,
                                       "unlocked-luks",
                                       G_VARIANT_TYPE ("a{ta{sv}}"),
                                       &local_error);
  if (local_error != NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error getting unlocked-luks: %s (%s, %d)",
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
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
                      GVariant *value;
                      value = lookup_asv (details, "unlocked-by-uid");
                      *out_uid = 0;
                      if (value != NULL)
                        {
                          *out_uid = g_variant_get_uint32 (value);
                          g_variant_unref (value);
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
  g_mutex_unlock (cleanup->lock);
  return ret;
}

/**
 * udisks_cleanup_ignore_unlocked_luks:
 * @cleanup: A #UDisksCleanup.
 * @cleartext_device: A cleartext device.
 *
 * Set @cleartext_device as currently being ignored. This ensures that
 * the entry for @cleartext_device won't get cleaned up by the cleanup
 * routines until udisks_cleanup_unignore_unlocked_luks() is called.
 *
 * Returns: %TRUE if @cleartext_device was successfully ignored,
 * %FALSE if it was already ignored.
 */
gboolean
udisks_cleanup_ignore_unlocked_luks (UDisksCleanup  *cleanup,
                                     dev_t           cleartext_device)
{
  gboolean ret;
  guint64 v;
  guint64 *cv;

  g_return_val_if_fail (UDISKS_IS_CLEANUP (cleanup), FALSE);

  g_mutex_lock (cleanup->lock);

  ret = FALSE;

  v = cleartext_device;
  if (g_hash_table_lookup (cleanup->currently_locking, &v) != NULL)
    goto out;

  cv = g_memdup (&v, sizeof (guint64));
  g_hash_table_insert (cleanup->currently_locking, cv, cv);

  ret = TRUE;

 out:
  g_mutex_unlock (cleanup->lock);
  return ret;
}

/**
 * udisks_cleanup_unignore_unlocked_luks:
 * @cleanup: A #UDisksCleanup.
 * @cleartext_device: A cleartext device.
 *
 * Stops ignoring a cleartext device previously ignored using
 * udisks_cleanup_ignore_unlocked_luks().
 */
void
udisks_cleanup_unignore_unlocked_luks (UDisksCleanup  *cleanup,
                                       dev_t           cleartext_device)
{
  guint64 v;

  g_return_if_fail (UDISKS_IS_CLEANUP (cleanup));

  g_mutex_lock (cleanup->lock);
  v = cleartext_device;
  g_warn_if_fail (g_hash_table_remove (cleanup->currently_locking, &v));
  g_mutex_unlock (cleanup->lock);
}

/* ---------------------------------------------------------------------------------------------------- */
