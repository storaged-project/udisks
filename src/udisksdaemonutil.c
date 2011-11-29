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
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>

#include <limits.h>
#include <stdlib.h>

#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udiskscleanup.h"

/**
 * SECTION:udisksdaemonutil
 * @title: Utilities
 * @short_description: Various utility routines
 *
 * Various utility routines.
 */

/**
 * udisks_decode_udev_string:
 * @str: An udev-encoded string or %NULL.
 *
 * Unescapes sequences like \x20 to " " and ensures the returned string is valid UTF-8.
 *
 * If the string is not valid UTF-8, try as hard as possible to convert to UTF-8.
 *
 * If %NULL is passed, then %NULL is returned.
 *
 * See udev_util_encode_string() in libudev/libudev-util.c in the udev
 * tree for what kinds of strings can be used.
 *
 * Returns: A valid UTF-8 string that must be freed with g_free().
 */
gchar *
udisks_decode_udev_string (const gchar *str)
{
  GString *s;
  gchar *ret;
  const gchar *end_valid;
  guint n;

  if (str == NULL)
    {
      ret = NULL;
      goto out;
    }

  s = g_string_new (NULL);
  for (n = 0; str[n] != '\0'; n++)
    {
      if (str[n] == '\\')
        {
          gint val;

          if (str[n + 1] != 'x' || str[n + 2] == '\0' || str[n + 3] == '\0')
            {
              g_warning ("**** NOTE: malformed encoded string `%s'", str);
              break;
            }

          val = (g_ascii_xdigit_value (str[n + 2]) << 4) | g_ascii_xdigit_value (str[n + 3]);

          g_string_append_c (s, val);

          n += 3;
        }
      else
        {
          g_string_append_c (s, str[n]);
        }
    }

  if (!g_utf8_validate (s->str, -1, &end_valid))
    {
      g_warning ("The string `%s' is not valid UTF-8. Invalid characters begins at `%s'", s->str, end_valid);
      ret = g_strndup (s->str, end_valid - s->str);
      g_string_free (s, TRUE);
    }
  else
    {
      ret = g_string_free (s, FALSE);
    }

 out:
  return ret;
}

/**
 * udisks_safe_append_to_object_path:
 * @str: A #GString to append to.
 * @s: A UTF-8 string.
 *
 * Appends @s to @str in a way such that only characters that can be
 * used in a D-Bus object path will be used. E.g. a character not in
 * <literal>[A-Z][a-z][0-9]_</literal> will be escaped as _HEX where
 * HEX is a two-digit hexadecimal number.
 *
 * Note that his mapping is not bijective - e.g. you cannot go back
 * to the original string.
 */
void
udisks_safe_append_to_object_path (GString      *str,
                                   const gchar  *s)
{
  guint n;
  for (n = 0; s[n] != '\0'; n++)
    {
      gint c = s[n];
      /* D-Bus spec sez:
       *
       * Each element must only contain the ASCII characters "[A-Z][a-z][0-9]_"
       */
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')
        {
          g_string_append_c (str, c);
        }
      else
        {
          /* Escape bytes not in [A-Z][a-z][0-9] as _<hex-with-two-digits> */
          g_string_append_printf (str, "_%02x", c);
        }
    }
}

/**
 * udisks_daemon_util_block_get_size:
 * @device: A #GUdevDevice for a top-level block device.
 * @out_media_available: (out): Return location for whether media is available or %NULL.
 * @out_media_change_detected: (out): Return location for whether media change is detected or %NULL.
 *
 * Gets the size of the @device top-level block device, checking for media in the process
 *
 * Returns: The size of @device or 0 if no media is available or if unknown.
 */
