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

#include "udisksutil.h"

/**
 * SECTION:udisksutil
 * @title: Miscellaneous Utility Functions
 * @short_description: Various Utilities
 *
 * Various utilities.
 */

/* ---------------------------------------------------------------------------------------------------- */

#define KILOBYTE_FACTOR 1000.0
#define MEGABYTE_FACTOR (1000.0 * 1000.0)
#define GIGABYTE_FACTOR (1000.0 * 1000.0 * 1000.0)
#define TERABYTE_FACTOR (1000.0 * 1000.0 * 1000.0 * 1000.0)

#define KIBIBYTE_FACTOR 1024.0
#define MEBIBYTE_FACTOR (1024.0 * 1024.0)
#define GIBIBYTE_FACTOR (1024.0 * 1024.0 * 1024.0)
#define TEBIBYTE_FACTOR (1024.0 * 1024.0 * 1024.0 * 10242.0)

static char *
get_pow2_size (guint64 size)
{
  gchar *str;
  gdouble displayed_size;
  const gchar *unit;
  guint digits;

  if (size < MEBIBYTE_FACTOR)
    {
      displayed_size = (double) size / KIBIBYTE_FACTOR;
      unit = "KiB";
    }
  else if (size < GIBIBYTE_FACTOR)
    {
      displayed_size = (double) size / MEBIBYTE_FACTOR;
      unit = "MiB";
    }
  else if (size < TEBIBYTE_FACTOR)
    {
      displayed_size = (double) size / GIBIBYTE_FACTOR;
      unit = "GiB";
    }
  else
    {
      displayed_size = (double) size / TEBIBYTE_FACTOR;
      unit = "TiB";
    }

  if (displayed_size < 10.0)
    digits = 1;
  else
    digits = 0;

  str = g_strdup_printf ("%.*f %s", digits, displayed_size, unit);

  return str;
}

static char *
get_pow10_size (guint64 size)
{
  gchar *str;
  gdouble displayed_size;
  const gchar *unit;
  guint digits;

  if (size < MEGABYTE_FACTOR)
    {
      displayed_size = (double) size / KILOBYTE_FACTOR;
      unit = "KB";
    }
  else if (size < GIGABYTE_FACTOR)
    {
      displayed_size = (double) size / MEGABYTE_FACTOR;
      unit = "MB";
    }
  else if (size < TERABYTE_FACTOR)
    {
      displayed_size = (double) size / GIGABYTE_FACTOR;
      unit = "GB";
    }
  else
    {
      displayed_size = (double) size / TERABYTE_FACTOR;
      unit = "TB";
    }

  if (displayed_size < 10.0)
    digits = 1;
  else
    digits = 0;

  str = g_strdup_printf ("%.*f %s", digits, displayed_size, unit);

  return str;
}

/**
 * udisks_util_get_size_for_display:
 * @size: Size in bytes
 * @use_pow2: Whether power-of-two units should be used instead of power-of-ten units.
 * @long_string: Whether to produce a long string.
 *
 * Gets a human-readable string that represents @size.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
udisks_util_get_size_for_display (guint64  size,
                                  gboolean use_pow2,
                                  gboolean long_string)
{
  gchar *str;

  if (long_string)
    {
      gchar *size_str;
      size_str = g_strdup_printf ("%'" G_GINT64_FORMAT, size);
      if (use_pow2)
        {
          gchar *pow2_str;
          pow2_str = get_pow2_size (size);
          /* Translators: The first %s is the size in power-of-2 units, e.g. '64 KiB'
           * the second %s is the size as a number e.g. '65,536 bytes'
           */
          str = g_strdup_printf (_("%s (%s bytes)"), pow2_str, size_str);
          g_free (pow2_str);
        }
      else
        {
          gchar *pow10_str;
          pow10_str = get_pow10_size (size);
          /* Translators: The first %s is the size in power-of-10 units, e.g. '100 KB'
           * the second %s is the size as a number e.g. '100,000 bytes'
           */
          str = g_strdup_printf (_("%s (%s bytes)"), pow10_str, size_str);
          g_free (pow10_str);
        }

      g_free (size_str);
    }
  else
    {
      if (use_pow2)
        {
          str = get_pow2_size (size);
        }
      else
        {
          str = get_pow10_size (size);
        }
    }
  return str;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef enum
{
  DRIVE_TYPE_UNSET,
  DRIVE_TYPE_DISK,
  DRIVE_TYPE_CARD,
  DRIVE_TYPE_DISC
} DriveType;

