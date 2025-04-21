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
#include <sys/file.h>
#include <fcntl.h>

#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <mntent.h>

#include <glib/gstdio.h>
#include <errno.h>

#include <blockdev/smart.h>

#include "udiskslogging.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxdriveata.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksbasejob.h"
#include "udiskssimplejob.h"
#include "udisksthreadedjob.h"
#include "udisksata.h"
#include "udiskslinuxdevice.h"
#include "udisksconfigmanager.h"

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

  UDisksThreadedJob *selftest_job;

  gboolean     secure_erase_in_progress;
  unsigned long drive_read, drive_write;
  gboolean     standby_enabled;

  gboolean     smart_is_from_blob;
  guint64      smart_updated;
  BDSmartATA  *smart_data;
};

struct _UDisksLinuxDriveAtaClass
{
  UDisksDriveAtaSkeletonClass parent_class;
};

static void drive_ata_iface_init (UDisksDriveAtaIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxDriveAta, udisks_linux_drive_ata, UDISKS_TYPE_DRIVE_ATA_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_DRIVE_ATA, drive_ata_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_drive_ata_finalize (GObject *object)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (object);

  bd_smart_ata_free (drive->smart_data);

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

G_LOCK_DEFINE_STATIC (object_lock);

static const gchar *
selftest_status_to_string (BDSmartATASelfTestStatus status)
{
  static const gchar * self_test_status_str[] = {
    "success",           /* BD_SMART_ATA_SELF_TEST_STATUS_COMPLETED_NO_ERROR */
    "aborted",           /* BD_SMART_ATA_SELF_TEST_STATUS_ABORTED_HOST */
    "interrupted",       /* BD_SMART_ATA_SELF_TEST_STATUS_INTR_HOST_RESET */
    "fatal",             /* BD_SMART_ATA_SELF_TEST_STATUS_ERROR_FATAL */
    "error_unknown",     /* BD_SMART_ATA_SELF_TEST_STATUS_ERROR_UNKNOWN */
    "error_electrical",  /* BD_SMART_ATA_SELF_TEST_STATUS_ERROR_ELECTRICAL */
    "error_servo",       /* BD_SMART_ATA_SELF_TEST_STATUS_ERROR_SERVO */
    "error_read",        /* BD_SMART_ATA_SELF_TEST_STATUS_ERROR_READ */
    "error_handling",    /* BD_SMART_ATA_SELF_TEST_STATUS_ERROR_HANDLING */
  };

  if (status == BD_SMART_ATA_SELF_TEST_STATUS_IN_PROGRESS)
    return "inprogress";
  if (status <= BD_SMART_ATA_SELF_TEST_STATUS_ERROR_HANDLING)
    return self_test_status_str[status];
  return "";
}

static void
count_failing_attrs (BDSmartATAAttribute **a,
                     gint *num_attributes_failing,
                     gint *num_attributes_failed_in_the_past,
                     gint64 *num_bad_sectors)
{
  *num_attributes_failing = 0;
  *num_attributes_failed_in_the_past = 0;
  *num_bad_sectors = 0;

  for (; *a; a++)
    {
      if ((*a)->failing_now)
        *num_attributes_failing += 1;
      if ((*a)->failed_past)
        *num_attributes_failed_in_the_past += 1;

      if (g_strcmp0 ((*a)->well_known_name, "reallocated-sector-count") == 0 ||
          g_strcmp0 ((*a)->well_known_name, "current-pending-sector") == 0)
        *num_bad_sectors += (*a)->value_raw;
    }
}

/* may be called from *any* thread when the SMART data has been updated */
static void
update_smart (UDisksLinuxDriveAta *drive,
              UDisksLinuxDevice   *device)
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
  gint64 num_bad_sectors = -1;
  guint16 word_82 = 0;
  guint16 word_85 = 0;

#ifdef HAVE_SMART
  supported = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_SMART");
  enabled = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_SMART_ENABLED");
  if (!supported && device->ata_identify_device_data)
    {
      /* ATA8: 7.16 IDENTIFY DEVICE - ECh, PIO Data-In - Table 29 IDENTIFY DEVICE data */
      word_82 = udisks_ata_identify_get_word (device->ata_identify_device_data, 82);
      word_85 = udisks_ata_identify_get_word (device->ata_identify_device_data, 85);
      supported = word_82 & (1<<0);
      enabled = word_85 & (1<<0);
    }
#endif

  G_LOCK (object_lock);
  if ((drive->smart_is_from_blob || enabled) && drive->smart_updated > 0)
    {
      if (drive->smart_is_from_blob)
        supported = enabled = TRUE;
      updated = drive->smart_updated;
      supported = drive->smart_data->smart_supported;
      enabled  = drive->smart_data->smart_enabled;
      failing = !drive->smart_data->overall_status_passed;
      temperature = drive->smart_data->temperature;
      power_on_seconds = drive->smart_data->power_on_time /* minutes */ * 60;
      selftest_status = selftest_status_to_string (drive->smart_data->self_test_status);
      selftest_percent_remaining = drive->smart_data->self_test_percent_remaining;
      count_failing_attrs (drive->smart_data->attributes,
                           &num_attributes_failing,
                           &num_attributes_failed_in_the_past,
                           &num_bad_sectors);
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
           UDisksLinuxDevice   *device)
{
  gboolean pm_supported = FALSE;
  gboolean pm_enabled = FALSE;
  gboolean apm_supported = FALSE;
  gboolean apm_enabled = FALSE;
  gboolean aam_supported = FALSE;
  gboolean aam_enabled = FALSE;
  gboolean write_cache_supported = FALSE;
  gboolean write_cache_enabled = FALSE;
  gboolean read_lookahead_supported = FALSE;
  gboolean read_lookahead_enabled = FALSE;
  gint aam_vendor_recommended_value = 0;
  guint16 word_82 = 0;
  guint16 word_83 = 0;
  guint16 word_85 = 0;
  guint16 word_86 = 0;
  guint16 word_94 = 0;

  pm_supported = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_PM");
  pm_enabled = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_PM_ENABLED");
  apm_supported = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_APM");
  apm_enabled = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_APM_ENABLED");
  aam_supported = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_AAM");
  aam_enabled = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_AAM_ENABLED");
  write_cache_supported = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_WRITE_CACHE");
  write_cache_enabled = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_WRITE_CACHE_ENABLED");
  read_lookahead_supported = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_READ_LOOKAHEAD");
  read_lookahead_enabled = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_READ_LOOKAHEAD_ENABLED");
  aam_vendor_recommended_value = g_udev_device_get_property_as_int (device->udev_device, "ID_ATA_FEATURE_SET_AAM_VENDOR_RECOMMENDED_VALUE");

  if (device->ata_identify_device_data)
    {
      /* ATA8: 7.16 IDENTIFY DEVICE - ECh, PIO Data-In - Table 29 IDENTIFY DEVICE data */
      word_82 = udisks_ata_identify_get_word (device->ata_identify_device_data, 82);
      word_85 = udisks_ata_identify_get_word (device->ata_identify_device_data, 85);

      /* available in udev since < 2012 */
      if (!g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA"))
        {
          word_83 = udisks_ata_identify_get_word (device->ata_identify_device_data, 83);
          word_86 = udisks_ata_identify_get_word (device->ata_identify_device_data, 86);
          word_94 = udisks_ata_identify_get_word (device->ata_identify_device_data, 94);

          pm_supported  = word_82 & (1<<3);
          pm_enabled    = word_85 & (1<<3);
          apm_supported = word_83 & (1<<3);
          apm_enabled   = word_86 & (1<<3);
          aam_supported = word_83 & (1<<9);
          aam_enabled   = word_86 & (1<<9);
          if (aam_supported)
            aam_vendor_recommended_value = (word_94 >> 8);
          write_cache_supported    = word_82 & (1<<5);
          write_cache_enabled      = word_85 & (1<<5);
        }

      /* added recently, unable to distinguish between "not supported by device"
       * and "not implemented by udev"
       */
      if (!read_lookahead_supported)
        {
          read_lookahead_supported = word_82 & (1<<6);
          read_lookahead_enabled   = word_85 & (1<<6);
        }
    }

  g_object_freeze_notify (G_OBJECT (drive));
  udisks_drive_ata_set_pm_supported (UDISKS_DRIVE_ATA (drive), !!pm_supported);
  udisks_drive_ata_set_pm_enabled (UDISKS_DRIVE_ATA (drive), !!pm_enabled);
  udisks_drive_ata_set_apm_supported (UDISKS_DRIVE_ATA (drive), !!apm_supported);
  udisks_drive_ata_set_apm_enabled (UDISKS_DRIVE_ATA (drive), !!apm_enabled);
  udisks_drive_ata_set_aam_supported (UDISKS_DRIVE_ATA (drive), !!aam_supported);
  udisks_drive_ata_set_aam_enabled (UDISKS_DRIVE_ATA (drive), !!aam_enabled);
  udisks_drive_ata_set_aam_vendor_recommended_value (UDISKS_DRIVE_ATA (drive), aam_vendor_recommended_value);
  udisks_drive_ata_set_write_cache_supported (UDISKS_DRIVE_ATA (drive), !!write_cache_supported);
  udisks_drive_ata_set_write_cache_enabled (UDISKS_DRIVE_ATA (drive), !!write_cache_enabled);
  udisks_drive_ata_set_read_lookahead_supported (UDISKS_DRIVE_ATA (drive), !!read_lookahead_supported);
  udisks_drive_ata_set_read_lookahead_enabled (UDISKS_DRIVE_ATA (drive), !!read_lookahead_enabled);
  g_object_thaw_notify (G_OBJECT (drive));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_security (UDisksLinuxDriveAta *drive,
                 UDisksLinuxDevice   *device)
{
  gint erase_unit = 0;
  gint enhanced_erase_unit = 0;
  gboolean frozen = FALSE;
  gboolean security_supported = FALSE;
  G_GNUC_UNUSED gboolean security_enabled = FALSE;
  guint16 word_82 = 0;
  guint16 word_85 = 0;
  guint16 word_89 = 0;
  guint16 word_90 = 0;
  guint16 word_128 = 0;

  security_supported = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_SECURITY");
  security_enabled = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_SECURITY_ENABLED");
  erase_unit = g_udev_device_get_property_as_int (device->udev_device, "ID_ATA_FEATURE_SET_SECURITY_ERASE_UNIT_MIN");
  enhanced_erase_unit = g_udev_device_get_property_as_int (device->udev_device, "ID_ATA_FEATURE_SET_SECURITY_ENHANCED_ERASE_UNIT_MIN");
  frozen = g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA_FEATURE_SET_SECURITY_FROZEN");

  if (!g_udev_device_get_property_as_boolean (device->udev_device, "ID_ATA")
      && device->ata_identify_device_data)
    {
      /* ATA8: 7.16 IDENTIFY DEVICE - ECh, PIO Data-In - Table 29 IDENTIFY DEVICE data */
      word_82  = udisks_ata_identify_get_word (device->ata_identify_device_data, 82);
      word_85  = udisks_ata_identify_get_word (device->ata_identify_device_data, 85);
      word_89  = udisks_ata_identify_get_word (device->ata_identify_device_data, 89);
      word_90  = udisks_ata_identify_get_word (device->ata_identify_device_data, 90);
      word_128 = udisks_ata_identify_get_word (device->ata_identify_device_data, 128);

      security_supported  = word_82 & (1<<1);
      security_enabled    = word_85 & (1<<1);
      if (security_supported)
        {
          erase_unit = (word_89 & 0xff) * 2;
          enhanced_erase_unit = (word_90 & 0xff) * 2;
        }
      frozen = word_128 & (1<<3);
    }

  g_object_freeze_notify (G_OBJECT (drive));
  /* TODO: export Security{Supported,Enabled} properties
  udisks_drive_ata_set_security_supported (UDISKS_DRIVE_ATA (drive), !!security_supported);
  udisks_drive_ata_set_security_enabled (UDISKS_DRIVE_ATA (drive), !!security_enabled);
  */
  udisks_drive_ata_set_security_erase_unit_minutes (UDISKS_DRIVE_ATA (drive), erase_unit);
  udisks_drive_ata_set_security_enhanced_erase_unit_minutes (UDISKS_DRIVE_ATA (drive), enhanced_erase_unit);
  udisks_drive_ata_set_security_frozen (UDISKS_DRIVE_ATA (drive), !!frozen);
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
  UDisksLinuxDevice *device;
  device = udisks_linux_drive_object_get_device (object, FALSE /* get_hw */);
  if (device == NULL)
    goto out;

  update_smart (drive, device);
  update_pm (drive, device);
  update_security (drive, device);

 out:
  /* ensure property changes are sent before the method return */
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (drive));
  if (device != NULL)
    g_object_unref (device);

  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean update_io_stats (UDisksLinuxDriveAta *drive, UDisksLinuxDevice *device)
{
  const gchar *drivepath = g_udev_device_get_sysfs_path (device->udev_device);
  gchar statpath[PATH_MAX];
  unsigned long drive_read, drive_write;
  FILE *statf;
  gboolean noio = FALSE;
  snprintf (statpath, sizeof(statpath), "%s/stat", drivepath);
  statf = fopen (statpath, "r");
  if (statf == NULL)
    {
      udisks_warning ("Failed to open %s\n", statpath);
    }
  else
    {
      if (fscanf (statf, "%lu %*u %*u %*u %lu", &drive_read, &drive_write) != 2)
        {
          udisks_warning ("Failed to read %s\n", statpath);
        }
      else
        {
          noio = drive_read == drive->drive_read && drive_write == drive->drive_write;
          udisks_debug ("drive_read=%lu, drive_write=%lu, old_drive_read=%lu, old_drive_write=%lu\n",
                        drive_read, drive_write, drive->drive_read, drive->drive_write);
          drive->drive_read = drive_read;
          drive->drive_write = drive_write;
        }
      fclose (statf);
    }
  return noio;
}

static BDExtraArg **
build_smart_extra_args (UDisksLinuxDevice *device)
{
  BDExtraArg **extra;

  if (!udisks_linux_device_is_dm_multipath (device))
    return NULL;

  /* Now that we're in UDisksLinuxDriveAta it means the device already passed
   * earlier selection for hardware type. The most likely scenario here are
   * SATA drives connected to a SAS HBA, so hardcode the SCSI to ATA Translation (SAT)
   * protocol for ATA PASS THROUGH SCSI.
   */
  extra = g_new0 (BDExtraArg *, 2);
  extra[0] = bd_extra_arg_new ("--device=sat,auto", NULL);
  return extra;
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
  UDisksLinuxDevice *device = NULL;
  GError *l_error = NULL;
  BDSmartATA *data;
  gboolean ret = FALSE;

  object = udisks_daemon_util_dup_object (drive, error);
  if (object == NULL)
    goto out;

  if (drive->secure_erase_in_progress)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_DEVICE_BUSY,
                           "Secure erase in progress");
      goto out;
    }

  device = udisks_linux_drive_object_get_device (object, FALSE /* get_hw */);
  if (device == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "No udev device");
      goto out;
    }

  /* TODO: use cancellable */

  if (simulate_path != NULL)
    {
      guint8 *blob;
      gsize blob_len;

      if (!g_file_get_contents (simulate_path,
                                (gchar **)&blob,
                                &blob_len,
                                error))
        {
          goto out;
        }

      data = bd_smart_ata_get_info_from_data (blob,
                                              blob_len,
                                              &l_error);
      g_free (blob);
    }
  else
    {
      BDExtraArg **extra;
      guchar count;
      gboolean noio = FALSE;
      gboolean awake;
      const gchar *smart_access;

      smart_access = g_udev_device_get_property (device->udev_device, "ID_ATA_SMART_ACCESS");
      if (g_strcmp0 (smart_access, "none") == 0)
        {
          /* FIXME: find a better error code */
          g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_CANCELLED,
                               "Refusing any I/O due to ID_ATA_SMART_ACCESS being set to 'none'");
          goto out;
        }

      if (drive->standby_enabled)
        noio = update_io_stats (drive, device);
      if (!udisks_ata_get_pm_state (g_udev_device_get_device_file (device->udev_device), error, &count))
        goto out;
      awake = count == 0xFF || count == 0x80;
      /* don't wake up disk unless specically asked to */
      if (nowakeup && (!awake || noio))
        {
          g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_WOULD_WAKEUP,
                               "Disk is in sleep mode and the nowakeup option was passed");
          goto out_io;
        }

      extra = build_smart_extra_args (device);
      data = bd_smart_ata_get_info (g_udev_device_get_device_file (device->udev_device),
                                    (const BDExtraArg **) extra,
                                    &l_error);
      bd_extra_arg_list_free (extra);
    }

  if (data == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED, l_error->message);
      g_clear_error (&l_error);
      goto out;
    }

  G_LOCK (object_lock);
  bd_smart_ata_free (drive->smart_data);
  drive->smart_data = data;
  drive->smart_is_from_blob = (simulate_path != NULL);
  drive->smart_updated = time (NULL);
  G_UNLOCK (object_lock);

  update_smart (drive, device);

  ret = TRUE;

  /* ensure property changes are sent before the method return */
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (drive));

 out_io:
  /* update stats again to account for the IO we just did to read the SMART info */
  if (drive->standby_enabled)
    update_io_stats (drive, device);

 out:
  g_clear_object (&device);
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
static gboolean
udisks_linux_drive_ata_smart_selftest_sync (UDisksLinuxDriveAta  *drive,
                                            const gchar          *type,
                                            GCancellable         *cancellable,
                                            GError              **error)
{
  UDisksLinuxDriveObject *object;
  UDisksLinuxDevice *device = NULL;
  GError *l_error = NULL;
  gboolean ret = FALSE;
  BDSmartSelfTestOp op;
  BDExtraArg **extra = NULL;

  object = udisks_daemon_util_dup_object (drive, error);
  if (object == NULL)
    goto out;

  device = udisks_linux_drive_object_get_device (object, FALSE /* get_hw */);
  if (device == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "No udev device");
      goto out;
    }

  if (g_strcmp0 (type, "short") == 0)
    op = BD_SMART_SELF_TEST_OP_SHORT;
  else if (g_strcmp0 (type, "extended") == 0)
    op = BD_SMART_SELF_TEST_OP_LONG;
  else if (g_strcmp0 (type, "offline") == 0)
    op = BD_SMART_SELF_TEST_OP_OFFLINE;
  else if (g_strcmp0 (type, "conveyance") == 0)
    op = BD_SMART_SELF_TEST_OP_CONVEYANCE;
  else if (g_strcmp0 (type, "abort") == 0)
    op = BD_SMART_SELF_TEST_OP_ABORT;
  else
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "unknown type %s", type);
      goto out;
    }

  extra = build_smart_extra_args (device);
  if (!bd_smart_device_self_test (g_udev_device_get_device_file (device->udev_device),
                                  op,
                                  (const BDExtraArg **) extra,
                                  &l_error))
    {
      g_set_error_literal (error,
                           UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           l_error->message);
      g_clear_error (&l_error);
      goto out;
    }

  ret = TRUE;

 out:
  bd_extra_arg_list_free (extra);
  g_clear_object (&device);
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
  block_object = udisks_linux_drive_object_get_block (object, FALSE);
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
      udisks_debug ("Error updating ATA smart for %s: %s (%s, %d)",
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
  GVariantBuilder builder;
  BDSmartATAAttribute **a;

  G_LOCK (object_lock);
  if (drive->smart_data == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "SMART data not collected");
    }
  else
    {
      g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ysqiiixia{sv})"));
      for (a = drive->smart_data->attributes; *a; a++)
        {
          gchar *well_known_name = NULL;

          if ((*a)->well_known_name == NULL)
            well_known_name = g_strdup_printf ("attribute-%d", (*a)->id);

          g_variant_builder_add (&builder,
                                 "(ysqiiixia{sv})",
                                 (*a)->id,
                                 well_known_name ? well_known_name : (*a)->well_known_name,
                                 (*a)->flags,
                                 (*a)->value,
                                 (*a)->worst,
                                 (*a)->threshold,
                                 (*a)->pretty_value,
                                 (*a)->pretty_value_unit,
                                 NULL); /* expansion unused for now */
          g_free (well_known_name);
        }
      udisks_drive_ata_complete_smart_get_attributes (UDISKS_DRIVE_ATA (drive), invocation,
                                                      g_variant_builder_end (&builder));
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
  UDisksLinuxDriveObject *object;
  UDisksLinuxBlockObject *block_object = NULL;
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
  block_object = udisks_linux_drive_object_get_block (object, FALSE);
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
  g_clear_object (&block_object);
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
      still_in_progress = drive->smart_data && drive->smart_data->self_test_status == BD_SMART_ATA_SELF_TEST_STATUS_IN_PROGRESS;
      progress = (100.0 - (drive->smart_data ? drive->smart_data->self_test_percent_remaining : 0)) / 100.0;
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
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksDaemon *daemon;
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  uid_t caller_uid;
  GError *error;

  error = NULL;
  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);
  block_object = udisks_linux_drive_object_get_block (object, FALSE);
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
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
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
                                                                                    FALSE,
                                                                                    selftest_job_func,
                                                                                    g_object_ref (drive),
                                                                                    g_object_unref,
                                                                                    NULL)); /* GCancellable */
      udisks_threaded_job_start (drive->selftest_job);
    }
  G_UNLOCK (object_lock);

  udisks_drive_ata_complete_smart_selftest_start (UDISKS_DRIVE_ATA (drive), invocation);

 out:
  g_clear_object (&object);
  g_clear_object (&block_object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static gboolean
handle_smart_set_enabled (UDisksDriveAta        *_drive,
                          GDBusMethodInvocation *invocation,
                          gboolean               value,
                          GVariant              *options)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  UDisksLinuxDriveObject *object = NULL;
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksDaemon *daemon;
  UDisksLinuxProvider *provider;
  GError *error = NULL;
  UDisksLinuxDevice *device = NULL;
  const gchar *message;
  const gchar *action_id;
  uid_t caller_uid;
  BDExtraArg **extra = NULL;

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
  provider = udisks_daemon_get_linux_provider (daemon);
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  if (value)
    {
      /* Translators: Shown in authentication dialog when the user
       * requests enabling SMART on a disk.
       *
       * Do not translate $(drive), it's a placeholder and
       * will be replaced by the name of the drive/device in question
       */
      message = N_("Authentication is required to enable SMART on $(drive)");
    }
  else
    {
      /* Translators: Shown in authentication dialog when the user
       * requests enabling SMART on a disk.
       *
       * Do not translate $(drive), it's a placeholder and
       * will be replaced by the name of the drive/device in question
       */
      message = N_("Authentication is required to disable SMART on $(drive)");
    }
  action_id = "org.freedesktop.udisks2.ata-smart-enable-disable";

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

  extra = build_smart_extra_args (device);
  if (!bd_smart_set_enabled (g_udev_device_get_device_file (device->udev_device),
                             value,
                             (const BDExtraArg **) extra,
                             &error))
    {
      if (g_error_matches (error, BD_SMART_ERROR, BD_SMART_ERROR_TECH_UNAVAIL))
        {
          UDisksAtaCommandInput input = {.command = 0xb0};
          UDisksAtaCommandOutput output = {0};
          gint fd;

          /* not supported by the libblockdev smart plugin */
          g_clear_error (&error);

          fd = open (g_udev_device_get_device_file (device->udev_device), O_RDONLY|O_NONBLOCK);
          if (fd == -1)
            {
              g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                     "Error opening device file %s while changing SMART status: %m",
                                                     g_udev_device_get_device_file (device->udev_device));
              goto out;
            }

          /* ATA8: 7.53.4 SMART ENABLE OPERATIONS - B0h/D8h, Non-Data
           *       7.53.2 SMART DISABLE OPERATIONS - B0h/D9h, Non-Data
           */
          if (value)
            input.feature = 0xd8;
          else
            input.feature = 0xd9;
          input.lba = 0x004fc2; /* will be encoded as 0xc2 0x4f 0x00 as per the ATA spec */
          if (!udisks_ata_send_command_sync (fd,
                                             -1,
                                             UDISKS_ATA_COMMAND_PROTOCOL_NONE,
                                             &input,
                                             &output,
                                             &error))
            {
              g_prefix_error (&error, "Error sending ATA command SMART, sub-command %s OPERATIONS: ",
                              value ? "ENABLE" : "DISABLE");
              g_dbus_method_invocation_take_error (invocation, error);
              close (fd);
              goto out;
            }
          close (fd);
        }
      else
        {
          g_dbus_method_invocation_return_error_literal (invocation,
                                                         UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                         error->message);
          g_clear_error (&error);
          goto out;
        }
    }

  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (block_object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);

  /* Reread new IDENTIFY data */
  if (!udisks_linux_device_reprobe_sync (device,
                                         udisks_linux_provider_get_udev_client (provider),
                                         NULL, &error))
    {
      g_prefix_error (&error, "Error reprobing device: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* if we just enabled SMART, re-read SMART data before returning */
  if (value)
    {
      if (!udisks_linux_drive_ata_refresh_smart_sync (drive,
                                                      FALSE, /* nowakeup */
                                                      NULL, /* simulate_path */
                                                      NULL, /* cancellable */
                                                      &error))
        {
          g_prefix_error (&error, "Error updating SMART data: ");
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
    }
  else
    {
      update_smart (drive, device);
    }
  /* ensure property changes are sent before the method return */
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (drive));

  udisks_drive_ata_complete_smart_set_enabled (_drive, invocation);

 out:
  bd_extra_arg_list_free (extra);
  g_clear_object (&device);
  g_clear_object (&block_object);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_drive_ata_get_pm_state:
 * @drive: A #UDisksLinuxDriveAta.
 * @error: Return location for error.
 * @pm_state: Return location for the current power state value.
 *
 * Get the current power mode state.
 *
 * The format of @pm_state is the result obtained from sending the
 * ATA command `CHECK POWER MODE` to the drive.
 *
 * Known values include:
 *  - `0x00`: Device is in PM2: Standby state.
 *  - `0x40`: Device is in the PM0: Active state, the NV Cache power mode is enabled, and the spindle is spun down or spinning down.
 *  - `0x41`: Device is in the PM0: Active state, the NV Cache power mode is enabled, and the spindle is spun up or spinning up.
 *  - `0x80`: Device is in PM1: Idle state.
 *  - `0xff`: Device is in the PM0: Active state or PM1: Idle State.
 *
 * Typically user interfaces will report "Drive is spun down" if @pm_state is
 * 0x00 and "Drive is spun up" otherwise.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
udisks_linux_drive_ata_get_pm_state (UDisksLinuxDriveAta  *drive,
                                     GError              **error,
                                     guchar               *pm_state)
{
  UDisksLinuxDriveObject *object;
  UDisksLinuxDevice *device = NULL;
  gboolean ret = FALSE;

  object = udisks_daemon_util_dup_object (drive, error);
  if (object == NULL)
    goto out;

  if (!udisks_drive_ata_get_pm_supported (UDISKS_DRIVE_ATA (drive)) ||
      !udisks_drive_ata_get_pm_enabled (UDISKS_DRIVE_ATA (drive)))
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "PM is not supported or enabled");
      goto out;
    }

  /* If a secure erase is in progress, the CHECK POWER command would be queued
   * until the erase has been completed (can easily take hours).
   */
  if (drive->secure_erase_in_progress)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_DEVICE_BUSY,
                           "A secure erase is in progress");
      goto out;
    }

  /* TODO: some SSD controllers may block for considerable time when trimming large amount of blocks */

  device = udisks_linux_drive_object_get_device (object, FALSE /* get_hw */);
  if (device == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "No udev device");
      goto out;
    }

  ret = udisks_ata_get_pm_state (g_udev_device_get_device_file (device->udev_device), error, pm_state);

 out:
  g_clear_object (&device);
  g_clear_object (&object);

  return ret;
}

