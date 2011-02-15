/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mntent.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>

#include "udiskspersistentstore.h"
#include "udisksdaemon.h"
#include "udisksmount.h"
#include "udisksmountmonitor.h"
#include "udisksprivate.h"

/**
 * SECTION:udiskspersistentstore
 * @title: UDisksPersistentStore
 * @short_description: Stores information that persists across reboots
 *
 * Object used to store information that persists across the life
 * cycle of the udisks daemon.
 *
 * The low-level interface consists of udisks_persistent_store_get()
 * and udisks_persistent_store_set() that can be used to get/set any
 * #GVariant.
 *
 * There are also higher-level interfaces such as the
 * <literal>udisks_persistent_store_mounted_fs_*()</literal> family of
 * functions that are used to manage mount points in
 * <filename>/media</filename> used when mounting/unmounting
 * filesystems via the #UDisksFilesystem interface.
 */

/**
 * UDisksPersistentStore:
 *
 * The #UDisksPersistentStore structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksPersistentStore
{
  GObject parent_instance;

  UDisksDaemon *daemon;

  gchar *given_path;
  gchar *given_temp_path;

  gchar *path;
  gchar *temp_path;

  GHashTable *currently_unmounting;
};

typedef struct _UDisksPersistentStoreClass UDisksPersistentStoreClass;

struct _UDisksPersistentStoreClass
{
  GObjectClass parent_class;
};

/*--------------------------------------------------------------------------------------------------------------*/

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_PATH,
  PROP_TEMP_PATH
};

enum
{
  LAST_SIGNAL,
};

/*static guint signals[LAST_SIGNAL] = { 0 };*/

/*--------------------------------------------------------------------------------------------------------------*/

G_DEFINE_TYPE (UDisksPersistentStore, udisks_persistent_store, G_TYPE_OBJECT)