static const struct
{
  const gchar *id;
  const gchar *media_name;
  const gchar *media_family;
  const gchar *media_icon;
  DriveType    media_type;
  const gchar *drive_icon;
} media_data[] =
{
  {"floppy",     N_("Floppy"),       N_("Floppy"), "media-floppy",      DRIVE_TYPE_DISK, "drive-removable-media-floppy"},
  {"floppy_zip", N_("Zip"),          N_("Zip"),    "media-floppy-jaz",  DRIVE_TYPE_DISK, "drive-removable-media-floppy-jaz"},
  {"floppy_jaz", N_("Jaz"),          N_("Jaz"),    "media-floppy-zip",  DRIVE_TYPE_DISK, "drive-removable-media-floppy-zip"},

  {"flash",      N_("Flash"),        N_("Flash"),        "media-flash",       DRIVE_TYPE_CARD, "drive-removable-media-flash"},
  {"flash_ms",   N_("MemoryStick"),  N_("MemoryStick"),  "media-flash-ms",    DRIVE_TYPE_CARD, "drive-removable-media-flash-ms"},
  {"flash_sm",   N_("SmartMedia"),   N_("SmartMedia"),   "media-flash-sm",    DRIVE_TYPE_CARD, "drive-removable-media-flash-sm"},
  {"flash_cf",   N_("CompactFlash"), N_("CompactFlash"), "media-flash-cf",    DRIVE_TYPE_CARD, "drive-removable-media-flash-cf"},
  {"flash_mmc",  N_("MMC"),          N_("SD"),           "media-flash-mmc",   DRIVE_TYPE_CARD, "drive-removable-media-flash-mmc"},
  {"flash_sd",   N_("SD"),           N_("SD"),           "media-flash-sd",    DRIVE_TYPE_CARD, "drive-removable-media-flash-sd"},
  {"flash_sdxc", N_("SDXC"),         N_("SD"),           "media-flash-sd-xc", DRIVE_TYPE_CARD, "drive-removable-media-flash-sd-xc"},
  {"flash_sdhc", N_("SDHC"),         N_("SD"),           "media-flash-sd-hc", DRIVE_TYPE_CARD, "drive-removable-media-flash-sd-hc"},

  {"optical_cd",             N_("CD-ROM"),    N_("CD"),      "media-optical-cd-rom",        DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_cd_r",           N_("CD-R"),      N_("CD"),      "media-optical-cd-r",          DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_cd_rw",          N_("CD-RW"),     N_("CD"),      "media-optical-cd-rw",         DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd",            N_("DVD"),       N_("DVD"),     "media-optical-dvd-rom",       DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_dvd_r",          N_("DVD-R"),     N_("DVD"),     "media-optical-dvd-r",         DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_rw",         N_("DVD-RW"),    N_("DVD"),     "media-optical-dvd-rw",        DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_ram",        N_("DVD-RAM"),   N_("DVD"),     "media-optical-dvd-ram",       DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_plus_r",     N_("DVD+R"),     N_("DVD"),     "media-optical-dvd-r-plus",    DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_plus_rw",    N_("DVD+RW"),    N_("DVD"),     "media-optical-dvd-rw-plus",   DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_plus_r_dl",  N_("DVD+R DL"),  N_("DVD"),     "media-optical-dvd-dl-r-plus", DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_plus_rw_dl", N_("DVD+RW DL"), N_("DVD"),     "media-optical-dvd-dl-r-plus", DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_bd",             N_("BD-ROM"),    N_("Blu-Ray"), "media-optical-bd-rom",        DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_bd_r",           N_("BD-R"),      N_("Blu-Ray"), "media-optical-bd-r",          DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_bd_re",          N_("BD-RE"),     N_("Blu-Ray"), "media-optical-bd-re",         DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_hddvd",          N_("HDDVD"),     N_("HDDVD"),   "media-optical-hddvd-rom",     DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_hddvd_r",        N_("HDDVD-R"),   N_("HDDVD"),   "media-optical-hddvd-r",       DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_hddvd_rw",       N_("HDDVD-RW"),  N_("HDDVD"),   "media-optical-hddvd-rw",      DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_mo",             N_("MO"),        N_("CD"),      "media-optical-mo",            DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_mrw",            N_("MRW"),       N_("CD"),      "media-optical-mrw",           DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_mrw_w",          N_("MRW-W"),     N_("CD"),      "media-optical-mrw-w",         DRIVE_TYPE_DISC, "drive-optical-recorder"},
};

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
strv_has (const gchar * const *haystack,
          const gchar          *needle)
{
  gboolean ret;
  guint n;

  ret = FALSE;

  for (n = 0; haystack != NULL && haystack[n] != NULL; n++)
    {
      if (g_strcmp0 (haystack[n], needle) == 0)
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  return ret;
}

/**
 * udisks_util_get_lun_info:
 * @lun: A #UDisksLun.
 * @out_name: (out allow-none): Return location for name or %NULL.
 * @out_description: (out allow-none): Return location for description or %NULL.
 * @out_drive_icon: (out allow-none): Return location for icon representing the drive or %NULL.
 * @out_media_description: (out allow-none): Return location for description of the media or %NULL.
 * @out_media_icon: (out allow-none): Return location for icon representing the media or %NULL.
 *
 * Gets information about a #UDisksLun object that is suitable to
 * present in an user interface. The returned strings are localized.
 *
 * If there is no media in @lun, then @out_media_icon is set to the
 * same value as @out_drive_icon.
 *
 * If the @lun doesn't support removable media, then %NULL is always
 * returned for @out_media_description and @out_media_icon.
 *
 * The returned data is best described by example:
 * <informaltable>
 *   <tgroup cols="6">
 *     <thead>
 *       <row>
 *         <entry>Device / Media</entry>
 *         <entry>name</entry>
 *         <entry>description</entry>
 *         <entry>icon</entry>
 *         <entry>media_description</entry>
 *         <entry>media_icon</entry>
 *       </row>
 *     </thead>
 *     <tbody>
 *       <row>
 *         <entry>Internal System Disk (Hard Disk)</entry>
 *         <entry>ST3320620AS</entry>
 *         <entry>320 GB Hard Disk</entry>
 *         <entry>drive-harddisk</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>Internal System Disk (Solid State)</entry>
 *         <entry>INTEL SSDSA2MH080G1GC</entry>
 *         <entry>80 GB Disk</entry>
 *         <entry>drive-harddisk</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (empty)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with CD-ROM data disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>CD-ROM Disc</entry>
 *         <entry>media-optical-cd-rom</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with mixed disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>Audio/Data CD-ROM Disc</entry>
 *         <entry>media-optical-cd-rom</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with audio disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>Audio Disc</entry>
 *         <entry>media-optical-cd-audio</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with DVD-ROM disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>DVD-ROM Disc</entry>
 *         <entry>media-optical-dvd-rom</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with blank DVD-R disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>Blank DVD-R Disc</entry>
 *         <entry>media-optical-dvd-r</entry>
 *       </row>
 *       <row>
 *         <entry>External USB Hard Disk</entry>
 *         <entry>WD 2500JB External</entry>
 *         <entry>250 GB Hard Disk</entry>
 *         <entry>drive-harddisk-usb</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>USB Compact Flash Reader (without media)</entry>
 *         <entry>BELKIN USB 2 HS-CF</entry>
 *         <entry>Compact Flash Drive</entry>
 *         <entry>drive-removable-media-flash-cf</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>USB Compact Flash Reader (with media)</entry>
 *         <entry>BELKIN USB 2 HS-CF</entry>
 *         <entry>Compact Flash Drive</entry>
 *         <entry>drive-removable-media-flash-cf</entry>
 *         <entry>Compact Flash media</entry>
 *         <entry>media-flash-cf</entry>
 *       </row>
 *     </tbody>
 *   </tgroup>
 * </informaltable>
 */
void
udisks_util_get_lun_info (UDisksLun  *lun,
                          gchar     **out_name,
                          gchar     **out_description,
                          GIcon     **out_icon,
                          gchar     **out_media_description,
                          GIcon     **out_media_icon)
{
  gchar *name;
  gchar *description;
  GIcon *icon;
  gchar *media_description;
  GIcon *media_icon;
  const gchar *vendor;
  const gchar *model;
  const gchar *media;
  const gchar *const *media_compat;
  gboolean removable;
  gboolean rotation_rate;
  guint64 size;
  gchar *size_str;
  guint n;
  GString *desc_str;
  DriveType desc_type;

  /* TODO: support presentation-name for overrides */

  g_return_if_fail (UDISKS_IS_LUN (lun));

  name = NULL;
  description = NULL;
  icon = NULL;
  media_description = NULL;
  media_icon = NULL;
  size_str = NULL;

  vendor = udisks_lun_get_vendor (lun);
  model = udisks_lun_get_model (lun);
  size = udisks_lun_get_size (lun);
  removable = udisks_lun_get_media_removable (lun);
  rotation_rate = udisks_lun_get_rotation_rate (lun);
  if (size > 0)
    size_str = udisks_util_get_size_for_display (size, FALSE, FALSE);
  media = udisks_lun_get_media (lun);
  media_compat = udisks_lun_get_media_compatibility (lun);

  /* Name is easy - that's just "$vendor $model" */
  if (strlen (vendor) == 0)
    vendor = NULL;
  if (strlen (model) == 0)
    model = NULL;
  name = g_strdup_printf ("%s%s%s",
                          vendor != NULL ? vendor : "",
                          vendor != NULL ? " " : "",
                          model != NULL ? model : "");

  desc_type = DRIVE_TYPE_UNSET;
  desc_str = g_string_new (NULL);
  for (n = 0; n < G_N_ELEMENTS (media_data) - 1; n++)
    {
      /* media_compat */
      if (strv_has (media_compat, media_data[n].id))
        {
          if (icon == NULL)
            icon = g_themed_icon_new_with_default_fallbacks (media_data[n].drive_icon);
          if (strstr (desc_str->str, media_data[n].media_family) == NULL)
            {
              if (desc_str->len > 0)
                g_string_append (desc_str, "/");
              g_string_append (desc_str, _(media_data[n].media_family));
            }
          desc_type = media_data[n].media_type;
        }

      /* media */
      if (g_strcmp0 (media, media_data[n].id) == 0)
        {
          if (media_description == NULL)
            {
              switch (media_data[n].media_type)
                {
                case DRIVE_TYPE_UNSET:
                  g_assert_not_reached ();
                  break;
                case DRIVE_TYPE_DISK:
                  media_description = g_strdup_printf (_("%s Disk"), _(media_data[n].media_name));
                  break;
                case DRIVE_TYPE_CARD:
                  media_description = g_strdup_printf (_("%s Card"), _(media_data[n].media_name));
                  break;
                case DRIVE_TYPE_DISC:
                  media_description = g_strdup_printf (_("%s Disc"), _(media_data[n].media_name));
                  break;
                }
            }
          if (media_icon == NULL)
            media_icon = g_themed_icon_new_with_default_fallbacks (media_data[n].media_icon);
        }
    }

  switch (desc_type)
    {
    case DRIVE_TYPE_UNSET:
      if (removable)
        {
          if (size_str != NULL)
            {
              description = g_strdup_printf (_("%s Drive"), size_str);
            }
          else
            {
              description = g_strdup (_("Drive"));
            }
        }
      else
        {
          if (rotation_rate == 0)
            {
              if (size_str != NULL)
                {
                  description = g_strdup_printf (_("%s Disk"), size_str);
                }
              else
                {
                  description = g_strdup (_("Disk"));
                }
            }
          else
            {
              if (size_str != NULL)
                {
                  description = g_strdup_printf (_("%s Hard Disk"), size_str);
                }
              else
                {
                  description = g_strdup (_("Hard Disk"));
                }
            }
        }
      break;

    case DRIVE_TYPE_CARD:
      description = g_strdup_printf (_("%s Card Reader"), desc_str->str);
      break;

    case DRIVE_TYPE_DISK:
      /* explicit fall-through */
    case DRIVE_TYPE_DISC:
      description = g_strdup_printf (_("%s Drive"), desc_str->str);
      break;
    }
  g_string_free (desc_str, TRUE);

  /* TODO: use -usb and -ieee1394 suffixes where appropriate */
  if (media_icon == NULL)
    {
      if (removable)
        media_icon = g_themed_icon_new_with_default_fallbacks ("drive-removable-media");
      else
        media_icon = g_themed_icon_new_with_default_fallbacks ("drive-harddisk");
    }
  if (icon == NULL)
    {
      if (removable)
        icon = g_themed_icon_new_with_default_fallbacks ("drive-removable-media");
      else
        icon = g_themed_icon_new_with_default_fallbacks ("drive-harddisk");
    }

  /* TODO: prepend "Blank ", "Mixed " or "Audio " for optical discs */

  /* return values to caller */
  if (out_name != NULL)
    *out_name = name;
  else
    g_free (name);
  if (out_description != NULL)
    *out_description = description;
  else
    g_free (description);
  if (out_icon != NULL)
    *out_icon = icon;
  else if (icon != NULL)
    g_object_unref (icon);
  if (out_media_description != NULL)
    *out_media_description = media_description;
  else
    g_free (media_description);
  if (out_media_icon != NULL)
    *out_media_icon = media_icon;
  else if (media_icon != NULL)
    g_object_unref (media_icon);

  g_free (size_str);
}

/* ---------------------------------------------------------------------------------------------------- */

const static struct
{
  const gchar *scheme;
  const gchar *name;
} part_scheme[] =
{
  {"mbr", N_("Master Boot Record")},
  {"gpt", N_("GUID Partition Table")},
  {"apm", N_("Apple Partition Map")},
  {NULL, NULL}
};

/**
 * udisks_util_get_part_scheme_for_display:
 * @scheme: A partitioning scheme id.
 *
 * Gets a human readable localized string for @scheme.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
udisks_util_get_part_scheme_for_display (const gchar *scheme)
{
  guint n;
  gchar *ret;

  for (n = 0; part_scheme[n].scheme != NULL; n++)
    {
      if (g_strcmp0 (part_scheme[n].scheme, scheme) == 0)
        {
          ret = g_strdup (_(part_scheme[n].name));
          goto out;
        }
    }

  /* Translators: Shown for unknown partitioning scheme.
   * First %s is the partition type scheme.
   */
  ret = g_strdup_printf (_("Unknown Scheme (%s)"), scheme);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

const static struct
{
  const gchar *scheme;
  const gchar *type;
  const gchar *name;
} part_type[] =
{
  /* see http://en.wikipedia.org/wiki/GUID_Partition_Table */

  /* Linux */
  {"gpt", "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Linux Basic Data Partition")}, /* Same as MS BDP */
  {"gpt", "A19D880F-05FC-4D3B-A006-743F0F84911E", N_("Linux RAID Partition")},
  {"gpt", "0657FD6D-A4AB-43C4-84E5-0933C84B4F4F", N_("Linux Swap Partition")},
  {"gpt", "E6D6D379-F507-44C2-A23C-238F2A3DF928", N_("Linux LVM Partition")},
  {"gpt", "8DA63339-0007-60C0-C436-083AC8230908", N_("Linux Reserved Partition")},
  /* Not associated with any OS */
  {"gpt", "024DEE41-33E7-11D3-9D69-0008C781F39F", N_("MBR Partition Scheme")},
  {"gpt", "C12A7328-F81F-11D2-BA4B-00A0C93EC93B", N_("EFI System Partition")},
  {"gpt", "21686148-6449-6E6F-744E-656564454649", N_("BIOS Boot Partition")},
  /* Microsoft */
  {"gpt", "E3C9E316-0B5C-4DB8-817D-F92DF00215AE", N_("Microsoft Reserved Partition")},
  {"gpt", "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7", N_("Microsoft Basic Data Partition")}, /* Same as Linux BDP */
  {"gpt", "5808C8AA-7E8F-42E0-85D2-E1E90434CFB3", N_("Microsoft LDM Metadata Partition")},
  {"gpt", "AF9B60A0-1431-4F62-BC68-3311714A69AD", N_("Microsoft LDM Data Partition")},
  {"gpt", "DE94BBA4-06D1-4D40-A16A-BFD50179D6AC", N_("Microsoft Windows Recovery Environment")},
  /* HP-UX */
  {"gpt", "75894C1E-3AEB-11D3-B7C1-7B03A0000000", N_("HP-UX Data Partition")},
  {"gpt", "E2A1E728-32E3-11D6-A682-7B03A0000000", N_("HP-UX Service Partition")},
  /* FreeBSD */
  {"gpt", "83BD6B9D-7F41-11DC-BE0B-001560B84F0F", N_("FreeBSD Boot Partition")},
  {"gpt", "516E7CB4-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD Data Partition")},
  {"gpt", "516E7CB5-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD Swap Partition")},
  {"gpt", "516E7CB6-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD UFS Partition")},
  {"gpt", "516E7CB8-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD Vinum Partition")},
  {"gpt", "516E7CBA-6ECF-11D6-8FF8-00022D09712B", N_("FreeBSD ZFS Partition")},
  /* Solaris */
  {"gpt", "6A82CB45-1DD2-11B2-99A6-080020736631", N_("Solaris Boot Partition")},
  {"gpt", "6A85CF4D-1DD2-11B2-99A6-080020736631", N_("Solaris Root Partition")},
  {"gpt", "6A87C46F-1DD2-11B2-99A6-080020736631", N_("Solaris Swap Partition")},
  {"gpt", "6A8B642B-1DD2-11B2-99A6-080020736631", N_("Solaris Backup Partition")},
  {"gpt", "6A898CC3-1DD2-11B2-99A6-080020736631", N_("Solaris /usr Partition")}, /* Same as Apple ZFS */
  {"gpt", "6A8EF2E9-1DD2-11B2-99A6-080020736631", N_("Solaris /var Partition")},
  {"gpt", "6A90BA39-1DD2-11B2-99A6-080020736631", N_("Solaris /home Partition")},
  {"gpt", "6A9283A5-1DD2-11B2-99A6-080020736631", N_("Solaris Alternate Sector Partition")},
  {"gpt", "6A945A3B-1DD2-11B2-99A6-080020736631", N_("Solaris Reserved Partition")},
  {"gpt", "6A9630D1-1DD2-11B2-99A6-080020736631", N_("Solaris Reserved Partition (2)")},
  {"gpt", "6A980767-1DD2-11B2-99A6-080020736631", N_("Solaris Reserved Partition (3)")},
  {"gpt", "6A96237F-1DD2-11B2-99A6-080020736631", N_("Solaris Reserved Partition (4)")},
  {"gpt", "6A8D2AC7-1DD2-11B2-99A6-080020736631", N_("Solaris Reserved Partition (5)")},
  /* Mac OS X */
  {"gpt", "48465300-0000-11AA-AA11-00306543ECAC", N_("Apple HFS/HFS+ Partition")},
  {"gpt", "55465300-0000-11AA-AA11-00306543ECAC", N_("Apple UFS Partition")},
  {"gpt", "6A898CC3-1DD2-11B2-99A6-080020736631", N_("Apple ZFS Partition")}, /* Same as Solaris /usr */
  {"gpt", "52414944-0000-11AA-AA11-00306543ECAC", N_("Apple RAID Partition")},
  {"gpt", "52414944-5F4F-11AA-AA11-00306543ECAC", N_("Apple RAID Partition (Offline)")},
  {"gpt", "426F6F74-0000-11AA-AA11-00306543ECAC", N_("Apple Boot Partition")},
  {"gpt", "4C616265-6C00-11AA-AA11-00306543ECAC", N_("Apple Label Partition")},
  {"gpt", "5265636F-7665-11AA-AA11-00306543ECAC", N_("Apple TV Recovery Partition")},
  /* NetBSD */
  {"gpt", "49F48D32-B10E-11DC-B99B-0019D1879648", N_("NetBSD Swap Partition")},
  {"gpt", "49F48D5A-B10E-11DC-B99B-0019D1879648", N_("NetBSD FFS Partition")},
  {"gpt", "49F48D82-B10E-11DC-B99B-0019D1879648", N_("NetBSD LFS Partition")},
  {"gpt", "49F48DAA-B10E-11DC-B99B-0019D1879648", N_("NetBSD RAID Partition")},
  {"gpt", "2DB519C4-B10F-11DC-B99B-0019D1879648", N_("NetBSD Concatenated Partition")},
  {"gpt", "2DB519EC-B10F-11DC-B99B-0019D1879648", N_("NetBSD Encrypted Partition")},

  /* see http://developer.apple.com/documentation/mac/Devices/Devices-126.html
   *     http://lists.apple.com/archives/Darwin-drivers/2003/May/msg00021.html */
  {"apm", "Apple_Unix_SVR2", N_("Apple UFS Partition")},
  {"apm", "Apple_HFS", N_("Apple HFS/HFS+ Partition")},
  {"apm", "Apple_partition_map", N_("Apple Partition Map")},
  {"apm", "Apple_Free", N_("Unused Partition")},
  {"apm", "Apple_Scratch", N_("Empty Partition")},
  {"apm", "Apple_Driver", N_("Driver Partition")},
  {"apm", "Apple_Driver43", N_("Driver 4.3 Partition")},
  {"apm", "Apple_PRODOS", N_("ProDOS file system")},
  {"apm", "DOS_FAT_12", N_("FAT 12")},
  {"apm", "DOS_FAT_16", N_("FAT 16")},
  {"apm", "DOS_FAT_32", N_("FAT 32")},
  {"apm", "Windows_FAT_16", N_("FAT 16 (Windows)")},
  {"apm", "Windows_FAT_32", N_("FAT 32 (Windows)")},

  /* see http://www.win.tue.nl/~aeb/partitions/partition_types-1.html */
  {"mbr", "0x00",  N_("Empty (0x00)")},
  {"mbr", "0x01",  N_("FAT12 (0x01)")},
  {"mbr", "0x04",  N_("FAT16 <32M (0x04)")},
  {"mbr", "0x05",  N_("Extended (0x05)")},
  {"mbr", "0x06",  N_("FAT16 (0x06)")},
  {"mbr", "0x07",  N_("HPFS/NTFS (0x07)")},
  {"mbr", "0x0b",  N_("W95 FAT32 (0x0b)")},
  {"mbr", "0x0c",  N_("W95 FAT32 (LBA) (0x0c)")},
  {"mbr", "0x0e",  N_("W95 FAT16 (LBA) (0x0e)")},
  {"mbr", "0x0f",  N_("W95 Ext d (LBA) (0x0f)")},
  {"mbr", "0x10",  N_("OPUS (0x10)")},
  {"mbr", "0x11",  N_("Hidden FAT12 (0x11)")},
  {"mbr", "0x12",  N_("Compaq diagnostics (0x12)")},
  {"mbr", "0x14",  N_("Hidden FAT16 <32M (0x14)")},
  {"mbr", "0x16",  N_("Hidden FAT16 (0x16)")},
  {"mbr", "0x17",  N_("Hidden HPFS/NTFS (0x17)")},
  {"mbr", "0x1b",  N_("Hidden W95 FAT32 (0x1b)")},
  {"mbr", "0x1c",  N_("Hidden W95 FAT32 (LBA) (0x1c)")},
  {"mbr", "0x1e",  N_("Hidden W95 FAT16 (LBA) (0x1e)")},
  {"mbr", "0x3c",  N_("PartitionMagic (0x3c)")},
  {"mbr", "0x81",  N_("Minix (0x81)")}, /* cf. http://en.wikipedia.org/wiki/MINIX_file_system */
  {"mbr", "0x82",  N_("Linux swap (0x82)")},
  {"mbr", "0x83",  N_("Linux (0x83)")},
  {"mbr", "0x84",  N_("Hibernation (0x84)")},
  {"mbr", "0x85",  N_("Linux Extended (0x85)")},
  {"mbr", "0x8e",  N_("Linux LVM (0x8e)")},
  {"mbr", "0xa0",  N_("Hibernation (0xa0)")},
  {"mbr", "0xa5",  N_("FreeBSD (0xa5)")},
  {"mbr", "0xa6",  N_("OpenBSD (0xa6)")},
  {"mbr", "0xa8",  N_("Mac OS X (0xa8)")},
  {"mbr", "0xaf",  N_("Mac OS X (0xaf)")},
  {"mbr", "0xbe",  N_("Solaris boot (0xbe)")},
  {"mbr", "0xbf",  N_("Solaris (0xbf)")},
  {"mbr", "0xeb",  N_("BeOS BFS (0xeb)")},
  {"mbr", "0xec",  N_("SkyOS SkyFS (0xec)")},
  {"mbr", "0xee",  N_("EFI GPT (0xee)")},
  {"mbr", "0xef",  N_("EFI (FAT-12/16/32 (0xef)")},
  {"mbr", "0xfd",  N_("Linux RAID autodetect (0xfd)")},
  {NULL,  NULL, NULL}
};