guint64
udisks_daemon_util_block_get_size (GUdevDevice *device,
                                   gboolean    *out_media_available,
                                   gboolean    *out_media_change_detected)
{
  gboolean media_available = FALSE;
  gboolean media_change_detected = TRUE;
  guint64 size = 0;

  /* figuring out if media is available is a bit tricky */
  if (g_udev_device_get_sysfs_attr_as_boolean (device, "removable"))
    {
      /* never try to open optical drives (might cause the door to close) or
       * floppy drives (makes noise)
       */
      if (g_udev_device_get_property_as_boolean (device, "ID_DRIVE_FLOPPY"))
        {
          /* assume media available */
          media_available = TRUE;
          media_change_detected = FALSE;
        }
      else if (g_udev_device_get_property_as_boolean (device, "ID_CDROM"))
        {
          /* Rely on (careful) work already done by udev's cdrom_id prober */
          if (g_udev_device_get_property_as_boolean (device, "ID_CDROM_MEDIA"))
            media_available = TRUE;
        }
      else
        {
          gint fd;
          /* For the general case, just rely on open(2) failing with
           * ENOMEDIUM if no medium is inserted
           */
          fd = open (g_udev_device_get_device_file (device), O_RDONLY);
          if (fd >= 0)
            {
              media_available = TRUE;
              close (fd);
            }
        }
    }
  else
    {
      /* not removable, so media is implicitly available */
      media_available = TRUE;
    }

  if (media_available && size == 0 && media_change_detected)
    size = g_udev_device_get_sysfs_attr_as_uint64 (device, "size") * 512;

  if (out_media_available != NULL)
    *out_media_available = media_available;

  if (out_media_change_detected != NULL)
    *out_media_change_detected = media_change_detected;

  return size;
}


/**
 * udisks_daemon_util_resolve_link:
 * @path: A path
 * @name: Name of a symlink in @path.
 *
 * Resolves the symlink @path/@name.
 *
 * Returns: A canonicalized absolute pathname or %NULL if the symlink
 * could not be resolved. Free with g_free().
 */
gchar *
udisks_daemon_util_resolve_link (const gchar *path,
                                 const gchar *name)
{
  gchar *full_path;
  gchar link_path[PATH_MAX];
  gchar resolved_path[PATH_MAX];
  gssize num;
  gboolean found_it;

  found_it = FALSE;

  full_path = g_build_filename (path, name, NULL);

  num = readlink (full_path, link_path, sizeof(link_path) - 1);
  if (num != -1)
    {
      char *absolute_path;

      link_path[num] = '\0';

      absolute_path = g_build_filename (path, link_path, NULL);
      if (realpath (absolute_path, resolved_path) != NULL)
        {
          found_it = TRUE;
        }
      g_free (absolute_path);
    }
  g_free (full_path);

  if (found_it)
    return g_strdup (resolved_path);
  else
    return NULL;
}

/**
 * udisks_daemon_util_resolve_links:
 * @path: A path
 * @dir_name: Name of a directory in @path holding symlinks.
 *
 * Resolves all symlinks in @path/@dir_name. This can be used to
 * easily walk e.g. holders or slaves of block devices.
 *
 * Returns: An array of canonicalized absolute pathnames. Free with g_strfreev().
 */
gchar **
udisks_daemon_util_resolve_links (const gchar *path,
                                  const gchar *dir_name)
{
  gchar *s;
  GDir *dir;
  const gchar *name;
  GPtrArray *p;

  p = g_ptr_array_new ();

  s = g_build_filename (path, dir_name, NULL);
  dir = g_dir_open (s, 0, NULL);
  if (dir == NULL)
    goto out;
  while ((name = g_dir_read_name (dir)) != NULL)
    {
      gchar *resolved;
      resolved = udisks_daemon_util_resolve_link (s, name);
      if (resolved != NULL)
        g_ptr_array_add (p, resolved);
    }
  g_ptr_array_add (p, NULL);

 out:
  if (dir != NULL)
    g_dir_close (dir);
  g_free (s);

  return (gchar **) g_ptr_array_free (p, FALSE);
}


