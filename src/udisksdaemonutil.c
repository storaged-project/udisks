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
#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>

#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>

#include <limits.h>
#include <stdlib.h>

#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksstate.h"
#include "udiskslogging.h"
#include "udiskslinuxblockobject.h"
#include "udiskslinuxdriveobject.h"

#if defined(HAVE_LIBSYSTEMD_LOGIN)
#include <systemd/sd-daemon.h>
#include <systemd/sd-login.h>
#endif

#if defined(HAVE_ELOGIND) && !defined(HAVE_LIBSYSTEMD_LOGIN)
#include <elogind/sd-login.h>
/* re-use HAVE_LIBSYSTEMD_LOGIN to not clutter the source file */
#define HAVE_LIBSYSTEMD_LOGIN 1
#endif

#if defined(HAVE_LIBSYSTEMD_LOGIN)
#define LOGIND_AVAILABLE() (access("/run/systemd/seats/", F_OK) >= 0)
#endif

/**
 * SECTION:udisksdaemonutil
 * @title: Utilities
 * @short_description: Various utility routines
 *
 * Various utility routines.
 */


/**
 * udisks_string_concat:
 * @a: First part
 * @b: Second part
 *
 * Returns: A new #GString holding the concatenation of the inputs.
 */
GString* udisks_string_concat (GString *a,
                               GString *b)
{
  GString *result;
  result = g_string_sized_new (a->len + b->len);
  g_string_append_len (result, a->str, a->len);
  g_string_append_len (result, b->str, b->len);
  return result;
}

gchar *
udisks_daemon_util_subst_str (const gchar *str,
           const gchar *from,
           const gchar *to)
{
    gchar **parts;
    gchar *result;

    parts = g_strsplit (str, from, 0);
    result = g_strjoinv (to, parts);
    g_strfreev (parts);
    return result;
}

gchar *
udisks_daemon_util_subst_str_and_escape (const gchar *str,
                      const gchar *from,
                      const gchar *to)
{
  gchar *quoted_and_escaped;
  gchar *ret;
  quoted_and_escaped = udisks_daemon_util_escape_and_quote (to);
  ret = udisks_daemon_util_subst_str (str, from, quoted_and_escaped);
  g_free (quoted_and_escaped);
  return ret;
}

/**
 * udisks_string_wipe_and_free:
 * @string: A string with potentially unsafe content or %NULL.
 *
 * Wipes the buffer and frees the string.
 */
void udisks_string_wipe_and_free (GString *string)
{
  if (string != NULL)
    {
      memset (string->str, '\0', string->len);
      g_string_free (string, TRUE);
    }
}

/**
 * udisks_variant_lookup_binary:
 * @dict: A dictionary #GVariant.
 * @name: The name of the item to lookup.
 * @out_text: (out): Return location for the binary text as #GString.
 *
 * Looks up binary data in a dictionary #GVariant and returns it as #GString.
 *
 * If the value is a bytestring ("ay"), it can contain arbitrary binary data
 * including '\0' values. If the value is a string ("s"), @out_text does not
 * include the terminating '\0' character.
 *
 * Returns: %TRUE if @dict contains an item @name of type "ay" or "s" that was
 * successfully stored in @out_text, and %FALSE otherwise.
 */
gboolean
udisks_variant_lookup_binary (GVariant     *dict,
                              const gchar  *name,
                              GString     **out_text)
{
  GVariant* item = g_variant_lookup_value (dict, name, NULL);
  if (item)
    return udisks_variant_get_binary (item, out_text);
  return FALSE;
}

/**
 * udisks_variant_get_binary:
 * @value: A #GVariant of type "ay" or "s".
 * @out_text: (out): Return location for the binary text as #GString.
 *
 * Gets binary data contained in a BYTEARRAY or STRING #GVariant and returns
 * it as a #GString.
 *
 * If the value is a bytestring ("ay"), it can contain arbitrary binary data
 * including '\0' values. If the value is a string ("s"), @out_text does not
 * include the terminating '\0' character.
 *
 * Returns: %TRUE if @value is a bytestring or string #GVariant and was
 * successfully stored in @out_text, and %FALSE otherwise.
 */
gboolean
udisks_variant_get_binary (GVariant  *value,
                           GString  **out_text)
{
  const gchar* str = NULL;
  gsize size = 0;

  if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
      str = g_variant_get_string (value, &size);
  else if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTESTRING))
      str = g_variant_get_fixed_array (value, &size, sizeof (guchar));

  if (str)
    {
      *out_text = g_string_new_len (str, size);
      return TRUE;
    }

  return FALSE;
}


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
              udisks_warning ("**** NOTE: malformed encoded string `%s'", str);
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
      udisks_warning ("The string `%s' is not valid UTF-8. Invalid characters begins at `%s'", s->str, end_valid);
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
          g_string_append_printf (str, "_%02x", (guint) c);
        }
    }
}