/**
 * udisks_util_get_part_types_for_scheme:
 * @scheme: A partitioning scheme id.
 *
 * Gets all known types for @scheme.
 *
 * Returns: (transfer container): A %NULL-terminated array of
 * strings. Only the container should be freed with g_free().
 */
const gchar **
udisks_util_get_part_types_for_scheme (const gchar *scheme)
{
  guint n;
  GPtrArray *p;

  p = g_ptr_array_new();
  for (n = 0; part_type[n].name != NULL; n++)
    {
      if (g_strcmp0 (part_type[n].scheme, scheme) == 0)
        {
          g_ptr_array_add (p, (gpointer) part_type[n].type);
        }
    }
  g_ptr_array_add (p, NULL);

  return (const gchar **) g_ptr_array_free (p, FALSE);
}

/**
 * udisks_util_get_part_type_for_display:
 * @scheme: A partitioning scheme id.
 * @type: A partition type.
 *
 * Gets a human readable localized string for @scheme and @type.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
udisks_util_get_part_type_for_display (const gchar *scheme,
                                       const gchar *type)
{
  guint n;
  gchar *ret;

  for (n = 0; part_type[n].name != NULL; n++)
    {
      if (g_strcmp0 (part_type[n].scheme, scheme) == 0 &&
          g_strcmp0 (part_type[n].type, type) == 0)
        {
          ret = g_strdup (_(part_type[n].name));
          goto out;
        }
    }

  /* Translators: Shown for unknown partition types.
   * First %s is the partition type.
   */
  ret = g_strdup_printf (_("Unknown Type (%s)"), type);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

