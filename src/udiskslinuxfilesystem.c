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

#include <glib/gstdio.h>

#include "udiskslinuxfilesystem.h"
#include "udiskslinuxblock.h"
#include "udisksdaemon.h"
#include "udiskspersistentstore.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxfilesystem
 * @title: UDisksLinuxFilesystem
 * @short_description: Filesystem manipulation on Linux
 *
 * This type provides an implementation of the #UDisksFilesystem
 * interface on Linux.
 */

typedef struct _UDisksLinuxFilesystemClass   UDisksLinuxFilesystemClass;

/**
 * UDisksLinuxFilesystem:
 *
 * The #UDisksLinuxFilesystem structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxFilesystem
{
  UDisksFilesystemSkeleton parent_instance;
};

struct _UDisksLinuxFilesystemClass
{
  UDisksFilesystemSkeletonClass parent_class;
};

static void filesystem_iface_init (UDisksFilesystemIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxFilesystem, udisks_linux_filesystem, UDISKS_TYPE_FILESYSTEM_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_FILESYSTEM, filesystem_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_filesystem_init (UDisksLinuxFilesystem *filesystem)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (filesystem),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_filesystem_class_init (UDisksLinuxFilesystemClass *klass)
{
}

/**
 * udisks_linux_filesystem_new:
 * @cancellable: A #GCancellable or %NULL.
 *
 * Creates a new #UDisksLinuxFilesystem instance.
 *
 * Returns: A new #UDisksLinuxFilesystem. Free with g_object_unref().
 */
