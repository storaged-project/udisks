/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
 * Copyright (C) 2020 Tomas Bzatek <tbzatek@redhat.com>
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

#include <glib/gstdio.h>

#include <libmount/libmount.h>

#include "udiskslogging.h"
#include "udiskslinuxmountoptions.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udisksstate.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxdevice.h"
#include "udisksconfigmanager.h"
#include "udisks-daemon-resources.h"


/* ---------------------------------------------------------------------------------------------------- */

static GHashTable * mount_options_parse_config_file (const gchar *filename, GError **error);
static GHashTable * mount_options_get_from_udev (UDisksLinuxDevice *device, GError **error);

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  gchar **defaults;
  gchar **allow;
  gchar **drivers;
} FSMountOptions;

static void
free_fs_mount_options (FSMountOptions *options)
{
  if (options)
    {
      g_strfreev (options->defaults);
      g_strfreev (options->allow);
      g_strfreev (options->drivers);
      g_free (options);
    }
}

static void
strv_append_unique (gchar **src, gchar ***dest)
{
  guint src_len;
  guint dest_len;
  gchar **s;
  gchar **l;
  guint l_len = 0;

  g_warn_if_fail (dest != NULL);

  if (!src || g_strv_length (src) == 0)
    return;

  if (!*dest)
    {
      *dest = g_strdupv (src);
      return;
    }

  src_len = g_strv_length (src);
  dest_len = g_strv_length (*dest);

  l = g_malloc (src_len * sizeof (gpointer));
  for (s = src; *s; s++)
    if (!g_strv_contains ((const gchar * const *) *dest, *s))
      l[l_len++] = g_strdup (*s);

  if (l_len > 0)
    {
      *dest = g_realloc (*dest, (dest_len + l_len + 1) * sizeof (gpointer));
      memcpy (*dest + dest_len, l, l_len * sizeof (gpointer));
      (*dest)[dest_len + l_len] = NULL;
    }

  g_free (l);
}

static void
append_fs_mount_options (const FSMountOptions *src, FSMountOptions *dest)
{
  if (!src)
    return;

  strv_append_unique (src->defaults, &dest->defaults);
  strv_append_unique (src->allow, &dest->allow);
  /* appending not allowed for 'drivers' */
}

/* Similar to append_fs_mount_options() but replaces the member data instead of appending */
static void
override_fs_mount_options (const FSMountOptions *src, FSMountOptions *dest)
{
  if (!src)
    return;

  if (src->defaults)
    {
      g_strfreev (dest->defaults);
      dest->defaults = g_strdupv (src->defaults);
    }
  if (src->allow)
    {
      g_strfreev (dest->allow);
      dest->allow = g_strdupv (src->allow);
    }
  if (src->drivers)
    {
      g_strfreev (dest->drivers);
      dest->drivers = g_strdupv (src->drivers);
    }
}

/*
 * udisks_mount_options_entry_free:
 * @entry: A #UDisksMountOptionsEntry.
 *
 * Frees the mount options entry.
 */
