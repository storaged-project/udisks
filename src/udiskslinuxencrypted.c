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

#include "udiskslogging.h"
#include "udiskslinuxencrypted.h"
#include "udiskslinuxencryptedhelpers.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksstate.h"
#include "udiskslinuxdevice.h"
#include "udiskslinuxblock.h"
#include "udisksfstabentry.h"
#include "udisksfstabmonitor.h"
#include "udiskscrypttabentry.h"
#include "udiskscrypttabmonitor.h"
#include "udisksspawnedjob.h"

/**
 * SECTION:udiskslinuxencrypted
 * @title: UDisksLinuxEncrypted
 * @short_description: Linux implementation of #UDisksEncrypted
 *
 * This type provides an implementation of the #UDisksEncrypted
 * interface on Linux.
 */

typedef struct _UDisksLinuxEncryptedClass   UDisksLinuxEncryptedClass;

/**
 * UDisksLinuxEncrypted:
 *
 * The #UDisksLinuxEncrypted structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxEncrypted
{
  UDisksEncryptedSkeleton parent_instance;
};

struct _UDisksLinuxEncryptedClass
{
  UDisksEncryptedSkeletonClass parent_class;
};

static void encrypted_iface_init (UDisksEncryptedIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxEncrypted, udisks_linux_encrypted, UDISKS_TYPE_ENCRYPTED_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_ENCRYPTED, encrypted_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_encrypted_init (UDisksLinuxEncrypted *encrypted)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (encrypted),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_encrypted_class_init (UDisksLinuxEncryptedClass *klass)
{
}

/**
 * udisks_linux_encrypted_new:
 *
 * Creates a new #UDisksLinuxEncrypted instance.
 *
 * Returns: A new #UDisksLinuxEncrypted. Free with g_object_unref().
 */
UDisksEncrypted *
udisks_linux_encrypted_new (void)
{
  return UDISKS_ENCRYPTED (g_object_new (UDISKS_TYPE_LINUX_ENCRYPTED,
                                         NULL));
}
/* ---------------------------------------------------------------------------------------------------- */

static void
update_child_configuration (UDisksLinuxEncrypted   *encrypted,
                            UDisksLinuxBlockObject *object)
{
  UDisksDaemon *daemon = udisks_linux_block_object_get_daemon (object);
  UDisksBlock *block = udisks_object_peek_block (UDISKS_OBJECT (object));

  udisks_encrypted_set_child_configuration
    (UDISKS_ENCRYPTED (encrypted),
     udisks_linux_find_child_configuration (daemon,
                                            udisks_block_get_id_uuid (block)));
}

