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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/bsg.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <scsi/scsi_ioctl.h>
#include <linux/cdrom.h>

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
#include "udiskssimplejob.h"
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

  gboolean     secure_erase_in_progress;
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

/* ---------------------------------------------------------------------------------------------------- */

static void
update_pm (UDisksLinuxDriveAta *drive,
           GUdevDevice         *device)
{
  gboolean supported = FALSE;
  gboolean enabled = FALSE;
  gboolean apm_supported = FALSE;
  gboolean apm_enabled = FALSE;
  gboolean aam_supported = FALSE;
  gboolean aam_enabled = FALSE;
  gint aam_vendor_recommended_value = 0;

  supported = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_PM");
  enabled = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_PM_ENABLED");
  apm_supported = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_APM");
  apm_enabled = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_APM_ENABLED");
  aam_supported = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_AAM");
  aam_enabled = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_AAM_ENABLED");
  aam_vendor_recommended_value = g_udev_device_get_property_as_int (device, "ID_ATA_FEATURE_SET_AAM_VENDOR_RECOMMENDED_VALUE");

  g_object_freeze_notify (G_OBJECT (drive));
  udisks_drive_ata_set_pm_supported (UDISKS_DRIVE_ATA (drive), supported);
  udisks_drive_ata_set_pm_enabled (UDISKS_DRIVE_ATA (drive), enabled);
  udisks_drive_ata_set_apm_supported (UDISKS_DRIVE_ATA (drive), apm_supported);
  udisks_drive_ata_set_apm_enabled (UDISKS_DRIVE_ATA (drive), apm_enabled);
  udisks_drive_ata_set_aam_supported (UDISKS_DRIVE_ATA (drive), aam_supported);
  udisks_drive_ata_set_aam_enabled (UDISKS_DRIVE_ATA (drive), aam_enabled);
  udisks_drive_ata_set_aam_vendor_recommended_value (UDISKS_DRIVE_ATA (drive), aam_vendor_recommended_value);
  g_object_thaw_notify (G_OBJECT (drive));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_security (UDisksLinuxDriveAta *drive,
                 GUdevDevice         *device)
{
  gint erase_unit = 0;
  gint enhanced_erase_unit = 0;
  gboolean frozen = FALSE;

  erase_unit = g_udev_device_get_property_as_int (device, "ID_ATA_FEATURE_SET_SECURITY_ERASE_UNIT_MIN");
  enhanced_erase_unit = g_udev_device_get_property_as_int (device, "ID_ATA_FEATURE_SET_SECURITY_ENHANCED_ERASE_UNIT_MIN");
  frozen = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_SECURITY_FROZEN");

  g_object_freeze_notify (G_OBJECT (drive));
  udisks_drive_ata_set_security_erase_unit_minutes (UDISKS_DRIVE_ATA (drive), erase_unit);
  udisks_drive_ata_set_security_enhanced_erase_unit_minutes (UDISKS_DRIVE_ATA (drive), enhanced_erase_unit);
  udisks_drive_ata_set_security_frozen (UDISKS_DRIVE_ATA (drive), frozen);
  g_object_thaw_notify (G_OBJECT (drive));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_drive_ata_update:
 * @drive: A #UDisksLinuxDriveAta.
 * @object: The enclosing #UDisksLinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_drive_ata_update (UDisksLinuxDriveAta    *drive,
                               UDisksLinuxDriveObject *object)
{
  GUdevDevice *device;
  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    goto out;

  update_smart (drive, device);
  update_pm (drive, device);
  update_security (drive, device);

 out:
  if (device != NULL)
    g_object_unref (device);

  return FALSE;
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

  if (drive->secure_erase_in_progress)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_DEVICE_BUSY,
                   "Secure erase in progress");
      goto out;
    }

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

  udisks_job_set_progress_valid (UDISKS_JOB (job), TRUE);
  udisks_job_set_progress (UDISKS_JOB (job), 0.0);

  while (TRUE)
    {
      gboolean still_in_progress;
      GPollFD poll_fd;
      gdouble progress;

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
      progress = (100.0 - drive->smart_selftest_percent_remaining) / 100.0;
      G_UNLOCK (object_lock);
      if (!still_in_progress)
        {
          ret = TRUE;
          goto out;
        }

      if (progress < 0.0)
        progress = 0.0;
      if (progress > 1.0)
        progress = 1.0;
      udisks_job_set_progress (UDISKS_JOB (job), progress);

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
          GError *c_error;

          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_CANCELLED,
                       "Self-test was cancelled");

          /* OK, cancelled ... still need to a) abort the test; and b) update the status */
          c_error = NULL;
          if (!udisks_linux_drive_ata_smart_selftest_sync (drive,
                                                           "abort",
                                                           NULL, /* cancellable */
                                                           &c_error))
            {
              udisks_warning ("Error aborting SMART selftest for %s on cancel path: %s (%s, %d)",
                              g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                              c_error->message, g_quark_to_string (c_error->domain), c_error->code);
              g_clear_error (&c_error);
            }
          if (!udisks_linux_drive_ata_refresh_smart_sync (drive,
                                                          FALSE, /* nowakeup */
                                                          NULL,  /* blob */
                                                          NULL,  /* cancellable */
                                                          &c_error))
            {
              udisks_warning ("Error updating ATA smart for %s on cancel path: %s (%s, %d)",
                              g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                              c_error->message, g_quark_to_string (c_error->domain), c_error->code);
              g_clear_error (&c_error);
            }
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
  uid_t caller_uid;
  gid_t caller_gid;
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

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &caller_gid,
                                               NULL,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
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
                                                                                    "ata-smart-selftest", caller_uid,
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