/**
 * udisks_g_object_ref_foreach:
 * @object: A #GObject to ref.
 * @unused: Unused parameter.
 *
 * This is a helper function for g_list_foreach. It expects a function
 * that takes two parameters but the standard g_object_ref takes just one
 * and using it makes gcc sad. So this function just calls g_object_ref
 * and throws away the second parameter.
 */
void
udisks_g_object_ref_foreach (gpointer object, gpointer unused)
{
  g_return_if_fail (G_IS_OBJECT (object));
  g_object_ref (G_OBJECT (object));
  return;
}

/**
 * udisks_g_object_ref_copy:
 * @object: A #GObject to ref.
 * @unused: Unused parameter.
 *
 * This is a helper function for g_list_copy_deep. It expects copy function
 * that takes two parameters but the standard g_object_ref takes just one
 * and using it makes gcc sad. So this function just calls g_object_ref
 * and throws away the second parameter.
 */
void *
udisks_g_object_ref_copy (gconstpointer object, gpointer unused)
{
  g_return_val_if_fail (G_IS_OBJECT (object), NULL);
  return g_object_ref (G_OBJECT (object));
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
      gchar *full_path_dir;

      link_path[num] = '\0';

      full_path_dir = g_path_get_dirname (full_path);
      absolute_path = g_build_filename (full_path_dir, link_path, NULL);
      g_free (full_path_dir);
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
  UDisksState *state;
  uid_t setup_by_user;
  UDisksObject *crypto_object;

  ret = FALSE;

  state = udisks_daemon_get_state (daemon);
  block = udisks_object_get_block (object);
  if (block == NULL)
    goto out;
  partition = udisks_object_get_partition (object);

  /* loop devices */
  if (udisks_state_has_loop (state, udisks_block_get_device (block), &setup_by_user))
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
      if (udisks_state_find_unlocked_crypto_dev (state,
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

  /* MDRaid devices */
  if (g_strcmp0 (udisks_block_get_mdraid (block), "/") != 0)
    {
      uid_t started_by_user;
      if (udisks_state_has_mdraid (state, udisks_block_get_device_number (block), &started_by_user))
        {
          if (started_by_user == user)
            {
              ret = TRUE;
              goto out;
            }
        }
    }

 out:
  g_clear_object (&partition);
  g_clear_object (&block);
  return ret;
}

/* Need this until we can depend on a libpolkit with this bugfix
 *
 * http://cgit.freedesktop.org/polkit/commit/?h=wip/js-rule-files&id=224f7b892478302dccbe7e567b013d3c73d376fd
 */
static void
_safe_polkit_details_insert (PolkitDetails *details, const gchar *key, const gchar *value)
{
  if (value != NULL && strlen (value) > 0)
    polkit_details_insert (details, key, value);
}

static void
_safe_polkit_details_insert_int (PolkitDetails *details, const gchar *key, gint value)
{
  gchar buf[32];
  snprintf (buf, sizeof buf, "%d", value);
  polkit_details_insert (details, key, buf);
}

static void
_safe_polkit_details_insert_uint64 (PolkitDetails *details, const gchar *key, guint64 value)
{
  gchar buf[32];
  snprintf (buf, sizeof buf, "0x%08llx", (unsigned long long int) value);
  polkit_details_insert (details, key, buf);
}

static gboolean
check_authorization_no_polkit (UDisksDaemon            *daemon,
                               UDisksObject            *object,
                               const gchar             *action_id,
                               GVariant                *options,
                               const gchar             *message,
                               GDBusMethodInvocation   *invocation,
                               GError                 **error)
{
  gboolean ret = FALSE;
  uid_t caller_uid = -1;
  GError *sub_error = NULL;

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL,         /* GCancellable* */
                                               &caller_uid,
                                               NULL,         /* gid_t *out_gid */
                                               NULL,         /* gchar **out_user_name */
                                               &sub_error))
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error getting uid for caller with bus name %s: %s (%s, %d)",
                   g_dbus_method_invocation_get_sender (invocation),
                   sub_error->message, g_quark_to_string (sub_error->domain), sub_error->code);
      g_clear_error (&sub_error);
      goto out;
    }

  /* only allow root */
  if (caller_uid == 0)
    {
      ret = TRUE;
    }
  else
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_NOT_AUTHORIZED,
                   "Not authorized to perform operation (polkit authority not available and caller is not uid 0)");
    }

 out:
  return ret;
}