/**
 * udisks_linux_encrypted_update:
 * @encrypted: A #UDisksLinuxEncrypted.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_encrypted_update (UDisksLinuxEncrypted   *encrypted,
                               UDisksLinuxBlockObject *object)
{
  update_child_configuration (encrypted, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksObject *
wait_for_cleartext_object (UDisksDaemon *daemon,
                           gpointer      user_data)
{
  const gchar *crypto_object_path = user_data;
  UDisksObject *ret = NULL;
  GList *objects, *l;

  objects = udisks_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_get_block (object);
      if (block != NULL)
        {
          if (g_strcmp0 (udisks_block_get_crypto_backing_device (block), crypto_object_path) == 0)
            {
              g_object_unref (block);
              ret = g_object_ref (object);
              goto out;
            }
          g_object_unref (block);
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
check_crypttab (UDisksBlock  *block,
                gboolean      load_passphrase,
                gboolean     *out_found,
                gchar       **out_name,
                gchar       **out_passphrase,
                gchar       **out_options,
                GError      **error)
{
  gboolean ret = FALSE;
  GVariantIter iter;
  const gchar *type;
  GVariant *details;

  g_variant_iter_init (&iter, udisks_block_get_configuration (block));
  while (g_variant_iter_next (&iter, "(&s@a{sv})", &type, &details))
    {
      if (g_strcmp0 (type, "crypttab") == 0)
        {
          const gchar *passphrase_path;
          if (out_found != NULL)
            *out_found = TRUE;
          g_variant_lookup (details, "name", "^ay", out_name);
          g_variant_lookup (details, "options", "^ay", out_options);
          if (g_variant_lookup (details, "passphrase-path", "^&ay", &passphrase_path) &&
              strlen (passphrase_path) > 0 &&
              !g_str_has_prefix (passphrase_path, "/dev"))
            {
              if (load_passphrase)
                {
                  if (!g_file_get_contents (passphrase_path,
                                            out_passphrase,
                                            NULL,
                                            error))
                    {
                      g_variant_unref (details);
                      goto out;
                    }
                }
            }
          ret = TRUE;
          g_variant_unref (details);
          goto out;
        }
      g_variant_unref (details);
    }

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
has_option (const gchar *options,
            const gchar *option)
{
  gboolean ret = FALSE;
  gchar **tokens;
  guint n;

  tokens = g_strsplit (options, ",", -1);
  for (n = 0; tokens != NULL && tokens[n] != NULL; n++)
    {
      if (g_strcmp0 (tokens[n], option) == 0)
        {
          ret = TRUE;
          goto out;
        }
    }
  g_strfreev (tokens);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_unlock (UDisksEncrypted        *encrypted,
               GDBusMethodInvocation  *invocation,
               const gchar            *passphrase,
               GVariant               *options)
{
  UDisksObject *object = NULL;
  UDisksBlock *block;
  UDisksDaemon *daemon;
  UDisksState *state;
  gchar *name = NULL;
  UDisksObject *cleartext_object = NULL;
  UDisksBlock *cleartext_block;
  UDisksLinuxDevice *cleartext_device = NULL;
  GError *error = NULL;
  uid_t caller_uid;
  const gchar *action_id;
  const gchar *message;
  gboolean is_in_crypttab = FALSE;
  gchar *crypttab_name = NULL;
  gchar *crypttab_passphrase = NULL;
  gchar *crypttab_options = NULL;
  gchar *device = NULL;
  gboolean read_only = FALSE;
  GString *effective_passphrase = NULL;
  LuksJobData data;

  object = udisks_daemon_util_dup_object (encrypted, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = udisks_object_peek_block (object);
  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);

  /* TODO: check if the device is mentioned in /etc/crypttab (see crypttab(5)) - if so use that
   *
   *       Of course cryptsetup(8) don't support that, see https://bugzilla.redhat.com/show_bug.cgi?id=692258
   */

  /* Fail if the device is not a LUKS device */
  if (!(g_strcmp0 (udisks_block_get_id_usage (block), "crypto") == 0 &&
        g_strcmp0 (udisks_block_get_id_type (block), "crypto_LUKS") == 0))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Device %s does not appear to be a LUKS device",
                                             udisks_block_get_device (block));
      goto out;
    }

  /* Fail if device is already unlocked */
  cleartext_object = udisks_daemon_wait_for_object_sync (daemon,
                                                         wait_for_cleartext_object,
                                                         g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object))),
                                                         g_free,
                                                         0, /* timeout_seconds */
                                                         NULL); /* error */
  if (cleartext_object != NULL)
    {
      UDisksBlock *unlocked_block;
      unlocked_block = udisks_object_peek_block (cleartext_object);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Device %s is already unlocked as %s",
                                             udisks_block_get_device (block),
                                             udisks_block_get_device (unlocked_block));
      goto out;
    }

  /* we need the uid of the caller for the unlocked-luks file */
  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* fallback mechanism: keyfile_contents -> passphrase -> crypttab_passphrase -> error (no key) */
  if (!udisks_variant_lookup_binary (options, "keyfile_contents", &effective_passphrase)) {
    if (passphrase && (strlen (passphrase) > 0))
      {
        effective_passphrase = g_string_new (passphrase);
      }
    else
      {
        /* check if in crypttab file */
        error = NULL;
        if (!check_crypttab (block,
                             TRUE,
                             &is_in_crypttab,
                             &crypttab_name,
                             &crypttab_passphrase,
                             &crypttab_options,
                             &error))
          {
            g_dbus_method_invocation_take_error (invocation, error);
            goto out;
          }
        if (is_in_crypttab && crypttab_passphrase != NULL && strlen (crypttab_passphrase) > 0)
          {
            effective_passphrase = g_string_new (crypttab_passphrase);
          }
        else
          {
            g_dbus_method_invocation_return_error (invocation,
                                                   UDISKS_ERROR,
                                                   UDISKS_ERROR_FAILED,
                                                   "No key available to unlock device %s",
                                                   udisks_block_get_device (block));
            goto out;
          }
      }
  }

  /* Now, check that the user is actually authorized to unlock the device.
   */
  action_id = "org.freedesktop.udisks2.encrypted-unlock";
  /* Translators: Shown in authentication dialog when the user
   * requests unlocking an encrypted device.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to unlock the encrypted device $(drive)");
  if (!udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (is_in_crypttab && has_option (crypttab_options, "x-udisks-auth"))
        {
          action_id = "org.freedesktop.udisks2.encrypted-unlock-crypttab";
        }
      else if (udisks_block_get_hint_system (block))
        {
          action_id = "org.freedesktop.udisks2.encrypted-unlock-system";
        }
      else if (!udisks_daemon_util_on_user_seat (daemon, object, caller_uid))
        {
          action_id = "org.freedesktop.udisks2.encrypted-unlock-other-seat";
        }
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  /* calculate the name to use */
  if (is_in_crypttab && crypttab_name != NULL)
    name = g_strdup (crypttab_name);
  else
    name = g_strdup_printf ("luks-%s", udisks_block_get_id_uuid (block));

  device = udisks_block_dup_device (block);

  /* TODO: support reading a 'readonly' option from @options */
  if (udisks_block_get_read_only (block))
    read_only = TRUE;

  data.device = device;
  data.map_name = name;
  data.passphrase = effective_passphrase;
  data.read_only = read_only;

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               object,
                                               "encrypted-unlock",
                                               caller_uid,
                                               luks_open_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* cancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error unlocking %s: %s",
                                             udisks_block_get_device (block),
                                             error->message);
      goto out;
    }

  /* Determine the resulting cleartext object */
  error = NULL;
  cleartext_object = udisks_daemon_wait_for_object_sync (daemon,
                                                         wait_for_cleartext_object,
                                                         g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object))),
                                                         g_free,
                                                         10, /* timeout_seconds */
                                                         &error);
  if (cleartext_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for cleartext object after unlocking %s",
                      udisks_block_get_device (block));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  cleartext_block = udisks_object_peek_block (cleartext_object);

  udisks_notice ("Unlocked LUKS device %s as %s",
                 udisks_block_get_device (block),
                 udisks_block_get_device (cleartext_block));

  cleartext_device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (cleartext_object));

  /* update the unlocked-luks file */
  udisks_state_add_unlocked_luks (state,
                                  udisks_block_get_device_number (cleartext_block),
                                  udisks_block_get_device_number (block),
                                  g_udev_device_get_sysfs_attr (cleartext_device->udev_device, "dm/uuid"),
                                  caller_uid);

  udisks_encrypted_complete_unlock (encrypted,
                                    invocation,
                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (cleartext_object)));

 out:
  g_free (device);
  g_free (crypttab_name);
  g_free (crypttab_passphrase);
  g_free (crypttab_options);
  g_free (name);
  g_clear_object (&cleartext_device);
  g_clear_object (&cleartext_object);
  g_clear_object (&object);
  udisks_string_wipe_and_free (effective_passphrase);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