#define ATA_DEFAULT_COMMAND_TIMEOUT_MSEC (5 * 1000)

typedef struct
{
  guint8  command;
  guint8  feature;
  guint8  count;
  guint8  device;
  guint32 lba;
  gsize   buffer_size;
  guchar *buffer;
} AtaCommandInput;

typedef struct
{
  guint8  error;
  guint8  count;
  guint8  device;
  guint8  status;
  guint32 lba;
  gsize   buffer_size;
  guchar *buffer;
} AtaCommandOutput;

typedef enum
{
  ATA_COMMAND_PROTOCOL_NONE,           /* Non-data */
  ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST,  /* PIO Data-In */
  ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE,  /* PIO Data-Out */
} AtaCommandProtocol;

static gboolean
ata_send_command (gint                 fd,
                  gint                 timeout_msec,
                  AtaCommandProtocol   protocol,
                  AtaCommandInput     *input,
                  AtaCommandOutput    *output,
                  GError             **error)
{
  struct sg_io_v4 io_v4;
  uint8_t cdb[16];
  gint cdb_len = 16;
  uint8_t sense[32];
  uint8_t *desc = sense+8;
  gboolean use_ata12 = FALSE;
  gboolean ret = FALSE;
  gint rc;

  g_return_val_if_fail (fd != -1, FALSE);
  g_return_val_if_fail (timeout_msec == -1 || timeout_msec > 0, FALSE);
  g_return_val_if_fail (/* protocol >= 0 && */ protocol <= 2, FALSE);
  g_return_val_if_fail (input != NULL, FALSE);
  g_return_val_if_fail (input->buffer_size == 0 || input->buffer != NULL, FALSE);
  g_return_val_if_fail (output != NULL, FALSE);
  g_return_val_if_fail (output->buffer_size == 0 || output->buffer != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (timeout_msec == -1)
    timeout_msec = ATA_DEFAULT_COMMAND_TIMEOUT_MSEC;

  /* zero outputs, even if returning an error */
  output->error = 0;
  output->count = 0;
  output->device = 0;
  output->status = 0;
  output->lba = 0;
  if (output->buffer != NULL)
    memset (output->buffer, 0, output->buffer_size);

  memset (cdb, 0, sizeof (cdb));

  /* Prefer ATA PASS-THROUGH (16) to ATA PASS-THROUGH (12) since the op-code
   * for the latter clashes with the MMC blank command.
   *
   * TODO: this is hard-coded to FALSE for now - should retry with the 12-byte
   *       version only if the 16-byte version fails. But we don't do that
   *       right now
   */
  use_ata12 = FALSE;

  if (use_ata12)
    {
      /* Do not confuse optical drive firmware with ATA commands
       * some drives are reported to blank CD-RWs because the op-code
       * for ATA PASS-THROUGH (12) clashes with the MMC blank command.
       *
       * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=556635
       */
      if (ioctl (fd, CDROM_GET_CAPABILITY, NULL) >= 0)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Refusing to send ATA PASS-THROUGH (12) to optical drive");
          goto out;
        }

      /*
       * ATA Pass-Through 12 byte command, as described in
       *
       *  T10 04-262r8 ATA Command Pass-Through
       *
       * from http://www.t10.org/ftp/t10/document.04/04-262r8.pdf
       */
      cdb[0] = 0xa1;                        /* OPERATION CODE: 12 byte pass through */
      switch (protocol)
        {
        case ATA_COMMAND_PROTOCOL_NONE:
          cdb[1] = 3 << 1;                  /* PROTOCOL: Non-data */
          cdb[2] = 0x20;                    /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=0, T_LENGTH=0 */
          break;
        case ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST:
          cdb[1] = 4 << 1;                  /* PROTOCOL: PIO Data-In */
          cdb[2] = 0x2e;                    /* OFF_LINE=0, CK_COND=1, T_DIR=1, BYT_BLOK=1, T_LENGTH=2 */
          break;
        case ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE:
          cdb[1] = 5 << 1;                  /* PROTOCOL: PIO Data-Out */
          cdb[2] = 0x26;                    /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=1, T_LENGTH=2 */
          break;
        default:
          g_assert_not_reached ();
          break;
        }
      cdb[3] = input->feature;              /* FEATURES */
      cdb[4] = input->count;                /* SECTORS */
      cdb[5] = (input->lba >>  0) & 0xff;   /* LBA LOW */
      cdb[6] = (input->lba >>  8) & 0xff;   /* LBA MID */
      cdb[7] = (input->lba >> 16) & 0xff;   /* LBA HIGH */
      cdb[8] = input->device;               /* SELECT */
      cdb[9] = input->command;              /* ATA COMMAND */
      cdb_len = 12;
    }
  else
    {
      /*
       * ATA Pass-Through 16 byte command, as described in
       *
       *  T10 04-262r8 ATA Command Pass-Through
       *
       * from http://www.t10.org/ftp/t10/document.04/04-262r8.pdf
       */
      cdb[0] = 0x85;                        /* OPERATION CODE: 16 byte pass through */
      switch (protocol)
        {
        case ATA_COMMAND_PROTOCOL_NONE:
          cdb[1] = 3 << 1;                  /* PROTOCOL: Non-data */
          cdb[2] = 0x20;                    /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=0, T_LENGTH=0 */
          break;
        case ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST:
          cdb[1] = 4 << 1;                  /* PROTOCOL: PIO Data-In */
          cdb[2] = 0x2e;                    /* OFF_LINE=0, CK_COND=1, T_DIR=1, BYT_BLOK=1, T_LENGTH=2 */
          break;
        case ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE:
          cdb[1] = 5 << 1;                  /* PROTOCOL: PIO Data-Out */
          cdb[2] = 0x26;                    /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=1, T_LENGTH=2 */
          break;
        default:
          g_assert_not_reached ();
          break;
        }
      cdb[ 3] = (input->feature >>  8) & 0xff;   /* FEATURES */
      cdb[ 4] = (input->feature >>  0) & 0xff;   /* FEATURES */
      cdb[ 5] = (input->count >>  8) & 0xff;     /* SECTORS */
      cdb[ 6] = (input->count >>  0) & 0xff;     /* SECTORS */
      cdb[ 8] = (input->lba >> 16) & 0xff;       /* LBA HIGH */
      cdb[10] = (input->lba >>  8) & 0xff;       /* LBA MID */
      cdb[12] = (input->lba >>  0) & 0xff;       /* LBA LOW */
      cdb[13] = input->device;                   /* SELECT */
      cdb[14] = input->command;                  /* ATA COMMAND */
      cdb_len = 16;
    }

  /* See http://sg.danny.cz/sg/sg_io.html and http://www.tldp.org/HOWTO/SCSI-Generic-HOWTO/index.html
   * for detailed information about how the SG_IO ioctl work
   */

  memset (sense, 0, sizeof (sense));
  memset (&io_v4, 0, sizeof (io_v4));
  io_v4.guard = 'Q';
  io_v4.protocol = BSG_PROTOCOL_SCSI;
  io_v4.subprotocol = BSG_SUB_PROTOCOL_SCSI_CMD;
  io_v4.request_len = cdb_len;
  io_v4.request = (uintptr_t) cdb;
  io_v4.max_response_len = sizeof (sense);
  io_v4.response = (uintptr_t) sense;
  io_v4.din_xfer_len = output->buffer_size;
  io_v4.din_xferp = (uintptr_t) output->buffer;
  io_v4.dout_xfer_len = input->buffer_size;
  io_v4.dout_xferp = (uintptr_t) input->buffer;
  if (timeout_msec == G_MAXINT)
    io_v4.timeout = G_MAXUINT;
  else
    io_v4.timeout = timeout_msec;

  rc = ioctl (fd, SG_IO, &io_v4);
  if (rc != 0)
    {
      /* could be that the driver doesn't do version 4, try version 3 */
      if (errno == EINVAL)
        {
          struct sg_io_hdr io_hdr;

          memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
          io_hdr.interface_id = 'S';
          io_hdr.cmdp = (unsigned char*) cdb;
          io_hdr.cmd_len = cdb_len;
          switch (protocol)
            {
            case ATA_COMMAND_PROTOCOL_NONE:
              io_hdr.dxfer_direction = SG_DXFER_NONE;
              break;

            case ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST:
              io_hdr.dxferp = output->buffer;
              io_hdr.dxfer_len = output->buffer_size;
              io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
              break;

            case ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE:
              io_hdr.dxferp = input->buffer;
              io_hdr.dxfer_len = input->buffer_size;
              io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
              break;
            }
          io_hdr.sbp = sense;
          io_hdr.mx_sb_len = sizeof (sense);
          if (timeout_msec == G_MAXINT)
            io_hdr.timeout = G_MAXUINT;
          else
            io_hdr.timeout = timeout_msec;

          rc = ioctl(fd, SG_IO, &io_hdr);
          if (rc != 0)
            {
              g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "SGIO v3 ioctl failed (v4 not supported): %m");
              goto out;
            }
        }
      else
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "SGIO v4 ioctl failed: %m");
          goto out;
        }
    }

  if (!(sense[0] == 0x72 && desc[0] == 0x9 && desc[1] == 0x0c))
    {
      gchar *s = udisks_daemon_util_hexdump (sense, 32);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unexpected sense data returned:\n%s", s);
      g_free (s);
      goto out;
    }

  output->error = desc[3];
  output->count = desc[5];
  output->device = desc[12];
  output->status = desc[13];
  output->lba = (desc[11] << 16) | (desc[9] << 8) | desc[7];

  /* TODO: be more exact with the error code perhaps? */
  if (output->error != 0 || output->status & 0x01)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ATA command failed: error=0x%02x count=0x%02x status=0x%02x",
                   output->error, output->count, output->status);
      goto out;
    }

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_pm_get_state (UDisksDriveAta        *_drive,
                     GDBusMethodInvocation *invocation,
                     GVariant              *options)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  UDisksLinuxDriveObject  *object = NULL;
  UDisksDaemon *daemon;
  GUdevDevice *device = NULL;
  gint fd = -1;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;

  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);

  if (!udisks_drive_ata_get_pm_supported (UDISKS_DRIVE_ATA (drive)) ||
      !udisks_drive_ata_get_pm_enabled (UDISKS_DRIVE_ATA (drive)))
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "PM is not supported or enabled");
      goto out;
    }

  /* If a secure erase is in progress, the CHECK POWER command would be queued
   * until the erase has been completed (can easily take hours). So just return
   * 0xff which is active/idle...
   */
  if (drive->secure_erase_in_progress)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_DEVICE_BUSY,
                                             "A secure erase is in progress");
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * requests the power state of a drive.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to check power state for $(drive)");
  action_id = "org.freedesktop.udisks2.ata-check-power";

  /* TODO: maybe not check with polkit if this is OK (consider gnome-disks(1) polling all drives every few seconds) */

  /* Check that the user is authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No udev device");
      goto out;
    }

  fd = open (g_udev_device_get_device_file (device), O_RDONLY|O_NONBLOCK);
  if (fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Error opening device file %s: %m",
                                             g_udev_device_get_device_file (device));
      goto out;
    }

  {
    /* ATA8: 7.8 CHECK POWER MODE - E5h, Non-Data */
    AtaCommandInput input = {.command = 0xe5};
    AtaCommandOutput output = {0};
    if (!ata_send_command (fd,
                           -1,
                           ATA_COMMAND_PROTOCOL_NONE,
                           &input,
                           &output,
                           &error))
      {
        g_prefix_error (&error, "Error sending ATA command CHECK POWER MODE: ");
        g_dbus_method_invocation_take_error (invocation, error);
        goto out;
      }
    /* count field is used for the state, see ATA8: table 102 */
    udisks_drive_ata_complete_pm_get_state (_drive, invocation, output.count);
  }

 out:
  if (fd != -1)
    close (fd);
  g_clear_object (&device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_pm_standby (UDisksDriveAta        *_drive,
                   GDBusMethodInvocation *invocation,
                   GVariant              *options)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  UDisksLinuxDriveObject *object = NULL;
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksBlock *block = NULL;
  UDisksDaemon *daemon;
  GUdevDevice *device = NULL;
  gint fd = -1;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;
  pid_t caller_pid;

  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block_object = udisks_linux_drive_object_get_block (object, FALSE);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unable to find block device for drive");
      goto out;
    }
  block = udisks_object_peek_block (UDISKS_OBJECT (block_object));

  daemon = udisks_linux_drive_object_get_daemon (object);

  if (!udisks_drive_ata_get_pm_supported (UDISKS_DRIVE_ATA (drive)) ||
      !udisks_drive_ata_get_pm_enabled (UDISKS_DRIVE_ATA (drive)))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "PM is not supported or enabled");
      goto out;
    }

  error = NULL;
  if (!udisks_daemon_util_get_caller_pid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_pid,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * tries to put a drive into standby mode.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to put $(drive) in standby mode");
  action_id = "org.freedesktop.udisks2.ata-standby";
  if (udisks_block_get_hint_system (block))
    {
      action_id = "org.freedesktop.udisks2.ata-standby-system";
    }
  else if (!udisks_daemon_util_on_same_seat (daemon, UDISKS_OBJECT (object), caller_pid))
    {
      action_id = "org.freedesktop.udisks2.ata-standby-other-seat";
    }

  /* Check that the user is authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No udev device");
      goto out;
    }

  fd = open (g_udev_device_get_device_file (device), O_RDONLY|O_NONBLOCK);
  if (fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Error opening device file %s: %m",
                                             g_udev_device_get_device_file (device));
      goto out;
    }

  {
    /* ATA8: 7.55 STANDBY IMMEDIATE - E0h, Non-Data */
    AtaCommandInput input = {.command = 0xe0};
    AtaCommandOutput output = {0};
    if (!ata_send_command (fd,
                           -1,
                           ATA_COMMAND_PROTOCOL_NONE,
                           &input,
                           &output,
                           &error))
      {
        g_prefix_error (&error, "Error sending ATA command STANDBY IMMEDIATE: ");
        g_dbus_method_invocation_take_error (invocation, error);
        goto out;
      }
    udisks_drive_ata_complete_pm_standby (_drive, invocation);
  }

 out:
  if (fd != -1)
    close (fd);
  g_clear_object (&device);
  g_clear_object (&block_object);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_pm_wakeup (UDisksDriveAta        *_drive,
                  GDBusMethodInvocation *invocation,
                  GVariant              *options)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  UDisksLinuxDriveObject *object = NULL;
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksBlock *block = NULL;
  UDisksDaemon *daemon;
  GUdevDevice *device = NULL;
  gint fd = -1;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;
  pid_t caller_pid;
  guchar buf[4096];

  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block_object = udisks_linux_drive_object_get_block (object, FALSE);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unable to find block device for drive");
      goto out;
    }
  block = udisks_object_peek_block (UDISKS_OBJECT (block_object));

  daemon = udisks_linux_drive_object_get_daemon (object);

  if (!udisks_drive_ata_get_pm_supported (UDISKS_DRIVE_ATA (drive)) ||
      !udisks_drive_ata_get_pm_enabled (UDISKS_DRIVE_ATA (drive)))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "PM is not supported or enabled");
      goto out;
    }

  error = NULL;
  if (!udisks_daemon_util_get_caller_pid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_pid,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * tries to wake up a drive from standby mode.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to wake up $(drive) from standby mode");
  action_id = "org.freedesktop.udisks2.ata-standby";
  if (udisks_block_get_hint_system (block))
    {
      action_id = "org.freedesktop.udisks2.ata-standby-system";
    }
  else if (!udisks_daemon_util_on_same_seat (daemon, UDISKS_OBJECT (object), caller_pid))
    {
      action_id = "org.freedesktop.udisks2.ata-standby-other-seat";
    }

  /* Check that the user is authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No udev device");
      goto out;
    }

  fd = open (g_udev_device_get_device_file (device), O_RDONLY);
  if (fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Error opening device file %s: %m",
                                             g_udev_device_get_device_file (device));
      goto out;
    }

  if (read (fd, buf, sizeof (buf)) != sizeof (buf))
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Error reading %d bytes from %s: %m",
                                             (gint) sizeof (buf),
                                             g_udev_device_get_device_file (device));
      goto out;
    }

  udisks_drive_ata_complete_pm_wakeup (_drive, invocation);

 out:
  if (fd != -1)
    close (fd);
  g_clear_object (&device);
  g_clear_object (&block_object);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
ata_pm_standby_to_string (guint value)
{
  gchar *ret;
  gint seconds = -1;

  if (value == 0)
    {
      ret = g_strdup ("disabled");
      goto out;
    }
  else if (value == 253)
    {
      ret = g_strdup ("vendor-defined");
      goto out;
    }
  else if (value == 254)
    {
      ret = g_strdup ("reserved");
      goto out;
    }
  else if (value < 241)
    {
      seconds = value * 5;
    }
  else if (value < 252)
    {
      seconds = (value - 240) * 30 * 60;
    }
  else if (value == 252)
    {
      seconds = 21 * 60;
    }
  else if (value == 255)
    {
      seconds = 21 * 60 + 15;
    }

  ret = g_strdup_printf ("%d seconds", seconds);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  gint ata_pm_standby;
  gint ata_apm_level;
  gint ata_aam_level;
  UDisksLinuxDriveAta *ata;
  GUdevDevice *device;
  GVariant *configuration;
  UDisksDrive *drive;
  UDisksLinuxDriveObject *object;
} ApplyConfData;

static void
apply_conf_data_free (ApplyConfData *data)
{
  g_clear_object (&data->ata);
  g_clear_object (&data->device);
  g_variant_unref (data->configuration);
  g_clear_object (&data->drive);
  g_clear_object (&data->object);
  g_free (data);
}

static gpointer
apply_configuration_thread_func (gpointer user_data)
{
  ApplyConfData *data = user_data;
  const gchar *device_file = NULL;
  gint fd = -1;
  GError *error = NULL;

  device_file = g_udev_device_get_device_file (data->device);

  udisks_notice ("Applying configuration from %s/udisks2/%s.conf to %s",
                 PACKAGE_SYSCONF_DIR, udisks_drive_get_id (data->drive), device_file);


  fd = open (device_file, O_RDONLY|O_NONBLOCK);
  if (fd == -1)
    {
      udisks_error ("Error opening device file %s: %m", device_file);
      goto out;
    }

  if (data->ata_pm_standby != -1)
    {
      /* ATA8: 7.18 IDLE - E3h, Non-Data */
      AtaCommandInput input = {.command = 0xe3, .count = data->ata_pm_standby};
      AtaCommandOutput output = {0};
      if (!ata_send_command (fd,
                             -1,
                             ATA_COMMAND_PROTOCOL_NONE,
                             &input,
                             &output,
                             &error))
        {
          udisks_error ("Error sending ATA command IDLE (timeout=%d) to %s: %s (%s, %d)",
                        data->ata_pm_standby, device_file,
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
      else
        {
          gchar *pretty = ata_pm_standby_to_string (data->ata_pm_standby);
          udisks_notice ("Set standby timer to %s (value %d) on %s [%s]",
                         pretty, data->ata_pm_standby, device_file, udisks_drive_get_id (data->drive));
          g_free (pretty);
        }
    }

  if (data->ata_apm_level != -1)
    {
      /* ATA8: 7.48 SET FEATURES - EFh, Non-Data
       *       7.48.6 Enable/disable the APM feature set
       */
      AtaCommandInput input = {.command = 0xef, .feature = 0x05, .count = data->ata_apm_level};
      AtaCommandOutput output = {0};
      if (data->ata_apm_level == 0xff)
        {
          input.feature = 0x85;
          input.count = 0x00;
        }
      if (!ata_send_command (fd,
                             -1,
                             ATA_COMMAND_PROTOCOL_NONE,
                             &input,
                             &output,
                             &error))
        {
          udisks_error ("Error sending ATA command SET FEATURES, sub-command 0x%02x (ata_apm_level=%d) to %s: %s (%s, %d)",
                        input.feature, data->ata_apm_level, device_file,
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
      else
        {
          udisks_notice ("Set APM level to %d on %s [%s]",
                         data->ata_apm_level, device_file, udisks_drive_get_id (data->drive));
        }
    }

  if (data->ata_aam_level != -1)
    {
      /* ATA8: 7.48 SET FEATURES - EFh, Non-Data
       *       7.48.11 Enable/disable the AAM feature set
       */
      AtaCommandInput input = {.command = 0xef, .feature = 0x42, .count = data->ata_aam_level};
      AtaCommandOutput output = {0};
      if (data->ata_apm_level == 0xff)
        {
          input.feature = 0xc2;
          input.count = 0x00;
        }
      if (!ata_send_command (fd,
                             -1,
                             ATA_COMMAND_PROTOCOL_NONE,
                             &input,
                             &output,
                             &error))
        {
          udisks_error ("Error sending ATA command SET FEATURES, sub-command 0x%02x (ata_aam_level=%d) to %s: %s (%s, %d)",
                        input.feature, data->ata_aam_level, device_file,
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
      else
        {
          udisks_notice ("Set AAM value to %d on %s [%s]",
                         data->ata_aam_level, device_file, udisks_drive_get_id (data->drive));
        }
    }

 out:
  if (fd != -1)
    close (fd);
  apply_conf_data_free (data);
  return NULL;
}

/**
 * udisks_linux_drive_ata_apply_configuration:
 * @drive: A #UDisksLinuxDriveAta.
 * @device: A #GUdevDevice
 * @configuration: The configuration to apply.
 *
 * Spawns a thread to apply @configuration to @drive, if any. Does not
 * wait for the thread to terminate.
 */
void
udisks_linux_drive_ata_apply_configuration (UDisksLinuxDriveAta     *drive,
                                            GUdevDevice             *device,
                                            GVariant                *configuration)
{
  gboolean has_conf = FALSE;
  ApplyConfData *data = NULL;

  data = g_new0 (ApplyConfData, 1);
  data->ata_pm_standby = -1;
  data->ata_apm_level = -1;
  data->ata_aam_level = -1;
  data->ata = g_object_ref (drive);
  data->device = g_object_ref (device);
  data->configuration = g_variant_ref (configuration);

  data->object = udisks_daemon_util_dup_object (drive, NULL);
  if (data->object == NULL)
    goto out;

  data->drive = udisks_object_get_drive (UDISKS_OBJECT (data->object));
  if (data->drive == NULL)
    goto out;


  has_conf |= g_variant_lookup (configuration, "ata-pm-standby", "i", &data->ata_pm_standby);
  has_conf |= g_variant_lookup (configuration, "ata-apm-level", "i", &data->ata_apm_level);
  has_conf |= g_variant_lookup (configuration, "ata-aam-level", "i", &data->ata_aam_level);

  /* don't do anything if none of the configuration is set */
  if (!has_conf)
    goto out;

  /* this can easily take a long time and thus block (the drive may be in standby mode
   * and needs to spin up) - so run it in a thread
   */
  g_thread_new ("apply-conf-thread",
                apply_configuration_thread_func,
                data);

  data = NULL; /* don't free data below */

 out:
  if (data != NULL)
    apply_conf_data_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_secure_erase_update_progress_timeout (gpointer user_data)
{
  UDisksJob *job = UDISKS_JOB (user_data);
  gint64 now;
  gint64 start;
  gint64 end;
  gdouble progress;

  now = g_get_real_time ();
  start = udisks_job_get_start_time (job);
  end = udisks_job_get_expected_end_time (job);

  progress = ((gdouble) (now - start)) / (end - start);
  if (progress < 0)
    progress = 0;
  if (progress > 1)
    progress = 1;

  /* TODO: if we've exceeded the expected end time, we could add
   * another couple of minutes or so... that'd be kinda cheating
   * though, wouldn't it?
   */

  udisks_job_set_progress (job, progress);

  return TRUE; /* keep source around */
}

/**
 * udisks_linux_drive_ata_secure_erase_sync:
 * @drive: A #UDisksLinuxDriveAta.
 * @caller_uid: The unix user if of the caller requesting the operation.
 * @enhanced: %TRUE to use the enhanced version of the ATA secure erase command.
 * @error: Return location for error or %NULL.
 *
 * Performs an ATA Secure Erase opeartion. Blocks the calling thread until the operation completes.
 *
 * This operation may take a very long time (hours) to complete.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
udisks_linux_drive_ata_secure_erase_sync (UDisksLinuxDriveAta     *drive,
                                          uid_t                    caller_uid,
                                          gboolean                 enhanced,
                                          GError                 **error)
{
  gboolean ret = FALSE;
  UDisksDrive *_drive = NULL;
  UDisksLinuxDriveObject *object = NULL;
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksDaemon *daemon;
  GUdevDevice *device = NULL;
  const gchar *device_file = NULL;
  gint fd = -1;
  union
  {
    guchar buf[512];
    guint16 words[256];
  } identify;
  guint16 word_82;
  guint16 word_128;
  UDisksBaseJob *job = NULL;
  gint num_minutes = 0;
  guint timeout_id = 0;
  gboolean claimed = FALSE;
  GError *local_error = NULL;
  const gchar *pass = "xxxx";
  gboolean clear_passwd_on_failure = FALSE;

  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_ATA (drive), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  object = udisks_daemon_util_dup_object (drive, &local_error);
  if (object == NULL)
    goto out;
  _drive = udisks_object_peek_drive (UDISKS_OBJECT (object));

  block_object = udisks_linux_drive_object_get_block (object, FALSE);
  if (block_object == NULL)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Unable to find block device for drive");
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "No udev device");
      goto out;
    }

  if (drive->secure_erase_in_progress)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_DEVICE_BUSY,
                   "Secure erase in progress");
      goto out;
    }

  /* Use O_EXCL so it fails if mounted or in use */
  device_file = g_udev_device_get_device_file (device);
  fd = open (g_udev_device_get_device_file (device), O_RDONLY | O_EXCL);
  if (fd == -1)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error opening device file %s: %m",
                   device_file);
      goto out;
    }

  drive->secure_erase_in_progress = TRUE;

  claimed = TRUE;

  /* First get the IDENTIFY data directly from the drive, for sanity checks */
  {
    /* ATA8: 7.16 IDENTIFY DEVICE - ECh, PIO Data-In */
    AtaCommandInput input = {.command = 0xec};
    AtaCommandOutput output = {.buffer = identify.buf, .buffer_size = sizeof (identify.buf)};
    if (!ata_send_command (fd,
                           -1,
                           ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST,
                           &input,
                           &output,
                           &local_error))
      {
        g_prefix_error (&local_error, "Error sending ATA command IDENTIFY DEVICE: ");
        goto out;
      }
  }

  /* Support of the Security feature set is indicated in IDENTIFY
   * DEVICE and IDENTIFY PACKET DEVICE data word 82 and data word 128.
   * Security information in words 82, 89 and 90 is fixed until the
   * next power-on reset and shall not change unless DEVICE
   * CONFIGURATION OVERLAY removes support for the Security feature
   * set.  Security information in words 85, 92 and 128 are variable
   * and may change.  If the Security feature set is not supported,
   * then words 89, 90, 92 and 128 are N/A.
   *
   * word 82:  ...
   *           1     The Security feature set is supported
   *
   * word 128: 15:9  Reserved
   *           8     Master Password Capability: 0 = High, 1 = Maximum
   *           7:6   Reserved
   *           5     Enhanced security erase supported
   *           4     Security count expired
   *           3     Security frozen
   *           2     Security locked
   *           1     Security enabled
   *           0     Security supported
   */
  word_82 = GUINT16_FROM_LE (identify.words[82]);
  word_128 = GUINT16_FROM_LE (identify.words[128]);

  if (!(
        (word_82 & (1<<1)) && (word_128 & (1<<0))
        ))
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Drive does not support the ATA security feature");
      goto out;
    }

  if (word_128 & (1<<3))
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Drive is frozen, cannot perform a secure erase");
      goto out;
    }

  if (enhanced && !(word_128 & (1<<5)))
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Enhanced erase requested but not supported");
      goto out;
    }

  /* OK, all checks done, let's do this thing! */

  /* First, set up a Job object to track progress */
  num_minutes = enhanced ? 2 * GUINT16_FROM_LE (identify.words[90]) : 2 * GUINT16_FROM_LE (identify.words[89]);
  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         enhanced ? "ata-enhanced-secure-erase" : "ata-secure-erase",
                                         caller_uid, NULL);
  udisks_job_set_cancelable (UDISKS_JOB (job), FALSE);

  /* A value of 510 (255 in the IDENTIFY DATA register) means "erase
   * is expected to take _at least_ 508 minutes" ... so don't attempt
   * to predict when the job is going to end and don't report progress
   */
  if (num_minutes != 510)
    {
      udisks_job_set_expected_end_time (UDISKS_JOB (job),
                                        g_get_real_time () + num_minutes * 60LL * G_USEC_PER_SEC);
      udisks_job_set_progress_valid (UDISKS_JOB (job), TRUE);
      timeout_id = g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                                               1,
                                               on_secure_erase_update_progress_timeout,
                                               g_object_ref (job),
                                               g_object_unref);
    }

  /* Second, set the user password to 'xxxx' */
  {
    /* ATA8: 7.45 SECURITY SET PASSWORD - F1h, PIO Data-Out */
    guchar buf[512];
    AtaCommandInput input = {.command = 0xf1, .buffer = buf, .buffer_size = sizeof (buf)};
    AtaCommandOutput output = {0};
    memset (buf, 0, sizeof (buf));
    memcpy (buf + 2, pass, strlen (pass));
    if (!ata_send_command (fd,
                           -1,
                           ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE,
                           &input,
                           &output,
                           &local_error))
      {
        g_prefix_error (&local_error, "Error sending ATA command SECURITY SET PASSWORD: ");
        goto out;
      }
  }

  clear_passwd_on_failure = TRUE;

  udisks_notice ("Commencing ATA%s secure erase of %s (%s). This operation is expected to take at least %d minutes to complete",
                 enhanced ? " enhanced" : "",
                 device_file,
                 udisks_drive_get_id (_drive),
                 num_minutes);

  /* Third... do SECURITY ERASE PREPARE */
  {
    /* ATA8: 7.42 SECURITY ERASE PREPARE - F3h, Non-Data */
    AtaCommandInput input = {.command = 0xf3};
    AtaCommandOutput output = {0};
    if (!ata_send_command (fd,
                           -1,
                           ATA_COMMAND_PROTOCOL_NONE,
                           &input,
                           &output,
                           &local_error))
      {
        g_prefix_error (&local_error, "Error sending ATA command SECURITY ERASE PREPARE: ");
        goto out;
      }
  }

  /* Fourth... do SECURITY ERASE UNIT */
  {
    /* ATA8: 7.43 SECURITY ERASE UNIT - F4h, PIO Data-Out */
    guchar buf[512];
    AtaCommandInput input = {.command = 0xf4, .buffer = buf, .buffer_size = sizeof (buf)};
    AtaCommandOutput output = {0};
    memset (buf, 0, sizeof (buf));
    if (enhanced)
      buf[0] |= 0x02;
    memcpy (buf + 2, pass, strlen (pass));
    if (!ata_send_command (fd,
                           G_MAXINT, /* disable timeout */
                           ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE,
                           &input,
                           &output,
                           &local_error))
      {
        g_prefix_error (&local_error, "Error sending ATA command SECURITY ERASE UNIT (enhanced=%d): ",
                        enhanced ? 1 : 0);
        goto out;
      }
  }

  clear_passwd_on_failure = FALSE;

  udisks_linux_block_object_reread_partition_table (UDISKS_LINUX_BLOCK_OBJECT (block_object));

  ret = TRUE;

 out:
  /* Clear the password if something went wrong */
  if (clear_passwd_on_failure)
    {
      /* ATA8: 7.41 SECURITY DISABLE PASSWORD - F6h, PIO Data-Out */
      guchar buf[512];
      AtaCommandInput input = {.command = 0xf6, .buffer = buf, .buffer_size = sizeof (buf)};
      AtaCommandOutput output = {0};
      GError *cleanup_error = NULL;
      memset (buf, 0, sizeof (buf));
      memcpy (buf + 2, pass, strlen (pass));
      if (!ata_send_command (fd,
                             -1,
                             ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE,
                             &input,
                             &output,
                             &cleanup_error))
        {
          udisks_error ("Failed to clear user password '%s' on %s (%s) while attemping clean-up after a failed secure erase operation. You may need to manually unlock the drive. The error was: %s (%s, %d)",
                        pass,
                        device_file,
                        udisks_drive_get_id (_drive),
                        cleanup_error->message, g_quark_to_string (cleanup_error->domain), cleanup_error->code);
          g_clear_error (&cleanup_error);
        }
      else
        {
          udisks_info ("Successfully removed user password '%s' from %s (%s) during clean-up after a failed secure erase operation",
                       pass,
                       device_file,
                       udisks_drive_get_id (_drive));
        }
    }

  if (ret)
    {
      udisks_notice ("Finished securely erasing %s (%s)",
                     device_file,
                     udisks_drive_get_id (_drive));
    }
  else
    {
      udisks_notice ("Error securely erasing %s (%s): %s (%s, %d)",
                     device_file,
                     udisks_drive_get_id (_drive),
                     local_error->message, g_quark_to_string (local_error->domain), local_error->code);
    }

  if (claimed)
    drive->secure_erase_in_progress = FALSE;

  if (timeout_id > 0)
    g_source_remove (timeout_id);
  if (job != NULL)
    {
      /* propagate error, if any */
      if (local_error == NULL)
        {
          udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, "");
        }
      else
        {
          gchar *s = g_strdup_printf ("Secure Erase failed: %s (%s, %d)",
                                      local_error->message, g_quark_to_string (local_error->domain), local_error->code);
          udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, s);
          g_free (s);
        }
    }
  if (local_error != NULL)
    g_propagate_error (error, local_error);
  if (fd != -1)
    close (fd);
  g_clear_object (&device);
  g_clear_object (&block_object);
  g_clear_object (&object);
  return ret;
}