/**
 * udisks_daemon_util_check_authorization_sync:
 * @daemon: A #UDisksDaemon.
 * @object: (allow-none): The #GDBusObject that the call is on or %NULL.
 * @action_id: The action id to check for.
 * @options: (allow-none): A #GVariant to check for the <quote>auth.no_user_interaction</quote> option or %NULL.
 * @message: The message to convey (use N_).
 * @invocation: The invocation to check for.
 *
 * Checks if the caller represented by @invocation is authorized for
 * the action identified by @action_id, optionally displaying @message
 * if authentication is needed. Additionally, if the caller is not
 * authorized, the appropriate error is already returned to the caller
 * via @invocation.
 *
 * The calling thread is blocked for the duration of the authorization
 * check which could be a very long time since it may involve
 * presenting an authentication dialog and having a human user use
 * it. If <quote>auth.no_user_interaction</quote> in @options is %TRUE
 * no authentication dialog will be presented and the check is not
 * expected to take a long time.
 *
 * See <xref linkend="udisks-polkit-details"/> for the variables that
 * can be used in @message but note that not all variables can be used
 * in all checks. For example, any check involving a #UDisksDrive or a
 * #UDisksBlock object can safely include the fragment
 * <quote>$(drive)</quote> since it will always expand to the name of
 * the drive, e.g. <quote>INTEL SSDSA2MH080G1GC (/dev/sda1)</quote> or
 * the block device file e.g. <quote>/dev/vg_lucifer/lv_root</quote>
 * or <quote>/dev/sda1</quote>. However this won't work for operations
 * that isn't on a drive or block device, for example calls on the
 * <link linkend="gdbus-interface-org-freedesktop-UDisks2-Manager.top_of_page">Manager</link>
 * object.
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
  GError *error = NULL;
  if (!udisks_daemon_util_check_authorization_sync_with_error (daemon,
                                                               object,
                                                               action_id,
                                                               options,
                                                               message,
                                                               invocation,
                                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return FALSE;
    }

  return TRUE;
}

gboolean
udisks_daemon_util_check_authorization_sync_with_error (UDisksDaemon           *daemon,
                                                        UDisksObject           *object,
                                                        const gchar            *action_id,
                                                        GVariant               *options,
                                                        const gchar            *message,
                                                        GDBusMethodInvocation  *invocation,
                                                        GError                **error)
{
  PolkitAuthority *authority = NULL;
  PolkitSubject *subject = NULL;
  PolkitDetails *details = NULL;
  PolkitCheckAuthorizationFlags flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE;
  PolkitAuthorizationResult *result = NULL;
  GError *sub_error = NULL;
  gboolean ret = FALSE;
  UDisksBlock *block = NULL;
  UDisksDrive *drive = NULL;
  UDisksPartition *partition = NULL;
  UDisksObject *block_object = NULL;
  UDisksObject *drive_object = NULL;
  gboolean auth_no_user_interaction = FALSE;
  const gchar *details_device = NULL;
  gchar *details_drive = NULL;

  authority = udisks_daemon_get_authority (daemon);
  if (authority == NULL)
    {
      ret = check_authorization_no_polkit (daemon, object, action_id, options, message, invocation, error);
      goto out;
    }

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
  polkit_details_insert (details, "polkit.gettext_domain", "udisks2");

  /* Find drive associated with the block device, if any */
  if (object != NULL)
    {
      block = udisks_object_get_block (object);
      if (block != NULL)
        {
          block_object = g_object_ref (object);
          drive_object = udisks_daemon_find_object (daemon, udisks_block_get_drive (block));
          if (drive_object != NULL)
            drive = udisks_object_get_drive (drive_object);
        }

      partition = udisks_object_get_partition (object);

      if (drive == NULL)
        drive = udisks_object_get_drive (object);
    }

  if (block != NULL)
    details_device = udisks_block_get_preferred_device (block);

  /* If we have a drive, use vendor/model in the message (in addition to Block:preferred-device) */
  if (drive != NULL)
    {
      gchar *s;
      const gchar *vendor;
      const gchar *model;

      vendor = udisks_drive_get_vendor (drive);
      model = udisks_drive_get_model (drive);
      if (vendor == NULL)
        vendor = "";
      if (model == NULL)
        model = "";

      if (strlen (vendor) > 0 && strlen (model) > 0)
        s = g_strdup_printf ("%s %s", vendor, model);
      else if (strlen (vendor) > 0)
        s = g_strdup (vendor);
      else
        s = g_strdup (model);

      if (block != NULL)
        {
          details_drive = g_strdup_printf ("%s (%s)", s, udisks_block_get_preferred_device (block));
        }
      else
        {
          details_drive = s;
          s = NULL;
        }
      g_free (s);

      _safe_polkit_details_insert (details, "drive.wwn", udisks_drive_get_wwn (drive));
      _safe_polkit_details_insert (details, "drive.serial", udisks_drive_get_serial (drive));
      _safe_polkit_details_insert (details, "drive.vendor", udisks_drive_get_vendor (drive));
      _safe_polkit_details_insert (details, "drive.model", udisks_drive_get_model (drive));
      _safe_polkit_details_insert (details, "drive.revision", udisks_drive_get_revision (drive));
      if (udisks_drive_get_removable (drive))
        {
          const gchar *const *media_compat;
          GString *media_compat_str;
          const gchar *sep = ",";

          polkit_details_insert (details, "drive.removable", "true");
          _safe_polkit_details_insert (details, "drive.removable.bus", udisks_drive_get_connection_bus (drive));

          media_compat_str = g_string_new (NULL);
          media_compat = udisks_drive_get_media_compatibility (drive);
          if (media_compat)
            {
              guint i;

              for (i = 0; media_compat[i] && strlen(media_compat[i]); i++)
                {
                  if (i)
                    g_string_append (media_compat_str, sep);
                  g_string_append (media_compat_str, media_compat[i]);
                }
            }

          _safe_polkit_details_insert (details, "drive.removable.media", media_compat_str->str);
          g_string_free (media_compat_str, TRUE);
        }
    }

  if (block != NULL)
    {
      _safe_polkit_details_insert (details, "id.type",    udisks_block_get_id_type (block));
      _safe_polkit_details_insert (details, "id.usage",   udisks_block_get_id_usage (block));
      _safe_polkit_details_insert (details, "id.version", udisks_block_get_id_version (block));
      _safe_polkit_details_insert (details, "id.label",   udisks_block_get_id_label (block));
      _safe_polkit_details_insert (details, "id.uuid",    udisks_block_get_id_uuid (block));
    }

  if (partition != NULL)
    {
      _safe_polkit_details_insert_int    (details, "partition.number", udisks_partition_get_number (partition));
      _safe_polkit_details_insert        (details, "partition.type",   udisks_partition_get_type_ (partition));
      _safe_polkit_details_insert_uint64 (details, "partition.flags",  udisks_partition_get_flags (partition));
      _safe_polkit_details_insert        (details, "partition.name",   udisks_partition_get_name (partition));
      _safe_polkit_details_insert        (details, "partition.uuid",   udisks_partition_get_uuid (partition));
    }

  /* Fall back to Block:preferred-device */
  if (details_drive == NULL && block != NULL)
    details_drive = udisks_block_dup_preferred_device (block);

  if (details_device != NULL)
    polkit_details_insert (details, "device", details_device);
  if (details_drive != NULL)
    polkit_details_insert (details, "drive", details_drive);

  sub_error = NULL;
  result = polkit_authority_check_authorization_sync (authority,
                                                      subject,
                                                      action_id,
                                                      details,
                                                      flags,
                                                      NULL, /* GCancellable* */
                                                      &sub_error);
  if (result == NULL)
    {
      if (sub_error->domain != POLKIT_ERROR)
        {
          /* assume polkit authority is not available (e.g. could be the service
           * manager returning org.freedesktop.systemd1.Masked)
           */
          g_clear_error (&sub_error);
          ret = check_authorization_no_polkit (daemon, object, action_id, options, message, invocation, error);
        }
      else
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Error checking authorization: %s (%s, %d)",
                       sub_error->message,
                       g_quark_to_string (sub_error->domain),
                       sub_error->code);
          g_clear_error (&sub_error);
        }
      goto out;
    }
  if (!polkit_authorization_result_get_is_authorized (result))
    {
      if (polkit_authorization_result_get_dismissed (result))
        g_set_error (error,
                     UDISKS_ERROR,
                     UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED,
                     "The authentication dialog was dismissed");
      else
        g_set_error (error,
                     UDISKS_ERROR,
                     polkit_authorization_result_get_is_challenge (result) ?
                     UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN :
                     UDISKS_ERROR_NOT_AUTHORIZED,
                     "Not authorized to perform operation");
      goto out;
    }

  ret = TRUE;

 out:
  g_free (details_drive);
  g_clear_object (&block_object);
  g_clear_object (&drive_object);
  g_clear_object (&block);
  g_clear_object (&partition);
  g_clear_object (&drive);
  g_clear_object (&subject);
  g_clear_object (&details);
  g_clear_object (&result);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
