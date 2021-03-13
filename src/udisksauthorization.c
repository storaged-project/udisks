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

#include <glib.h>
#include <glib/gstdio.h>
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksauthorization.h"

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