const static struct
{
  const gchar *usage;
  const gchar *type;
  const gchar *version;
  const gchar *long_name;
  const gchar *short_name;
} id_type[] =
{
  {"filesystem", "vfat",              "FAT12", N_("FAT (12-bit version)"),              N_("FAT")},
  {"filesystem", "vfat",              "FAT16", N_("FAT (16-bit version)"),              N_("FAT")},
  {"filesystem", "vfat",              "FAT32", N_("FAT (32-bit version)"),              N_("FAT")},
  {"filesystem", "ntfs",              "*",     N_("FAT (version %s)"),                  N_("FAT")},
  {"filesystem", "vfat",              NULL,    N_("FAT"),                               N_("FAT")},
  {"filesystem", "ntfs",              "*",     N_("NTFS (version %s)"),                 N_("NTFS")},
  {"filesystem", "ntfs",              NULL,    N_("NTFS"),                              N_("NTFS")},
  {"filesystem", "hfs",               NULL,    N_("HFS"),                               N_("HFS")},
  {"filesystem", "hfsplus",           NULL,    N_("HFS+"),                              N_("HFS+")},
  {"filesystem", "ext2",              "*",     N_("Ext2 (version %s)"),                 N_("Ext2")},
  {"filesystem", "ext2",              NULL,    N_("Ext2"),                              N_("Ext2")},
  {"filesystem", "ext3",              "*",     N_("Ext3 (version %s)"),                 N_("Ext3")},
  {"filesystem", "ext3",              NULL,    N_("Ext3"),                              N_("Ext3")},
  {"filesystem", "ext4",              "*",     N_("Ext4 (version %s)"),                 N_("Ext4")},
  {"filesystem", "ext4",              NULL,    N_("Ext4"),                              N_("Ext4")},
  {"filesystem", "jdb",               "*",     N_("Journal for Ext (version %s)"),      N_("JDB")},
  {"filesystem", "jdb",               "*",     N_("Journal for Ext"),                   N_("JDB")},
  {"filesystem", "xfs",               "*",     N_("XFS (version %s)"),                  N_("XFS")},
  {"filesystem", "xfs",               NULL,    N_("XFS"),                               N_("XFS")},
  {"filesystem", "iso9660",           "*",     N_("ISO 9660 (version %s)"),             N_("ISO9660")},
  {"filesystem", "iso9660",           NULL,    N_("ISO 9660"),                          N_("ISO9660")},
  {"filesystem", "udf",               "*",     N_("UDF (version %s)"),                  N_("UDF")},
  {"filesystem", "udf",               NULL,    N_("UDF"),                               N_("UDF")},
  {"other",      "swap",              "*",     N_("Swap (version %s)"),                 N_("Swap")},
  {"other",      "swap",              NULL,    N_("Swap"),                              N_("Swap")},
  {"raid",       "LVM2_member",       "*",     N_("LVM2 Phyiscal Volume (version %s)"), N_("LVM2 PV")},
  {"raid",       "LVM2_member",       NULL,    N_("LVM2 Phyiscal Volume"),              N_("LVM2 PV")},
  {"raid",       "linux_raid_member", "*",     N_("Software RAID Component (version %s)"), N_("MD Raid")},
  {"raid",       "linux_raid_member", NULL,    N_("Software RAID Component"),           N_("MD Raid")},
  {"crypto",     "crypto_LUKS",       "*",     N_("LUKS Encryption (version %s)"),      N_("LUKS")},
  {"crypto",     "crypto_LUKS",       NULL,    N_("LUKS Encryption"),                   N_("LUKS")},
  {NULL, NULL, NULL, NULL}
};