UDisksFilesystem *
udisks_linux_filesystem_new (void)
{
  return UDISKS_FILESYSTEM (g_object_new (UDISKS_TYPE_LINUX_FILESYSTEM,
                                          NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_uid_sync (GDBusMethodInvocation   *invocation,
              GCancellable            *cancellable,
              uid_t                   *out_uid,
              GError                 **error)
{
  gboolean ret;
  const gchar *caller;
  GVariant *value;
  GError *local_error;

  ret = FALSE;

  caller = g_dbus_method_invocation_get_sender (invocation);

  local_error = NULL;
  value = g_dbus_connection_call_sync (g_dbus_method_invocation_get_connection (invocation),
                                       "org.freedesktop.DBus",  /* bus name */
                                       "/org/freedesktop/DBus", /* object path */
                                       "org.freedesktop.DBus",  /* interface */
                                       "GetConnectionUnixUser", /* method */
                                       g_variant_new ("(s)", caller),
                                       G_VARIANT_TYPE ("(u)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1, /* timeout_msec */
                                       cancellable,
                                       &local_error);
  if (value == NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error determining uid of caller %s: %s (%s, %d)",
                   caller,
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
      goto out;
    }

  G_STATIC_ASSERT (sizeof (uid_t) == sizeof (guint32));
  g_variant_get (value, "(u)", out_uid);

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  const gchar *fstype;
  const gchar * const *defaults;
  const gchar * const *allow;
  const gchar * const *allow_uid_self;
  const gchar * const *allow_gid_self;
} FSMountOptions;

/* ---------------------- vfat -------------------- */

static const gchar *vfat_defaults[] = { "uid=", "gid=", "shortname=mixed", "dmask=0077", "utf8=1", "showexec", NULL };
static const gchar *vfat_allow[] = { "flush", "utf8=", "shortname=", "umask=", "dmask=", "fmask=", "codepage=", "iocharset=", "usefree", "showexec", NULL };
static const gchar *vfat_allow_uid_self[] = { "uid=", NULL };
static const gchar *vfat_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- ntfs -------------------- */
/* this is assuming that ntfs-3g is used */

static const gchar *ntfs_defaults[] = { "uid=", "gid=", "dmask=0077", "fmask=0177", NULL };
static const gchar *ntfs_allow[] = { "umask=", "dmask=", "fmask=", NULL };
static const gchar *ntfs_allow_uid_self[] = { "uid=", NULL };
static const gchar *ntfs_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- iso9660 -------------------- */

static const gchar *iso9660_defaults[] = { "uid=", "gid=", "iocharset=utf8", "mode=0400", "dmode=0500", NULL };
static const gchar *iso9660_allow[] = { "norock", "nojoliet", "iocharset=", "mode=", "dmode=", NULL };
static const gchar *iso9660_allow_uid_self[] = { "uid=", NULL };
static const gchar *iso9660_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- udf -------------------- */

static const gchar *udf_defaults[] = { "uid=", "gid=", "iocharset=utf8", "umask=0077", NULL };
static const gchar *udf_allow[] = { "iocharset=", "umask=", NULL };
static const gchar *udf_allow_uid_self[] = { "uid=", NULL };
static const gchar *udf_allow_gid_self[] = { "gid=", NULL };

/* ------------------------------------------------ */
/* TODO: support context= */

static const gchar *any_allow[] = { "exec", "noexec", "nodev", "nosuid", "atime", "noatime", "nodiratime", "ro", "rw", "sync", "dirsync", NULL };

static const FSMountOptions fs_mount_options[] =
  {
    { "vfat", vfat_defaults, vfat_allow, vfat_allow_uid_self, vfat_allow_gid_self },
    { "ntfs", ntfs_defaults, ntfs_allow, ntfs_allow_uid_self, ntfs_allow_gid_self },
    { "iso9660", iso9660_defaults, iso9660_allow, iso9660_allow_uid_self, iso9660_allow_gid_self },
    { "udf", udf_defaults, udf_allow, udf_allow_uid_self, udf_allow_gid_self },
  };

/* ------------------------------------------------ */

static int num_fs_mount_options = sizeof(fs_mount_options) / sizeof(FSMountOptions);

static const FSMountOptions *
find_mount_options_for_fs (const gchar *fstype)
{
  int n;
  const FSMountOptions *fsmo;

  for (n = 0; n < num_fs_mount_options; n++)
    {
      fsmo = fs_mount_options + n;
      if (g_strcmp0 (fsmo->fstype, fstype) == 0)
        goto out;
    }

  fsmo = NULL;
 out:
  return fsmo;
}

static gid_t
find_primary_gid (uid_t uid)
{
  struct passwd *pw;
  gid_t gid;

  gid = (gid_t) - 1;

  pw = getpwuid (uid);
  if (pw == NULL)
    {
      g_warning ("Couldn't look up uid %d: %m", uid);
      goto out;
    }
  gid = pw->pw_gid;

 out:
  return gid;
}

static gboolean
is_uid_in_gid (uid_t uid,
               gid_t gid)
{
  gboolean ret;
  struct passwd *pw;
  static gid_t supplementary_groups[128];
  int num_supplementary_groups = 128;
  int n;

  /* TODO: use some #define instead of harcoding some random number like 128 */

  ret = FALSE;

  pw = getpwuid (uid);
  if (pw == NULL)
    {
      g_warning ("Couldn't look up uid %d: %m", uid);
      goto out;
    }
  if (pw->pw_gid == gid)
    {
      ret = TRUE;
      goto out;
    }

  if (getgrouplist (pw->pw_name, pw->pw_gid, supplementary_groups, &num_supplementary_groups) < 0)
    {
      g_warning ("Couldn't find supplementary groups for uid %d: %m", uid);
      goto out;
    }

  for (n = 0; n < num_supplementary_groups; n++)
    {
      if (supplementary_groups[n] == gid)
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  return ret;
}

static gboolean
is_mount_option_allowed (const FSMountOptions *fsmo,
                         const gchar          *option,
                         uid_t                 caller_uid)
{
  int n;
  gchar *endp;
  uid_t uid;
  gid_t gid;
  gboolean allowed;
  const gchar *ep;
  gsize ep_len;

  allowed = FALSE;

  /* first run through the allowed mount options */
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->allow != NULL && fsmo->allow[n] != NULL; n++)
        {
          ep = strstr (fsmo->allow[n], "=");
          if (ep != NULL && ep[1] == '\0')
            {
              ep_len = ep - fsmo->allow[n] + 1;
              if (strncmp (fsmo->allow[n], option, ep_len) == 0)
                {
                  allowed = TRUE;
                  goto out;
                }
            }
          else
            {
              if (strcmp (fsmo->allow[n], option) == 0)
                {
                  allowed = TRUE;
                  goto out;
                }
            }
        }
    }
  for (n = 0; any_allow[n] != NULL; n++)
    {
      ep = strstr (any_allow[n], "=");
      if (ep != NULL && ep[1] == '\0')
        {
          ep_len = ep - any_allow[n] + 1;
          if (strncmp (any_allow[n], option, ep_len) == 0)
            {
              allowed = TRUE;
              goto out;
            }
        }
      else
        {
          if (strcmp (any_allow[n], option) == 0)
            {
              allowed = TRUE;
              goto out;
            }
        }
    }

  /* .. then check for mount options where the caller is allowed to pass
   * in his own uid
   */
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->allow_uid_self != NULL && fsmo->allow_uid_self[n] != NULL; n++)
        {
          const gchar *r_mount_option = fsmo->allow_uid_self[n];
          if (g_str_has_prefix (option, r_mount_option))
            {
              uid = strtol (option + strlen (r_mount_option), &endp, 10);
              if (*endp != '\0')
                continue;
              if (uid == caller_uid)
                {
                  allowed = TRUE;
                  goto out;
                }
            }
        }
    }

  /* .. ditto for gid
   */
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->allow_gid_self != NULL && fsmo->allow_gid_self[n] != NULL; n++)
        {
          const gchar *r_mount_option = fsmo->allow_gid_self[n];
          if (g_str_has_prefix (option, r_mount_option))
            {
              gid = strtol (option + strlen (r_mount_option), &endp, 10);
              if (*endp != '\0')
                continue;
              if (is_uid_in_gid (caller_uid, gid))
                {
                  allowed = TRUE;
                  goto out;
                }
            }
        }
    }

 out:
  return allowed;
}