dbus_freedesktop_guint32_get (GDBusMethodInvocation   *invocation,
                              GCancellable            *cancellable,
                              const gchar             *method,
                              guint32                 *out_value,
                              GError                 **error)
{
  gboolean ret = FALSE;
  GError *local_error = NULL;
  GVariant *value;
  guint32 fetched = 0;
  const gchar *caller = g_dbus_method_invocation_get_sender (invocation);


  value = g_dbus_connection_call_sync (g_dbus_method_invocation_get_connection (invocation),
                                       "org.freedesktop.DBus",  /* bus name */
                                       "/org/freedesktop/DBus", /* object path */
                                       "org.freedesktop.DBus",  /* interface */
                                       method, /* method */
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
      g_clear_error (&local_error);
      goto out;
    }

  {
    G_STATIC_ASSERT (sizeof (uid_t) == sizeof (guint32));
    G_STATIC_ASSERT (sizeof (pid_t) == sizeof (guint32));
  }

  g_variant_get (value, "(u)", &fetched);
  if (out_value != NULL)
    *out_value = fetched;

  g_variant_unref (value);
  ret = TRUE;
out:
  return ret;
}


/**
 * udisks_daemon_util_get_caller_uid_sync:
 * @daemon: A #UDisksDaemon.
 * @invocation: A #GDBusMethodInvocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @out_uid: (out): Return location for resolved uid or %NULL.
 * @out_gid: (out) (allow-none): Return location for resolved gid or %NULL.
 * @out_user_name: (out) (allow-none): Return location for resolved user name or %NULL.
 * @error: Return location for error.
 *
 * Gets the UNIX user id (and possibly group id and user name) of the
 * peer represented by @invocation.
 *
 * Returns: %TRUE if the user id (and possibly group id) was obtained, %FALSE otherwise
 */