/**
 * udisks_util_get_id_for_display:
 * @usage: Usage id e.g. "filesystem" or "crypto".
 * @type: Type e.g. "ext4" or "crypto_LUKS"
 * @version: Version.
 * @long_string: Whether to produce a long string.
 *
 * Gets a human readable localized string for @usage, @type and @version.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
udisks_util_get_id_for_display (const gchar *usage,
                                const gchar *type,
                                const gchar *version,
                                gboolean     long_string)
{
  guint n;
  gchar *ret;

  ret = NULL;

  for (n = 0; id_type[n].usage != NULL; n++)
    {
      if (g_strcmp0 (id_type[n].usage, usage) == 0 &&
          g_strcmp0 (id_type[n].type, type) == 0)
        {
          if ((id_type[n].version == NULL && strlen (version) == 0))
            {
              if (long_string)
                ret = g_strdup (_(id_type[n].long_name));
              else
                ret = g_strdup (_(id_type[n].short_name));
              goto out;
            }
          else if ((g_strcmp0 (id_type[n].version, version) == 0 && strlen (version) > 0) ||
                   (g_strcmp0 (id_type[n].version, "*") == 0 && strlen (version) > 0))
            {
              if (long_string)
                ret = g_strdup_printf (_(id_type[n].long_name), version);
              else
                ret = g_strdup_printf (_(id_type[n].short_name), version);
              goto out;
            }
        }
    }

  if (long_string)
    {
      if (strlen (version) > 0)
        {
          /* Translators: Shown for unknown filesystem types.
           * First %s is the filesystem type, second %s is version.
           */
          ret = g_strdup_printf (_("Unknown (%s %s)"), type, version);
        }
      else
        {
          if (strlen (type) > 0)
            {
              /* Translators: Shown for unknown filesystem types.
               * First %s is the filesystem type.
               */
              ret = g_strdup_printf (_("Unknown (%s)"), type);
            }
          else
            {
              /* Translators: Shown for unknown filesystem types.
               */
              ret = g_strdup (_("Unknown"));
            }
        }
    }
  else
    {
      if (strlen (type) > 0)
        ret = g_strdup (type);
      else
        ret = g_strdup (_("Unknown"));
    }

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_util_get_media_compat_for_display:
 * @media_compat: An array of media types.
 *
 * Gets a human-readable string of the media in @media_compat. The
 * returned information is localized.
 *
 * Returns: A string that should be freed with g_free() or %NULL if unknown.
 */
