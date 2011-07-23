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
#include "udiskslinuxblock.h"
#include "udisksdaemon.h"
#include "udiskspersistentstore.h"
#include "udisksdaemonutil.h"
#include "udiskscleanup.h"

/**
 * SECTION:udiskslinuxencrypted
 * @title: UDisksLinuxEncrypted
 * @short_description: Locking and unlocking encrypted devices Linux
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

static gboolean
wait_for_cleartext_object (UDisksDaemon *daemon,
                           UDisksObject *object,
                           gpointer      user_data)
{
  const gchar *crypto_object_path = user_data;
  UDisksBlockDevice *block;
  gboolean ret;

  ret = FALSE;
  block = udisks_object_peek_block_device (object);
  if (block == NULL)
    goto out;

  if (g_strcmp0 (udisks_block_device_get_crypto_backing_device (block), crypto_object_path) == 0)
    ret = TRUE;

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
  UDisksObject *object;
  UDisksBlockDevice *block;
  UDisksDaemon *daemon;
  UDisksCleanup *cleanup;
  gchar *error_message;
  gchar *name;
  gchar *escaped_name;
  UDisksObject *cleartext_object;
  UDisksBlockDevice *cleartext_block;
  GUdevDevice *udev_cleartext_device;
  GError *error;
  uid_t caller_uid;
  const gchar *action_id;

  object = NULL;
  error_message = NULL;
  name = NULL;
  escaped_name = NULL;
  udev_cleartext_device = NULL;
  cleartext_object = NULL;

  object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (encrypted)));
  block = udisks_object_peek_block_device (object);
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));
  cleanup = udisks_daemon_get_cleanup (daemon);

  /* TODO: check if the device is mentioned in /etc/crypttab (see crypttab(5)) - if so use that
   *
   *       Of course cryptsetup(8) don't support that, see https://bugzilla.redhat.com/show_bug.cgi?id=692258
   */

  /* Fail if the device is not a LUKS device */
  if (!(g_strcmp0 (udisks_block_device_get_id_usage (block), "crypto") == 0 &&
        g_strcmp0 (udisks_block_device_get_id_type (block), "crypto_LUKS") == 0))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Device %s does not appear to be a LUKS device",
                                             udisks_block_device_get_device (block));
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
      UDisksBlockDevice *unlocked_block;
      unlocked_block = udisks_object_peek_block_device (cleartext_object);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Device %s is already unlocked as %s",
                                             udisks_block_device_get_device (block),
                                             udisks_block_device_get_device (unlocked_block));
      goto out;
    }

  /* we need the uid of the caller for the unlocked-luks file */
  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon, invocation, NULL /* GCancellable */, &caller_uid, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Now, check that the user is actually authorized to unlock the device.
   */
  action_id = "org.freedesktop.udisks2.encrypted-unlock";
  if (udisks_block_device_get_hint_system (block) &&
      !(udisks_daemon_util_setup_by_user (daemon, object, caller_uid)))
    action_id = "org.freedesktop.udisks2.encrypted-unlock-system";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    N_("Authentication is required to unlock the encrypted device $(udisks2.device)"),
                                                    invocation))
    goto out;

  /* calculate the name to use */
  name = g_strdup_printf ("LUKS-udisks2-%s", udisks_block_device_get_id_uuid (block));
  escaped_name = g_strescape (name, NULL);

  /* TODO: support a 'readonly' option */
  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              NULL,  /* GCancellable */
                                              &error_message,
                                              passphrase,  /* input_string */
                                              "cryptsetup luksOpen \"%s\" \"%s\"",
                                              udisks_block_device_get_device (block),
                                              escaped_name))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error unlocking %s: %s",
                                             udisks_block_device_get_device (block),
                                             error_message);
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
                      udisks_block_device_get_device (block));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  cleartext_block = udisks_object_peek_block_device (cleartext_object);

  udisks_notice ("Unlocked LUKS device %s as %s",
                 udisks_block_device_get_device (block),
                 udisks_block_device_get_device (cleartext_block));

  udev_cleartext_device = udisks_linux_block_get_device (UDISKS_LINUX_BLOCK (cleartext_object));

  /* update the unlocked-luks file */
  if (!udisks_cleanup_add_unlocked_luks (cleanup,
                                         makedev (udisks_block_device_get_major (cleartext_block),
                                                  udisks_block_device_get_minor (cleartext_block)),
                                         makedev (udisks_block_device_get_major (block),
                                                  udisks_block_device_get_minor (block)),
                                         g_udev_device_get_sysfs_attr (udev_cleartext_device, "dm/uuid"),
                                         caller_uid,
                                         &error))
    goto out;

  udisks_encrypted_complete_unlock (encrypted,
                                    invocation,
                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (cleartext_object)));

 out:
  g_free (escaped_name);
  g_free (name);
  g_free (error_message);
  if (udev_cleartext_device != NULL)
    g_object_unref (udev_cleartext_device);
  if (cleartext_object != NULL)
    g_object_unref (cleartext_object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_lock (UDisksEncrypted        *encrypted,
             GDBusMethodInvocation  *invocation,
             GVariant               *options)
{
  UDisksObject *object;
  UDisksBlockDevice *block;
  UDisksDaemon *daemon;
  UDisksCleanup *cleanup;
  gchar *error_message;
  gchar *name;
  gchar *escaped_name;
  UDisksObject *cleartext_object;
  UDisksBlockDevice *cleartext_block;
  GUdevDevice *device;
  uid_t unlocked_by_uid;
  dev_t cleartext_device_from_file;
  GError *error;
  uid_t caller_uid;

  object = NULL;
  daemon = NULL;
  error_message = NULL;
  name = NULL;
  escaped_name = NULL;
  cleartext_object = NULL;
  device = NULL;

  object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (encrypted)));
  block = udisks_object_peek_block_device (object);
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));
  cleanup = udisks_daemon_get_cleanup (daemon);

  /* TODO: check if the device is mentioned in /etc/crypttab (see crypttab(5)) - if so use that
   *
   *       Of course cryptsetup(8) don't support that, see https://bugzilla.redhat.com/show_bug.cgi?id=692258
   */

  /* Fail if the device is not a LUKS device */
  if (!(g_strcmp0 (udisks_block_device_get_id_usage (block), "crypto") == 0 &&
        g_strcmp0 (udisks_block_device_get_id_type (block), "crypto_LUKS") == 0))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Device %s does not appear to be a LUKS device",
                                             udisks_block_device_get_device (block));
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
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Device %s is not unlocked",
                                             udisks_block_device_get_device (block));
      goto out;
    }
  cleartext_block = udisks_object_peek_block_device (cleartext_object);

  error = NULL;
  cleartext_device_from_file = udisks_cleanup_find_unlocked_luks (cleanup,
                                                                  makedev (udisks_block_device_get_major (block),
                                                                           udisks_block_device_get_minor (block)),
                                                                  &unlocked_by_uid,
                                                                  &error);
  if (error != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error when looking for entry `%s' in unlocked-luks: %s (%s, %d)",
                                             udisks_block_device_get_device (block),
                                             error->message,
                                             g_quark_to_string (error->domain),
                                             error->code);
      g_error_free (error);
      goto out;
    }
  if (cleartext_device_from_file == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Entry for `%s' not found in luks",
                                             udisks_block_device_get_device (block));
      goto out;
    }
  /* TODO: allow locking stuff not in the persistent file? */

  /* we need the uid of the caller to check authorization */
  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon, invocation, NULL /* GCancellable */, &caller_uid, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Check that the user is authorized to lock the device - if he
   * already unlocked it, he is implicitly authorized...
   */
  if (caller_uid != 0 && (caller_uid != unlocked_by_uid))
    {
      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        object,
                                                        "org.freedesktop.udisks2.encrypted-lock-others",
                                                        options,
                                                        N_("Authentication is required to lock the encrypted device $(udisks2.device) unlocked by another user"),
                                                        invocation))
        goto out;
    }

  device = udisks_linux_block_get_device (UDISKS_LINUX_BLOCK (cleartext_object));
  escaped_name = g_strescape (g_udev_device_get_sysfs_attr (device, "dm/name"), NULL);

  if (!udisks_cleanup_ignore_unlocked_luks (cleanup,
                                            makedev (udisks_block_device_get_major (cleartext_block),
                                                     udisks_block_device_get_minor (cleartext_block))))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_ALREADY_UNMOUNTING,
                                             "Cannot lock %s as it's already being locked",
                                             udisks_block_device_get_device (block));
      goto out;
    }

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              NULL,  /* GCancellable */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "cryptsetup luksClose \"%s\"",
                                              escaped_name))
    {
      udisks_cleanup_unignore_unlocked_luks (cleanup,
                                             makedev (udisks_block_device_get_major (cleartext_block),
                                                      udisks_block_device_get_minor (cleartext_block)));
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error locking %s (%s): %s",
                                             udisks_block_device_get_device (cleartext_block),
                                             udisks_block_device_get_device (block),
                                             error_message);
      goto out;
    }

  /* OK, device locked.. now to remove the entry from persistent file */
  error = NULL;
  if (!udisks_cleanup_remove_unlocked_luks (cleanup,
                                            makedev (udisks_block_device_get_major (cleartext_block),
                                                     udisks_block_device_get_minor (cleartext_block)),
                                            &error))
    {
      if (error == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error removing entry for `%s' from unlocked-luks: Entry not found",
                                                 udisks_block_device_get_device (cleartext_block));

        }
      else
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error removing entry for `%s' from unlocked-luks: %s (%s, %d)",
                                                 udisks_block_device_get_device (cleartext_block),
                                                 error->message,
                                                 g_quark_to_string (error->domain),
                                                 error->code);
          g_error_free (error);
        }
      udisks_cleanup_unignore_unlocked_luks (cleanup,
                                             makedev (udisks_block_device_get_major (cleartext_block),
                                                      udisks_block_device_get_minor (cleartext_block)));
      goto out;
    }
  udisks_cleanup_unignore_unlocked_luks (cleanup,
                                         makedev (udisks_block_device_get_major (cleartext_block),
                                                  udisks_block_device_get_minor (cleartext_block)));

  udisks_notice ("Locked LUKS device %s (was unlocked as %s)",
                 udisks_block_device_get_device (block),
                 udisks_block_device_get_device (cleartext_block));

  udisks_encrypted_complete_lock (encrypted, invocation);

 out:
  if (device != NULL)
    g_object_unref (device);
  g_free (escaped_name);
  g_free (name);
  g_free (error_message);
  if (cleartext_object != NULL)
    g_object_unref (cleartext_object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
encrypted_iface_init (UDisksEncryptedIface *iface)
{
  iface->handle_unlock = handle_unlock;
  iface->handle_lock   = handle_lock;
}
