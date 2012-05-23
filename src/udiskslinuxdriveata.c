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
#include <stdio.h>
#include <mntent.h>

#include <glib/gstdio.h>
#include <errno.h>

#include <atasmart.h>

#include "udiskslogging.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxdriveata.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udisksdaemonutil.h"
#include "udisksbasejob.h"
#include "udisksthreadedjob.h"

/**
 * SECTION:udiskslinuxdriveata
 * @title: UDisksLinuxDriveAta
 * @short_description: Linux implementation of #UDisksDriveAta
 *
 * This type provides an implementation of the #UDisksDriveAta
 * interface on Linux.
 */

typedef struct _UDisksLinuxDriveAtaClass   UDisksLinuxDriveAtaClass;

/**
 * UDisksLinuxDriveAta:
 *
 * The #UDisksLinuxDriveAta structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxDriveAta
{
  UDisksDriveAtaSkeleton parent_instance;

  gboolean     smart_is_from_blob;
  guint64      smart_updated;
  gboolean     smart_failing;
  gdouble      smart_temperature;
  guint64      smart_power_on_seconds;
  gint         smart_num_attributes_failing;
  gint         smart_num_attributes_failed_in_the_past;
  gint64       smart_num_bad_sectors;
  const gchar *smart_selftest_status;
  gint         smart_selftest_percent_remaining;

  GVariant    *smart_attributes;

  UDisksThreadedJob *selftest_job;
};

struct _UDisksLinuxDriveAtaClass
{
  UDisksDriveAtaSkeletonClass parent_class;
};

static void drive_ata_iface_init (UDisksDriveAtaIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxDriveAta, udisks_linux_drive_ata, UDISKS_TYPE_DRIVE_ATA_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_DRIVE_ATA, drive_ata_iface_init));

G_LOCK_DEFINE_STATIC (object_lock);

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_drive_ata_finalize (GObject *object)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (object);

  if (drive->smart_attributes != NULL)
    g_variant_unref (drive->smart_attributes);

  if (G_OBJECT_CLASS (udisks_linux_drive_ata_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_drive_ata_parent_class)->finalize (object);
}


static void
udisks_linux_drive_ata_init (UDisksLinuxDriveAta *drive)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (drive),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_drive_ata_class_init (UDisksLinuxDriveAtaClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = udisks_linux_drive_ata_finalize;
}

/**
 * udisks_linux_drive_ata_new:
 *
 * Creates a new #UDisksLinuxDriveAta instance.
 *
 * Returns: A new #UDisksLinuxDriveAta. Free with g_object_unref().
 */