gchar *
udisks_util_get_media_compat_for_display (const gchar* const *media_compat)
{
  guint n;
  gboolean optical_cd;
  gboolean optical_dvd;
  gboolean optical_bd;
  gboolean optical_hddvd;
  GString *result;

  optical_cd = FALSE;
  optical_dvd = FALSE;
  optical_bd = FALSE;
  optical_hddvd = FALSE;

  result = g_string_new (NULL);

  for (n = 0; media_compat != NULL && media_compat[n] != NULL; n++)
    {
      const gchar *media_name;
      const gchar *media;

      media = media_compat[n];
      media_name = NULL;
      if (g_strcmp0 (media, "flash_cf") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("CompactFlash");
        }
      else if (g_strcmp0 (media, "flash_ms") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("MemoryStick");
        }
      else if (g_strcmp0 (media, "flash_sm") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("SmartMedia");
        }
      else if (g_strcmp0 (media, "flash_sd") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("SecureDigital");
        }
      else if (g_strcmp0 (media, "flash_sdhc") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("SD High Capacity");
        }
      else if (g_strcmp0 (media, "floppy") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("Floppy");
        }
      else if (g_strcmp0 (media, "floppy_zip") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("Zip");
        }
      else if (g_strcmp0 (media, "floppy_jaz") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("Jaz");
        }
      else if (g_str_has_prefix (media, "flash"))
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("Flash");
        }
      else if (g_str_has_prefix (media, "optical_cd"))
        {
          optical_cd = TRUE;
        }
      else if (g_str_has_prefix (media, "optical_dvd"))
        {
          optical_dvd = TRUE;
        }
      else if (g_str_has_prefix (media, "optical_bd"))
        {
          optical_bd = TRUE;
        }
      else if (g_str_has_prefix (media, "optical_hddvd"))
        {
          optical_hddvd = TRUE;
        }

      if (media_name != NULL)
        {
          if (result->len > 0)
            g_string_append_c (result, '/');
          g_string_append (result, media_name);
        }
    }

  if (optical_cd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, _("CD"));
    }
  if (optical_dvd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, _("DVD"));
    }
  if (optical_bd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, _("Blu-Ray"));
    }
  if (optical_hddvd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, _("HDDVD"));
    }

  if (result->len > 0)
    return g_string_free (result, FALSE);

  g_string_free (result, TRUE);
  return NULL;
}