static gchar **
prepend_default_mount_options (const FSMountOptions *fsmo,
                               uid_t caller_uid,
                               const gchar * const *given_options)
{
  GPtrArray *options;
  int n;
  gchar *s;
  gid_t gid;

  options = g_ptr_array_new ();
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->defaults != NULL && fsmo->defaults[n] != NULL; n++)
        {
          const gchar *option = fsmo->defaults[n];

          if (strcmp (option, "uid=") == 0)
            {
              s = g_strdup_printf ("uid=%d", caller_uid);
              g_ptr_array_add (options, s);
            }
          else if (strcmp (option, "gid=") == 0)
            {
              gid = find_primary_gid (caller_uid);
              if (gid != (gid_t) - 1)
                {
                  s = g_strdup_printf ("gid=%d", gid);
                  g_ptr_array_add (options, s);
                }
            }
          else
            {
              g_ptr_array_add (options, g_strdup (option));
            }
        }
    }
  for (n = 0; given_options[n] != NULL; n++)
    {
      g_ptr_array_add (options, g_strdup (given_options[n]));
    }

  g_ptr_array_add (options, NULL);

  return (char **) g_ptr_array_free (options, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

/*
 * calculate_fs_type: <internal>
 * @block: A #UDisksBlockDevice.
 * @requested_fs_type: The requested file system type or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Calculates the file system type to use.
 *
 * Returns: A valid UTF-8 string with the filesystem type (may be "auto") or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_fs_type (UDisksBlockDevice         *block,
                   const gchar               *requested_fs_type,
                   GError                   **error)
{
  gchar *fs_type_to_use;
  const gchar *probed_fs_type;

  probed_fs_type = NULL;
  if (block != NULL)
    probed_fs_type = udisks_block_device_get_id_type (block);

  fs_type_to_use = NULL;
  if (requested_fs_type != NULL && strlen (requested_fs_type) > 0)
    {
      /* TODO: maybe check that it's compatible with probed_fs_type */
      fs_type_to_use = g_strdup (requested_fs_type);
    }
  else
    {
      if (probed_fs_type != NULL && strlen (probed_fs_type) > 0)
        fs_type_to_use = g_strdup (probed_fs_type);
      else
        fs_type_to_use = g_strdup ("auto");
    }

  g_assert (fs_type_to_use == NULL || g_utf8_validate (fs_type_to_use, -1, NULL));

  return fs_type_to_use;
}

/*
 * calculate_mount_options: <internal>
 * @block: A #UDisksBlockDevice.
 * @caller_uid: The uid of the caller making the request.
 * @fs_type: The filesystem type to use or %NULL.
 * @requested_options: Options requested by the caller.
 * @out_auth_no_user_interaction: Return location for whether the 'auth_no_user_interaction' option was passed or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Calculates the mount option string to use. Ensures (by returning an
 * error) that only safe options are used.
 *
 * Returns: A string with mount options or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_mount_options (UDisksBlockDevice         *block,
                         uid_t                      caller_uid,
                         const gchar               *fs_type,
                         const gchar *const        *requested_options,
                         gboolean                  *out_auth_no_user_interaction,
                         GError                   **error)
{
  const FSMountOptions *fsmo;
  gchar **options_to_use;
  gchar *options_to_use_str;
  GString *str;
  guint n;
  gboolean auth_no_user_interaction;

  options_to_use = NULL;
  options_to_use_str = NULL;
  auth_no_user_interaction = FALSE;

  fsmo = find_mount_options_for_fs (fs_type);

  /* always prepend some reasonable default mount options; these are
   * chosen here; the user can override them if he wants to
   */
  options_to_use = prepend_default_mount_options (fsmo, caller_uid, requested_options);

  /* validate mount options */
  str = g_string_new ("uhelper=udisks2,nodev,nosuid");
  for (n = 0; options_to_use[n] != NULL; n++)
    {
      const gchar *option = options_to_use[n];

      if (g_strcmp0 (option, "auth_no_user_interaction") == 0)
        {
          auth_no_user_interaction = TRUE;
          continue;
        }

      /* avoid attacks like passing "shortname=lower,uid=0" as a single mount option */
      if (strstr (option, ",") != NULL)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_OPTION_NOT_PERMITTED,
                       "Malformed mount option `%s'",
                       option);
          g_string_free (str, TRUE);
          goto out;
        }

      /* first check if the mount option is allowed */
      if (!is_mount_option_allowed (fsmo, option, caller_uid))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_OPTION_NOT_PERMITTED,
                       "Mount option `%s' is not allowed",
                       option);
          g_string_free (str, TRUE);
          goto out;
        }

      g_string_append_c (str, ',');
      g_string_append (str, option);
    }
  options_to_use_str = g_string_free (str, FALSE);

 out:
  g_strfreev (options_to_use);

  g_assert (options_to_use_str == NULL || g_utf8_validate (options_to_use_str, -1, NULL));

  if (out_auth_no_user_interaction != NULL)
    *out_auth_no_user_interaction = auth_no_user_interaction;

  return options_to_use_str;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
ensure_utf8 (const gchar *s)
{
  const gchar *end;
  gchar *ret;

  if (!g_utf8_validate (s, -1, &end))
    {
      gchar *tmp;
      gint pos;
      /* TODO: could possibly return a nicer UTF-8 string  */
      pos = (gint) (end - s);
      tmp = g_strndup (s, end - s);
      ret = g_strdup_printf ("%s (Invalid UTF-8 at byte %d)", tmp, pos);
      g_free (tmp);
    }
  else
    {
      ret = g_strdup (s);
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/*
 * calculate_mount_point: <internal>
 * @block: A #UDisksBlockDevice.
 * @fs_type: The file system type to mount with
 * @error: Return location for error or %NULL.
 *
 * Calculates the mount point to use.
 *
 * Returns: A UTF-8 string with the mount point to use or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_mount_point (UDisksBlockDevice         *block,
                       const gchar               *fs_type,
                       GError                   **error)
{
  const gchar *label;
  const gchar *uuid;
  gchar *mount_point;
  gchar *orig_mount_point;
  GString *str;
  gchar *s;
  guint n;

  label = NULL;
  uuid = NULL;
  if (block != NULL)
    {
      label = udisks_block_device_get_id_label (block);
      uuid = udisks_block_device_get_id_uuid (block);
    }

  /* NOTE: UTF-8 has the nice property that valid UTF-8 strings only contains
   *       the byte 0x2F if it's for the '/' character (U+002F SOLIDUS).
   *
   *       See http://en.wikipedia.org/wiki/UTF-8 for details.
   */

  if (label != NULL && strlen (label) > 0)
    {
      str = g_string_new ("/media/");
      s = ensure_utf8 (label);
      for (n = 0; s[n] != '\0'; n++)
        {
          gint c = s[n];
          if (c == '/')
            g_string_append_c (str, '_');
          else
            g_string_append_c (str, c);
        }
      mount_point = g_string_free (str, FALSE);
      g_free (s);
    }
  else if (uuid != NULL && strlen (uuid) > 0)
    {
      str = g_string_new ("/media/");
      s = ensure_utf8 (uuid);
      for (n = 0; s[n] != '\0'; n++)
        {
          gint c = s[n];
          if (c == '/')
            g_string_append_c (str, '_');
          else
            g_string_append_c (str, c);
        }
      mount_point = g_string_free (str, FALSE);
      g_free (s);
    }
  else
    {
      mount_point = g_strdup ("/media/disk");
    }

  /* ... then uniqify the mount point */
  orig_mount_point = g_strdup (mount_point);
  n = 1;
  while (TRUE)
    {
      if (!g_file_test (mount_point, G_FILE_TEST_EXISTS))
        {
          break;
        }
      else
        {
          g_free (mount_point);
          mount_point = g_strdup_printf ("%s%d", orig_mount_point, n++);
        }
    }
  g_free (orig_mount_point);

  return mount_point;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_mount (UDisksFilesystem       *filesystem,
              GDBusMethodInvocation  *invocation,
              const gchar            *requested_fs_type,
              const gchar* const     *requested_options)
{
  UDisksObject *object;
  UDisksBlockDevice *block;
  UDisksDaemon *daemon;
  UDisksPersistentStore *store;
  uid_t caller_uid;
  const gchar * const *existing_mount_points;
  const gchar *probed_fs_usage;
  gchar *fs_type_to_use;
  gchar *mount_options_to_use;
  gchar *mount_point_to_use;
  gchar *escaped_fs_type_to_use;
  gchar *escaped_mount_options_to_use;
  gchar *escaped_mount_point_to_use;
  gchar *error_message;
  GError *error;
  gboolean auth_no_user_interaction;

  object = NULL;
  daemon = NULL;
  error_message = NULL;
  fs_type_to_use = NULL;
  mount_options_to_use = NULL;
  mount_point_to_use = NULL;
  escaped_fs_type_to_use = NULL;
  escaped_mount_options_to_use = NULL;
  escaped_mount_point_to_use = NULL;

  object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
  block = udisks_object_peek_block_device (object);
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));
  store = udisks_daemon_get_persistent_store (daemon);

  /* TODO: check if mount point is managed by e.g. /etc/fstab or
   *       similar - if so, use that instead of managing mount points
   *       in /media
   */

  /* First, fail if the device is already mounted */
  existing_mount_points = udisks_filesystem_get_mount_points (filesystem);
  if (existing_mount_points != NULL && g_strv_length ((gchar **) existing_mount_points) > 0)
    {
      GString *str;
      guint n;
      str = g_string_new (NULL);
      for (n = 0; existing_mount_points[n] != NULL; n++)
        {
          if (n > 0)
            g_string_append (str, ", ");
          g_string_append_printf (str, "`%s'", existing_mount_points[n]);
        }
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_ALREADY_MOUNTED,
                                             "Device %s is already mounted at %s.\n",
                                             udisks_block_device_get_device (block),
                                             str->str);
      g_string_free (str, TRUE);
      goto out;
    }

  /* Them fail if the device is not mountable - we actually allow mounting
   * devices that are not probed since since it could be that we just
   * don't have the data in the udev database but the device has a
   * filesystem *anyway*...
   *
   * For example, this applies to PC floppy devices - automatically
   * probing for media them creates annoying noise. So they won't
   * appear in the udev database.
   */
  probed_fs_usage = NULL;
  if (block != NULL)
    probed_fs_usage = udisks_block_device_get_id_usage (block);
  if (probed_fs_usage != NULL && strlen (probed_fs_usage) > 0 &&
      g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Cannot mount block device %s with probed usage `%s' - expected `filesystem'",
                                             udisks_block_device_get_device (block),
                                             probed_fs_usage);
      goto out;
    }

  /* we need the uid of the caller to check mount options */
  error = NULL;
  if (!get_uid_sync (invocation, NULL /* GCancellable */, &caller_uid, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* calculate filesystem type (guaranteed to be valid UTF-8) */
  error = NULL;
  fs_type_to_use = calculate_fs_type (block,
                                      requested_fs_type,
                                      &error);
  if (fs_type_to_use == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* calculate mount options (guaranteed to be valid UTF-8) */
  error = NULL;
  mount_options_to_use = calculate_mount_options (block,
                                                  caller_uid,
                                                  fs_type_to_use,
                                                  requested_options,
                                                  &auth_no_user_interaction,
                                                  &error);
  if (mount_options_to_use == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Now, check that the user is actually authorized to mount the
   * device. Need to do this before calculating a mount point since we
   * may be racing with other threads...
   *
   * TODO: want nicer authentication message + special treatment for system-internal
   */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.filesystem-mount",
                                                    auth_no_user_interaction,
                                                    N_("Authentication is required to mount $(udisks2.device)"),
                                                    invocation))
    goto out;

  /* calculate mount point (guaranteed to be valid UTF-8) */
  error = NULL;
  mount_point_to_use = calculate_mount_point (block,
                                              fs_type_to_use,
                                              &error);
  if (mount_point_to_use == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* create the mount point */
  if (g_mkdir (mount_point_to_use, 0700) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error creating mount point `%s': %m",
                                             mount_point_to_use);
      goto out;
    }

  /* update the mounted-fs file */
  if (!udisks_persistent_store_mounted_fs_add (store,
                                               udisks_block_device_get_device (block),
                                               mount_point_to_use,
                                               caller_uid,
                                               &error))
    goto out;

  escaped_fs_type_to_use       = g_strescape (fs_type_to_use, NULL);
  escaped_mount_options_to_use = g_strescape (mount_options_to_use, NULL);
  escaped_mount_point_to_use   = g_strescape (mount_point_to_use, NULL);

  /* run mount(8) */
  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              NULL,  /* GCancellable */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "mount -t \"%s\" -o \"%s\" \"%s\" \"%s\"",
                                              escaped_fs_type_to_use,
                                              escaped_mount_options_to_use,
                                              udisks_block_device_get_device (block),
                                              escaped_mount_point_to_use))
    {
      /* ugh, something went wrong.. we need to clean up the created mount point
       * and also remove the entry from our mounted-fs file
       *
       * Either of these operations shouldn't really fail...
       */
      error = NULL;
      if (!udisks_persistent_store_mounted_fs_remove (store,
                                                      mount_point_to_use,
                                                      &error))
        {
          udisks_daemon_log (daemon,
                             UDISKS_LOG_LEVEL_WARNING,
                             "Error removing mount point %s from filesystems file: %s (%s, %d)",
                             mount_point_to_use,
                             error->message,
                             g_quark_to_string (error->domain),
                             error->code);
          g_error_free (error);
        }
      if (g_rmdir (mount_point_to_use) != 0)
        {
          udisks_daemon_log (daemon,
                             UDISKS_LOG_LEVEL_WARNING,
                             "Error removing directory %s: %m",
                             mount_point_to_use);
        }
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error mounting %s at %s: %s",
                                             udisks_block_device_get_device (block),
                                             mount_point_to_use,
                                             error_message);
      goto out;
    }

  udisks_daemon_log (daemon,
                     UDISKS_LOG_LEVEL_INFO,
                     "Mounted %s at %s on behalf of uid %d",
                     udisks_block_device_get_device (block),
                     mount_point_to_use,
                     caller_uid);

  udisks_filesystem_complete_mount (filesystem, invocation, mount_point_to_use);

 out:
  g_free (error_message);
  g_free (escaped_fs_type_to_use);
  g_free (escaped_mount_options_to_use);
  g_free (escaped_mount_point_to_use);
  g_free (fs_type_to_use);
  g_free (mount_options_to_use);
  g_free (mount_point_to_use);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_unmount (UDisksFilesystem       *filesystem,
                GDBusMethodInvocation  *invocation,
                const gchar* const     *options)
{
  UDisksObject *object;
  UDisksBlockDevice *block;
  UDisksDaemon *daemon;
  UDisksPersistentStore *store;
  gchar *mount_point;
  gchar *escaped_mount_point;
  GError *error;
  uid_t mounted_by_uid;
  uid_t caller_uid;
  gchar *error_message;
  const gchar *const *mount_points;
  guint n;
  gboolean opt_force;
  gboolean rc;

  mount_point = NULL;
  escaped_mount_point = NULL;
  error_message = NULL;
  opt_force = FALSE;

  object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (filesystem)));
  block = udisks_object_peek_block_device (object);
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));
  store = udisks_daemon_get_persistent_store (daemon);

  for (n = 0; options != NULL && options[n] != NULL; n++)
    {
      const gchar *option = options[n];
      if (g_strcmp0 (option, "force") == 0)
        {
          opt_force = TRUE;
        }
      else
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_OPTION_NOT_PERMITTED,
                                                 "Unsupported option `%s'",
                                                 option);
          goto out;
        }
    }

  mount_points = udisks_filesystem_get_mount_points (filesystem);
  if (mount_points == NULL || g_strv_length ((gchar **) mount_points) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_MOUNTED,
                                             "Device `%s' is not mounted",
                                             udisks_block_device_get_device (block));
      goto out;
    }

  error = NULL;
  mount_point = udisks_persistent_store_mounted_fs_find (store,
                                                         udisks_block_device_get_device (block),
                                                         &mounted_by_uid,
                                                         &error);
  if (error != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error when looking for entry `%s' in mounted-fs: %s (%s, %d)",
                                             udisks_block_device_get_device (block),
                                             error->message,
                                             g_quark_to_string (error->domain),
                                             error->code);
      g_error_free (error);
      goto out;
    }
  if (mount_point == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Entry for `%s' not found in mounted-fs",
                                             udisks_block_device_get_device (block));
      goto out;
    }

  /* TODO: allow unmounting stuff not in the mounted-fs file? */

  error = NULL;
  if (!get_uid_sync (invocation, NULL, &caller_uid, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  if (caller_uid != 0 && (caller_uid != mounted_by_uid))
    {
      /* TODO: allow with special authorization (unmount-others) */
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_MOUNTED_BY_OTHER_USER,
                                             "Cannot unmount filesystem at `%s' mounted by other user with uid %d",
                                             mount_point,
                                             mounted_by_uid);
      goto out;
    }

  /* otherwise go ahead and unmount the filesystem */
  if (!udisks_persistent_store_mounted_fs_currently_unmounting_add (store, mount_point))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_ALREADY_UNMOUNTING,
                                             "Cannot unmount %s: Mount point `%s' is currently being unmounted",
                                             udisks_block_device_get_device (block),
                                             mount_point);
      goto out;
    }

  escaped_mount_point = g_strescape (mount_point, NULL);
  if (opt_force)
    {
      /* right now -l is the only way to "force unmount" file systems... */
      rc = udisks_daemon_launch_spawned_job_sync (daemon,
                                                  NULL,  /* GCancellable */
                                                  &error_message,
                                                  NULL,  /* input_string */
                                                  "umount -l \"%s\"",
                                                  escaped_mount_point);
    }
  else
    {
      rc = udisks_daemon_launch_spawned_job_sync (daemon,
                                                  NULL,  /* GCancellable */
                                                  &error_message,
                                                  NULL,  /* input_string */
                                                  "umount \"%s\"",
                                                  escaped_mount_point);
    }
  if (!rc)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error unmounting %s from %s: %s",
                                             udisks_block_device_get_device (block),
                                             mount_point,
                                             error_message);
      udisks_persistent_store_mounted_fs_currently_unmounting_remove (store, mount_point);
      goto out;
    }

  /* OK, filesystem unmounted.. now to remove the entry from mounted-fs as well as the mount point */
  error = NULL;
  if (!udisks_persistent_store_mounted_fs_remove (store,
                                                  mount_point,
                                                  &error))
    {
      if (error == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error removing entry for `%s' from mounted-fs: Entry not found",
                                                 mount_point);
        }
      else
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error removing entry for `%s' from mounted-fs: %s (%s, %d)",
                                                 mount_point,
                                                 error->message,
                                                 g_quark_to_string (error->domain),
                                                 error->code);
          g_error_free (error);
        }
      udisks_persistent_store_mounted_fs_currently_unmounting_remove (store, mount_point);
      goto out;
    }
  udisks_persistent_store_mounted_fs_currently_unmounting_remove (store, mount_point);

  /* OK, removed the entry. Finally: nuke the mount point */
  if (g_rmdir (mount_point) != 0)
    {
      udisks_daemon_log (daemon,
                         UDISKS_LOG_LEVEL_ERROR,
                         "Error removing mount point `%s': %m",
                         mount_point);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error removing mount point `%s': %m",
                                             mount_point);
      goto out;
    }

  udisks_daemon_log (daemon,
                     UDISKS_LOG_LEVEL_INFO,
                     "Unmounted %s from %s on behalf of uid %d",
                     udisks_block_device_get_device (block),
                     mount_point,
                     caller_uid);

  udisks_filesystem_complete_unmount (filesystem, invocation);

 out:
  g_free (error_message);
  g_free (escaped_mount_point);
  g_free (mount_point);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
filesystem_iface_init (UDisksFilesystemIface *iface)
{
  iface->handle_mount   = handle_mount;
  iface->handle_unmount = handle_unmount;
}