void
udisks_mount_options_entry_free (UDisksMountOptionsEntry *entry)
{
  if (entry)
    {
      g_free (entry->fs_type);
      g_free (entry->options);
      g_free (entry);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

#define MOUNT_OPTIONS_GLOBAL_CONFIG_FILE_NAME "mount_options.conf"

#define MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS  "defaults"
#define MOUNT_OPTIONS_KEY_DEFAULTS           "defaults"
#define MOUNT_OPTIONS_KEY_ALLOW              "allow"
#define MOUNT_OPTIONS_KEY_DRIVERS            "drivers"
#define MOUNT_OPTIONS_ARG_UID_SELF           "$UID"
#define MOUNT_OPTIONS_ARG_GID_SELF           "$GID"
#define UDEV_MOUNT_OPTIONS_PREFIX            "UDISKS_MOUNT_OPTIONS_"
#define FS_SIGNATURE_DRIVER_SEP              ":"
#define FS_SIGNATURE_DRIVER_SEP_CHR          ':'


/* transfer none */
static GHashTable *
get_options_for_block (GHashTable  *opts,
                       UDisksBlock *block)
{
  GHashTable *block_options = NULL;
  const gchar *block_device;
  const gchar * const *block_symlinks;
  GList *keys;
  GList *l;

  if (!block)
    return NULL;

  block_device = udisks_block_get_device (block);
  block_symlinks = udisks_block_get_symlinks (block);

  keys = g_hash_table_get_keys (opts);
  g_warn_if_fail (keys != NULL);
  for (l = keys; l != NULL; l = l->next)
    {
      if (!l->data || g_str_equal (l->data, MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS))
        continue;
      if (g_str_equal (l->data, block_device) ||
          (block_symlinks && g_strv_contains (block_symlinks, l->data)))
        {
          block_options = g_hash_table_lookup (opts, l->data);
          break;
        }
    }
  g_list_free (keys);

  return block_options;
}

/*
 * compute_block_level_mount_options: <internal>
 * @daemon: A #UDisksDaemon.
 * @block: A #UDisksBlock.
 * @fstype: The filesystem type to match or %NULL.
 * @fsmo: Filesystem type specific #FSMountOptions.
 * @fsmo_any: General ("any") #FSMountOptions.
 *
 * Calculate mount options for the given level of overrides. Matches the block
 * device-specific options on top of the defaults.
 *
 * Returns: %TRUE when mount options were overridden, %FALSE otherwise.
 */
static gboolean
compute_block_level_mount_options (GHashTable      *opts,
                                   UDisksBlock     *block,
                                   const gchar     *fstype,
                                   FSMountOptions  *fsmo,
                                   FSMountOptions  *fsmo_any)
{
  GHashTable *general_options;
  GHashTable *block_options;
  gboolean changed = FALSE;

  /* Compute general defaults first */
  general_options = g_hash_table_lookup (opts, MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS);
  if (general_options)
    {
      FSMountOptions *o;

      o = g_hash_table_lookup (general_options, MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS);
      override_fs_mount_options (o, fsmo_any);
      changed = changed || o != NULL;

      o = fstype ? g_hash_table_lookup (general_options, fstype) : NULL;
      override_fs_mount_options (o, fsmo);
      changed = changed || o != NULL;
    }

  /* Match specific block device */
  block_options = get_options_for_block (opts, block);

  /* Block device specific options should fully override "general" options per-member basis */
  if (block_options)
    {
      FSMountOptions *o;

      o = g_hash_table_lookup (block_options, MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS);
      override_fs_mount_options (o, fsmo_any);
      changed = changed || o != NULL;

      o = fstype ? g_hash_table_lookup (block_options, fstype) : NULL;
      override_fs_mount_options (o, fsmo);
      changed = changed || o != NULL;
    }

  return changed;
}

/*
 * compute_block_level_fs_drivers: <internal>
 * @daemon: A #UDisksDaemon.
 * @block: A #UDisksBlock.
 * @fs_signature: The filesystem signature to match.
 *
 * Calculate filesystem drivers for the given level of overrides. Matches the block
 * device-specific options on top of the defaults.
 *
 * Returns: (transfer full) (array zero-terminated=1): list of filesystem drivers. Free with g_strfreev().
 */
static gchar **
compute_block_level_fs_drivers (GHashTable      *opts,
                                UDisksBlock     *block,
                                const gchar     *fs_signature)
{
  GHashTable *general_options;
  GHashTable *block_options;
  gchar **drivers = NULL;

  /* Compute general defaults first */
  general_options = g_hash_table_lookup (opts, MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS);
  if (general_options)
    {
      FSMountOptions *o;

      o = g_hash_table_lookup (general_options, fs_signature);
      if (o)
        drivers = g_strdupv (o->drivers);
    }

  /* Match specific block device */
  block_options = get_options_for_block (opts, block);

  /* Block device specific options should fully override "general" options per-member basis */
  if (block_options)
    {
      FSMountOptions *o;

      o = g_hash_table_lookup (block_options, fs_signature);
      if (o)
        {
          g_strfreev (drivers);
          drivers = g_strdupv (o->drivers);
        }
    }

  return drivers;
}

/*
 * compute_mount_options_for_fs_type: <internal>
 * @daemon: A #UDisksDaemon.
 * @block: A #UDisksBlock.
 * @object: A #UDisksLinuxBlockObject.
 * @overrides: Config file overrides.
 * @fstype: The filesystem type to use or %NULL.
 *
 * Calculate mount options across different levels of overrides (builtin,
 * global config, local user config).
 *
 * Returns: (transfer full): Newly allocated #FSMountOptions options. Free with free_fs_mount_options().
 */
static FSMountOptions *
compute_mount_options_for_fs_type (UDisksDaemon           *daemon,
                                   UDisksBlock            *block,
                                   UDisksLinuxBlockObject *object,
                                   GHashTable             *overrides,
                                   const gchar            *fstype)
{
  UDisksLinuxDevice *device;
  GHashTable *builtin_opts;
  FSMountOptions *fsmo;
  FSMountOptions *fsmo_any;
  GHashTable *udev_overrides;
  GError *error = NULL;
  gboolean changed = FALSE;

  /* Builtin options, two-level hashtable */
  builtin_opts = g_object_get_data (G_OBJECT (daemon), "mount-options");
  g_return_val_if_fail (builtin_opts != NULL, NULL);

  fsmo = g_malloc0 (sizeof (FSMountOptions));
  fsmo_any = g_malloc0 (sizeof (FSMountOptions));
  compute_block_level_mount_options (builtin_opts, block, fstype, fsmo, fsmo_any);

  /* Global config file overrides, two-level hashtable */
  if (overrides)
    changed = compute_block_level_mount_options (overrides, block, fstype, fsmo, fsmo_any);

  /* udev properties, single-level hashtable */
  device = udisks_linux_block_object_get_device (object);
  udev_overrides = mount_options_get_from_udev (device, &error);
  if (udev_overrides)
    {
      FSMountOptions *o;

      o = g_hash_table_lookup (udev_overrides, MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS);
      override_fs_mount_options (o, fsmo_any);
      changed = changed || o != NULL;

      o = fstype ? g_hash_table_lookup (udev_overrides, fstype) : NULL;
      override_fs_mount_options (o, fsmo);
      changed = changed || o != NULL;

      g_hash_table_unref (udev_overrides);
    }
  else
    {
      udisks_warning ("Error getting udev mount options: %s",
                      error->message);
      g_clear_error (&error);
    }
  g_object_unref (device);

  /* Merge "any" and fstype-specific options */
  append_fs_mount_options (fsmo_any, fsmo);
  free_fs_mount_options (fsmo_any);
  fsmo_any = NULL;

  if (changed && fsmo->defaults)
    {
      gchar *opts = g_strjoinv (",", fsmo->defaults);
      udisks_notice ("Using overridden mount options: %s", opts);
      g_free (opts);
    }

  return fsmo;
}

/*
 * compute_drivers: <internal>
 * @daemon: A #UDisksDaemon.
 * @block: A #UDisksBlock.
 * @object: A #UDisksLinuxBlockObject.
 * @overrides: Config file overrides.
 * @fs_signature: Probed filesystem signature or %NULL if unavailable.
 * @fs_type: The preferred filesystem type to use or %NULL.
 *
 * Calculate filesystem drivers for the filesystem signature and preferred filesystem type.
 *
 * Returns: (transfer full) (array zero-terminated=1): list of filesystem drivers. Free with g_strfreev().
 */
static gchar **
compute_drivers (UDisksDaemon           *daemon,
                 UDisksBlock            *block,
                 UDisksLinuxBlockObject *object,
                 GHashTable             *overrides,
                 const gchar            *fs_signature,
                 const gchar            *fs_type)
{
  UDisksLinuxDevice *device;
  GHashTable *builtin_opts;
  GHashTable *udev_overrides;
  GError *error = NULL;
  gchar **drivers;

  /* No probed filesystem signature available or specific filesystem type is requested */
  if (!fs_signature || fs_type)
    {
      drivers = g_new0 (gchar *, 2);
      *drivers = g_strdup (fs_type);
      return drivers;
    }

  /* Builtin options, two-level hashtable */
  builtin_opts = g_object_get_data (G_OBJECT (daemon), "mount-options");
  g_return_val_if_fail (builtin_opts != NULL, NULL);
  drivers = compute_block_level_fs_drivers (builtin_opts, block, fs_signature);

  /* Global config file overrides, two-level hashtable */
  if (overrides)
    {
      gchar **d;

      d = compute_block_level_fs_drivers (overrides, block, fs_signature);
      if (d)
        {
          g_strfreev (drivers);
          drivers = d;
        }
    }

  /* udev properties, single-level hashtable */
  device = udisks_linux_block_object_get_device (object);
  udev_overrides = mount_options_get_from_udev (device, &error);
  if (udev_overrides)
    {
      FSMountOptions *o;

      o = g_hash_table_lookup (udev_overrides, fs_signature);
      if (o && o->drivers)
        {
          g_strfreev (drivers);
          drivers = g_strdupv (o->drivers);
        }

      g_hash_table_unref (udev_overrides);
    }
  else
    {
      udisks_warning ("Error getting udev mount options: %s",
                      error->message);
      g_clear_error (&error);
    }
  g_object_unref (device);

  /* No drivers configured for the specific fs_signature, use the signature itself */
  if (!drivers)
    {
      drivers = g_new0 (gchar *, 2);
      *drivers = g_strdup (fs_signature);
    }

  return drivers;
}

/* ---------------------------------------------------------------------------------------------------- */

/* transfer-full */
static gchar **
parse_mount_options_string (const gchar *str, gboolean strip_empty_values)
{
  GPtrArray *opts;
  char *optstr;
  char *name;
  size_t namesz;
  char *value;
  size_t valuesz;
  int ret;

  if (!str)
    return NULL;

  opts = g_ptr_array_new_with_free_func (g_free);
  optstr = (char *)str;

  while ((ret = mnt_optstr_next_option (&optstr, &name, &namesz, &value, &valuesz)) == 0)
    {
      gchar *opt;

      if (value == NULL || (strip_empty_values && valuesz == 0))
        {
          opt = g_strndup (name, namesz);
        }
      else
        {
          opt = g_strdup_printf ("%.*s=%.*s", (int) namesz, name, (int) valuesz, value);
        }
      g_ptr_array_add (opts, opt);
    }
  if (ret < 0)
    {
      udisks_warning ("Malformed mount options string '%s' at position %zd, ignoring",
                      str, optstr - str + 1);
      g_ptr_array_free (opts, TRUE);
      return NULL;
    }

  g_ptr_array_add (opts, NULL);
  return (gchar **) g_ptr_array_free (opts, FALSE);
}

/* transfer-full */
static gchar *
extract_fs_type (const gchar *key, const gchar **group)
{
  if (g_str_equal (key, MOUNT_OPTIONS_KEY_DEFAULTS) ||
      g_str_equal (key, MOUNT_OPTIONS_KEY_ALLOW))
    {
      *group = key;
      return g_strdup (MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS);
    }

  if (g_str_has_suffix (key, "_" MOUNT_OPTIONS_KEY_DEFAULTS))
    {
      *group = MOUNT_OPTIONS_KEY_DEFAULTS;
      return g_strndup (key, strlen (key) - strlen (MOUNT_OPTIONS_KEY_DEFAULTS) - 1);
    }
  if (g_str_has_suffix (key, "_" MOUNT_OPTIONS_KEY_ALLOW))
    {
      *group = MOUNT_OPTIONS_KEY_ALLOW;
      return g_strndup (key, strlen (key) - strlen (MOUNT_OPTIONS_KEY_ALLOW) - 1);
    }
  if (g_str_has_suffix (key, "_" MOUNT_OPTIONS_KEY_DRIVERS))
    {
      *group = MOUNT_OPTIONS_KEY_DRIVERS;
      return g_strndup (key, strlen (key) - strlen (MOUNT_OPTIONS_KEY_DRIVERS) - 1);
    }

  /* invalid key name */
  *group = NULL;
  return NULL;
}

static void
parse_key_value_pair (GHashTable *mount_options, const gchar *key, const gchar *value)
{
  FSMountOptions *ent;
  gchar *fs_type;
  const gchar *group = NULL;
  gchar **opts;

  fs_type = extract_fs_type (key, &group);
  if (!fs_type)
    {
      /* invalid or malformed key detected, do not parse and ignore */
      udisks_debug ("parse_key_value_pair: garbage key found: %s", key);
      return;
    }
  g_warn_if_fail (group != NULL);

  /* Trim equal 'fs_signature:fs_type' strings */
  if (strchr (fs_type, FS_SIGNATURE_DRIVER_SEP_CHR))
    {
      gchar **split = g_strsplit (fs_type, FS_SIGNATURE_DRIVER_SEP, 2);
      if (g_strv_length (split) == 2 && g_strcmp0 (split[0], split[1]) == 0)
        {
          g_free (fs_type);
          fs_type = g_strdup (split[0]);
        }
      g_strfreev (split);
    }

  ent = g_hash_table_lookup (mount_options, fs_type);
  if (!ent)
    {
      ent = g_malloc0 (sizeof (FSMountOptions));
      g_hash_table_replace (mount_options, g_strdup (fs_type), ent);
    }

  if (g_str_equal (group, MOUNT_OPTIONS_KEY_DRIVERS))
    {
      opts = g_strsplit (value, ",", -1);
    }
  else
    {
      opts = parse_mount_options_string (value,
                                         /* strip empty values for _allow groups for easier matching */
                                         !g_str_equal (group, MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS));
    }

#define ASSIGN_OPTS(g,p) \
  if (g_str_equal (group, g)) \
    { \
      if (ent->p) \
        { \
          g_warning ("mount_options_parse_group: Duplicate key '%s' detected", key); \
          g_strfreev (ent->p); \
        } \
      ent->p = opts; \
    } \
  else

  ASSIGN_OPTS (MOUNT_OPTIONS_KEY_ALLOW, allow)
  ASSIGN_OPTS (MOUNT_OPTIONS_KEY_DEFAULTS, defaults)
  ASSIGN_OPTS (MOUNT_OPTIONS_KEY_DRIVERS, drivers)
    {
      /* should be caught by extract_fs_type() already */
      g_warning ("parse_key_value_pair: Unmatched key '%s' found, ignoring", key);
    }

  g_free (fs_type);
}

static GHashTable *
mount_options_parse_group (GKeyFile *key_file, const gchar *group_name, GError **error)
{
  GHashTable *mount_options;
  gchar **keys;
  gsize keys_len = 0;

  keys = g_key_file_get_keys (key_file, group_name, &keys_len, error);
  g_warn_if_fail (keys != NULL);

  mount_options = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) free_fs_mount_options);
  for (; keys_len > 0; keys_len--)
    {
      gchar *key;
      gchar *value;
      GError *e = NULL;

      key = g_ascii_strdown (keys[keys_len - 1], -1);
      value = g_key_file_get_string (key_file, group_name, keys[keys_len - 1], &e);
      if (!value)
        {
          udisks_warning ("mount_options_parse_group: cannot retrieve value for key '%s': %s",
                          key, e->message);
          g_error_free (e);
        }
      else
        {
          parse_key_value_pair (mount_options, key, value);
        }
      g_free (value);
      g_free (key);
    }

  g_strfreev (keys);

  return mount_options;
}