UDisksDriveAta *
udisks_linux_drive_ata_new (void)
{
  return UDISKS_DRIVE_ATA (g_object_new (UDISKS_TYPE_LINUX_DRIVE_ATA,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/* may be called from *any* thread when the SMART data has been updated */
static void
update_smart (UDisksLinuxDriveAta *drive,
              GUdevDevice         *device)
{
  gboolean supported = FALSE;
  gboolean enabled = FALSE;
  guint64 updated = 0;
  gboolean failing = FALSE;
  gdouble temperature = 0.0;
  guint64 power_on_seconds = 0;
  const gchar *selftest_status = NULL;
  gint selftest_percent_remaining = -1;
  gint num_attributes_failing = -1;
  gint num_attributes_failed_in_the_past = -1;
  gint64 num_bad_sectors = 1;

  supported = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_SMART");
  enabled = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_SMART_ENABLED");

  G_LOCK (object_lock);
  if (drive->smart_updated > 0)
    {
      if (drive->smart_is_from_blob)
        supported = enabled = TRUE;
      updated = drive->smart_updated;
      failing = drive->smart_failing;
      temperature = drive->smart_temperature;
      power_on_seconds = drive->smart_power_on_seconds;
      num_attributes_failing = drive->smart_num_attributes_failing;
      num_attributes_failed_in_the_past = drive->smart_num_attributes_failed_in_the_past;
      num_bad_sectors = drive->smart_num_bad_sectors;
      selftest_status = drive->smart_selftest_status;
      selftest_percent_remaining = drive->smart_selftest_percent_remaining;
    }
  G_UNLOCK (object_lock);

  if (selftest_status == NULL)
    selftest_status = "";

  g_object_freeze_notify (G_OBJECT (drive));
  udisks_drive_ata_set_smart_supported (UDISKS_DRIVE_ATA (drive), supported);
  udisks_drive_ata_set_smart_enabled (UDISKS_DRIVE_ATA (drive), enabled);
  udisks_drive_ata_set_smart_updated (UDISKS_DRIVE_ATA (drive), updated);
  udisks_drive_ata_set_smart_failing (UDISKS_DRIVE_ATA (drive), failing);
  udisks_drive_ata_set_smart_temperature (UDISKS_DRIVE_ATA (drive), temperature);
  udisks_drive_ata_set_smart_power_on_seconds (UDISKS_DRIVE_ATA (drive), power_on_seconds);
  udisks_drive_ata_set_smart_num_attributes_failing (UDISKS_DRIVE_ATA (drive), num_attributes_failing);
  udisks_drive_ata_set_smart_num_attributes_failed_in_the_past (UDISKS_DRIVE_ATA (drive), num_attributes_failed_in_the_past);
  udisks_drive_ata_set_smart_num_bad_sectors (UDISKS_DRIVE_ATA (drive), num_bad_sectors);
  udisks_drive_ata_set_smart_selftest_status (UDISKS_DRIVE_ATA (drive), selftest_status);
  udisks_drive_ata_set_smart_selftest_percent_remaining (UDISKS_DRIVE_ATA (drive), selftest_percent_remaining);
  g_object_thaw_notify (G_OBJECT (drive));
}

/**
 * udisks_linux_drive_ata_update:
 * @drive: A #UDisksLinuxDriveAta.
 * @object: The enclosing #UDisksLinuxDriveObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_drive_ata_update (UDisksLinuxDriveAta    *drive,
                               UDisksLinuxDriveObject *object)
{
  GUdevDevice *device;
  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    goto out;
  update_smart (drive, device);
 out:
  if (device != NULL)
    g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GVariantBuilder builder;
  gint num_attributes_failing;
  gint num_attributes_failed_in_the_past;
} ParseData;

static void
parse_attr_cb (SkDisk                           *d,
               const SkSmartAttributeParsedData *a,
               void                             *user_data)
{
  ParseData *data = user_data;
  gboolean failed = FALSE;
  gboolean failed_in_the_past = FALSE;
  gint current, worst, threshold;

  current =   a->current_value_valid ? a->current_value : -1;
  worst =     a->worst_value_valid   ? a->worst_value : -1;
  threshold = a->threshold_valid     ? a->threshold : -1;

  g_variant_builder_add (&data->builder,
                         "(ysqiiixia{sv})",
                         a->id,
                         a->name,
                         a->flags,
                         current,
                         worst,
                         threshold,
                         a->pretty_value,     a->pretty_unit,
                         NULL); /* expansion unused for now */

  if (current > 0 && threshold > 0 && current <= threshold)
    failed = TRUE;

  if (worst > 0 && threshold > 0 && worst <= threshold)
    failed_in_the_past = TRUE;

  if (failed)
    data->num_attributes_failing += 1;

  if (failed_in_the_past)
    data->num_attributes_failed_in_the_past += 1;
}

static const gchar *
selftest_status_to_string (SkSmartSelfTestExecutionStatus status)
{
  const gchar *ret;
  switch (status)
    {
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_SUCCESS_OR_NEVER:
      ret = "success";
      break;
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_ABORTED:
      ret = "aborted";
      break;
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_INTERRUPTED:
      ret = "interrupted";
      break;
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_FATAL:
      ret = "fatal";
      break;
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_UNKNOWN:
      ret = "error_unknown";
      break;
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_ELECTRICAL:
      ret = "error_electrical";
      break;
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_SERVO:
      ret = "error_servo";
      break;
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_READ:
      ret = "error_read";
      break;
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_ERROR_HANDLING:
      ret = "error_handling";
      break;
    case SK_SMART_SELF_TEST_EXECUTION_STATUS_INPROGRESS:
      ret = "inprogress";
      break;
    default:
      ret = "";
      break;
    }
  return ret;
}

/**
 * udisks_linux_drive_ata_refresh_smart_sync:
 * @drive: The #UDisksLinuxDriveAta to refresh.
 * @nowakeup: If %TRUE, will not wake up the disk if asleep.
 * @simulate_path: If not %NULL, the path of a file with a libatasmart blob to use.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error.
 *
 * Synchronously refreshes ATA S.M.A.R.T. data on @drive using one of
 * the physical drives associated with it. The calling thread is
 * blocked until the data has been obtained.
 *
 * If @nowake is %TRUE and the disk is in a sleep state this fails
 * with %UDISKS_ERROR_WOULD_WAKEUP.
 *
 * This may only be called if @drive has been associated with a
 * #UDisksLinuxDriveObject instance.
 *
 * This method may be called from any thread.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
udisks_linux_drive_ata_refresh_smart_sync (UDisksLinuxDriveAta  *drive,
                                           gboolean              nowakeup,
                                           const gchar          *simulate_path,
                                           GCancellable         *cancellable,
                                           GError              **error)
{
  UDisksLinuxDriveObject *object;
  GUdevDevice *device = NULL;
  gboolean ret = FALSE;
  SkDisk *d = NULL;
  SkBool awake;
  SkBool good;
  uint64_t temp_mkelvin = 0;
  uint64_t power_on_msec = 0;
  uint64_t num_bad_sectors = 0;
  const SkSmartParsedData *data;
  ParseData parse_data;

  object = udisks_daemon_util_dup_object (drive, error);
  if (object == NULL)
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  g_assert (device != NULL);

  /* TODO: use cancellable */

  if (simulate_path != NULL)
    {
      gchar *blob;
      gsize blob_len;

      if (!g_file_get_contents (simulate_path,
                                &blob,
                                &blob_len,
                                error))
        {
          goto out;
        }

      if (sk_disk_open (NULL, &d) != 0)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "sk_disk_open: %m");
          goto out;
        }

      if (sk_disk_set_blob (d, blob, blob_len) != 0)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "sk_disk_set_blob: %m");
          g_free (blob);
          goto out;
        }
      g_free (blob);
    }
  else
    {
      if (sk_disk_open (g_udev_device_get_device_file (device), &d) != 0)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "sk_disk_open: %m");
          goto out;
        }

      if (sk_disk_check_sleep_mode (d, &awake) != 0)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "sk_disk_check_sleep_mode: %m");
          goto out;
        }

      /* don't wake up disk unless specically asked to */
      if (nowakeup && !awake)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_WOULD_WAKEUP,
                       "Disk is in sleep mode and the nowakeup option was passed");
          goto out;
        }
    }

  if (sk_disk_smart_read_data (d) != 0)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "sk_disk_smart_read_data: %m");
      goto out;
    }

  if (sk_disk_smart_status (d, &good) != 0)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "sk_disk_smart_status: %m");
      goto out;
    }

  if (sk_disk_smart_parse (d, &data) != 0)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "sk_disk_smart_parse: %m");
      goto out;
    }

  /* don't care if these are failing or not */
  sk_disk_smart_get_temperature (d, &temp_mkelvin);
  sk_disk_smart_get_power_on (d, &power_on_msec);
  sk_disk_smart_get_bad (d, &num_bad_sectors);

  memset (&parse_data, 0, sizeof (ParseData));
  g_variant_builder_init (&parse_data.builder, G_VARIANT_TYPE ("a(ysqiiixia{sv})"));
  sk_disk_smart_parse_attributes (d, parse_attr_cb, &parse_data);

  G_LOCK (object_lock);
  drive->smart_is_from_blob = (simulate_path != NULL);
  drive->smart_updated = time (NULL);
  drive->smart_failing = !good;
  drive->smart_temperature = temp_mkelvin / 1000.0;
  drive->smart_power_on_seconds = power_on_msec / 1000.0;
  drive->smart_num_attributes_failing = parse_data.num_attributes_failing;
  drive->smart_num_attributes_failed_in_the_past = parse_data.num_attributes_failed_in_the_past;
  drive->smart_num_bad_sectors = num_bad_sectors;
  drive->smart_selftest_status = selftest_status_to_string (data->self_test_execution_status);
  drive->smart_selftest_percent_remaining = data->self_test_execution_percent_remaining;
  if (drive->smart_attributes != NULL)
    g_variant_unref (drive->smart_attributes);
  drive->smart_attributes = g_variant_ref_sink (g_variant_builder_end (&parse_data.builder));
  G_UNLOCK (object_lock);

  update_smart (drive, device);

  ret = TRUE;

 out:
  g_clear_object (&device);
  if (d != NULL)
    sk_disk_free (d);
  g_clear_object (&object);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_drive_ata_smart_selftest_sync:
 * @drive: A #UDisksLinuxDriveAta.
 * @type: The type of selftest to run.
 * @cancellable: (allow-none): A #GCancellable that can be used to cancel the operation or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Starts (or aborts) a SMART self-test on @drive. Valid values for
 * @type includes 'short', 'extended', 'conveyance' and 'abort'.
 *
 * The calling thread is blocked while sending the command to the
 * drive but will return immediately after the drive acknowledges the
 * command.
 *
 * Returns: %TRUE if the operation succeed, %FALSE if @error is set.
 */
