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

#include "udiskslogging.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxdrive.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxdrive
 * @title: UDisksLinuxDrive
 * @short_description: Linux implementation of #UDisksDrive
 *
 * This type provides an implementation of the #UDisksDrive interface
 * on Linux.
 */

typedef struct _UDisksLinuxDriveClass   UDisksLinuxDriveClass;

/**
 * UDisksLinuxDrive:
 *
 * The #UDisksLinuxDrive structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxDrive
{
  UDisksDriveSkeleton parent_instance;

  gint64 time_detected;
  gint64 time_media_detected;
  gchar *sort_key;
};

struct _UDisksLinuxDriveClass
{
  UDisksDriveSkeletonClass parent_class;
};

static void drive_iface_init (UDisksDriveIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxDrive, udisks_linux_drive, UDISKS_TYPE_DRIVE_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_DRIVE, drive_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_drive_finalize (GObject *object)
{
  UDisksLinuxDrive *drive = UDISKS_LINUX_DRIVE (object);

  g_free (drive->sort_key);

  if (G_OBJECT_CLASS (udisks_linux_drive_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_drive_parent_class)->finalize (object);
}

static void
udisks_linux_drive_init (UDisksLinuxDrive *drive)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (drive),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_drive_class_init (UDisksLinuxDriveClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_drive_finalize;
}

/**
 * udisks_linux_drive_new:
 *
 * Creates a new #UDisksLinuxDrive instance.
 *
 * Returns: A new #UDisksLinuxDrive. Free with g_object_unref().
 */