gboolean
udisks_daemon_util_get_caller_uid_sync (UDisksDaemon            *daemon,
                                        GDBusMethodInvocation   *invocation,
                                        GCancellable            *cancellable,
                                        uid_t                   *out_uid,
                                        gid_t                   *out_gid,
                                        gchar                  **out_user_name,
                                        GError                 **error)
{
  gboolean ret;
  uid_t uid;

  /* TODO: cache this on @daemon */

  ret = FALSE;

  if (!dbus_freedesktop_guint32_get (invocation, cancellable,
                                     "GetConnectionUnixUser",
                                     &uid, error))
    {
      goto out;
    }

  if (out_uid != NULL)
    *out_uid = uid;

  if (out_gid != NULL || out_user_name != NULL)
    {
      struct passwd pwstruct;
      gchar pwbuf[8192];
      struct passwd *pw = NULL;
      int rc;

      rc = getpwuid_r (uid, &pwstruct, pwbuf, sizeof pwbuf, &pw);
      if (rc == 0 && pw == NULL)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "User with uid %d does not exist", (gint) uid);
          goto out;
        }
      else if (pw == NULL)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Error looking up passwd struct for uid %d: %m", (gint) uid);
          goto out;
        }
      if (out_gid != NULL)
        *out_gid = pw->pw_gid;
      if (out_user_name != NULL)
        *out_user_name = g_strdup (pwstruct.pw_name);
    }

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_daemon_util_get_caller_pid_sync:
 * @daemon: A #UDisksDaemon.
 * @invocation: A #GDBusMethodInvocation.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @out_pid: (out): Return location for resolved pid or %NULL.
 * @error: Return location for error.
 *
 * Gets the UNIX process id of the peer represented by @invocation.
 *
 * Returns: %TRUE if the process id was obtained, %FALSE otherwise
 */