gboolean
udisks_linux_drive_ata_smart_selftest_sync (UDisksLinuxDriveAta     *drive,
                                            const gchar             *type,
                                            GCancellable            *cancellable,
                                            GError                 **error)
{
  UDisksLinuxDriveObject  *object;
  GUdevDevice *device;
  SkDisk *d = NULL;
  gboolean ret = FALSE;
  SkSmartSelfTest test;

  object = udisks_daemon_util_dup_object (drive, error);
  if (object == NULL)
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  g_assert (device != NULL);

  if (g_strcmp0 (type, "short") == 0)
    test = SK_SMART_SELF_TEST_SHORT;
  else if (g_strcmp0 (type, "extended") == 0)
    test = SK_SMART_SELF_TEST_EXTENDED;
  else if (g_strcmp0 (type, "conveyance") == 0)
    test = SK_SMART_SELF_TEST_CONVEYANCE;
  else if (g_strcmp0 (type, "abort") == 0)
    test = SK_SMART_SELF_TEST_ABORT;
  else
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "unknown type %s", type);
      goto out;
    }

  if (sk_disk_open (g_udev_device_get_device_file (device), &d) != 0)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "sk_disk_open: %m");
      goto out;
    }

  if (sk_disk_smart_self_test (d, test) != 0)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "sk_disk_smart_self_test: %m");
      goto out;
    }

  ret = TRUE;

 out:
  g_clear_object (&device);
  if (d != NULL)
    sk_disk_free (d);
  g_clear_object (&object);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_smart_update (UDisksDriveAta        *_drive,
                     GDBusMethodInvocation *invocation,
                     GVariant              *options)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  UDisksLinuxDriveObject *object;
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksDaemon *daemon;
  gboolean nowakeup = FALSE;
  const gchar *atasmart_blob = NULL;
  GError *error;
  const gchar *message;
  const gchar *action_id;

  daemon = NULL;

  error = NULL;
  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);
  block_object = udisks_linux_drive_object_get_block (object, TRUE);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unable to find physical block device for drive");
      goto out;
    }

  g_variant_lookup (options, "nowakeup", "b", &nowakeup);
  g_variant_lookup (options, "atasmart_blob", "s", &atasmart_blob);

  /* Translators: Shown in authentication dialog when the user
   * refreshes SMART data from a disk.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to update SMART data from $(drive)");
  action_id = "org.freedesktop.udisks2.ata-smart-update";

  if (atasmart_blob != NULL)
    {
      /* Translators: Shown in authentication dialog when the user
       * tries to simulate SMART data from a libatasmart blob.
       *
       * Do not translate $(drive), it's a placeholder and
       * will be replaced by the name of the drive/device in question
       */
      message = N_("Authentication is required to set SMART data from a blob on $(drive)");
      action_id = "org.freedesktop.udisks2.ata-smart-simulate";
    }
  else
    {
      if (!udisks_drive_ata_get_smart_supported (UDISKS_DRIVE_ATA (drive)))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "SMART is not supported");
          goto out;
        }

      if (!udisks_drive_ata_get_smart_enabled (UDISKS_DRIVE_ATA (drive)))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "SMART is not enabled");
          goto out;
        }
    }

  /* Check that the user is authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (block_object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  error = NULL;
  if (!udisks_linux_drive_ata_refresh_smart_sync (drive,
                                                  nowakeup,
                                                  atasmart_blob,
                                                  NULL, /* cancellable */
                                                  &error))
    {
      udisks_warning ("Error updating ATA smart for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_drive_ata_complete_smart_update (UDISKS_DRIVE_ATA (drive), invocation);

 out:
  g_clear_object (&block_object);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_smart_get_attributes (UDisksDriveAta        *_drive,
                             GDBusMethodInvocation *invocation,
                             GVariant              *options)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);

  G_LOCK (object_lock);
  if (drive->smart_attributes == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "SMART data not collected");
    }
  else
    {
      udisks_drive_ata_complete_smart_get_attributes (UDISKS_DRIVE_ATA (drive), invocation,
                                                      drive->smart_attributes);
    }
  G_UNLOCK (object_lock);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_smart_selftest_abort (UDisksDriveAta        *_drive,
                             GDBusMethodInvocation *invocation,
                             GVariant              *options)
{
  UDisksLinuxDriveObject  *object;
  UDisksLinuxBlockObject *block_object;
  UDisksDaemon *daemon;
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  GError *error;

  error = NULL;
  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);
  block_object = udisks_linux_drive_object_get_block (object, TRUE);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unable to find physical block device for drive");
      goto out;
    }

  if (!udisks_drive_ata_get_smart_supported (UDISKS_DRIVE_ATA (drive)) ||
      !udisks_drive_ata_get_smart_enabled (UDISKS_DRIVE_ATA (drive)))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "SMART is not supported or enabled");
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (block_object),
                                                    "org.freedesktop.udisks2.ata-smart-selftest",
                                                    options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * aborts a running SMART self-test.
                                                     *
                                                     * Do not translate $(drive), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to abort a SMART self-test on $(drive)"),
                                                    invocation))
    goto out;

  error = NULL;
  if (!udisks_linux_drive_ata_smart_selftest_sync (drive,
                                                   "abort",
                                                   NULL, /* cancellable */
                                                   &error))
    {
      udisks_warning ("Error aborting SMART selftest for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* This wakes up the selftest thread */
  G_LOCK (object_lock);
  if (drive->selftest_job != NULL)
    {
      g_cancellable_cancel (udisks_base_job_get_cancellable (UDISKS_BASE_JOB (drive->selftest_job)));
    }
  G_UNLOCK (object_lock);
  /* TODO: wait for the selftest thread to terminate */

  error = NULL;
  if (!udisks_linux_drive_ata_refresh_smart_sync (drive,
                                                  FALSE, /* nowakeup */
                                                  NULL,  /* blob */
                                                  NULL,  /* cancellable */
                                                  &error))
    {
      udisks_warning ("Error updating ATA smart for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_drive_ata_complete_smart_selftest_abort (UDISKS_DRIVE_ATA (drive), invocation);

 out:
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
selftest_job_func (UDisksThreadedJob  *job,
                   GCancellable       *cancellable,
                   gpointer            user_data,
                   GError            **error)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (user_data);
  UDisksLinuxDriveObject  *object;
  gboolean ret = FALSE;

  object = udisks_daemon_util_dup_object (drive, error);
  if (object == NULL)
    goto out;

  while (TRUE)
    {
      gboolean still_in_progress;
      GPollFD poll_fd;

      if (!udisks_linux_drive_ata_refresh_smart_sync (drive,
                                                      FALSE, /* nowakeup */
                                                      NULL,  /* blob */
                                                      NULL,  /* cancellable */
                                                      error))
        {
          udisks_warning ("Error updating ATA smart for %s while polling during self-test: %s (%s, %d)",
                          g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                          (*error)->message, g_quark_to_string ((*error)->domain), (*error)->code);
          goto out;
        }

      /* TODO: set estimation properties etc. on the Job object */

      G_LOCK (object_lock);
      still_in_progress = (g_strcmp0 (drive->smart_selftest_status, "inprogress") == 0);
      G_UNLOCK (object_lock);
      if (!still_in_progress)
        {
          ret = TRUE;
          goto out;
        }

      /* Sleep for 30 seconds or until we're cancelled */
      if (g_cancellable_make_pollfd (cancellable, &poll_fd))
        {
          gint poll_ret;
          do
            {
              poll_ret = g_poll (&poll_fd, 1, 30 * 1000);
            }
          while (poll_ret == -1 && errno == EINTR);
          g_cancellable_release_fd (cancellable);
        }
      else
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Error creating pollfd for cancellable");
          goto out;
        }

      /* Check if we're cancelled */
      if (g_cancellable_is_cancelled (cancellable))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_CANCELLED,
                       "Self-test was cancelled");
          goto out;
        }
    }

  ret = TRUE;

 out:
  /* terminate the job */
  G_LOCK (object_lock);
  drive->selftest_job = NULL;
  G_UNLOCK (object_lock);
  g_clear_object (&object);
  return ret;
}