static GHashTable *
mount_options_parse_key_file (GKeyFile *key_file, GError **error)
{
  GHashTable *mount_options = NULL;
  gchar **groups;
  gsize groups_len = 0;

  groups = g_key_file_get_groups (key_file, &groups_len);
  if (groups == NULL || groups_len == 0)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_NOT_SUPPORTED,
                           "Failed to parse mount options: No sections found.");
      g_strfreev (groups);
      return NULL;
    }

  mount_options = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_hash_table_destroy);
  for (; groups_len > 0; groups_len--)
    {
      GHashTable *opts;
      GError *local_error = NULL;
      gchar *group = groups[groups_len - 1];

      opts = mount_options_parse_group (key_file, group, &local_error);
      if (! opts)
        {
          udisks_warning ("Failed to parse mount options section %s: %s",
                          group, local_error->message);
          g_error_free (local_error);
          /* ignore the whole section, continue with the rest */
        }
      else
        {
          g_hash_table_replace (mount_options, g_strdup (group), opts);
        }
    }
  g_strfreev (groups);

  return mount_options;
}

/* returns two-level hashtable with block specifics at the first level */
static GHashTable *
mount_options_parse_config_file (const gchar *filename, GError **error)
{
  GKeyFile *key_file;
  GHashTable *mount_options;

  key_file = g_key_file_new ();
  if (! g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, error))
    {
      g_key_file_free (key_file);
      return NULL;
    }

  mount_options = mount_options_parse_key_file (key_file, error);
  g_key_file_free (key_file);

  return mount_options;
}

