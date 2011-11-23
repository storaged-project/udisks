/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008 David Zeuthen <zeuthen@gmail.com>
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

#include "udiskslogging.h"
#include "udiskspersistentstore.h"
#include "udisksmount.h"
#include "udisksmountmonitor.h"
#include "udisksprivate.h"

/**
 * SECTION:udiskspersistentstore
 * @title: UDisksPersistentStore
 * @short_description: Stores information that persists across reboots
 *
 * Object used to store information that persists across the life
 * cycle of the process.
 *
 * The low-level interface consists of udisks_persistent_store_get()
 * and udisks_persistent_store_set() that can be used to get/set any
 * #GVariant.
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

  gchar *path;
  gchar *temp_path;

  GMutex lock;

  /* key-path -> GVariant */
  GHashTable *cache;
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

  g_hash_table_unref (store->cache);
  g_mutex_clear (&store->lock);
  g_free (store->path);
  g_free (store->temp_path);

  if (G_OBJECT_CLASS (udisks_persistent_store_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_persistent_store_parent_class)->finalize (object);
}

static void
udisks_persistent_store_constructed (GObject *object)
{
  UDisksPersistentStore *store = UDISKS_PERSISTENT_STORE (object);

  g_warn_if_fail (g_file_test (store->path, G_FILE_TEST_IS_DIR));
  g_warn_if_fail (g_file_test (store->temp_path, G_FILE_TEST_IS_DIR));

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
    case PROP_PATH:
      g_assert (store->path == NULL);
      store->path = g_value_dup_string (value);
      break;

    case PROP_TEMP_PATH:
      g_assert (store->temp_path == NULL);
      store->temp_path = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_persistent_store_init (UDisksPersistentStore *store)
{
  g_mutex_init (&store->lock);
  store->cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_variant_unref);
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
   * UDisksPersistentStore:path:
   *
   * The path to store data that will persist across reboots.
   *
   * Data will be stored in this directory - if it does not exist, it
   * will be created with mode 0700.
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
   * Data will be stored in this directory - if it does not exist, it
   * will be created with mode 0700.
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
 * @path: Path to where to store data that will persist across reboots (e.g. <filename>/var/lib/udisks2</filename>).
 * @temp_path: Path to where to store data that will persist only until next reboot (e.g. <filename>/run/udisks2</filename>).
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
udisks_persistent_store_new (const gchar   *path,
                             const gchar   *temp_path)
{
  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (temp_path != NULL, NULL);
  return UDISKS_PERSISTENT_STORE (g_object_new (UDISKS_TYPE_PERSISTENT_STORE,
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
  return store->path;
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
  return store->temp_path;
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

  g_mutex_lock (&store->lock);

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

  /* see if it's already in the cache */
  ret = g_hash_table_lookup (store->cache, path);
  if (ret != NULL)
    {
      g_variant_ref (ret);
      goto out;
    }

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
  g_mutex_unlock (&store->lock);
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

  g_mutex_lock (&store->lock);

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

  g_hash_table_insert (store->cache, g_strdup (path), g_variant_ref (value));

  if (!g_file_set_contents (path,
                            data,
                            size,
                            error))
    goto out;

  ret = TRUE;

 out:
  g_mutex_unlock (&store->lock);

  g_free (path);
  g_free (data);
  g_variant_unref (normalized);
  g_variant_unref (value);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