gboolean
udisks_daemon_util_get_caller_pid_sync (UDisksDaemon            *daemon,
                                        GDBusMethodInvocation   *invocation,
                                        GCancellable            *cancellable,
                                        pid_t                   *out_pid,
                                        GError                 **error)
{
  // "GetConnectionUnixProcessID"

  /* TODO: cache this on @daemon */
  /* NOTE: pid_t is a signed 32 bit, but the
   * GetConnectionUnixProcessID dbus method returns an unsigned */

  return dbus_freedesktop_guint32_get (invocation, cancellable,
                                       "GetConnectionUnixProcessID",
                                       (guint32*)(out_pid), error);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_daemon_util_dup_object:
 * @interface_: (type GDBusInterface): A #GDBusInterface<!-- -->-derived instance.
 * @error: %NULL, or an unset #GError to set if the return value is %NULL.
 *
 * Gets the enclosing #UDisksObject for @interface, if any.
 *
 * Returns: (transfer full) (type UDisksObject): Either %NULL or a
 * #UDisksObject<!-- -->-derived instance that must be released with
 * g_object_unref().
 */
gpointer
udisks_daemon_util_dup_object (gpointer   interface_,
                               GError   **error)
{
  gpointer ret;

  g_return_val_if_fail (G_IS_DBUS_INTERFACE (interface_), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = g_dbus_interface_dup_object (interface_);
  if (ret == NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "No enclosing object for interface");
    }

  return ret;
}

static void
escaper (GString *s, const gchar *str)
{
  const gchar *p;
  for (p = str; *p != '\0'; p++)
    {
      gint c = *p;
      switch (c)
        {
        case '"':
          g_string_append (s, "\\\"");
          break;

        case '\\':
          g_string_append (s, "\\\\");
          break;

        default:
          g_string_append_c (s, c);
          break;
        }
    }
}

/**
 * udisks_daemon_util_escape_and_quote:
 * @str: The string to escape.
 *
 * Like udisks_daemon_util_escape() but also wraps the result in
 * double-quotes.
 *
 * Returns: The double-quoted and escaped string. Free with g_free().
 */
gchar *
udisks_daemon_util_escape_and_quote (const gchar *str)
{
  GString *s;

  g_return_val_if_fail (str != NULL, NULL);

  s = g_string_new ("\"");
  escaper (s, str);
  g_string_append_c (s, '"');

  return g_string_free (s, FALSE);
}

/**
 * udisks_daemon_util_escape:
 * @str: The string to escape.
 *
 * Escapes double-quotes (&quot;) and back-slashes (\) in a string
 * using back-slash (\).
 *
 * Returns: The escaped string. Free with g_free().
 */
gchar *
udisks_daemon_util_escape (const gchar *str)
{
  GString *s;

  g_return_val_if_fail (str != NULL, NULL);

  s = g_string_new (NULL);
  escaper (s, str);

  return g_string_free (s, FALSE);
}

/**
 * udisks_daemon_util_on_user_seat:
 * @daemon: A #UDisksDaemon.
 * @object: The #GDBusObject that the call is on or %NULL.
 * @user: The user to check for.
 *
 * Checks whether the device represented by @object (if any) is plugged into
 * a seat where the caller represented by @user is logged in and active.
 *
 * This works if @object is a drive or a block object.
 *
 * Returns: %TRUE if @object is on the same seat as one of @user's
 *  active sessions, %FALSE otherwise.
 */
gboolean
udisks_daemon_util_on_user_seat (UDisksDaemon *daemon,
                                 UDisksObject *object,
                                 uid_t         user)
{
#if !defined(HAVE_LIBSYSTEMD_LOGIN)
  /* if we don't have systemd, assume it's always the same seat */
  return TRUE;
#else
  gboolean ret = FALSE;
  char *session = NULL;
  char *seat = NULL;
  const gchar *drive_seat;
  UDisksObject *drive_object = NULL;
  UDisksDrive *drive = NULL;

  /* if we don't have logind, assume it's always the same seat */
  if (!LOGIND_AVAILABLE())
    return TRUE;

  if (UDISKS_IS_LINUX_BLOCK_OBJECT (object))
    {
      UDisksLinuxBlockObject *linux_block_object;
      UDisksBlock *block;
      linux_block_object = UDISKS_LINUX_BLOCK_OBJECT (object);
      block = udisks_object_get_block (UDISKS_OBJECT (linux_block_object));
      if (block != NULL)
        {
          drive_object = udisks_daemon_find_object (daemon, udisks_block_get_drive (block));
          g_object_unref (block);
        }
    }
  else if (UDISKS_IS_LINUX_DRIVE_OBJECT (object))
    {
      drive_object = g_object_ref (object);
    }

  if (drive_object == NULL)
    goto out;

  drive = udisks_object_get_drive (UDISKS_OBJECT (drive_object));
  if (drive == NULL)
    goto out;

  drive_seat = udisks_drive_get_seat (drive);

  if (drive_seat != NULL && sd_uid_is_on_seat (user, TRUE, drive_seat) > 0)
    {
      ret = TRUE;
      goto out;
    }

 out:
  free (seat);
  free (session);
  g_clear_object (&drive_object);
  g_clear_object (&drive);
  return ret;
#endif /* HAVE_LIBSYSTEMD_LOGIN */
}

/**
 * udisks_daemon_util_hexdump:
 * @data: Pointer to data.
 * @len: Length of data.
 *
 * Utility function to generate a hexadecimal representation of @len
 * bytes of @data.
 *
 * Returns: A multi-line string. Free with g_free() when done using it.
 */
gchar *
udisks_daemon_util_hexdump (gconstpointer data, gsize len)
{
  const guchar *bdata = data;
  guint n, m;
  GString *ret;

  ret = g_string_new (NULL);
  for (n = 0; n < len; n += 16)
    {
      g_string_append_printf (ret, "%04x: ", n);

      for (m = n; m < n + 16; m++)
        {
          if (m > n && (m%4) == 0)
            g_string_append_c (ret, ' ');
          if (m < len)
            g_string_append_printf (ret, "%02x ", (guint) bdata[m]);
          else
            g_string_append (ret, "   ");
        }

      g_string_append (ret, "   ");

      for (m = n; m < len && m < n + 16; m++)
        g_string_append_c (ret, g_ascii_isprint (bdata[m]) ? bdata[m] : '.');

      g_string_append_c (ret, '\n');
    }

  return g_string_free (ret, FALSE);
}

/**
 * udisks_daemon_util_hexdump_debug:
 * @data: Pointer to data.
 * @len: Length of data.
 *
 * Utility function to dumps the hexadecimal representation of @len
 * bytes of @data generated with udisks_daemon_util_hexdump() using
 * udisks_debug().
 */
void
udisks_daemon_util_hexdump_debug (gconstpointer data, gsize len)
{
  gchar *s = udisks_daemon_util_hexdump (data, len);
  udisks_debug ("Hexdump of %" G_GSIZE_FORMAT " bytes:\n%s", len, s);
  g_free (s);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_daemon_util_file_set_contents:
 * @filename: (type filename): Name of a file to write @contents to, in the GLib file name encoding.
 * @contents: (array length=length) (element-type guint8): String to write to the file.
 * @contents_len: Length of @contents, or -1 if @contents is a NUL-terminated string.
 * @mode_for_new_file: Mode for new file.
 * @error: Return location for a #GError, or %NULL.
 *
 * Like g_file_set_contents() but preserves the mode of the file if it
 * already exists and sets it to @mode_for_new_file otherwise.
 *
 * Return value: %TRUE on success, %FALSE if an error occurred
 */
gboolean
udisks_daemon_util_file_set_contents (const gchar  *filename,
                                      const gchar  *contents,
                                      gssize        contents_len,
                                      gint          mode_for_new_file,
                                      GError      **error)
{
  gboolean ret;
  struct stat statbuf;
  gint mode;
  gchar *tmpl;
  gint fd;
  FILE *f;

  ret = FALSE;
  tmpl = NULL;

  if (stat (filename, &statbuf) != 0)
    {
      if (errno == ENOENT)
        {
          mode = mode_for_new_file;
        }
      else
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "Error stat(2)'ing %s: %m",
                       filename);
          goto out;
        }
    }
  else
    {
      mode = statbuf.st_mode;
    }

  tmpl = g_strdup_printf ("%s.XXXXXX", filename);
  fd = g_mkstemp_full (tmpl, O_RDWR, mode);
  if (fd == -1)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error creating temporary file: %m");
      goto out;
    }

  f = fdopen (fd, "w");
  if (f == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error calling fdopen: %m");
      g_unlink (tmpl);
      goto out;
    }

  if (contents_len < 0 )
    contents_len = strlen (contents);
  if (fwrite (contents, 1, contents_len, f) != (gsize) contents_len)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error calling fwrite on temp file: %m");
      fclose (f);
      g_unlink (tmpl);
      goto out;
    }

  if (fsync (fileno (f)) != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error calling fsync on temp file: %m");
      fclose (f);
      g_unlink (tmpl);
      goto out;
    }
  fclose (f);

  if (rename (tmpl, filename) != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error renaming temp file to final file: %m");
      g_unlink (tmpl);
      goto out;
    }

  ret = TRUE;

 out:
  g_free (tmpl);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * UDisksInhibitCookie:
 *
 * Opaque data structure used in udisks_daemon_util_inhibit_system_sync() and
 * udisks_daemon_util_uninhibit_system_sync().
 */