static gboolean
handle_pm_get_state (UDisksDriveAta        *_drive,
                     GDBusMethodInvocation *invocation,
                     GVariant              *options)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  UDisksLinuxDriveObject  *object = NULL;
  UDisksDaemon *daemon;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;
  guchar count;

  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
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
  daemon = udisks_linux_drive_object_get_daemon (object);
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  if (udisks_linux_drive_ata_get_pm_state (drive, &error, &count))
    udisks_drive_ata_complete_pm_get_state (_drive, invocation, count);
  else
    g_dbus_method_invocation_take_error (invocation, error);

 out:
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_pm_standby_wakeup (UDisksDriveAta        *_drive,
                          GDBusMethodInvocation *invocation,
                          GVariant              *options,
                          gboolean              do_wakeup)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  UDisksLinuxDriveObject *object = NULL;
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksBlock *block = NULL;
  UDisksDaemon *daemon;
  UDisksLinuxDevice *device = NULL;
  gint fd = -1;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;
  uid_t caller_uid;
  guchar buf[4096];
  int open_flags = (do_wakeup) ? (O_RDONLY) : (O_RDONLY|O_NONBLOCK);

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
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * tries to wake up a drive from standby mode or tries to put a drive into
   * standby mode.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = (do_wakeup) ? N_("Authentication is required to wake up $(drive) from standby mode") :
                          N_("Authentication is required to put $(drive) in standby mode");
  action_id = "org.freedesktop.udisks2.ata-standby";
  if (udisks_block_get_hint_system (block))
    {
      action_id = "org.freedesktop.udisks2.ata-standby-system";
    }
  else if (!udisks_daemon_util_on_user_seat (daemon, UDISKS_OBJECT (object), caller_uid))
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

  device = udisks_linux_drive_object_get_device (object, FALSE /* get_hw */);
  if (device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No udev device");
      goto out;
    }

  fd = open (g_udev_device_get_device_file (device->udev_device), open_flags);
  if (fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Error opening device file %s while changing PM state: %m",
                                             g_udev_device_get_device_file (device->udev_device));
      goto out;
    }

  if (do_wakeup)
   {
      if (read (fd, buf, sizeof (buf)) != sizeof (buf))
        {
          g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                 "Error reading %d bytes from %s: %m",
                                                 (gint) sizeof (buf),
                                                 g_udev_device_get_device_file (device->udev_device));
          goto out;
        }
      udisks_drive_ata_complete_pm_wakeup (_drive, invocation);
   }
  else
   {
     /* ATA8: 7.55 STANDBY IMMEDIATE - E0h, Non-Data */
     UDisksAtaCommandInput input = {.command = 0xe0};
     UDisksAtaCommandOutput output = {0};
     if (!udisks_ata_send_command_sync (fd,
                                        -1,
                                        UDISKS_ATA_COMMAND_PROTOCOL_NONE,
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


static gboolean
handle_pm_standby (UDisksDriveAta        *_drive,
                   GDBusMethodInvocation *invocation,
                   GVariant              *options)
{
 return handle_pm_standby_wakeup (_drive, invocation, options, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_pm_wakeup (UDisksDriveAta        *_drive,
                  GDBusMethodInvocation *invocation,
                  GVariant              *options)
{
  return handle_pm_standby_wakeup (_drive, invocation, options, TRUE);
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
  gboolean ata_write_cache_enabled;
  gboolean ata_write_cache_enabled_set;
  gboolean ata_read_lookahead_enabled;
  gboolean ata_read_lookahead_enabled_set;
  UDisksLinuxDriveAta *ata;
  UDisksLinuxDevice *device;
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

static void
apply_configuration_thread_func (GTask        *task,
                                 gpointer      source_object,
                                 gpointer      task_data,
                                 GCancellable *cancellable)
{
  ApplyConfData *data = task_data;
  UDisksDaemon *daemon;
  const gchar *device_file = NULL;
  gint fd = -1;
  GError *error = NULL;

  daemon = udisks_linux_drive_object_get_daemon (data->object);
  device_file = g_udev_device_get_device_file (data->device->udev_device);

  udisks_notice ("Applying configuration from %s/%s.conf to %s",
                 udisks_config_manager_get_config_dir (udisks_daemon_get_config_manager (daemon)),
                 udisks_drive_get_id (data->drive), device_file);

  /* Use O_RDRW instead of O_RDONLY to force a 'change' uevent so properties are updated */
  fd = open (device_file, O_RDWR|O_NONBLOCK);
  if (fd == -1)
    {
      udisks_critical ("Error opening device file %s while applying ATA configuration: %m", device_file);
      goto out;
    }

  if (data->ata_apm_level != -1)
    {
      /* ATA8: 7.48 SET FEATURES - EFh, Non-Data
       *       7.48.6 Enable/disable the APM feature set
       */
      UDisksAtaCommandInput input = {.command = 0xef, .feature = 0x05, .count = data->ata_apm_level};
      UDisksAtaCommandOutput output = {0};
      if (data->ata_apm_level == 0xff)
        {
          input.feature = 0x85;
          input.count = 0x00;
        }
      if (!udisks_ata_send_command_sync (fd,
                                         -1,
                                         UDISKS_ATA_COMMAND_PROTOCOL_NONE,
                                         &input,
                                         &output,
                                         &error))
        {
          udisks_critical ("Error sending ATA command SET FEATURES, sub-command 0x%02x (ata_apm_level=%d) to %s: %s (%s, %d)",
                        (guint) input.feature, data->ata_apm_level, device_file,
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
      else
        {
          udisks_notice ("Set APM level to %d on %s [%s]",
                         data->ata_apm_level, device_file, udisks_drive_get_id (data->drive));
        }
    }

  if (data->ata_pm_standby != -1)
    {
      /* ATA8: 7.18 IDLE - E3h, Non-Data */
      UDisksAtaCommandInput input = {.command = 0xe3, .count = data->ata_pm_standby};
      UDisksAtaCommandOutput output = {0};
      if (!udisks_ata_send_command_sync (fd,
                                         -1,
                                         UDISKS_ATA_COMMAND_PROTOCOL_NONE,
                                         &input,
                                         &output,
                                         &error))
        {
          udisks_critical ("Error sending ATA command IDLE (timeout=%d) to %s: %s (%s, %d)",
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
          data->ata->standby_enabled = data->ata_pm_standby != 0;
        }
    }

  if (data->ata_aam_level != -1)
    {
      /* ATA8: 7.48 SET FEATURES - EFh, Non-Data
       *       7.48.11 Enable/disable the AAM feature set
       */
      UDisksAtaCommandInput input = {.command = 0xef, .feature = 0x42, .count = data->ata_aam_level};
      UDisksAtaCommandOutput output = {0};
      if (data->ata_aam_level == 0xff)
        {
          input.feature = 0xc2;
          input.count = 0x00;
        }
      if (!udisks_ata_send_command_sync (fd,
                                         -1,
                                         UDISKS_ATA_COMMAND_PROTOCOL_NONE,
                                         &input,
                                         &output,
                                         &error))
        {
          udisks_critical ("Error sending ATA command SET FEATURES, sub-command 0x%02x (ata_aam_level=%d) to %s: %s (%s, %d)",
                        (guint) input.feature, data->ata_aam_level, device_file,
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
      else
        {
          udisks_notice ("Set AAM value to %d on %s [%s]",
                         data->ata_aam_level, device_file, udisks_drive_get_id (data->drive));
        }
    }

  if (data->ata_write_cache_enabled_set)
    {
      /* ATA8: 7.48 SET FEATURES - EFh, Non-Data
       *       7.48.4 Enable/disable volatile write cache
       */
      UDisksAtaCommandInput input = {.command = 0xef, .feature = 0x82};
      UDisksAtaCommandOutput output = {0};
      if (data->ata_write_cache_enabled)
        input.feature = 0x02;
      if (!udisks_ata_send_command_sync (fd,
                                         -1,
                                         UDISKS_ATA_COMMAND_PROTOCOL_NONE,
                                         &input,
                                         &output,
                                         &error))
        {
          udisks_critical ("Error sending ATA command SET FEATURES, sub-command 0x%02x to %s: %s (%s, %d)",
                        (guint) input.feature, device_file,
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
      else
        {
          udisks_notice ("%s Write-Cache on %s [%s]",
                         data->ata_write_cache_enabled ? "Enabled" : "Disabled",
                         device_file, udisks_drive_get_id (data->drive));
        }
    }

  if (data->ata_read_lookahead_enabled_set)
    {
      /* ATA8: 7.48 SET FEATURES - EFh, Non-Data
       *       7.48.13 Enable/disable read look-ahead
       */
      UDisksAtaCommandInput input = {.command = 0xef, .feature = 0x55};
      UDisksAtaCommandOutput output = {0};
      if (data->ata_read_lookahead_enabled)
        input.feature = 0xaa;
      if (!udisks_ata_send_command_sync (fd,
                                         -1,
                                         UDISKS_ATA_COMMAND_PROTOCOL_NONE,
                                         &input,
                                         &output,
                                         &error))
        {
          udisks_critical ("Error sending ATA command SET FEATURES, sub-command 0x%02x to %s: %s (%s, %d)",
                        (guint) input.feature, device_file,
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
      else
        {
          udisks_notice ("%s Read Look-ahead on %s [%s]",
                         data->ata_read_lookahead_enabled ? "Enabled" : "Disabled",
                         device_file, udisks_drive_get_id (data->drive));
        }
    }

 out:
  if (fd != -1)
    close (fd);
  g_task_return_boolean (task, TRUE);
}

/**
 * udisks_linux_drive_ata_apply_configuration:
 * @drive: A #UDisksLinuxDriveAta.
 * @device: A #UDisksLinuxDevice
 * @configuration: The configuration to apply.
 *
 * Spawns a thread to apply @configuration to @drive, if any. Does not
 * wait for the thread to terminate.
 */
void
udisks_linux_drive_ata_apply_configuration (UDisksLinuxDriveAta *drive,
                                            UDisksLinuxDevice   *device,
                                            GVariant            *configuration)
{
  gboolean has_conf = FALSE;
  ApplyConfData *data = NULL;
  GTask *task;

  data = g_new0 (ApplyConfData, 1);
  data->ata_pm_standby = -1;
  data->ata_apm_level = -1;
  data->ata_aam_level = -1;
  data->ata_write_cache_enabled = FALSE;
  data->ata_write_cache_enabled_set = FALSE;
  data->ata_read_lookahead_enabled = FALSE;
  data->ata_read_lookahead_enabled_set = FALSE;
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
  if (g_variant_lookup (configuration, "ata-write-cache-enabled", "b", &data->ata_write_cache_enabled))
    {
      data->ata_write_cache_enabled_set = TRUE;
      has_conf = TRUE;
    }
  if (g_variant_lookup (configuration, "ata-read-lookahead-enabled", "b", &data->ata_read_lookahead_enabled))
    {
      data->ata_read_lookahead_enabled_set = TRUE;
      has_conf = TRUE;
    }

  /* don't do anything if none of the configuration is set */
  if (!has_conf)
    goto out;

  /* this can easily take a long time and thus block (the drive may be in standby mode
   * and needs to spin up) - so run it in a thread
   */
  task = g_task_new (data->object, NULL, NULL, NULL);
  g_task_set_task_data (task, data, (GDestroyNotify) apply_conf_data_free);
  g_task_run_in_thread (task, apply_configuration_thread_func);
  g_object_unref (task);

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
 * Performs an ATA Secure Erase operation. Blocks the calling thread until the operation completes.
 *
 * This operation may take a very long time (hours) to complete.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
udisks_linux_drive_ata_secure_erase_sync (UDisksLinuxDriveAta  *drive,
                                          uid_t                 caller_uid,
                                          gboolean              enhanced,
                                          GError              **error)
{
  gboolean ret = FALSE;
  UDisksDrive *_drive = NULL;
  UDisksLinuxDriveObject *object = NULL;
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksDaemon *daemon;
  UDisksLinuxDevice *device = NULL;
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
  gint num_tries = 0;
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
  if (_drive == NULL)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Failed to get Drive interface for object");
      goto out;
    }

  block_object = udisks_linux_drive_object_get_block (object, FALSE);
  if (block_object == NULL)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Unable to find block device for drive");
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);

  device = udisks_linux_drive_object_get_device (object, FALSE /* get_hw */);
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
  device_file = g_udev_device_get_device_file (device->udev_device);
  fd = open (g_udev_device_get_device_file (device->udev_device), O_RDONLY | O_EXCL);
  if (fd == -1)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error opening device file %s for secure erase: %m",
                   device_file);
      goto out;
    }

  drive->secure_erase_in_progress = TRUE;

  /* Acquire an exclusive BSD lock to prevent udev probes */
  while (flock (fd, LOCK_EX | LOCK_NB) != 0)
    {
      g_usleep (100 * 1000); /* microseconds */
      if (num_tries++ > 5)
        break;
    }
  claimed = TRUE;

  /* First get the IDENTIFY data directly from the drive, for sanity checks */
  {
    /* ATA8: 7.16 IDENTIFY DEVICE - ECh, PIO Data-In */
    UDisksAtaCommandInput input = {.command = 0xec, .count = 1};
    UDisksAtaCommandOutput output = {.buffer = identify.buf, .buffer_size = sizeof (identify.buf)};
    if (!udisks_ata_send_command_sync (fd,
                                       -1,
                                       UDISKS_ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST,
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
                                         caller_uid, FALSE, NULL);
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
    UDisksAtaCommandInput input = {.command = 0xf1, .buffer = buf, .buffer_size = sizeof (buf)};
    UDisksAtaCommandOutput output = {0};
    memset (buf, 0, sizeof (buf));
    memcpy (buf + 2, pass, strlen (pass));
    if (!udisks_ata_send_command_sync (fd,
                                       -1,
                                       UDISKS_ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE,
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
    UDisksAtaCommandInput input = {.command = 0xf3};
    UDisksAtaCommandOutput output = {0};
    if (!udisks_ata_send_command_sync (fd,
                                       -1,
                                       UDISKS_ATA_COMMAND_PROTOCOL_NONE,
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
    UDisksAtaCommandInput input = {.command = 0xf4, .buffer = buf, .buffer_size = sizeof (buf)};
    UDisksAtaCommandOutput output = {0};
    memset (buf, 0, sizeof (buf));
    if (enhanced)
      buf[0] |= 0x02;
    memcpy (buf + 2, pass, strlen (pass));
    if (!udisks_ata_send_command_sync (fd,
                                       G_MAXINT, /* disable timeout */
                                       UDISKS_ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE,
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

  if (! udisks_linux_block_object_reread_partition_table (UDISKS_LINUX_BLOCK_OBJECT (block_object),
                                                          &local_error))
    {
      udisks_warning ("%s", local_error->message);
      g_clear_error (&local_error);
    }

  ret = TRUE;

 out:
  /* Clear the password if something went wrong */
  if (clear_passwd_on_failure)
    {
      /* ATA8: 7.41 SECURITY DISABLE PASSWORD - F6h, PIO Data-Out */
      guchar buf[512];
      UDisksAtaCommandInput input = {.command = 0xf6, .buffer = buf, .buffer_size = sizeof (buf)};
      UDisksAtaCommandOutput output = {0};
      GError *cleanup_error = NULL;
      memset (buf, 0, sizeof (buf));
      memcpy (buf + 2, pass, strlen (pass));
      if (!udisks_ata_send_command_sync (fd,
                                         -1,
                                         UDISKS_ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE,
                                         &input,
                                         &output,
                                         &cleanup_error))
        {
          udisks_critical ("Failed to clear user password '%s' on %s (%s) while attempting clean-up after a failed secure erase operation. You may need to manually unlock the drive. The error was: %s (%s, %d)",
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
                     _drive ? udisks_drive_get_id (_drive) : "",
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
  gboolean enhanced = FALSE;

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
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
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

  if (!udisks_linux_drive_ata_secure_erase_sync (drive, caller_uid, enhanced, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  if (!udisks_linux_block_object_reread_partition_table (UDISKS_LINUX_BLOCK_OBJECT (block_object), &error))
    {
      udisks_warning ("%s", error->message);
      g_clear_error (&error);
    }

  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (block_object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);

 out:
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
  iface->handle_smart_set_enabled = handle_smart_set_enabled;

  iface->handle_pm_get_state = handle_pm_get_state;
  iface->handle_pm_standby = handle_pm_standby;
  iface->handle_pm_wakeup = handle_pm_wakeup;

  iface->handle_security_erase_unit = handle_security_erase_unit;
}
