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
 * This type is used for cleaning up when devices are removed while
 * still in use. It is implementing by running a separate thread that
 * maintains a set of items to clean up and tries to shrink the set by
 * doing the cleanup work required for each item.
 *
 * The thread itself needs to be kicked when state changes or devices
 * are e.g. removed (using the udisks_cleanup_check() method) from
 * e.g. #UDisksProvider providers.
 *
 * Right now the type only handles mounts made via the <link
 * linkend="gdbus-method-org-freedesktop-UDisks2-Filesystem.Mount">Mount()
 * D-Bus method</link>.
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
static void udisks_cleanup_check_mounted_fs (UDisksCleanup *cleanup);

G_DEFINE_TYPE (UDisksCleanup, udisks_cleanup, G_TYPE_OBJECT);


static void
udisks_cleanup_init (UDisksCleanup *cleanup)
{
  cleanup->lock = g_mutex_new ();
  cleanup->currently_unmounting = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
udisks_cleanup_finalize (GObject *object)
{
  UDisksCleanup *cleanup = UDISKS_CLEANUP (object);

  g_hash_table_unref (cleanup->currently_unmounting);
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
 * Creates a new #UDisksCleanup.
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
  udisks_debug ("Cleanup check");

  g_mutex_lock (cleanup->lock);

  /* mounted-fs */
  udisks_cleanup_check_mounted_fs (cleanup);

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

/*
 * The 'mounted-fs' persistent value has type a{sa{sv}} and is a dict
 * from mount_point (e.g. /media/smallfs) into a set of details. Known
 * details include
 *
 *   block-device:      an uint64 with the dev_t mounted at the current location
 *   mounted-by-uid:    an uint32 with the uid of the user who mounted the device
 */

/* returns TRUE if the entry should be kept */
static gboolean
udisks_cleanup_check_mounted_fs_entry (UDisksCleanup  *cleanup,
                                       GVariant       *value)
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
  gboolean attempt_no_cleanup;
  UDisksMountMonitor *monitor;
  GUdevClient *udev_client;
  GUdevDevice *udev_device;

  keep = FALSE;
  is_mounted = FALSE;
  device_exists = FALSE;
  attempt_no_cleanup = FALSE;
  block_device_value = NULL;
  details = NULL;
  udev_device = NULL;

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
      udisks_info ("mounted-fs entry %s is invalid: no block-device key/value pair", s);
      g_free (s);
      attempt_no_cleanup = FALSE;
      goto out;
    }
  block_device = g_variant_get_uint64 (block_device_value);

  udisks_debug ("Validating mounted-fs entry for mount point %s", mount_point);

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
    device_exists = TRUE;

  /* OK, entry is valid - keep it around */
  if (is_mounted && device_exists)
    keep = TRUE;

 out:

  if (!keep && !attempt_no_cleanup)
    {
      g_assert (g_str_has_prefix (mount_point, "/media"));

      if (!device_exists)
        {
          udisks_notice ("Cleaning up mount point %s since device %d:%d no longer exist",
                         mount_point, major (block_device), minor (block_device));
        }
      else if (!is_mounted)
        {
          udisks_notice ("Cleaning up mount point %s since device %d:%d is no longer mounted",
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
  if (udev_device != NULL)
    g_object_unref (udev_device);
  if (block_device_value != NULL)
    g_variant_unref (block_device_value);
  if (details != NULL)
    g_variant_unref (details);
  return keep;
}

/* called with mutex->lock held */
static void
udisks_cleanup_check_mounted_fs (UDisksCleanup *cleanup)
{
  gboolean changed;
  GVariant *value;
  GVariant *new_value;
  GVariantBuilder builder;
  GError *error;

  udisks_debug ("Checking mounted-fs");

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
          if (udisks_cleanup_check_mounted_fs_entry (cleanup, child))
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
 * High-level function to add an entry to the
 * <literal>mounted-fs</literal>. The entry represents a mount-point
 * automatically created and managed by <command>udisksd</command>.
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
 * Returns an entry for @block_device_file, if it exists.
 *
 * Returns: The mount point for @block_device_file or %NULL if not
 * found or if @error is set.
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
 * Set @mount_point as currently being ignored.
 *
 * This ensures that @mount_point won't get cleaned up by the cleanup
 * routines (this is typically called whenever a filesystem is
 * unmounted).
 *
 * Once unmounting completes (successfully or otherwise),
 * udisks_cleanup_unignore_mounted_fs() should be called with
 * @mount_point.
 *
 * Returns: %TRUE if @mount_point was successfully ignore, %FALSE if it was already ignored.
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
 * Removes a mount point previously added with
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