/**
 * udisks_daemon_util_setup_by_user:
 * @daemon: A #UDisksDaemon.
 * @object: The #GDBusObject that the call is on or %NULL.
 * @user: The user in question.
 *
 * Checks whether the device represented by @object (if any) has been
 * setup by @user.
 *
 * Returns: %TRUE if @object has been set-up by @user, %FALSE if not.
 */
gboolean
udisks_daemon_util_setup_by_user (UDisksDaemon *daemon,
                                  UDisksObject *object,
                                  uid_t         user)
{
  gboolean ret;
  UDisksBlock *block = NULL;
  UDisksPartition *partition = NULL;
  UDisksCleanup *cleanup;
  uid_t setup_by_user;
  UDisksObject *crypto_object;

  ret = FALSE;

  cleanup = udisks_daemon_get_cleanup (daemon);
  block = udisks_object_get_block (object);
  if (block == NULL)
    goto out;
  partition = udisks_object_get_partition (object);

  /* loop devices */
  if (udisks_cleanup_has_loop (cleanup, udisks_block_get_device (block), &setup_by_user))
    {
      if (setup_by_user == user)
        {
          ret = TRUE;
          goto out;
        }
    }

  /* partition of a loop device */
  if (partition != NULL)
    {
      UDisksObject *partition_object = NULL;
      partition_object = udisks_daemon_find_object (daemon, udisks_partition_get_table (partition));
      if (partition_object != NULL)
        {
          if (udisks_daemon_util_setup_by_user (daemon, partition_object, user))
            {
              ret = TRUE;
              g_object_unref (partition_object);
              goto out;
            }
          g_object_unref (partition_object);
        }
    }

  /* LUKS devices */
  crypto_object = udisks_daemon_find_object (daemon, udisks_block_get_crypto_backing_device (block));
  if (crypto_object != NULL)
    {
      UDisksBlock *crypto_block;
      crypto_block = udisks_object_peek_block (crypto_object);
      if (udisks_cleanup_find_unlocked_luks (cleanup,
                                             udisks_block_get_device_number (crypto_block),
                                             &setup_by_user))
        {
          if (setup_by_user == user)
            {
              ret = TRUE;
              g_object_unref (crypto_object);
              goto out;
            }
        }
      g_object_unref (crypto_object);
    }

 out:
  g_clear_object (&partition);
  g_clear_object (&block);
  return ret;
}

/**
 * udisks_daemon_util_check_authorization_sync:
 * @daemon: A #UDisksDaemon.
 * @object: (allow-none): The #GDBusObject that the call is on or %NULL.
 * @action_id: The action id to check for.
 * @options: (allow-none): A #GVariant to check for the <literal>auth.no_user_interaction</literal> option or %NULL.
 * @message: The message to convey (use N_).
 * @invocation: The invocation to check for.
 *
 * Checks if the caller represented by @invocation is authorized for
 * the action identified by @action_id, optionally displaying @message
 * if authentication is needed. Additionally, if the caller is not
 * authorized, the appropriate error is already returned to the caller
 * via @invocation.
 *
 * The calling thread is blocked for the duration of the
 * authentication which may be a very long time unless
 * @auth_no_user_interaction is %TRUE.
 *
 * The follow variables can be used in @message
 *
 * - udisks2.device - If @object has a #UDisksBlock interface, this property is set to the value of the #UDisksBlock::preferred-device property.
 *
 * Returns: %TRUE if caller is authorized, %FALSE if not.
 */