udisks_linux_encrypted_lock (UDisksLinuxEncrypted   *encrypted,
                             GDBusMethodInvocation  *invocation,
                             GVariant               *options,
                             GError                **error)
{
  UDisksObject *object = NULL;
  UDisksBlock *block = NULL;
  UDisksDaemon *daemon = NULL;
  UDisksState *state = NULL;
  UDisksObject *cleartext_object = NULL;
  UDisksBlock *cleartext_block = NULL;
  UDisksLinuxDevice *device = NULL;
  uid_t unlocked_by_uid;
  dev_t cleartext_device_from_file;
  uid_t caller_uid;
  gboolean ret;
  LuksJobData data;
  GError *loc_error = NULL;
  gchar *cleartext_path = NULL;

  object = udisks_daemon_util_dup_object (encrypted, error);
  if (object == NULL)
    {
      ret = FALSE;
      goto out;
    }

  block = udisks_object_peek_block (object);
  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);

  /* TODO: check if the device is mentioned in /etc/crypttab (see crypttab(5)) - if so use that
   *
   *       Of course cryptsetup(8) don't support that, see https://bugzilla.redhat.com/show_bug.cgi?id=692258
   */

  /* Fail if the device is not a LUKS device */
  if (!(g_strcmp0 (udisks_block_get_id_usage (block), "crypto") == 0 &&
        g_strcmp0 (udisks_block_get_id_type (block), "crypto_LUKS") == 0))
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Device %s does not appear to be a LUKS device",
                   udisks_block_get_device (block));
      ret = FALSE;
      goto out;
    }

  /* Fail if device is not unlocked */
  cleartext_object = udisks_daemon_wait_for_object_sync (daemon,
                                                         wait_for_cleartext_object,
                                                         g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object))),
                                                         g_free,
                                                         0, /* timeout_seconds */
                                                         NULL); /* error */
  if (cleartext_object == NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Device %s is not unlocked",
                   udisks_block_get_device (block));
      ret = FALSE;
      goto out;
    }
  cleartext_block = udisks_object_peek_block (cleartext_object);

  cleartext_device_from_file = udisks_state_find_unlocked_luks (state,
                                                                udisks_block_get_device_number (block),
                                                                &unlocked_by_uid);
  if (cleartext_device_from_file == 0)
    {
      /* allow locking stuff not mentioned in unlocked-luks, but treat it like root unlocked it */
      unlocked_by_uid = 0;
    }

  /* we need the uid of the caller to check authorization */
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               NULL,
                                               NULL,
                                               error))
    {
      ret = FALSE;
      goto out;
    }

  /* Check that the user is authorized to lock the device - if he
   * already unlocked it, he is implicitly authorized...
   */
  if (caller_uid != 0 && (caller_uid != unlocked_by_uid))
    {
      if (!udisks_daemon_util_check_authorization_sync_with_error (daemon,
                                                                   object,
                                                                   "org.freedesktop.udisks2.encrypted-lock-others",
                                                                   options,
                                                                   /* Translators: Shown in authentication dialog when the user
                                                                    * requests locking an encrypted device that was previously.
                                                                    * unlocked by another user.
                                                                    *
                                                                    * Do not translate $(drive), it's a placeholder and
                                                                    * will be replaced by the name of the drive/device in question
                                                                    */
                                                                   N_("Authentication is required to lock the encrypted device $(drive) unlocked by another user"),
                                                                   invocation,
                                                                   error))
        {
          ret = FALSE;
          goto out;
        }
    }

  device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (cleartext_object));
  data.map_name = g_udev_device_get_sysfs_attr (device->udev_device, "dm/name");

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               object,
                                               "encrypted-lock",
                                               caller_uid,
                                               luks_close_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* cancellable */
                                               &loc_error))
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error locking %s (%s): %s",
                   udisks_block_get_device (cleartext_block),
                   udisks_block_get_device (block),
                   loc_error->message);
      ret = FALSE;
      g_clear_error (&loc_error);
      goto out;
    }

  cleartext_path = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  if (! udisks_daemon_wait_for_object_to_disappear_sync (daemon,
                                                         wait_for_cleartext_object,
                                                         cleartext_path,
                                                         NULL,
                                                         10,
                                                         &loc_error))
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error waiting for cleartext object to disappear after locking the LUKS device: %s",
                   loc_error->message);
      g_clear_error (&loc_error);
      ret = FALSE;
      goto out;
    }

  udisks_notice ("Locked LUKS device %s (was unlocked as %s)",
                 udisks_block_get_device (block),
                 udisks_block_get_device (cleartext_block));
  ret = TRUE;

 out:
  if (device != NULL)
    g_object_unref (device);
  if (cleartext_object != NULL)
    g_object_unref (cleartext_object);
  g_clear_object (&object);
  g_free (cleartext_path);

  return ret;
}

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_lock (UDisksEncrypted        *encrypted,
             GDBusMethodInvocation  *invocation,
             GVariant               *options)
{
  GError *error = NULL;

  if (!udisks_linux_encrypted_lock (UDISKS_LINUX_ENCRYPTED (encrypted), invocation, options, &error))
    g_dbus_method_invocation_take_error (invocation, error);
  else
    udisks_encrypted_complete_lock (encrypted, invocation);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_change_passphrase (UDisksEncrypted        *encrypted,
                          GDBusMethodInvocation  *invocation,
                          const gchar            *passphrase,
                          const gchar            *new_passphrase,
                          GVariant               *options)
{
  UDisksObject *object = NULL;
  UDisksBlock *block;
  UDisksDaemon *daemon;
  uid_t caller_uid;
  const gchar *action_id;
  GError *error = NULL;
  gchar *device = NULL;
  LuksJobData data;

  object = udisks_daemon_util_dup_object (encrypted, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = udisks_object_peek_block (object);
  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));

  /* TODO: check if the device is mentioned in /etc/crypttab (see crypttab(5)) - if so use that
   *
   *       Of course cryptsetup(8) don't support that, see https://bugzilla.redhat.com/show_bug.cgi?id=692258
   */

  /* Fail if the device is not a LUKS device */
  if (!(g_strcmp0 (udisks_block_get_id_usage (block), "crypto") == 0 &&
        g_strcmp0 (udisks_block_get_id_type (block), "crypto_LUKS") == 0))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Device %s does not appear to be a LUKS device",
                                             udisks_block_get_device (block));
      goto out;
    }

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* Now, check that the user is actually authorized to unlock the device.
   */
  action_id = "org.freedesktop.udisks2.encrypted-change-passphrase";
  if (udisks_block_get_hint_system (block) &&
      !(udisks_daemon_util_setup_by_user (daemon, object, caller_uid)))
    action_id = "org.freedesktop.udisks2.encrypted-change-passphrase-system";
  //if (is_in_crypttab)
  //  action_id = "org.freedesktop.udisks2.encrypted-unlock-crypttab";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * requests unlocking an encrypted device.
                                                     *
                                                     * Do not translate $(drive), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to unlock the encrypted device $(drive)"),
                                                    invocation))
    goto out;

  device = udisks_block_dup_device (block);
  data.device = device;

  /* handle keyfiles */
  if (!udisks_variant_lookup_binary (options, "old_keyfile_contents",
                                     &(data.passphrase)))
    data.passphrase = g_string_new (passphrase);
  if (!udisks_variant_lookup_binary (options, "new_keyfile_contents",
                                     &(data.new_passphrase)))
    data.new_passphrase = g_string_new (new_passphrase);

  if (!udisks_daemon_launch_threaded_job_sync (daemon,
                                               object,
                                               "encrypted-modify",
                                               caller_uid,
                                               luks_change_key_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* cancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error changing passphrase on device %s: %s",
                                             udisks_block_get_device (block),
                                             error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_encrypted_complete_change_passphrase (encrypted, invocation);

 out:
  g_free (device);
  udisks_string_wipe_and_free (data.passphrase);
  udisks_string_wipe_and_free (data.new_passphrase);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
encrypted_iface_init (UDisksEncryptedIface *iface)
{
  iface->handle_unlock              = handle_unlock;
  iface->handle_lock                = handle_lock;
  iface->handle_change_passphrase   = handle_change_passphrase;
}