static gboolean
handle_smart_selftest_start (UDisksDriveAta        *_drive,
                             GDBusMethodInvocation *invocation,
                             const gchar           *type,
                             GVariant              *options)
{
  UDisksLinuxDriveObject  *object;
  UDisksLinuxBlockObject *block_object;
  UDisksDaemon *daemon;
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  GError *error;

  error = NULL;
  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);
  block_object = udisks_linux_drive_object_get_block (object, TRUE);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unable to find physical block device for drive");
      goto out;
    }

  if (!udisks_drive_ata_get_smart_supported (UDISKS_DRIVE_ATA (drive)) ||
      !udisks_drive_ata_get_smart_enabled (UDISKS_DRIVE_ATA (drive)))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "SMART is not supported or enabled");
      goto out;
    }

  G_LOCK (object_lock);
  if (drive->selftest_job != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "There is already SMART self-test running");
      G_UNLOCK (object_lock);
      goto out;
    }
  G_UNLOCK (object_lock);

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (block_object),
                                                    "org.freedesktop.udisks2.ata-smart-selftest",
                                                    options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * initiates a SMART self-test.
                                                     *
                                                     * Do not translate $(drive), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to start a SMART self-test on $(drive)"),
                                                    invocation))
    goto out;

  error = NULL;
  if (!udisks_linux_drive_ata_smart_selftest_sync (drive,
                                                   type,
                                                   NULL, /* cancellable */
                                                   &error))
    {
      udisks_warning ("Error starting SMART selftest for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  G_LOCK (object_lock);
  if (drive->selftest_job == NULL)
    {
      drive->selftest_job = UDISKS_THREADED_JOB (udisks_daemon_launch_threaded_job (daemon,
                                                                                    UDISKS_OBJECT (object),
                                                                                    selftest_job_func,
                                                                                    g_object_ref (drive),
                                                                                    g_object_unref,
                                                                                    NULL)); /* GCancellable */
    }
  G_UNLOCK (object_lock);

  udisks_drive_ata_complete_smart_selftest_start (UDISKS_DRIVE_ATA (drive), invocation);

 out:
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
drive_ata_iface_init (UDisksDriveAtaIface *iface)
{
  iface->handle_smart_update = handle_smart_update;
  iface->handle_smart_get_attributes = handle_smart_get_attributes;
  iface->handle_smart_selftest_abort = handle_smart_selftest_abort;
  iface->handle_smart_selftest_start = handle_smart_selftest_start;
}