gboolean
udisks_daemon_util_check_authorization_sync (UDisksDaemon          *daemon,
                                             UDisksObject          *object,
                                             const gchar           *action_id,
                                             GVariant              *options,
                                             const gchar           *message,
                                             GDBusMethodInvocation *invocation)
{
  PolkitSubject *subject;
  PolkitDetails *details;
  PolkitCheckAuthorizationFlags flags;
  PolkitAuthorizationResult *result;
  GError *error;
  gboolean ret;
  UDisksBlock *block;
  gboolean auth_no_user_interaction;

  ret = FALSE;
  subject = NULL;
  details = NULL;
  result = NULL;
  flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE;
  auth_no_user_interaction = FALSE;

  subject = polkit_system_bus_name_new (g_dbus_method_invocation_get_sender (invocation));
  if (options != NULL)
    {
      g_variant_lookup (options,
                        "auth.no_user_interaction",
                        "b",
                        &auth_no_user_interaction);
    }
  if (!auth_no_user_interaction)
    flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;

  details = polkit_details_new ();
  polkit_details_insert (details, "polkit.message", message);
  polkit_details_insert (details, "polkit.gettext_domain", "udisks2"); /* TODO: set up translation */

  /* setup other details that @message can use */
  if (object != NULL)
    {
      block = udisks_object_peek_block (object);
      if (block != NULL)
        polkit_details_insert (details, "udisks2.device", udisks_block_get_preferred_device (block));
    }

  error = NULL;
  result = polkit_authority_check_authorization_sync (udisks_daemon_get_authority (daemon),
                                                      subject,
                                                      action_id,
                                                      details,
                                                      flags,
                                                      NULL, /* GCancellable* */
                                                      &error);
  if (result == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error checking authorization: %s (%s, %d)",
                                             error->message,
                                             g_quark_to_string (error->domain),
                                             error->code);
      g_error_free (error);
      goto out;
    }
  if (!polkit_authorization_result_get_is_authorized (result))
    {
      if (polkit_authorization_result_get_dismissed (result))
        g_dbus_method_invocation_return_error_literal (invocation,
                                                       UDISKS_ERROR,
                                                       UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED,
                                                       "The authentication dialog was dismissed");
      else
        g_dbus_method_invocation_return_error_literal (invocation,
                                                       UDISKS_ERROR,
                                                       polkit_authorization_result_get_is_challenge (result) ?
                                                       UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN :
                                                       UDISKS_ERROR_NOT_AUTHORIZED,
                                                       "Not authorized to perform operation");
      goto out;
    }

  ret = TRUE;

 out:
  if (subject != NULL)
    g_object_unref (subject);
  if (details != NULL)
    g_object_unref (details);
  if (result != NULL)
    g_object_unref (result);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_daemon_util_get_caller_uid_sync:
 * @daemon: A #UDisksDaemon.
 * @invocation: A #GDBusMethodInvocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @out_uid: (out): Return location for resolved uid or %NULL.
 * @out_gid: (out) (allow-none): Return location for resolved gid or %NULL.
 * @error: Return location for error.
 *
 * Gets the UNIX user id (and possibly group id) of the peer
 * represented by @invocation.
 *
 * Returns: %TRUE if the user id (and possibly group id) was obtained, %FALSE otherwise
 */
gboolean
udisks_daemon_util_get_caller_uid_sync (UDisksDaemon            *daemon,
                                        GDBusMethodInvocation   *invocation,
                                        GCancellable            *cancellable,
                                        uid_t                   *out_uid,
                                        gid_t                   *out_gid,
                                        GError                 **error)
{
  gboolean ret;
  const gchar *caller;
  GVariant *value;
  GError *local_error;
  uid_t uid;

  /* TODO: cache this on @daemon */

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
  g_variant_get (value, "(u)", &uid);
  if (out_uid != NULL)
    *out_uid = uid;

  if (out_gid != NULL)
    {
      struct passwd pwstruct;
      gchar pwbuf[8192];
      static struct passwd *pw;
      int rc;

      rc = getpwuid_r (uid, &pwstruct, pwbuf, sizeof pwbuf, &pw);
      if (rc == 0 && pw == NULL)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "User with uid %d does not exist", (gint) uid);
        }
      else if (pw == NULL)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Error looking up passwd struct for uid %d: %m", (gint) uid);
          goto out;
        }
      *out_gid = pw->pw_gid;
    }

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */
