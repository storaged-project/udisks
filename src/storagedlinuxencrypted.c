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

#include "storagedlogging.h"
#include "storagedlinuxencrypted.h"
#include "storagedlinuxblockobject.h"
#include "storageddaemon.h"
#include "storageddaemonutil.h"
#include "storagedstate.h"
#include "storagedlinuxdevice.h"
#include "storagedlinuxblock.h"
#include "storagedfstabentry.h"
#include "storagedfstabmonitor.h"
#include "storagedcrypttabentry.h"
#include "storagedcrypttabmonitor.h"

/**
 * SECTION:storagedlinuxencrypted
 * @title: StoragedLinuxEncrypted
 * @short_description: Linux implementation of #StoragedEncrypted
 *
 * This type provides an implementation of the #StoragedEncrypted
 * interface on Linux.
 */

typedef struct _StoragedLinuxEncryptedClass   StoragedLinuxEncryptedClass;

/**
 * StoragedLinuxEncrypted:
 *
 * The #StoragedLinuxEncrypted structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxEncrypted
{
  StoragedEncryptedSkeleton parent_instance;
};

struct _StoragedLinuxEncryptedClass
{
  StoragedEncryptedSkeletonClass parent_class;
};

static void encrypted_iface_init (StoragedEncryptedIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxEncrypted, storaged_linux_encrypted, STORAGED_TYPE_ENCRYPTED_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_ENCRYPTED, encrypted_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_encrypted_init (StoragedLinuxEncrypted *encrypted)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (encrypted),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_encrypted_class_init (StoragedLinuxEncryptedClass *klass)
{
}

/**
 * storaged_linux_encrypted_new:
 *
 * Creates a new #StoragedLinuxEncrypted instance.
 *
 * Returns: A new #StoragedLinuxEncrypted. Free with g_object_unref().
 */
StoragedEncrypted *
storaged_linux_encrypted_new (void)
{
  return STORAGED_ENCRYPTED (g_object_new (STORAGED_TYPE_LINUX_ENCRYPTED,
                                         NULL));
}
/* ---------------------------------------------------------------------------------------------------- */

static void
update_child_configuration (StoragedLinuxEncrypted   *encrypted,
                            StoragedLinuxBlockObject *object)
{
  StoragedDaemon *daemon = storaged_linux_block_object_get_daemon (object);
  StoragedBlock *block = storaged_object_peek_block (STORAGED_OBJECT (object));

  storaged_encrypted_set_child_configuration
    (STORAGED_ENCRYPTED (encrypted),
     storaged_linux_find_child_configuration (daemon,
                                              storaged_block_get_id_uuid (block)));
}

/**
 * storaged_linux_encrypted_update:
 * @encrypted: A #StoragedLinuxEncrypted.
 * @object: The enclosing #StoragedLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_encrypted_update (StoragedLinuxEncrypted   *encrypted,
                                 StoragedLinuxBlockObject *object)
{
  update_child_configuration (encrypted, object);
}

/* ---------------------------------------------------------------------------------------------------- */

static StoragedObject *
wait_for_cleartext_object (StoragedDaemon *daemon,
                           gpointer        user_data)
{
  const gchar *crypto_object_path = user_data;
  StoragedObject *ret = NULL;
  GList *objects, *l;

  objects = storaged_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;

      block = storaged_object_get_block (object);
      if (block != NULL)
        {
          if (g_strcmp0 (storaged_block_get_crypto_backing_device (block), crypto_object_path) == 0)
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
check_crypttab (StoragedBlock   *block,
                gboolean         load_passphrase,
                gboolean        *out_found,
                gchar          **out_name,
                gchar          **out_passphrase,
                gchar          **out_options,
                GError         **error)
{
  gboolean ret = FALSE;
  GVariantIter iter;
  const gchar *type;
  GVariant *details;

  g_variant_iter_init (&iter, storaged_block_get_configuration (block));
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
handle_unlock (StoragedEncrypted        *encrypted,
               GDBusMethodInvocation    *invocation,
               const gchar              *passphrase,
               GVariant                 *options)
{
  StoragedObject *object = NULL;
  StoragedBlock *block;
  StoragedDaemon *daemon;
  StoragedState *state;
  gchar *error_message = NULL;
  gchar *name = NULL;
  gchar *escaped_name = NULL;
  StoragedObject *cleartext_object = NULL;
  StoragedBlock *cleartext_block;
  StoragedLinuxDevice *cleartext_device = NULL;
  GError *error = NULL;
  uid_t caller_uid;
  const gchar *action_id;
  const gchar *message;
  gboolean is_in_crypttab = FALSE;
  gchar *crypttab_name = NULL;
  gchar *crypttab_passphrase = NULL;
  gchar *crypttab_options = NULL;
  gchar *escaped_device = NULL;
  gboolean read_only = FALSE;

  object = storaged_daemon_util_dup_object (encrypted, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = storaged_object_peek_block (object);
  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  state = storaged_daemon_get_state (daemon);

  /* TODO: check if the device is mentioned in /etc/crypttab (see crypttab(5)) - if so use that
   *
   *       Of course cryptsetup(8) don't support that, see https://bugzilla.redhat.com/show_bug.cgi?id=692258
   */

  /* Fail if the device is not a LUKS device */
  if (!(g_strcmp0 (storaged_block_get_id_usage (block), "crypto") == 0 &&
        g_strcmp0 (storaged_block_get_id_type (block), "crypto_LUKS") == 0))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Device %s does not appear to be a LUKS device",
                                             storaged_block_get_device (block));
      goto out;
    }

  /* Fail if device is already unlocked */
  cleartext_object = storaged_daemon_wait_for_object_sync (daemon,
                                                         wait_for_cleartext_object,
                                                         g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object))),
                                                         g_free,
                                                         0, /* timeout_seconds */
                                                         NULL); /* error */
  if (cleartext_object != NULL)
    {
      StoragedBlock *unlocked_block;
      unlocked_block = storaged_object_peek_block (cleartext_object);
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Device %s is already unlocked as %s",
                                             storaged_block_get_device (block),
                                             storaged_block_get_device (unlocked_block));
      goto out;
    }

  /* we need the uid of the caller for the unlocked-luks file */
  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

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

  /* Now, check that the user is actually authorized to unlock the device.
   */
  action_id = "org.storaged.Storaged.encrypted-unlock";
  /* Translators: Shown in authentication dialog when the user
   * requests unlocking an encrypted device.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to unlock the encrypted device $(drive)");
  if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (is_in_crypttab && has_option (crypttab_options, "x-storaged-auth"))
        {
          action_id = "org.storaged.Storaged.encrypted-unlock-crypttab";
        }
      else if (storaged_block_get_hint_system (block))
        {
          action_id = "org.storaged.Storaged.encrypted-unlock-system";
        }
      else if (!storaged_daemon_util_on_user_seat (daemon, object, caller_uid))
        {
          action_id = "org.storaged.Storaged.encrypted-unlock-other-seat";
        }
    }

  if (!storaged_daemon_util_check_authorization_sync (daemon,
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
    name = g_strdup_printf ("luks-%s", storaged_block_get_id_uuid (block));
  escaped_name = storaged_daemon_util_escape_and_quote (name);

  /* if available, use and prefer the /etc/crypttab passphrase */
  if (is_in_crypttab && crypttab_passphrase != NULL && strlen (crypttab_passphrase) > 0)
    {
      passphrase = crypttab_passphrase;
    }

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (block));

  /* TODO: support reading a 'readonly' option from @options */
  if (storaged_block_get_read_only (block))
    read_only = TRUE;

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                object,
                                                "encrypted-unlock", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                passphrase,  /* input_string */
                                                "cryptsetup luksOpen %s %s %s",
                                                escaped_device,
                                                escaped_name,
                                                read_only ? "--readonly" : ""))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error unlocking %s: %s",
                                             storaged_block_get_device (block),
                                             error_message);
      goto out;
    }

  /* Determine the resulting cleartext object */
  error = NULL;
  cleartext_object = storaged_daemon_wait_for_object_sync (daemon,
                                                         wait_for_cleartext_object,
                                                         g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object))),
                                                         g_free,
                                                         10, /* timeout_seconds */
                                                         &error);
  if (cleartext_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for cleartext object after unlocking %s",
                      storaged_block_get_device (block));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  cleartext_block = storaged_object_peek_block (cleartext_object);

  storaged_notice ("Unlocked LUKS device %s as %s",
                   storaged_block_get_device (block),
                   storaged_block_get_device (cleartext_block));

  cleartext_device = storaged_linux_block_object_get_device (STORAGED_LINUX_BLOCK_OBJECT (cleartext_object));

  /* update the unlocked-luks file */
  storaged_state_add_unlocked_luks (state,
                                    storaged_block_get_device_number (cleartext_block),
                                    storaged_block_get_device_number (block),
                                    g_udev_device_get_sysfs_attr (cleartext_device->udev_device, "dm/uuid"),
                                    caller_uid);

  storaged_encrypted_complete_unlock (encrypted,
                                      invocation,
                                      g_dbus_object_get_object_path (G_DBUS_OBJECT (cleartext_object)));

 out:
  g_free (escaped_device);
  g_free (crypttab_name);
  g_free (crypttab_passphrase);
  g_free (crypttab_options);
  g_free (escaped_name);
  g_free (name);
  g_free (error_message);
  g_clear_object (&cleartext_device);
  g_clear_object (&cleartext_object);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
storaged_linux_encrypted_lock (StoragedLinuxEncrypted   *encrypted,
                               GDBusMethodInvocation    *invocation,
                               GVariant                 *options,
                               GError                  **error)
{
  StoragedObject *object;
  StoragedBlock *block;
  StoragedDaemon *daemon;
  StoragedState *state;
  gchar *error_message;
  gchar *name;
  gchar *escaped_name;
  StoragedObject *cleartext_object;
  StoragedBlock *cleartext_block;
  StoragedLinuxDevice *device;
  uid_t unlocked_by_uid;
  dev_t cleartext_device_from_file;
  uid_t caller_uid;
  gboolean ret;

  object = NULL;
  daemon = NULL;
  error_message = NULL;
  name = NULL;
  escaped_name = NULL;
  cleartext_object = NULL;
  device = NULL;

  object = storaged_daemon_util_dup_object (encrypted, error);
  if (object == NULL)
    {
      ret = FALSE;
      goto out;
    }

  block = storaged_object_peek_block (object);
  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  state = storaged_daemon_get_state (daemon);

  /* TODO: check if the device is mentioned in /etc/crypttab (see crypttab(5)) - if so use that
   *
   *       Of course cryptsetup(8) don't support that, see https://bugzilla.redhat.com/show_bug.cgi?id=692258
   */

  /* Fail if the device is not a LUKS device */
  if (!(g_strcmp0 (storaged_block_get_id_usage (block), "crypto") == 0 &&
        g_strcmp0 (storaged_block_get_id_type (block), "crypto_LUKS") == 0))
    {
      g_set_error (error,
                   STORAGED_ERROR,
                   STORAGED_ERROR_FAILED,
                   "Device %s does not appear to be a LUKS device",
                   storaged_block_get_device (block));
      ret = FALSE;
      goto out;
    }

  /* Fail if device is not unlocked */
  cleartext_object = storaged_daemon_wait_for_object_sync (daemon,
                                                         wait_for_cleartext_object,
                                                         g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object))),
                                                         g_free,
                                                         0, /* timeout_seconds */
                                                         NULL); /* error */
  if (cleartext_object == NULL)
    {
      g_set_error (error,
                   STORAGED_ERROR,
                   STORAGED_ERROR_FAILED,
                   "Device %s is not unlocked",
                   storaged_block_get_device (block));
      ret = FALSE;
      goto out;
    }
  cleartext_block = storaged_object_peek_block (cleartext_object);

  cleartext_device_from_file = storaged_state_find_unlocked_luks (state,
                                                                  storaged_block_get_device_number (block),
                                                                  &unlocked_by_uid);
  if (cleartext_device_from_file == 0)
    {
      /* allow locking stuff not mentioned in unlocked-luks, but treat it like root unlocked it */
      unlocked_by_uid = 0;
    }

  /* we need the uid of the caller to check authorization */
  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
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
      if (!storaged_daemon_util_check_authorization_sync_with_error (daemon,
                                                                     object,
                                                                     "org.storaged.Storaged.encrypted-lock-others",
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

  device = storaged_linux_block_object_get_device (STORAGED_LINUX_BLOCK_OBJECT (cleartext_object));
  escaped_name = storaged_daemon_util_escape_and_quote (g_udev_device_get_sysfs_attr (device->udev_device, "dm/name"));

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                object,
                                                "encrypted-lock", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "cryptsetup luksClose %s",
                                                escaped_name))
    {
      g_set_error (error,
                   STORAGED_ERROR,
                   STORAGED_ERROR_FAILED,
                   "Error locking %s (%s): %s",
                   storaged_block_get_device (cleartext_block),
                   storaged_block_get_device (block),
                   error_message);
      ret = FALSE;
      goto out;
    }

  storaged_notice ("Locked LUKS device %s (was unlocked as %s)",
                   storaged_block_get_device (block),
                   storaged_block_get_device (cleartext_block));
  ret = TRUE;

 out:
  if (device != NULL)
    g_object_unref (device);
  g_free (escaped_name);
  g_free (name);
  g_free (error_message);
  if (cleartext_object != NULL)
    g_object_unref (cleartext_object);
  g_clear_object (&object);

  return ret;
}

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_lock (StoragedEncrypted        *encrypted,
             GDBusMethodInvocation    *invocation,
             GVariant                 *options)
{
  GError *error = NULL;

  if (!storaged_linux_encrypted_lock (STORAGED_LINUX_ENCRYPTED (encrypted), invocation, options, &error))
    g_dbus_method_invocation_take_error (invocation, error);
  else
    storaged_encrypted_complete_lock (encrypted, invocation);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_change_passphrase (StoragedEncrypted        *encrypted,
                          GDBusMethodInvocation    *invocation,
                          const gchar              *passphrase,
                          const gchar              *new_passphrase,
                          GVariant                 *options)
{
  StoragedObject *object = NULL;
  StoragedBlock *block;
  StoragedDaemon *daemon;
  gchar *error_message = NULL;
  uid_t caller_uid;
  const gchar *action_id;
  gchar *passphrases = NULL;
  GError *error = NULL;
  gchar *escaped_device = NULL;

  object = storaged_daemon_util_dup_object (encrypted, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = storaged_object_peek_block (object);
  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));

  /* TODO: check if the device is mentioned in /etc/crypttab (see crypttab(5)) - if so use that
   *
   *       Of course cryptsetup(8) don't support that, see https://bugzilla.redhat.com/show_bug.cgi?id=692258
   */

  /* Fail if the device is not a LUKS device */
  if (!(g_strcmp0 (storaged_block_get_id_usage (block), "crypto") == 0 &&
        g_strcmp0 (storaged_block_get_id_type (block), "crypto_LUKS") == 0))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Device %s does not appear to be a LUKS device",
                                             storaged_block_get_device (block));
      goto out;
    }

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Now, check that the user is actually authorized to unlock the device.
   */
  action_id = "org.storaged.Storaged.encrypted-change-passphrase";
  if (storaged_block_get_hint_system (block) &&
      !(storaged_daemon_util_setup_by_user (daemon, object, caller_uid)))
    action_id = "org.storaged.Storaged.encrypted-change-passphrase-system";
  //if (is_in_crypttab)
  //  action_id = "org.storaged.Storaged.encrypted-unlock-crypttab";
  if (!storaged_daemon_util_check_authorization_sync (daemon,
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

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (block));

  passphrases = g_strdup_printf ("%s\n%s", passphrase, new_passphrase);
  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                object,
                                                "encrypted-modify", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                passphrases,  /* input_string */
                                                "cryptsetup --force-password luksChangeKey %s",
                                                escaped_device))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error changing passphrase on device %s: %s",
                                             storaged_block_get_device (block),
                                             error_message);
      goto out;
    }

  storaged_encrypted_complete_change_passphrase (encrypted, invocation);

 out:
  g_free (escaped_device);
  g_free (passphrases);
  g_free (error_message);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
encrypted_iface_init (StoragedEncryptedIface *iface)
{
  iface->handle_unlock              = handle_unlock;
  iface->handle_lock                = handle_lock;
  iface->handle_change_passphrase   = handle_change_passphrase;
}