UDisksDrive *
udisks_linux_drive_new (void)
{
  return UDISKS_DRIVE (g_object_new (UDISKS_TYPE_LINUX_DRIVE,
                                          NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
set_id (UDisksDrive *iface)
{
  GString *id;
  const gchar *vendor;
  const gchar *model;
  const gchar *serial;
  const gchar *wwn;
  guint n;

  vendor = udisks_drive_get_vendor (iface);
  model = udisks_drive_get_model (iface);
  serial = udisks_drive_get_serial (iface);
  wwn = udisks_drive_get_wwn (iface);

  id = g_string_new (NULL);

  /* VENDOR-MODEL-[SERIAL|WWN] */

  if (vendor != NULL && strlen (vendor) > 0)
    {
      g_string_append (id, vendor);
    }
  if (model != NULL && strlen (model) > 0)
    {
      if (id->len > 0)
        g_string_append_c (id, '-');
      g_string_append (id, model);
    }

  if (serial != NULL && strlen (serial) > 0)
    {
      if (id->len > 0)
        g_string_append_c (id, '-');
      g_string_append (id, serial);
    }
  else if (wwn != NULL && strlen (wwn) > 0)
    {
      if (id->len > 0)
        g_string_append_c (id, '-');
      g_string_append (id, wwn);
    }
  else
    {
      g_string_set_size (id, 0);
    }

  for (n = 0; n < id->len; n++)
    {
      if (id->str[n] == '/' || id->str[n] == ' ')
        id->str[n] = '-';
    }

  udisks_drive_set_id (iface, id->str);

  g_string_free (id, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
_g_variant_equal0 (GVariant *a, GVariant *b)
{
  gboolean ret = FALSE;
  if (a == NULL && b == NULL)
    {
      ret = TRUE;
      goto out;
    }
  if (a == NULL || b == NULL)
    goto out;
  ret = g_variant_equal (a, b);
out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct {
  const gchar *asv_key;
  const gchar *group;
  const gchar *key;
  const GVariantType *type;
} VariantKeyfileMapping;

static const VariantKeyfileMapping drive_configuration_mapping[3] = {
  {"ata-pm-standby",  "ATA", "StandbyTimeout", G_VARIANT_TYPE_INT32},
  {"ata-apm-level",   "ATA", "APMLevel",       G_VARIANT_TYPE_INT32},
  {"ata-aam-level",   "ATA", "AAMLevel",       G_VARIANT_TYPE_INT32},
};

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
configuration_get_path (UDisksLinuxDrive *drive)
{
  const gchar *id;
  gchar *path = NULL;

  id = udisks_drive_get_id (UDISKS_DRIVE (drive));
  if (id == NULL || strlen (id) == 0)
    goto out;

  path = g_strdup_printf (PACKAGE_SYSCONF_DIR "/udisks2/%s.conf", id);

 out:
  return path;
}

/* returns TRUE if configuration changed */
static gboolean
update_configuration (UDisksLinuxDrive       *drive,
                      UDisksLinuxDriveObject *object)
{
  GKeyFile *key_file = NULL;
  gboolean ret = FALSE;
  gchar *path = NULL;
  GError *error = NULL;
  GVariant *value = NULL;
  GVariantBuilder builder;
  GVariant *old_value;
  guint n;

  path = configuration_get_path (drive);
  if (path == NULL)
    goto out;

  key_file = g_key_file_new ();
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_NONE,
                                  &error))
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          udisks_error ("Error loading drive config file: %s (%s, %d)",
                        error->message, g_quark_to_string (error->domain), error->code);
        }
      g_clear_error (&error);
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  for (n = 0; n < G_N_ELEMENTS (drive_configuration_mapping); n++)
    {
      const VariantKeyfileMapping *mapping = &drive_configuration_mapping[n];

      if (!g_key_file_has_key (key_file, mapping->group, mapping->key, NULL))
        continue;

      if (mapping->type == G_VARIANT_TYPE_INT32)
        {
          gint32 int_value = g_key_file_get_integer (key_file, mapping->group, mapping->key, &error);
          if (error != NULL)
            {
              udisks_error ("Error parsing int32 key %s in group %s in drive config file %s: %s (%s, %d)",
                            mapping->key, mapping->group, path,
                            error->message, g_quark_to_string (error->domain), error->code);
              g_clear_error (&error);
            }
          else
            {
              g_variant_builder_add (&builder, "{sv}", mapping->asv_key, g_variant_new_int32 (int_value));
            }
        }
      else
        {
          g_assert_not_reached ();
        }
    }

  value = g_variant_ref_sink (g_variant_builder_end (&builder));

 out:
  old_value = udisks_drive_get_configuration (UDISKS_DRIVE (drive));
  if (!_g_variant_equal0 (old_value, value))
    ret = TRUE;
  udisks_drive_set_configuration (UDISKS_DRIVE (drive), value);

  if (key_file != NULL)
    g_key_file_unref (key_file);
  if (value != NULL)
    g_variant_unref (value);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static const struct
{
  const gchar *udev_property;
  const gchar *media_name;
  gboolean force_non_removable;
  gboolean force_removable;
} drive_media_mapping[] =
{
  { "ID_DRIVE_THUMB", "thumb", TRUE, FALSE },
  { "ID_DRIVE_FLASH", "flash", FALSE, TRUE },
  { "ID_DRIVE_FLASH_CF", "flash_cf", FALSE, TRUE },
  { "ID_DRIVE_FLASH_MS", "flash_ms", FALSE, TRUE },
  { "ID_DRIVE_FLASH_SM", "flash_sm", FALSE, TRUE },
  { "ID_DRIVE_FLASH_SD", "flash_sd", FALSE, TRUE },
  { "ID_DRIVE_FLASH_SDHC", "flash_sdhc", FALSE, TRUE },
  { "ID_DRIVE_FLASH_SDXC", "flash_sdxc", FALSE, TRUE },
  { "ID_DRIVE_FLASH_MMC", "flash_mmc", FALSE, TRUE },
  { "ID_DRIVE_FLOPPY", "floppy", FALSE, TRUE },
  { "ID_DRIVE_FLOPPY_ZIP", "floppy_zip", FALSE, TRUE },
  { "ID_DRIVE_FLOPPY_JAZ", "floppy_jaz", FALSE, TRUE },
  { "ID_CDROM", "optical_cd", FALSE, TRUE },
  { "ID_CDROM_CD_R", "optical_cd_r", FALSE, TRUE },
  { "ID_CDROM_CD_RW", "optical_cd_rw", FALSE, TRUE },
  { "ID_CDROM_DVD", "optical_dvd", FALSE, TRUE },
  { "ID_CDROM_DVD_R", "optical_dvd_r", FALSE, TRUE },
  { "ID_CDROM_DVD_RW", "optical_dvd_rw", FALSE, TRUE },
  { "ID_CDROM_DVD_RAM", "optical_dvd_ram", FALSE, TRUE },
  { "ID_CDROM_DVD_PLUS_R", "optical_dvd_plus_r", FALSE, TRUE },
  { "ID_CDROM_DVD_PLUS_RW", "optical_dvd_plus_rw", FALSE, TRUE },
  { "ID_CDROM_DVD_PLUS_R_DL", "optical_dvd_plus_r_dl", FALSE, TRUE },
  { "ID_CDROM_DVD_PLUS_RW_DL", "optical_dvd_plus_rw_dl", FALSE, TRUE },
  { "ID_CDROM_BD", "optical_bd", FALSE, TRUE },
  { "ID_CDROM_BD_R", "optical_bd_r", FALSE, TRUE },
  { "ID_CDROM_BD_RE", "optical_bd_re", FALSE, TRUE },
  { "ID_CDROM_HDDVD", "optical_hddvd", FALSE, TRUE },
  { "ID_CDROM_HDDVD_R", "optical_hddvd_r", FALSE, TRUE },
  { "ID_CDROM_HDDVD_RW", "optical_hddvd_rw", FALSE, TRUE },
  { "ID_CDROM_MO", "optical_mo", FALSE, TRUE },
  { "ID_CDROM_MRW", "optical_mrw", FALSE, TRUE },
  { "ID_CDROM_MRW_W", "optical_mrw_w", FALSE, TRUE },
  { NULL, NULL, FALSE, FALSE }
};

static const struct
{
  const gchar *udev_property;
  const gchar *media_name;
} media_mapping[] =
{
  { "ID_DRIVE_MEDIA_FLASH", "flash" },
  { "ID_DRIVE_MEDIA_FLASH_CF", "flash_cf" },
  { "ID_DRIVE_MEDIA_FLASH_MS", "flash_ms" },
  { "ID_DRIVE_MEDIA_FLASH_SM", "flash_sm" },
  { "ID_DRIVE_MEDIA_FLASH_SD", "flash_sd" },
  { "ID_DRIVE_MEDIA_FLASH_SDHC", "flash_sdhc" },
  { "ID_DRIVE_MEDIA_FLASH_SDXC", "flash_sdxc" },
  { "ID_DRIVE_MEDIA_FLASH_MMC", "flash_mmc" },
  { "ID_DRIVE_MEDIA_FLOPPY", "floppy" },
  { "ID_DRIVE_MEDIA_FLOPPY_ZIP", "floppy_zip" },
  { "ID_DRIVE_MEDIA_FLOPPY_JAZ", "floppy_jaz" },
  { "ID_CDROM_MEDIA_CD", "optical_cd" },
  { "ID_CDROM_MEDIA_CD_R", "optical_cd_r" },
  { "ID_CDROM_MEDIA_CD_RW", "optical_cd_rw" },
  { "ID_CDROM_MEDIA_DVD", "optical_dvd" },
  { "ID_CDROM_MEDIA_DVD_R", "optical_dvd_r" },
  { "ID_CDROM_MEDIA_DVD_RW", "optical_dvd_rw" },
  { "ID_CDROM_MEDIA_DVD_RAM", "optical_dvd_ram" },
  { "ID_CDROM_MEDIA_DVD_PLUS_R", "optical_dvd_plus_r" },
  { "ID_CDROM_MEDIA_DVD_PLUS_RW", "optical_dvd_plus_rw" },
  { "ID_CDROM_MEDIA_DVD_PLUS_R_DL", "optical_dvd_plus_r_dl" },
  { "ID_CDROM_MEDIA_DVD_PLUS_RW_DL", "optical_dvd_plus_rw_dl" },
  { "ID_CDROM_MEDIA_BD", "optical_bd" },
  { "ID_CDROM_MEDIA_BD_R", "optical_bd_r" },
  { "ID_CDROM_MEDIA_BD_RE", "optical_bd_re" },
  { "ID_CDROM_MEDIA_HDDVD", "optical_hddvd" },
  { "ID_CDROM_MEDIA_HDDVD_R", "optical_hddvd_r" },
  { "ID_CDROM_MEDIA_HDDVD_RW", "optical_hddvd_rw" },
  { "ID_CDROM_MEDIA_MO", "optical_mo" },
  { "ID_CDROM_MEDIA_MRW", "optical_mrw" },
  { "ID_CDROM_MEDIA_MRW_W", "optical_mrw_w" },
  { NULL, NULL }
};

static gint
ptr_str_array_compare (const gchar **a,
                       const gchar **b)
{
  return g_strcmp0 (*a, *b);
}

static void
set_media (UDisksDrive      *iface,
           GUdevDevice      *device,
           gboolean          is_pc_floppy_drive)
{
  guint n;
  GPtrArray *media_compat_array;
  const gchar *media_in_drive;
  gboolean is_disc = FALSE;
  gboolean disc_is_blank = FALSE;
  guint disc_session_count = 0;
  guint disc_track_count = 0;
  guint disc_track_count_audio = 0;
  guint disc_track_count_data = 0;
  gboolean force_non_removable = FALSE;
  gboolean force_removable = FALSE;
  gboolean ejectable;
  gboolean removable;

  media_compat_array = g_ptr_array_new ();
  for (n = 0; drive_media_mapping[n].udev_property != NULL; n++)
    {
      if (g_udev_device_get_property_as_boolean (device, drive_media_mapping[n].udev_property))
        {
          g_ptr_array_add (media_compat_array, (gpointer) drive_media_mapping[n].media_name);
          if (drive_media_mapping[n].force_non_removable)
            force_non_removable = TRUE;
          if (drive_media_mapping[n].force_removable)
            force_removable = TRUE;
        }
    }
  g_ptr_array_sort (media_compat_array, (GCompareFunc) ptr_str_array_compare);
  g_ptr_array_add (media_compat_array, NULL);

  removable = ejectable = g_udev_device_get_sysfs_attr_as_boolean (device, "removable");
  if (force_non_removable)
    removable = FALSE;
  if (force_removable)
    removable = TRUE;
  udisks_drive_set_media_removable (iface, removable);
  if (is_pc_floppy_drive)
    ejectable = FALSE;
  udisks_drive_set_ejectable (iface, ejectable);

  media_in_drive = NULL;
  if (udisks_drive_get_media_available (iface))
    {
      for (n = 0; media_mapping[n].udev_property != NULL; n++)
        {
          if (g_udev_device_get_property_as_boolean (device, media_mapping[n].udev_property))
            {
              media_in_drive = media_mapping[n].media_name;
              break;
            }
        }
      /* If the media isn't set (from e.g. udev rules), just pick the first one in media_compat - note
       * that this may be NULL (if we don't know what media is compatible with the drive) which is OK.
       */
      if (media_in_drive == NULL)
        media_in_drive = ((const gchar **) media_compat_array->pdata)[0];
    }
  udisks_drive_set_media_compatibility (iface, (const gchar* const *) media_compat_array->pdata);
  udisks_drive_set_media (iface, media_in_drive);
  g_ptr_array_free (media_compat_array, TRUE);

  if (g_udev_device_get_property_as_boolean (device, "ID_CDROM_MEDIA"))
    {
      const gchar *state;
      is_disc = TRUE;
      state = g_udev_device_get_property (device, "ID_CDROM_MEDIA_STATE");
      if (g_strcmp0 (state, "blank") == 0)
        disc_is_blank = TRUE;
      disc_session_count = g_udev_device_get_property_as_int (device, "ID_CDROM_MEDIA_SESSION_COUNT");
      disc_track_count = g_udev_device_get_property_as_int (device, "ID_CDROM_MEDIA_TRACK_COUNT");
      disc_track_count_audio = g_udev_device_get_property_as_int (device, "ID_CDROM_MEDIA_TRACK_COUNT_AUDIO");
      disc_track_count_data = g_udev_device_get_property_as_int (device, "ID_CDROM_MEDIA_TRACK_COUNT_DATA");
    }
  udisks_drive_set_optical (iface, is_disc);
  udisks_drive_set_optical_blank (iface, disc_is_blank);
  udisks_drive_set_optical_num_sessions (iface, disc_session_count);
  udisks_drive_set_optical_num_tracks (iface, disc_track_count);
  udisks_drive_set_optical_num_audio_tracks (iface, disc_track_count_audio);
  udisks_drive_set_optical_num_data_tracks (iface, disc_track_count_data);
}

static void
set_rotation_rate (UDisksDrive      *iface,
                   GUdevDevice      *device)
{
  gint rate;

  if (!g_udev_device_get_sysfs_attr_as_boolean (device, "queue/rotational"))
    {
      rate = 0;
    }
  else
    {
      rate = -1;
      if (g_udev_device_has_property (device, "ID_ATA_ROTATION_RATE_RPM"))
        rate = g_udev_device_get_property_as_int (device, "ID_ATA_ROTATION_RATE_RPM");
    }
  udisks_drive_set_rotation_rate (iface, rate);
}

static void
set_connection_bus (UDisksDrive      *iface,
                    GUdevDevice      *device)
{
  GUdevDevice *parent;

  /* note: @device may vary - it can be any path for drive */

  udisks_drive_set_connection_bus (iface, "");
  parent = g_udev_device_get_parent_with_subsystem (device, "usb", "usb_interface");
  if (parent != NULL)
    {
      /* TODO: should probably check that it's a storage interface */
      udisks_drive_set_connection_bus (iface, "usb");
      g_object_unref (parent);
      goto out;
    }

  parent = g_udev_device_get_parent_with_subsystem (device, "firewire", NULL);
  if (parent != NULL)
    {
      /* TODO: should probably check that it's a storage interface */
      udisks_drive_set_connection_bus (iface, "ieee1394");
      g_object_unref (parent);
      goto out;
    }

  if (g_str_has_prefix (g_udev_device_get_name (device), "mmcblk"))
    {
      udisks_drive_set_connection_bus (iface, "sdio");
      goto out;
    }

 out:
  ;
}

static void
set_media_time_detected (UDisksLinuxDrive *drive,
                         GUdevDevice      *device,
                         gboolean          is_pc_floppy_drive,
                         gboolean          coldplug)
{
  UDisksDrive *iface = UDISKS_DRIVE (drive);
  gint64 now;

  now = g_get_real_time ();

  /* First, initialize time_detected */
  if (drive->time_detected == 0)
    {
      if (coldplug)
        {
          drive->time_detected = now - g_udev_device_get_usec_since_initialized (device);
        }
      else
        {
          drive->time_detected = now;
        }
    }

  if (!g_udev_device_get_sysfs_attr_as_boolean (device, "removable") || is_pc_floppy_drive)
    {
      drive->time_media_detected = drive->time_detected;
    }
  else
    {
      if (!udisks_drive_get_media_available (iface))
        {
          /* no media currently available */
          drive->time_media_detected = 0;
        }
      else
        {
          /* media currently available */
          if (drive->time_media_detected == 0)
            {
              if (coldplug)
                {
                  drive->time_media_detected = drive->time_detected;
                }
              else
                {
                  drive->time_media_detected = now;
                }
            }
        }
    }

  udisks_drive_set_time_detected (iface, drive->time_detected);
  udisks_drive_set_time_media_detected (iface, drive->time_media_detected);
}

static gchar *
append_fixedup_sd (const gchar *prefix,
                   const gchar *device_name)
{
  guint num_alphas, n;
  GString *str;

  g_return_val_if_fail (g_str_has_prefix (device_name, "sd"), NULL);

  /* make sure sdaa comes after e.g. sdz by inserting up to 5 '_' characters
   * between sd and a in sda...
   */
  for (num_alphas = 0; g_ascii_isalpha (device_name[num_alphas + 2]); num_alphas++)
    ;
  str = g_string_new (prefix);
  g_string_append (str, "sd");
  for (n = 0; n < 5 - num_alphas; n++)
    g_string_append_c (str, '_');

  g_string_append (str, device_name + 2);

  return g_string_free (str, FALSE);
}

/**
 * udisks_linux_drive_update:
 * @drive: A #UDisksLinuxDrive.
 * @object: The enclosing #UDisksLinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_drive_update (UDisksLinuxDrive       *drive,
                           UDisksLinuxDriveObject *object)
{
  gboolean ret = FALSE;
  UDisksDrive *iface = UDISKS_DRIVE (drive);
  GUdevDevice *device;
  guint64 size;
  gboolean media_available;
  gboolean media_change_detected;
  gboolean is_pc_floppy_drive = FALSE;
  gboolean removable_hint = FALSE;
  UDisksDaemon *daemon;
  UDisksLinuxProvider *provider;
  gboolean coldplug = FALSE;
  const gchar *seat;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    goto out;

  if (object != NULL)
    {
      daemon = udisks_linux_drive_object_get_daemon (object);
      provider = udisks_daemon_get_linux_provider (daemon);
      coldplug = udisks_linux_provider_get_coldplug (provider);
    }

  if (g_udev_device_get_property_as_boolean (device, "ID_DRIVE_FLOPPY") ||
      g_str_has_prefix (g_udev_device_get_name (device), "fd"))
    is_pc_floppy_drive = TRUE;

  /* this is the _almost_ the same for both ATA and SCSI devices (cf. udev's ata_id and scsi_id)
   * but we special case since there are subtle differences...
   */
  if (g_udev_device_get_property_as_boolean (device, "ID_ATA"))
    {
      const gchar *model;

      model = g_udev_device_get_property (device, "ID_MODEL_ENC");
      if (model != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (model);
          g_strstrip (s);
          udisks_drive_set_model (iface, s);
          g_free (s);
        }

      udisks_drive_set_vendor (iface, g_udev_device_get_property (device, ""));
      udisks_drive_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      udisks_drive_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL_SHORT"));
      udisks_drive_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));
    }
  else if (g_udev_device_get_property_as_boolean (device, "ID_SCSI"))
    {
      const gchar *vendor;
      const gchar *model;

      vendor = g_udev_device_get_property (device, "ID_VENDOR_ENC");
      if (vendor != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (vendor);
          g_strstrip (s);
          udisks_drive_set_vendor (iface, s);
          g_free (s);
        }

      model = g_udev_device_get_property (device, "ID_MODEL_ENC");
      if (model != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (model);
          g_strstrip (s);
          udisks_drive_set_model (iface, s);
          g_free (s);
        }

      udisks_drive_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      udisks_drive_set_serial (iface, g_udev_device_get_property (device, "ID_SCSI_SERIAL"));
      udisks_drive_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));
    }
  else if (g_str_has_prefix (g_udev_device_get_name (device), "mmcblk"))
    {
      /* sigh, mmc is non-standard and using ID_NAME instead of ID_MODEL.. */
      udisks_drive_set_model (iface, g_udev_device_get_property (device, "ID_NAME"));
      udisks_drive_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL"));
      /* TODO:
       *  - lookup Vendor from manfid and oemid in sysfs
       *  - lookup Revision from fwrev and hwrev in sysfs
       */
    }
  else
    {
      const gchar *vendor;
      const gchar *model;
      const gchar *name;

      name = g_udev_device_get_name (device);

      /* generic fallback... */
      vendor = g_udev_device_get_property (device, "ID_VENDOR_ENC");
      if (vendor != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (vendor);
          g_strstrip (s);
          udisks_drive_set_vendor (iface, s);
          g_free (s);
        }
      else
        {
          vendor = g_udev_device_get_property (device, "ID_VENDOR");
          if (vendor != NULL)
            {
              udisks_drive_set_vendor (iface, vendor);
            }
          /* workaround for missing ID_VENDOR for floppy drives */
          else if (is_pc_floppy_drive)
            {
              udisks_drive_set_vendor (iface, "");
            }
          /* workaround for missing ID_VENDOR on virtio-blk */
          else if (g_str_has_prefix (name, "vd"))
            {
              /* TODO: could lookup the vendor sysfs attr on the virtio object */
              udisks_drive_set_vendor (iface, "");
            }
        }

      model = g_udev_device_get_property (device, "ID_MODEL_ENC");
      if (model != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (model);
          g_strstrip (s);
          udisks_drive_set_model (iface, s);
          g_free (s);
        }
      else
        {
          model = g_udev_device_get_property (device, "ID_MODEL");
          if (model != NULL)
            {
              udisks_drive_set_model (iface, model);
            }
          /* workaround for missing ID_MODEL for floppy drives */
          else if (g_str_has_prefix (name, "fd"))
            {
              udisks_drive_set_model (iface, "Floppy Drive");
            }
          /* workaround for missing ID_MODEL on virtio-blk */
          else if (g_str_has_prefix (name, "vd"))
            {
              udisks_drive_set_model (iface, "VirtIO Disk");
            }
        }

      udisks_drive_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      udisks_drive_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL_SHORT"));
      if (g_udev_device_has_property (device, "ID_WWN_WITH_EXTENSION"))
        udisks_drive_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));
      else
        udisks_drive_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN"));
    }

  /* common bits go here */
  size = udisks_daemon_util_block_get_size (device,
                                            &media_available,
                                            &media_change_detected);
  udisks_drive_set_size (iface, size);
  udisks_drive_set_media_available (iface, media_available);
  udisks_drive_set_media_change_detected (iface, media_change_detected);
  set_media (iface, device, is_pc_floppy_drive);
  set_rotation_rate (iface, device);
  set_connection_bus (iface, device);

  if (udisks_drive_get_media_removable (iface) ||
      g_strcmp0 (udisks_drive_get_connection_bus (iface), "usb") == 0 ||
      g_strcmp0 (udisks_drive_get_connection_bus (iface), "sdio") == 0 ||
      g_strcmp0 (udisks_drive_get_connection_bus (iface), "ieee1394") == 0)
    removable_hint = TRUE;
  udisks_drive_set_removable (iface, removable_hint);

  seat = g_udev_device_get_property (device, "ID_SEAT");
  /* assume seat0 if not set */
  if (seat == NULL || strlen (seat) == 0)
    seat = "seat0";
  udisks_drive_set_seat (iface, seat);

  set_media_time_detected (drive, device, is_pc_floppy_drive, coldplug);

  /* calculate sort-key  */
  if (drive->sort_key == NULL)
    {
      if (coldplug)
        {
          const gchar *device_name;
          /* TODO: adjust device_name for better sort order (so e.g. sdaa comes after sdz) */
          device_name = g_udev_device_get_name (device);
          if (udisks_drive_get_removable (iface))
            {
              /* make sure fd* BEFORE sr* BEFORE sd* */
              if (g_str_has_prefix (device_name, "fd"))
                {
                  drive->sort_key = g_strdup_printf ("00coldplug/10removable/%s", device_name);
                }
              else if (g_str_has_prefix (device_name, "sr"))
                {
                  drive->sort_key = g_strdup_printf ("00coldplug/11removable/%s", device_name);
                }
              else if (g_str_has_prefix (device_name, "sd"))
                {
                  drive->sort_key = append_fixedup_sd ("00coldplug/12removable/", device_name);
                }
              else
                {
                  drive->sort_key = g_strdup_printf ("00coldplug/12removable/%s", device_name);
                }
            }
          else
            {
              if (g_str_has_prefix (device_name, "sd"))
                drive->sort_key = append_fixedup_sd ("00coldplug/00fixed/", device_name);
              else
                drive->sort_key = g_strdup_printf ("00coldplug/00fixed/%s", device_name);
            }
        }
      else
        {
          drive->sort_key = g_strdup_printf ("01hotplug/%" G_GINT64_FORMAT, drive->time_detected);
        }
      udisks_drive_set_sort_key (iface, drive->sort_key);
    }

  set_id (iface);

  ret = update_configuration (drive, object);

 out:
  if (device != NULL)
    g_object_unref (device);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_eject (UDisksDrive           *_drive,
              GDBusMethodInvocation *invocation,
              GVariant              *options)
{
  UDisksLinuxDrive *drive = UDISKS_LINUX_DRIVE (_drive);
  UDisksLinuxDriveObject *object;
  UDisksLinuxBlockObject *block_object = NULL;
  UDisksBlock *block = NULL;
  UDisksDaemon *daemon = NULL;
  const gchar *action_id;
  const gchar *message;
  gchar *error_message = NULL;
  GError *error = NULL;
  gchar *escaped_device = NULL;
  uid_t caller_uid;
  gid_t caller_gid;
  pid_t caller_pid;

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
                                             "Unable to find block device for drive");
      goto out;
    }
  block = udisks_object_peek_block (UDISKS_OBJECT (block_object));

  /* refuse to eject if drive appears to be in use */
  if (!udisks_linux_drive_object_is_not_in_use (object, NULL, &error))
    {
      g_prefix_error (&error, "Cannot eject drive in use: ");
      g_dbus_method_invocation_take_error (invocation, error);
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

  /* Translators: Shown in authentication dialog when the user
   * requests ejecting media from a drive.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to eject $(drive)");
  action_id = "org.freedesktop.udisks2.eject-media";
  if (udisks_block_get_hint_system (block))
    {
      action_id = "org.freedesktop.udisks2.eject-media-system";
    }
  else if (!udisks_daemon_util_on_same_seat (daemon, UDISKS_OBJECT (object), caller_pid))
    {
      action_id = "org.freedesktop.udisks2.eject-media-other-seat";
    }

  /* Check that the user is actually authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (block_object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  escaped_device = udisks_daemon_util_escape_and_quote (udisks_block_get_device (block));

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
                                              "drive-eject", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "eject %s",
                                              escaped_device))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error ejecting %s: %s",
                                             udisks_block_get_device (block),
                                             error_message);
      goto out;
    }

  udisks_drive_complete_eject (UDISKS_DRIVE (drive), invocation);

 out:
  g_free (escaped_device);
  g_clear_object (&block_object);
  g_free (error_message);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_set_configuration (UDisksDrive           *_drive,
                          GDBusMethodInvocation *invocation,
                          GVariant              *configuration,
                          GVariant              *options)
{
  UDisksLinuxDrive *drive = UDISKS_LINUX_DRIVE (_drive);
  UDisksDaemon *daemon;
  UDisksLinuxDriveObject *object;
  const gchar *action_id;
  const gchar *message;
  GKeyFile *key_file = NULL;
  GError *error = NULL;
  gchar *path = NULL;
  gchar *data = NULL;
  gsize data_len;
  guint n;

  object = udisks_daemon_util_dup_object (drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);

  /* Translators: Shown in authentication dialog when the user
   * changes settings for a drive.
   *
   * Do not translate $(drive), it's a placeholder and will be
   * replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to configure settings for $(drive)");
  action_id = "org.freedesktop.udisks2.modify-drive-settings";

  /* Check that the user is actually authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  path = configuration_get_path (drive);
  if (path == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Drive has no persistent unique id");
      goto out;
    }

  key_file = g_key_file_new ();
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                  &error))
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      /* not a problem, just create a new file */
      g_key_file_set_comment (key_file,
                              NULL, /* group_name */
                              NULL, /* key */
                              " See udisks(8) for the format of this file.",
                              NULL);
      g_clear_error (&error);
    }

  for (n = 0; n < G_N_ELEMENTS (drive_configuration_mapping); n++)
    {
      const VariantKeyfileMapping *mapping = &drive_configuration_mapping[n];
      GVariant *value = NULL;

      value = g_variant_lookup_value (configuration, mapping->asv_key, mapping->type);
      if (value == NULL)
        {
          g_key_file_remove_key (key_file, mapping->group, mapping->key, NULL);
        }
      else
        {
          if (mapping->type == G_VARIANT_TYPE_INT32)
            {
              g_key_file_set_integer (key_file, mapping->group, mapping->key, g_variant_get_int32 (value));
            }
          else
            {
              g_assert_not_reached ();
            }
        }
    }

  data = g_key_file_to_data (key_file, &data_len, NULL);

  if (!udisks_daemon_util_file_set_contents (path,
                                             data,
                                             data_len,
                                             0600, /* mode to use if non-existant */
                                             &error) != 0)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }


  udisks_drive_complete_set_configuration (UDISKS_DRIVE (drive), invocation);

 out:
  g_free (data);
  g_free (path);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
drive_iface_init (UDisksDriveIface *iface)
{
  iface->handle_eject = handle_eject;
  iface->handle_set_configuration = handle_set_configuration;
}