struct UDisksInhibitCookie
{
  /*< private >*/
  guint32 magic;
#ifdef HAVE_LIBSYSTEMD_LOGIN
  gint fd;
#endif
};

/**
 * udisks_daemon_util_inhibit_system_sync:
 * @reason: A human readable explanation of why the system is being inhibited.
 *
 * Tries to inhibit the system.
 *
 * Right now only
 * <ulink url="http://www.freedesktop.org/wiki/Software/systemd/inhibit">systemd</ulink>
 * inhibitors are supported but other inhibitors can be added in the future.
 *
 * Returns: A cookie that can be used with udisks_daemon_util_uninhibit_system_sync().
 */
UDisksInhibitCookie *
udisks_daemon_util_inhibit_system_sync (const gchar  *reason)
{
#ifdef HAVE_LIBSYSTEMD_LOGIN
  UDisksInhibitCookie *ret = NULL;
  GDBusConnection *connection = NULL;
  GVariant *value = NULL;
  GUnixFDList *fd_list = NULL;
  gint32 index = -1;
  GError *error = NULL;

  g_return_val_if_fail (reason != NULL, NULL);

  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (connection == NULL)
    {
      udisks_critical ("Error getting system bus: %s (%s, %d)",
                    error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      goto out;
    }

  value = g_dbus_connection_call_with_unix_fd_list_sync (connection,
                                                         "org.freedesktop.login1",
                                                         "/org/freedesktop/login1",
                                                         "org.freedesktop.login1.Manager",
                                                         "Inhibit",
                                                         g_variant_new ("(ssss)",
                                                                        "sleep:shutdown:idle", /* what */
                                                                        "Disk Manager",        /* who */
                                                                        reason,                /* why */
                                                                        "block"),              /* mode */
                                                         G_VARIANT_TYPE ("(h)"),
                                                         G_DBUS_CALL_FLAGS_NONE,
                                                         -1,       /* default timeout */
                                                         NULL,     /* fd_list */
                                                         &fd_list, /* out_fd_list */
                                                         NULL, /* GCancellable */
                                                         &error);
  if (value == NULL)
    {
      udisks_critical ("Error inhibiting: %s (%s, %d)",
                    error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      goto out;
    }

  g_variant_get (value, "(h)", &index);
  g_assert (index >= 0 && index < g_unix_fd_list_get_length (fd_list));

  ret = g_new0 (UDisksInhibitCookie, 1);
  ret->magic = 0xdeadbeef;
  ret->fd = g_unix_fd_list_get (fd_list, index, &error);
  if (ret->fd == -1)
    {
      udisks_critical ("Error getting fd: %s (%s, %d)",
                    error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      g_free (ret);
      ret = NULL;
      goto out;
    }

 out:
  if (value != NULL)
    g_variant_unref (value);
  g_clear_object (&fd_list);
  g_clear_object (&connection);
  return ret;
#else
  /* non-systemd: just return a dummy pointer */
  g_return_val_if_fail (reason != NULL, NULL);
  return (UDisksInhibitCookie* ) &udisks_daemon_util_inhibit_system_sync;
#endif
}

/**
 * udisks_daemon_util_uninhibit_system_sync:
 * @cookie: %NULL or a cookie obtained from udisks_daemon_util_inhibit_system_sync().
 *
 * Does nothing if @cookie is %NULL, otherwise uninhibits.
 */
void
udisks_daemon_util_uninhibit_system_sync (UDisksInhibitCookie *cookie)
{
#ifdef HAVE_LIBSYSTEMD_LOGIN
  if (cookie != NULL)
    {
      g_assert (cookie->magic == 0xdeadbeef);
      if (close (cookie->fd) != 0)
        {
          udisks_critical ("Error closing inhbit-fd: %m");
        }
      g_free (cookie);
    }
#else
  /* non-systemd: check dummy pointer */
  g_warn_if_fail (cookie == (UDisksInhibitCookie* ) &udisks_daemon_util_inhibit_system_sync);
#endif
}

/**
 * udisks_daemon_util_get_free_mdraid_device:
 *
 * Gets a free MD RAID device.
 *
 * Returns: A string of the form "/dev/mdNNN" that should be freed
 * with g_free() or %NULL if no free device is available.
 */
gchar *
udisks_daemon_util_get_free_mdraid_device (void)
{
  gchar *ret = NULL;
  gint n;
  gchar buf[PATH_MAX];

  /* Ideally we wouldn't need this racy function... but mdadm(8)
   * insists that the user chooses a name. It should just choose one
   * itself but that's not how things work right now.
   */
  for (n = 127; n >= 0; n--)
    {
      snprintf (buf, sizeof buf, "/sys/block/md%d", n);
      if (!g_file_test (buf, G_FILE_TEST_EXISTS))
        {
          ret = g_strdup_printf ("/dev/md%d", n);
          goto out;
        }
    }

 out:
  return ret;
}


/**
 * udisks_ata_identify_get_word:
 * @identify_data: (allow-none): A 512-byte array containing ATA IDENTIFY or ATA IDENTIFY PACKET DEVICE data or %NULL.
 * @word_number: The word number to get - must be less than 256.
 *
 * Gets a <quote>word</quote> from position @word_number from
 * @identify_data.
 *
 * Returns: The word at the specified position or 0 if @identify_data is %NULL.
 */
guint16
udisks_ata_identify_get_word (const guchar *identify_data, guint word_number)
{
  const guint16 *words = (const guint16 *) identify_data;
  guint16 ret = 0;

  g_return_val_if_fail (word_number < 256, 0);

  if (identify_data == NULL)
    goto out;

  ret = GUINT16_FROM_LE (words[word_number]);

 out:
  return ret;
}