/* returns second level of mount options (not block-specific) */
static GHashTable *
mount_options_get_from_udev (UDisksLinuxDevice *device, GError **error)
{
  GHashTable *mount_options;
  const gchar * const *keys;

  g_warn_if_fail (device != NULL);
  if (!device->udev_device)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "'device' is not a valid UDisksLinuxDevice");
      return NULL;
    }

  mount_options = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) free_fs_mount_options);

  keys = g_udev_device_get_property_keys (device->udev_device);
  for (; *keys; keys++)
    if (g_str_has_prefix (*keys, UDEV_MOUNT_OPTIONS_PREFIX))
      {
        gchar *key;
        const gchar *value;

        key = g_ascii_strdown (*keys + strlen (UDEV_MOUNT_OPTIONS_PREFIX), -1);
        value = g_udev_device_get_property (device->udev_device, *keys);
        if (!value)
          {
            udisks_warning ("mount_options_get_from_udev: cannot retrieve value for udev property %s",
                            *keys);
          }
        else
          {
            parse_key_value_pair (mount_options, key, value);
          }
        g_free (key);
      }

  return mount_options;
}

/*
 * udisks_linux_mount_options_get_builtin: <internal>
 *
 * Get built-in set of default mount options. This function will never
 * fail, the process is aborted in case of a parse error.
 *
 * Returns: (transfer full): A two-level #GHashTable.
 */
GHashTable *
udisks_linux_mount_options_get_builtin (void)
{
  GResource *daemon_resource;
  GBytes *builtin_opts_bytes;
  GKeyFile *key_file;
  GHashTable *mount_options;
  GError *error = NULL;

  daemon_resource = udisks_daemon_resources_get_resource ();
  builtin_opts_bytes = g_resource_lookup_data (daemon_resource,
                                               "/org/freedesktop/UDisks2/data/builtin_mount_options.conf",
                                               G_RESOURCE_LOOKUP_FLAGS_NONE,
                                               &error);

  if (builtin_opts_bytes == NULL)
    {
      udisks_error ("Failed to read built-in mount options resource: %s", error->message);
      g_error_free (error);
      return NULL;
    }

  key_file = g_key_file_new ();
  if (! g_key_file_load_from_bytes (key_file, builtin_opts_bytes, G_KEY_FILE_NONE, &error))
    {
      /* should never happen */
      udisks_error ("Failed to read built-in mount options: %s", error->message);
      g_error_free (error);
      g_key_file_free (key_file);
      g_bytes_unref (builtin_opts_bytes);
      return NULL;
    }

  mount_options = mount_options_parse_key_file (key_file, &error);
  g_key_file_free (key_file);
  g_bytes_unref (builtin_opts_bytes);

  if (mount_options == NULL)
    {
      /* should never happen either */
      udisks_error ("Failed to parse built-in mount options: %s", error->message);
      g_error_free (error);
    }
  else if (!g_hash_table_contains (mount_options, MOUNT_OPTIONS_CONFIG_GROUP_DEFAULTS))
    {
      g_hash_table_destroy (mount_options);
      mount_options = NULL;
      udisks_error ("Failed to parse built-in mount options: No global `defaults` section found.");
    }

  return mount_options;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_uid_in_gid (uid_t uid,
               gid_t gid)
{
  GError *error = NULL;
  gid_t primary_gid = -1;
  gchar *user_name = NULL;
  static gid_t supplementary_groups[128];
  int num_supplementary_groups = 128;
  int n;

  /* TODO: use some #define instead of hardcoding some random number like 128 */

  if (! udisks_daemon_util_get_user_info (uid, &primary_gid, &user_name, &error))
    {
      udisks_warning ("%s", error->message);
      g_error_free (error);
      return FALSE;
    }
  if (primary_gid == gid)
    {
      g_free (user_name);
      return TRUE;
    }

  if (getgrouplist (user_name, primary_gid, supplementary_groups, &num_supplementary_groups) < 0)
    {
      udisks_warning ("Error getting supplementary groups for uid %u: %m", uid);
      g_free (user_name);
      return FALSE;
    }
  g_free (user_name);

  for (n = 0; n < num_supplementary_groups; n++)
    {
      if (supplementary_groups[n] == gid)
        return TRUE;
    }

  return FALSE;
}

/* extracts option names from @allow that carry the @arg string as an argument */
static gchar **
extract_opts_with_arg (gchar **allow,
                       const gchar *arg)
{
  GPtrArray *opts;

  if (allow == NULL)
    return NULL;

  opts = g_ptr_array_new ();
  for (; *allow; allow++)
    {
      gchar *eq;

      eq = g_strrstr (*allow, arg);
      if (eq && eq != *allow && *(eq - 1) == '=')
        g_ptr_array_add (opts, g_strndup (*allow, eq - *allow - 1));
    }
  g_ptr_array_add (opts, NULL);

  return (gchar **) g_ptr_array_free (opts, FALSE);
}

#define VARIANT_NULL_STRING  "\1"

static gboolean
is_mount_option_allowed (const FSMountOptions *fsmo,
                         const gchar * const  *allow_uid_self,
                         const gchar * const  *allow_gid_self,
                         const gchar          *option,
                         const gchar          *value,
                         uid_t                 caller_uid)
{
  gchar *endp;
  uid_t uid;
  gid_t gid;
  gchar *s;

  /* match the exact option=value string within allowed options */
  if (fsmo && fsmo->allow && value && strlen (value) > 0)
    {
      s = g_strdup_printf ("%s=%s", option, value);
      if (g_strv_contains ((const gchar * const *) fsmo->allow, s))
        {
          g_free (s);
          /* not checking whether the option is in UID/GID_self as
           * this is what was explicitly allowed by sysadmin (overrides) */
          return TRUE;
        }
      g_free (s);
    }

  /* .. then check for mount options where the caller is allowed to pass
   * in his own uid
   */
  if (fsmo && allow_uid_self && g_strv_contains (allow_uid_self, option))
    {
      if (value == NULL || strlen (value) == 0)
        {
          udisks_warning ("is_mount_option_allowed: option '%s' is listed within allow_uid_self but has no value",
                          option);
          return FALSE;
        }
      uid = strtol (value, &endp, 10);
      if (*endp != '\0')
        /* malformed value string */
        return FALSE;
      return (uid == caller_uid);
    }

  /* .. ditto for gid
   */
  if (fsmo && allow_gid_self && g_strv_contains (allow_gid_self, option))
    {
      if (value == NULL || strlen (value) == 0)
        {
          udisks_warning ("is_mount_option_allowed: option '%s' is listed within allow_gid_self but has no value",
                          option);
          return FALSE;
        }
      gid = strtol (value, &endp, 10);
      if (*endp != '\0')
        /* malformed value string */
        return FALSE;
      return is_uid_in_gid (caller_uid, gid);
    }

  /* the above UID/GID checks also assure that none of the options
   * would be checked again against the _allow array */

  /* match within allowed mount options */
  if (fsmo && fsmo->allow)
    {
      /* simple 'option' match */
      if (g_strv_contains ((const gchar * const *) fsmo->allow, option))
        return TRUE;
    }

  if (g_str_has_prefix (option, "x-"))
    {
      return TRUE;
    }

  return FALSE;
}

static GVariant *
prepend_default_mount_options (const FSMountOptions *fsmo,
                               const gchar * const  *allow_uid_self,
                               const gchar * const  *allow_gid_self,
                               uid_t                 caller_uid,
                               GVariant             *given_options,
                               gboolean              shared_fs)
{
  GVariantBuilder builder;
  gint n;
  gchar *s;
  gid_t gid;
  const gchar *option_string;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  if (fsmo != NULL)
    {
      gchar **defaults = fsmo->defaults;

      for (n = 0; defaults != NULL && defaults[n] != NULL; n++)
        {
          const gchar *option = defaults[n];
          const gchar *eq = strchr (option, '=');

          if (eq != NULL)
            {
              const gchar *value = eq + 1;
              gchar *option_name = g_strndup (option, eq - option);

              /* check that 'option=value' is explicitly allowed */
              if (value && strlen (value) > 0 && fsmo->allow &&
                  g_strv_contains ((const gchar * const *) fsmo->allow, option) &&
                  !g_str_equal (value, MOUNT_OPTIONS_ARG_UID_SELF) &&
                  !g_str_equal (value, MOUNT_OPTIONS_ARG_GID_SELF))
                {
                  g_variant_builder_add (&builder, "{ss}", option_name, value);
                }
              else if (allow_uid_self && g_strv_contains (allow_uid_self, option_name))
                {
                  /* append caller UID */
                  s = g_strdup_printf ("%u", caller_uid);
                  g_variant_builder_add (&builder, "{ss}", option_name, s);
                  g_free (s);
                }
              else if (allow_gid_self && g_strv_contains (allow_gid_self, option_name))
                {
                  if (udisks_daemon_util_get_user_info (caller_uid, &gid, NULL, NULL))
                    {
                      s = g_strdup_printf ("%u", gid);
                      g_variant_builder_add (&builder, "{ss}", option_name, s);
                      g_free (s);
                    }
                }
              else if (shared_fs && g_str_equal (option_name, "mode"))
                {
                  /* set different 'mode' and 'dmode' options for file systems mounted at shared
                     location (otherwise they cannot be used by anybody else so mounting them at
                     a shared location doesn't make much sense */
                  gchar *shared_mode = g_strdup (value);

                  /* give group and others the same permissions as to the owner
                     without the 'write' permission, but at least 'read'
                     (HINT: keep in mind that chars are ints in C and that
                     digits are ordered naturally in the ASCII table) */
                  shared_mode[2] = MAX(shared_mode[1] - 2, '4');
                  shared_mode[3] = MAX(shared_mode[1] - 2, '4');
                  g_variant_builder_add (&builder, "{ss}", option_name, shared_mode);
                  g_free (shared_mode);
                }
              else if (shared_fs && g_str_equal (option_name, "dmode"))
                {
                  /* see right above */
                  /* Does any other dmode than 0555 make sense for a FS mounted
                     at a shared location?  */
                  g_variant_builder_add (&builder, "{ss}", option_name, "0555");
                }
              else
                {
                  g_variant_builder_add (&builder, "{ss}", option_name, value);
                }
              g_free (option_name);
            }
          else
            g_variant_builder_add (&builder, "{ss}", option, VARIANT_NULL_STRING);
        }
    }

  if (g_variant_lookup (given_options,
                        "options",
                        "&s", &option_string))
    {
      gchar **split_option_string;
      split_option_string = g_strsplit (option_string, ",", -1);
      for (n = 0; split_option_string[n] != NULL; n++)
        {
          gchar *option = split_option_string[n];
          const gchar *eq = strchr (option, '=');

          if (eq != NULL)
            {
              const gchar *value = eq + 1;
              gsize opt_len = eq - option;

              s = g_strndup (option, opt_len);
              g_variant_builder_add (&builder, "{ss}", s, value);
              g_free (s);
            }
          else
            g_variant_builder_add (&builder, "{ss}", option, VARIANT_NULL_STRING);

          g_free (option);
        }
      g_free (split_option_string);
    }

  return g_variant_builder_end (&builder);
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksMountOptionsEntry *
calculate_mount_options_for_fs_type (UDisksDaemon  *daemon,
                                     UDisksBlock   *block,
                                     UDisksLinuxBlockObject *object,
                                     GHashTable    *overrides,
                                     uid_t          caller_uid,
                                     gboolean       shared_fs,
                                     const gchar   *fs_type,
                                     GVariant      *options,
                                     GError       **error)
{
  UDisksMountOptionsEntry *entry;
  FSMountOptions *fsmo;
  gchar **allow_uid_self = NULL;
  gchar **allow_gid_self = NULL;
  GVariant *options_to_use;
  GVariantIter iter;
  gchar *options_to_use_str = NULL;
  gchar *key, *value;
  GString *str;

  fsmo = compute_mount_options_for_fs_type (daemon, block, object, overrides, fs_type);

  allow_uid_self = extract_opts_with_arg (fsmo->allow, MOUNT_OPTIONS_ARG_UID_SELF);
  allow_gid_self = extract_opts_with_arg (fsmo->allow, MOUNT_OPTIONS_ARG_GID_SELF);

  /* always prepend some reasonable default mount options; these are
   * chosen here; the user can override them if he wants to
   */
  options_to_use = prepend_default_mount_options (fsmo,
                                                  (const gchar * const *) allow_uid_self,
                                                  (const gchar * const *) allow_gid_self,
                                                  caller_uid,
                                                  options,
                                                  shared_fs);

  /* validate mount options */
  str = g_string_new ("uhelper=udisks2,nodev,nosuid");
  g_variant_iter_init (&iter, options_to_use);
  while (g_variant_iter_next (&iter, "{&s&s}", &key, &value))
    {
      /* GVariant doesn't handle NULL strings gracefully */
      if (g_str_equal (value, VARIANT_NULL_STRING))
        value = NULL;
      /* avoid attacks like passing "shortname=lower,uid=0" as a single mount option */
      if (strchr (key, ',') != NULL || (value && strchr (value, ',') != NULL))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_OPTION_NOT_PERMITTED,
                       "Malformed mount option `%s'",
                       key);
          g_string_free (str, TRUE);
          goto out;
        }

      /* first check if the mount option is allowed */
      if (!is_mount_option_allowed (fsmo,
                                    (const gchar * const *) allow_uid_self,
                                    (const gchar * const *) allow_gid_self,
                                    key, value, caller_uid))
        {
          if (value == NULL)
            {
              g_set_error (error,
                           UDISKS_ERROR,
                           UDISKS_ERROR_OPTION_NOT_PERMITTED,
                           "Mount option `%s' is not allowed",
                           key);
            }
          else
            {
              g_set_error (error,
                           UDISKS_ERROR,
                           UDISKS_ERROR_OPTION_NOT_PERMITTED,
                           "Mount option `%s=%s' is not allowed",
                           key, value);
            }
          g_string_free (str, TRUE);
          goto out;
        }

      g_string_append_c (str, ',');
      if (value == NULL)
        g_string_append (str, key);
      else
        g_string_append_printf (str, "%s=%s", key, value);
    }
  options_to_use_str = g_string_free (str, FALSE);

 out:
  g_variant_unref (options_to_use);
  free_fs_mount_options (fsmo);
  g_strfreev (allow_uid_self);
  g_strfreev (allow_gid_self);

  if (!options_to_use_str)
    return NULL;

  g_assert (g_utf8_validate (options_to_use_str, -1, NULL));
  entry = g_new0 (UDisksMountOptionsEntry, 1);
  entry->options = options_to_use_str;

  return entry;
}