static void
udisks_persistent_store_finalize (GObject *object)
{
  UDisksPersistentStore *store = UDISKS_PERSISTENT_STORE (object);

  g_free (store->given_path);
  g_free (store->given_temp_path);
  g_free (store->path);
  g_free (store->temp_path);
  g_hash_table_unref (store->currently_unmounting);

  if (G_OBJECT_CLASS (udisks_persistent_store_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_persistent_store_parent_class)->finalize (object);
}

static void
udisks_persistent_store_constructed (GObject *object)
{
  UDisksPersistentStore *store = UDISKS_PERSISTENT_STORE (object);

  g_warn_if_fail (g_file_test (store->given_path, G_FILE_TEST_IS_DIR));
  store->path = g_strdup_printf ("%s/udisks-persistence-2.0", store->given_path);
  if (!g_file_test (store->path, G_FILE_TEST_IS_DIR))
    {
      if (g_mkdir (store->path, 0700) != 0)
        {
          g_warning ("Error creating %s: %m", store->path);
        }
    }

  g_warn_if_fail (g_file_test (store->given_temp_path, G_FILE_TEST_IS_DIR));
  store->temp_path = g_strdup_printf ("%s/udisks-persistence-2.0", store->given_temp_path);
  if (!g_file_test (store->temp_path, G_FILE_TEST_IS_DIR))
    {
      if (g_mkdir (store->temp_path, 0700) != 0)
        {
          g_warning ("Error creating %s: %m", store->temp_path);
        }
    }

  if (G_OBJECT_CLASS (udisks_persistent_store_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_persistent_store_parent_class)->constructed (object);
}

static void
udisks_persistent_store_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  UDisksPersistentStore *store = UDISKS_PERSISTENT_STORE (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_persistent_store_get_daemon (store));
      break;

    case PROP_PATH:
      g_value_set_string (value, udisks_persistent_store_get_path (store));
      break;

    case PROP_TEMP_PATH:
      g_value_set_string (value, udisks_persistent_store_get_temp_path (store));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_persistent_store_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  UDisksPersistentStore *store = UDISKS_PERSISTENT_STORE (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (store->daemon == NULL);
      /* we don't take a reference to the daemon */
      store->daemon = g_value_get_object (value);
      break;

    case PROP_PATH:
      g_assert (store->given_path == NULL);
      store->given_path = g_value_dup_string (value);
      break;

    case PROP_TEMP_PATH:
      g_assert (store->given_temp_path == NULL);
      store->given_temp_path = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_persistent_store_init (UDisksPersistentStore *store)
{
  store->currently_unmounting = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
udisks_persistent_store_class_init (UDisksPersistentStoreClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_persistent_store_finalize;
  gobject_class->constructed  = udisks_persistent_store_constructed;
  gobject_class->get_property = udisks_persistent_store_get_property;
  gobject_class->set_property = udisks_persistent_store_set_property;

  /**
   * UDisksProvider:daemon:
   *
   * The #UDisksDaemon the store is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the store is for",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksPersistentStore:path:
   *
   * The path to store data that will persist across reboots.
   *
   * Data will be stored in a sub-directory of this directory called
   * <filename>udisks-persistence-2.0</filename>. If this directory
   * does not exist, it will be created with mode 0700.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_PATH,
                                   g_param_spec_string ("path",
                                                        "Path",
                                                        "The path to store data at",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksPersistentStore:temp-path:
   *
   * The path to store data that will not persist across reboots.
   *
   * Data will be stored in a sub-directory of this directory called
   * <filename>udisks-persistence-2.0</filename>. If this directory
   * does not exist, it will be created with mode 0700.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_TEMP_PATH,
                                   g_param_spec_string ("temp-path",
                                                        "Temporary Path",
                                                        "The path to store temporary data at",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_persistent_store_new:
 * @daemon: A #UDisksDaemon the store is for.
 * @path: Path to where to store data that will persist across reboots (e.g. <filename>/var/lib/udisks2</filename>).
 * @temp_path: Path to where to store data that will persist only until next reboot (e.g. <filename>/dev/.udisks2</filename>).
 *
 * Creates a new #UDisksPersistentStore object.
 *
 * Data will be stored in a sub-directory of @path and @temp_path
 * called <filename>udisks-persistence-2.0</filename>. See the
 * #UDisksPersistentStore:path and #UDisksPersistentStore:temp-path
 * properties for more information.
 *
 * Returns: A #UDisksPersistentStore. Free with g_object_unref().
 */
UDisksPersistentStore *
udisks_persistent_store_new (UDisksDaemon  *daemon,
                             const gchar   *path,
                             const gchar   *temp_path)
{
  /* only allow NULL for the tests to work */
  g_return_val_if_fail (daemon == NULL || UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (temp_path != NULL, NULL);
  return UDISKS_PERSISTENT_STORE (g_object_new (UDISKS_TYPE_PERSISTENT_STORE,
                                                "daemon", daemon,
                                                "path", path,
                                                "temp-path", temp_path,
                                                NULL));
}

/**
 * udisks_persistent_store_get_path:
 * @store: A #UDisksPersistentStore.
 *
 * Gets the path that @store stores its data at.
 *
 * Returns: A string owned by @store. Do not free.
 */
const gchar *
udisks_persistent_store_get_path (UDisksPersistentStore *store)
{
  g_return_val_if_fail (UDISKS_IS_PERSISTENT_STORE (store), NULL);
  return store->given_path;
}

/**
 * udisks_persistent_store_get_temp_path:
 * @store: A #UDisksPersistentStore.
 *
 * Gets the that @store stores its temporary data at.
 *
 * Returns: A string owned by @store. Do not free.
 */
const gchar *
udisks_persistent_store_get_temp_path (UDisksPersistentStore *store)
{
  g_return_val_if_fail (UDISKS_IS_PERSISTENT_STORE (store), NULL);
  return store->given_temp_path;
}

/**
 * udisks_persistent_store_get_daemon:
 * @store: A #UDisksPersistentStore.
 *
 * Gets the daemon used by @store.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @store.
 */
UDisksDaemon *
udisks_persistent_store_get_daemon (UDisksPersistentStore   *store)
{
  g_return_val_if_fail (UDISKS_IS_PERSISTENT_STORE (store), NULL);
  return store->daemon;
}

/**
 * udisks_persistent_store_get:
 * @store: A #UDisksPersistentStore.
 * @flags: Zero or more flags from the #UDisksPersistentFlags enumeration.
 * @key: The key to get the value for. Must be ASCII and not contain the '/' character.
 * @type: A definite #GVariantType.
 * @error: Return location for error or %NULL.
 *
 * Low-level function to look up the value for @key, if any.
 *
 * Returns: The value or %NULL if not found or if @error is set. The
 * returned #GVariant must be freed with g_variant_unref().
 */
GVariant *
udisks_persistent_store_get (UDisksPersistentStore   *store,
                             UDisksPersistentFlags    flags,
                             const gchar             *key,
                             const GVariantType      *type,
                             GError                 **error)
{
  gchar *path;
  GVariant *ret;
  gchar *contents;
  gsize length;
  GError *local_error;

  g_return_val_if_fail (UDISKS_IS_PERSISTENT_STORE (store), NULL);
  g_return_val_if_fail (key != NULL, NULL);
  g_return_val_if_fail (g_variant_type_is_definite (type), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  path = NULL;
  contents = NULL;

  /* TODO:
   *
   * - could use a cache here to avoid loading files all the time
   * - could also mmap the file
   */

  if (flags & UDISKS_PERSISTENT_FLAGS_NORMAL_STORE)
    path = g_strdup_printf ("%s/%s", store->path, key);
  else if (flags & UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE)
    path = g_strdup_printf ("%s/%s", store->temp_path, key);
  else
    g_assert_not_reached ();

  local_error = NULL;
  if (!g_file_get_contents (path,
                            &contents,
                            &length,
                            &local_error))
    {
      if (local_error->domain == G_FILE_ERROR && local_error->code == G_FILE_ERROR_NOENT)
        {
          /* this is not an error */
          g_error_free (local_error);
          goto out;
        }
      g_propagate_error (error, local_error);
      goto out;
    }

  ret = g_variant_new_from_data (type,
                                 (gconstpointer) contents,
                                 length,
                                 FALSE,
                                 g_free,
                                 contents);
  g_variant_ref_sink (ret);

  contents = NULL; /* ownership transfered to the returned GVariant */

 out:
  g_free (contents);
  g_free (path);

  return ret;
}

/**
 * udisks_persistent_store_set:
 * @store: A #UDisksPersistentStore.
 * @flags: Zero or more flags from the #UDisksPersistentFlags enumeration.
 * @key: The key to get the value for. Must be ASCII and not contain the '/' character.
 * @type: A definite #GVariantType.
 * @value: The value to set.
 * @error: Return location for error or %NULL.
 *
 * Low-level function that sets the value for @key to @value.
 *
 * If @value is floating it is consumed.
 *
 * Returns: %TRUE if setting the value succeeded, %FALSE if @error is set.
 */
gboolean
udisks_persistent_store_set (UDisksPersistentStore   *store,
                             UDisksPersistentFlags    flags,
                             const gchar             *key,
                             const GVariantType      *type,
                             GVariant                *value,
                             GError                 **error)
{
  gboolean ret;
  gsize size;
  gchar *path;
  gchar *data;
  GVariant *normalized;

  g_return_val_if_fail (UDISKS_IS_PERSISTENT_STORE (store), FALSE);
  g_return_val_if_fail (key != NULL, FALSE);
  g_return_val_if_fail (g_variant_type_is_definite (type), FALSE);
  g_return_val_if_fail (g_variant_is_of_type (value, type), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;

  g_variant_ref_sink (value);
  normalized = g_variant_get_normal_form (value);
  size = g_variant_get_size (normalized);
  data = g_malloc (size);
  g_variant_store (normalized, data);

  if (flags & UDISKS_PERSISTENT_FLAGS_NORMAL_STORE)
    path = g_strdup_printf ("%s/%s", store->path, key);
  else if (flags & UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE)
    path = g_strdup_printf ("%s/%s", store->temp_path, key);
  else
    g_assert_not_reached ();

  if (!g_file_set_contents (path,
                            data,
                            size,
                            error))
    goto out;

  ret = TRUE;

 out:
  g_free (path);
  g_free (data);
  g_variant_unref (normalized);
  g_variant_unref (value);
  return ret;
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
 *   block-device-file: a byte-string with the block device that is mounted at the given location
 *   mounted-by-uid:    an uint32 with the uid of the user who mounted the device
 */

static gboolean
mounted_fs_entry_is_valid (UDisksPersistentStore   *store,
                           GVariant                *value)
{
  const gchar *mount_point;
  GVariant *details = NULL;
  GVariant *block_device_file_value = NULL;
  const gchar *block_device_file;
  gboolean ret;
  gchar *s;
  struct stat statbuf;
  UDisksMountMonitor *mount_monitor;
  GList *mounts;
  GList *l;
  gboolean found_mount;

  //udisks_daemon_log (store->daemon, UDISKS_LOG_LEVEL_DEBUG, "TODO: validate %s", g_variant_print (value, TRUE));

  ret = FALSE;
  g_variant_get (value,
                 "{&s@a{sv}}",
                 &mount_point,
                 &details);

  /* Don't consider entries being unmounted right now */
  if (g_hash_table_lookup (store->currently_unmounting, mount_point) != NULL)
    {
      ret = TRUE;
      goto out;
    }

  block_device_file_value = lookup_asv (details, "block-device-file");
  if (block_device_file_value == NULL)
    {
      s = g_variant_print (value, TRUE);
      udisks_daemon_log (store->daemon, UDISKS_LOG_LEVEL_ERROR,
                         "mounted-fs entry %s is invalid: no block-device-file key/value pair",
                         s);
      g_free (s);
      goto out;
    }

  block_device_file = g_variant_get_bytestring (block_device_file_value);
  if (stat (block_device_file, &statbuf) != 0)
    {
      s = g_variant_print (value, TRUE);
      udisks_daemon_log (store->daemon, UDISKS_LOG_LEVEL_ERROR,
                         "mounted-fs entry %s is invalid: error statting block-device-file %s: %m",
                         s,
                         block_device_file);
      g_free (s);
      goto out;
    }
  if (!S_ISBLK (statbuf.st_mode))
    {
      s = g_variant_print (value, TRUE);
      udisks_daemon_log (store->daemon, UDISKS_LOG_LEVEL_ERROR,
                         "mounted-fs entry %s is invalid: block-device-file %s is not a block device",
                         s,
                         block_device_file);
      g_free (s);
      goto out;
    }

  found_mount = FALSE;
  mount_monitor = udisks_daemon_get_mount_monitor (store->daemon);
  mounts = udisks_mount_monitor_get_mounts_for_dev (mount_monitor, statbuf.st_rdev);
  for (l = mounts; l != NULL; l = l->next)
    {
      UDisksMount *mount = UDISKS_MOUNT (l->data);
      if (g_strcmp0 (udisks_mount_get_mount_path (mount), mount_point) == 0)
        {
          found_mount = TRUE;
          break;
        }
    }
  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);

  if (!found_mount)
    {
      s = g_variant_print (value, TRUE);
      udisks_daemon_log (store->daemon, UDISKS_LOG_LEVEL_ERROR,
                         "mounted-fs entry %s is invalid: block-device-file %s is not mounted at %s",
                         s,
                         block_device_file,
                         mount_point);
      g_free (s);
      goto out;
    }

  /* OK, entry is valid */
  ret = TRUE;

 out:
  if (block_device_file_value != NULL)
    g_variant_unref (block_device_file_value);
  if (details != NULL)
    g_variant_unref (details);

  /* clean up mount point if entry was invalid */
  if (!ret)
    {
      g_assert (g_str_has_prefix (mount_point, "/media"));
      /* but only if the directory actually exists (user might have manually cleaned it up etc.) */
      if (g_file_test (mount_point, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
        {
          if (g_rmdir (mount_point) != 0)
            {
              udisks_daemon_log (store->daemon, UDISKS_LOG_LEVEL_ERROR,
                                 "Error removing directory %s: %m",
                                 mount_point);
            }
          else
            {
              udisks_daemon_log (store->daemon, UDISKS_LOG_LEVEL_INFO,
                                 "Cleaned up mount point %s",
                                 mount_point);
            }
        }
    }
  return ret;
}

/**
 * udisks_persistent_store_mounted_fs_cleanup:
 * @store: A #UDisksPersistentStore.
 *
 * Cleans up stale entries and mount points.
 */
void
udisks_persistent_store_mounted_fs_cleanup (UDisksPersistentStore *store)
{
  gboolean changed;
  GVariant *value;
  GVariant *new_value;
  GVariantBuilder builder;
  GError *error;

  udisks_daemon_log (store->daemon,
                     UDISKS_LOG_LEVEL_DEBUG,
                     "Cleaning up stale entries and mount points from the mounted-fs file");

  changed = FALSE;

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (store,
                                       UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
                                       "mounted-fs",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &error);
  if (error != NULL)
    {
      g_warning ("%s: Error getting mounted-fs: %s (%s, %d)",
                 G_STRFUNC,
                 error->message,
                 g_quark_to_string (error->domain),
                 error->code);
      g_error_free (error);
      goto out;
    }

  /* include valid entries */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          if (mounted_fs_entry_is_valid (store, child))
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
      if (!udisks_persistent_store_set (store,
                                        UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
                                        "mounted-fs",
                                        G_VARIANT_TYPE ("a{sa{sv}}"),
                                        new_value, /* consumes new_value */
                                        &error))
        {
          g_warning ("%s: Error setting mounted-fs: %s (%s, %d)",
                     G_STRFUNC,
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

/**
 * udisks_persistent_store_mounted_fs_add:
 * @store: A #UDisksPersistentStore.
 * @block_device_file: The block device.
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
udisks_persistent_store_mounted_fs_add (UDisksPersistentStore   *store,
                                        const gchar             *block_device_file,
                                        const gchar             *mount_point,
                                        uid_t                    uid,
                                        GError                 **error)
{
  gboolean ret;
  GVariant *value;
  GVariant *new_value;
  GVariant *details_value;
  GVariantBuilder builder;
  GVariantBuilder details_builder;
  GError *local_error;

  g_return_val_if_fail (UDISKS_IS_PERSISTENT_STORE (store), FALSE);
  g_return_val_if_fail (block_device_file != NULL, FALSE);
  g_return_val_if_fail (mount_point != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;

  /* load existing entries */
  local_error = NULL;
  value = udisks_persistent_store_get (store,
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
                         "block-device-file",
                         g_variant_new_bytestring (block_device_file));
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
  if (!udisks_persistent_store_set (store,
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
  return ret;
}

/**
 * udisks_persistent_store_mounted_fs_remove:
 * @store: A #UDisksPersistentStore.
 * @mount_point: The mount point.
 * @error: Return location for error or %NULL.
 *
 * Removes an entry previously added with udisks_persistent_store_mounted_fs_add().
 *
 * Returns: %TRUE if the entry was removed, %FALSE if @error is set.
 */
gboolean
udisks_persistent_store_mounted_fs_remove (UDisksPersistentStore   *store,
                                           const gchar             *mount_point,
                                           GError                 **error)
{
  gboolean ret;
  GVariant *value;
  GVariant *new_value;
  GVariantBuilder builder;
  GError *local_error;
  gboolean removed;

  g_return_val_if_fail (UDISKS_IS_PERSISTENT_STORE (store), FALSE);
  g_return_val_if_fail (mount_point != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;
  removed = FALSE;

  /* load existing entries */
  local_error = NULL;
  value = udisks_persistent_store_get (store,
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
      if (!udisks_persistent_store_set (store,
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
  return ret;
}

/**
 * udisks_persistent_store_mounted_fs_find:
 * @store: A #UDisksPersistentStore.
 * @block_device_file: The block device.
 * @out_uid: Return location for the user id who mounted the device or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Returns an entry for @block_device_file, if it exists.
 *
 * Returns: The mount point for @block_device_file or %NULL if not
 * found or if @error is set.
 */
gchar *
udisks_persistent_store_mounted_fs_find (UDisksPersistentStore   *store,
                                         const gchar             *block_device_file,
                                         uid_t                   *out_uid,
                                         GError                 **error)
{
  gchar *ret;
  GVariant *value;
  GError *local_error;

  g_return_val_if_fail (UDISKS_IS_PERSISTENT_STORE (store), NULL);
  g_return_val_if_fail (block_device_file != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  value = NULL;

  /* load existing entries */
  local_error = NULL;
  value = udisks_persistent_store_get (store,
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
          GVariant *block_device_file_value;

          g_variant_get (child,
                         "{&s@a{sv}}",
                         &mount_point,
                         &details);

          block_device_file_value = lookup_asv (details, "block-device-file");
          if (block_device_file_value != NULL)
            {
              const gchar *iter_block_device_file;
              iter_block_device_file = g_variant_get_bytestring (block_device_file_value);
              if (g_strcmp0 (iter_block_device_file, block_device_file) == 0)
                {
                  ret = g_strdup (mount_point);
                  g_variant_unref (block_device_file_value);
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
                  g_variant_unref (block_device_file_value);
                  g_variant_unref (details);
                  g_variant_unref (child);
                  goto out;
                }
              g_variant_unref (block_device_file_value);
            }
          g_variant_unref (details);
          g_variant_unref (child);
        }
    }

 out:
  if (value != NULL)
    g_variant_unref (value);
  return ret;
}

/**
 * udisks_persistent_store_mounted_fs_currently_unmounting_add:
 * @store: A #UDisksPersistentStore.
 * @mount_point: A mount point.
 *
 * Set @mount_point as currently being unmounted.
 *
 * This ensures that @mount_point won't get cleaned up when
 * udisks_persistent_store_mounted_fs_cleanup() is called (this is
 * typically called whenever a filesystem is unmounted).
 *
 * Once unmounting completes (successfully or otherwise),
 * udisks_persistent_store_mounted_fs_currently_unmounting_remove()
 * should be called with @mount_point.
 *
 * Returns: %TRUE if @mount_point was added, %FALSE if it was already added.
 */
gboolean
udisks_persistent_store_mounted_fs_currently_unmounting_add (UDisksPersistentStore   *store,
                                                             const gchar             *mount_point)
{
  gboolean ret;

  g_return_val_if_fail (UDISKS_IS_PERSISTENT_STORE (store), FALSE);
  g_return_val_if_fail (mount_point != NULL, FALSE);

  ret = FALSE;

  if (g_hash_table_lookup (store->currently_unmounting, mount_point) != NULL)
    goto out;

  g_hash_table_insert (store->currently_unmounting, g_strdup (mount_point), (gpointer) mount_point);

  ret = TRUE;

 out:
  return ret;
}

/**
 * udisks_persistent_store_mounted_fs_currently_unmounting_remove:
 * @store: A #UDisksPersistentStore.
 * @mount_point: A mount point.
 *
 * Removes a mount point previously added with
 * udisks_persistent_store_mounted_fs_currently_unmounting_add().
 */
void
udisks_persistent_store_mounted_fs_currently_unmounting_remove (UDisksPersistentStore   *store,
                                                                const gchar             *mount_point)
{
  g_return_if_fail (UDISKS_IS_PERSISTENT_STORE (store));
  g_return_if_fail (mount_point != NULL);

  g_warn_if_fail (g_hash_table_remove (store->currently_unmounting, mount_point));
}

/* ---------------------------------------------------------------------------------------------------- */