static gboolean
handle_security_erase_unit (UDisksDriveAta        *_drive,
                            GDBusMethodInvocation *invocation,
                            GVariant              *options)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  UDisksLinuxDriveObject *object = NULL;
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksDaemon *daemon;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;
  uid_t caller_uid;
  gid_t caller_gid;
  gboolean enhanced = FALSE;
  UDisksInhibitCookie *inhibit_cookie = NULL;

  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block_object = udisks_linux_drive_object_get_block (object, FALSE);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unable to find block device for drive");
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &caller_gid,
                                               NULL,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  g_variant_lookup (options, "enhanced", "b", &enhanced);

  /* Translators: Shown in authentication dialog when the user
   * requests erasing a hard disk using the SECURE ERASE UNIT command.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to perform a secure erase of $(drive)");
  action_id = "org.freedesktop.udisks2.ata-secure-erase";

  /* Check that the user is authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  inhibit_cookie = udisks_daemon_util_inhibit_system_sync (N_("Formatting Device"));

  if (!udisks_linux_drive_ata_secure_erase_sync (drive, caller_uid, enhanced, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  udisks_linux_block_object_reread_partition_table (UDISKS_LINUX_BLOCK_OBJECT (block_object));

  udisks_drive_ata_complete_security_erase_unit (_drive, invocation);

 out:
  udisks_daemon_util_uninhibit_system_sync (inhibit_cookie);
  g_clear_object (&block_object);
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

  iface->handle_pm_get_state = handle_pm_get_state;
  iface->handle_pm_standby = handle_pm_standby;
  iface->handle_pm_wakeup = handle_pm_wakeup;

  iface->handle_security_erase_unit = handle_security_erase_unit;
}