/*
 * udisks_linux_calculate_mount_options: <internal>
 * @daemon: A #UDisksDaemon.
 * @block: A #UDisksBlock.
 * @caller_uid: The uid of the caller making the request.
 * @fs_signature: Probed filesystem signature or %NULL if unavailable.
 * @fs_type: The preferred filesystem type to use or %NULL.
 * @options: Options requested by the caller.
 * @error: Return location for error or %NULL.
 *
 * Calculates filesystem types for a given filesystem signature and a preferred
 * filesystem type and returns a list of computed mount options for each filesystem
 * driver defined. Ensures (by returning an error) that only safe options are used.
 *
 * Returns: (transfer full) (array zero-terminated=1): A list of #UDisksMountOptionsEntry or %NULL if @error is set.
 */
UDisksMountOptionsEntry **
udisks_linux_calculate_mount_options (UDisksDaemon  *daemon,
                                      UDisksBlock   *block,
                                      uid_t          caller_uid,
                                      const gchar   *fs_signature,
                                      const gchar   *fs_type,
                                      GVariant      *options,
                                      GError       **error)
{
  UDisksLinuxBlockObject *object = NULL;
  UDisksConfigManager *config_manager;
  UDisksLinuxDevice *device = NULL;
  gboolean shared_fs = FALSE;
  GHashTable *overrides;
  gchar *config_file_path;
  GError *l_error = NULL;
  GPtrArray *ptr_array;
  gchar **drivers;
  gchar **d;

  config_manager = udisks_daemon_get_config_manager (daemon);
  object = udisks_daemon_util_dup_object (block, NULL);
  device = udisks_linux_block_object_get_device (object);
  if (device != NULL && device->udev_device != NULL &&
      g_udev_device_get_property_as_boolean (device->udev_device, "UDISKS_FILESYSTEM_SHARED"))
    shared_fs = TRUE;

  /* Global config file overrides */
  config_file_path = g_build_filename (udisks_config_manager_get_config_dir (config_manager),
                                       MOUNT_OPTIONS_GLOBAL_CONFIG_FILE_NAME, NULL);
  overrides = mount_options_parse_config_file (config_file_path, &l_error);
  if (!overrides)
    {
      if (! g_error_matches (l_error, G_FILE_ERROR, G_FILE_ERROR_NOENT) /* not found */ &&
          ! g_error_matches (l_error, UDISKS_ERROR, UDISKS_ERROR_NOT_SUPPORTED) /* empty file */ )
        {
          udisks_warning ("Error reading global mount options config file %s: %s",
                          config_file_path, l_error->message);
        }
      g_clear_error (&l_error);
    }
  g_free (config_file_path);

  /* Compute filesystem drivers for given @fs_signature and @fs_type */
  drivers = compute_drivers (daemon, block, object, overrides, fs_signature, fs_type);

  ptr_array = g_ptr_array_new_with_free_func ((GDestroyNotify) udisks_mount_options_entry_free);
  for (d = drivers; *d; d++)
    {
      UDisksMountOptionsEntry *entry;
      gchar *fs_type_full;

      if (fs_signature && g_strcmp0 (fs_signature, "auto") != 0 && g_strcmp0 (fs_signature, *d) != 0)
        fs_type_full = g_strdup_printf ("%s" FS_SIGNATURE_DRIVER_SEP "%s", fs_signature, *d);
      else
        fs_type_full = g_strdup (*d);

      entry = calculate_mount_options_for_fs_type (daemon,
                                                   block,
                                                   object,
                                                   overrides,
                                                   caller_uid,
                                                   shared_fs,
                                                   fs_type_full,
                                                   options,
                                                   error);
      g_free (fs_type_full);
      if (!entry)
        {
          /* Failure computing any of the filesystem types will result
           * in failure of the whole computation.
           */
          g_ptr_array_free (ptr_array, TRUE);
          ptr_array = NULL;
          break;
        }
      entry->fs_type = g_strdup (*d);
      g_ptr_array_add (ptr_array, entry);
    }

  g_clear_object (&device);
  g_clear_object (&object);
  if (overrides)
    g_hash_table_unref (overrides);
  g_strfreev (drivers);

  if (!ptr_array)
    return NULL;

  g_ptr_array_add (ptr_array, NULL);  /* trailing NULL element */
  return (UDisksMountOptionsEntry **) g_ptr_array_free (ptr_array, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */
