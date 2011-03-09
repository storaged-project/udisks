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

#include <string.h>

//#include <glib/gi18n-lib.h>
#define _(x) x

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
 * @long_string: Whether to produce a long string
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

static gboolean
strv_has_prefix (const gchar * const *haystack,
                 const gchar          *needle_prefix)
{
  gboolean ret;
  guint n;

  ret = FALSE;

  for (n = 0; haystack != NULL && haystack[n] != NULL; n++)
    {
      if (g_str_has_prefix (haystack[n], needle_prefix))
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  return ret;
}

static const gchar *
lun_get_drive_icon_name (UDisksLun *lun)
{
  const gchar *ret;
  const gchar* const *media_compat;

  ret = NULL;

  media_compat = udisks_lun_get_media_compatibility (lun);

  /* TODO: it would probably be nice to export a property whether this device can
   *       burn discs etc. so we can use the 'drive-optical-recorder' icon when
   *       applicable.
   */

  /* first, try to switch on media type */
  if (strv_has (media_compat, "optical_cd"))
    ret = "drive-optical";
  else if (strv_has (media_compat, "floppy"))
    ret = "drive-removable-media-floppy";
  else if (strv_has (media_compat, "floppy_zip"))
    ret = "drive-removable-media-floppy-zip";
  else if (strv_has (media_compat, "floppy_jaz"))
    ret = "drive-removable-media-floppy-jaz";
  else if (strv_has (media_compat, "flash_cf"))
    ret = "drive-removable-media-flash-cf";
  else if (strv_has (media_compat, "flash_ms"))
    ret = "drive-removable-media-flash-ms";
  else if (strv_has (media_compat, "flash_sm"))
    ret = "drive-removable-media-flash-sm";
  else if (strv_has (media_compat, "flash_sd"))
    ret = "drive-removable-media-flash-sd";
  else if (strv_has (media_compat, "flash_sdhc"))
    ret = "drive-removable-media-flash-sd"; /* TODO: get icon name for sdhc */
  else if (strv_has (media_compat, "flash_mmc"))
    ret = "drive-removable-media-flash-sd"; /* TODO: get icon for mmc */
  else if (strv_has_prefix (media_compat, "flash"))
    ret = "drive-removable-media-flash";

  if (ret != NULL)
    goto out;

  /* TODO: check something like connection-interface to choose USB/Firewire/etc icons */
  if (udisks_lun_get_media_removable (lun))
    {
      ret = "drive-removable-media";
      goto out;
    }
  ret = "drive-harddisk";

 out:
  return ret;

}

static const struct
{
        const char *disc_type;
        const char *icon_name;
} disc_data[] = {
  {"optical_cd",             "media-optical-cd-rom",        },
  {"optical_cd_r",           "media-optical-cd-r",          },
  {"optical_cd_rw",          "media-optical-cd-rw",         },
  {"optical_dvd",            "media-optical-dvd-rom",       },
  {"optical_dvd_r",          "media-optical-dvd-r",         },
  {"optical_dvd_rw",         "media-optical-dvd-rw",        },
  {"optical_dvd_ram",        "media-optical-dvd-ram",       },
  {"optical_dvd_plus_r",     "media-optical-dvd-r-plus",    },
  {"optical_dvd_plus_rw",    "media-optical-dvd-rw-plus",   },
  {"optical_dvd_plus_r_dl",  "media-optical-dvd-dl-r-plus", },
  {"optical_dvd_plus_rw_dl", "media-optical-dvd-dl-r-plus", },
  {"optical_bd",             "media-optical-bd-rom",        },
  {"optical_bd_r",           "media-optical-bd-r",          },
  {"optical_bd_re",          "media-optical-bd-re",         },
  {"optical_hddvd",          "media-optical-hddvd-rom",     },
  {"optical_hddvd_r",        "media-optical-hddvd-r",       },
  {"optical_hddvd_rw",       "media-optical-hddvd-rw",      },
  {"optical_mo",             "media-optical-mo",            },
  {"optical_mrw",            "media-optical-mrw",           },
  {"optical_mrw_w",          "media-optical-mrw-w",         },
  {NULL, NULL}
};

static const gchar *
lun_get_media_icon_name (UDisksLun *lun)
{
  const gchar *ret;
  const gchar *media;

  ret = NULL;

  /* first try the media */
  media = udisks_lun_get_media (lun);
  if (strlen (media) > 0)
    {
      if (g_strcmp0 (media, "flash_cf") == 0)
        ret = "media-flash-cf";
      else if (g_strcmp0 (media, "flash_ms") == 0)
        ret = "media-flash-ms";
      else if (g_strcmp0 (media, "flash_sm") == 0)
        ret = "media-flash-sm";
      else if (g_strcmp0 (media, "flash_sd") == 0)
        ret = "media-flash-sd";
      else if (g_strcmp0 (media, "flash_sdhc") == 0)
        ret = "media-flash-sd"; /* TODO: get icon name for sdhc */
      else if (g_strcmp0 (media, "flash_mmc") == 0)
        ret = "media-flash-sd"; /* TODO: get icon for mmc */
      else if (g_strcmp0 (media, "floppy") == 0)
        ret = "media-floppy";
      else if (g_strcmp0 (media, "floppy_zip") == 0)
        ret = "media-floppy-zip";
      else if (g_strcmp0 (media, "floppy_jaz") == 0)
        ret = "media-floppy-jaz";
      else if (g_str_has_prefix (media, "flash"))
        ret = "media-flash";
      else if (g_str_has_prefix (media, "optical"))
        {
          guint n;
          for (n = 0; disc_data[n].disc_type != NULL; n++)
            {
              if (g_strcmp0 (disc_data[n].disc_type, media) == 0)
                {
                  ret = disc_data[n].icon_name;
                  break;
                }
            }
          if (ret == NULL)
            ret = "media-optical";
        }
    }

  if (ret != NULL)
    goto out;

  /* TODO: check something like connection-interface to choose USB/Firewire/etc icons */
  if (udisks_lun_get_media_removable (lun))
    {
      ret = "drive-removable-media";
      goto out;
    }
  ret = "drive-harddisk";

 out:
  return ret;
}

static void
get_lun_name_from_media_compat (UDisksLun *lun,
                                GString   *result)
{
  guint n;
  gboolean optical_cd;
  gboolean optical_dvd;
  gboolean optical_bd;
  gboolean optical_hddvd;
  const gchar* const *media_compat;

  media_compat = udisks_lun_get_media_compatibility (lun);

  optical_cd = FALSE;
  optical_dvd = FALSE;
  optical_bd = FALSE;
  optical_hddvd = FALSE;

  for (n = 0; media_compat != NULL && media_compat[n] != NULL; n++) {
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
}

/**
 * udisks_util_get_lun_info:
 * @lun: A #UDisksLun.
 * @out_name: (out allow-none): Return location for name or %NULL.
 * @out_description: (out allow-none): Return location for description or %NULL.
 * @out_drive_icon: (out allow-none): Return location for icon representing the drive or %NULL.
 * @out_media_icon: (out allow-none): Return location for icon representing the media or %NULL.
 *
 * Gets information about a #UDisksLun object that is suitable to
 * present in an user interface. The returned information is
 * localized.
 *
 * If the @lun doesn't support removable media, then the returned icon
 * for the media is always the same as for the drive.
 */
void
udisks_util_get_lun_info (UDisksLun  *lun,
                          gchar     **out_name,
                          gchar     **out_description,
                          GIcon     **out_drive_icon,
                          GIcon     **out_media_icon)
{
  gchar *name;
  gchar *description;
  const gchar *vendor;
  const gchar *model;
  guint64 size;
  gboolean is_removable;
  GString *result;
  gboolean rotation_rate;
  gchar *strsize;

  g_return_if_fail (UDISKS_IS_LUN (lun));

  name = NULL;
  description = NULL;

  strsize = NULL;
  result = g_string_new (NULL);

  /* TODO: support presentation-name for overrides */

  vendor = udisks_lun_get_vendor (lun);
  model = udisks_lun_get_model (lun);
  if (strlen (vendor) == 0)
    vendor = NULL;
  if (strlen (model) == 0)
    model = NULL;

  size = udisks_lun_get_size (lun);

  is_removable = udisks_lun_get_media_removable (lun);
  rotation_rate = udisks_lun_get_rotation_rate (lun);

  if (size > 0)
    strsize = udisks_util_get_size_for_display (size, FALSE, FALSE);

  if (is_removable)
    {
      get_lun_name_from_media_compat (lun, result);

      /* If we know the media type, just append Drive */
      if (result->len > 0)
        {
          GString *new_result;

          new_result = g_string_new (NULL);
          /* Translators: %s is the media type e.g. 'CD/DVD' or 'CompactFlash' */
          g_string_append_printf (new_result, _("%s Drive"), result->str);
          g_string_free (result, TRUE);
          result = new_result;
        }
      else
        {
          /* Otherwise just "Removable Drive", possibly with the size */
          if (strsize != NULL)
            {
              g_string_append_printf (result,
                                      _("%s Removable Drive"),
                                      strsize);
            }
          else
            {
              g_string_append (result,
                               _("Removable Drive"));
            }
        }
    }
  else
    {
      /* Media is not removable, use "Hard Disk" resp. "Solid-State Disk"
       * unless we actually know what media types the drive is compatible with
       */
      get_lun_name_from_media_compat (lun, result);

      if (result->len > 0)
        {
          GString *new_result;
          new_result = g_string_new (NULL);
          if (strsize != NULL)
            {
              /* Translators: first %s is the size, second %s is the media type
               * e.g. 'CD/DVD' or 'CompactFlash'
               */
              g_string_append_printf (new_result, _("%s %s Drive"),
                                      strsize,
                                      result->str);
            }
          else
            {
              /* Translators: %s is the media type e.g. 'CD/DVD' or 'CompactFlash' */
              g_string_append_printf (new_result, _("%s Drive"),
                                      result->str);
            }
          g_string_free (result, TRUE);
          result = new_result;
        }
      else
        {
          if (rotation_rate != 0)
            {
              if (strsize != NULL)
                {
                  /* Translators: This string is used to describe a hard disk.
                   * The first %s is the size of the drive e.g. '45 GB'.
                   */
                  g_string_append_printf (result, _("%s Hard Disk"), strsize);
                }
              else
                {
                  /* Translators: This string is used to describe a hard disk where the size
                   * is not known.
                   */
                  g_string_append (result, _("Hard Disk"));
                }
            }
          else
            {
              if (strsize != NULL)
                {
                  /* Translators: This string is used to describe a non-rotating disk. The first %s is
                   * the size of the drive e.g. '45 GB'.
                   */
                  g_string_append_printf (result, _("%s Disk"),
                                          strsize);
                }
              else
                {
                  /* Translators: This string is used to describe a non-rotating disk where the size
                   * is not known.
                   */
                  g_string_append (result, _("Disk"));
                }
            }
        }
    }

  description = g_string_free (result, FALSE);

  name = g_strdup_printf ("%s%s%s",
                          vendor != NULL ? vendor : "",
                          vendor != NULL ? " " : "",
                          model != NULL ? model : "");

  if (out_name != NULL)
    *out_name = name;
  else
    g_free (name);

  if (out_description != NULL)
    *out_description = description;
  else
    g_free (description);

  if (out_drive_icon != NULL)
    *out_drive_icon = g_themed_icon_new_with_default_fallbacks (lun_get_drive_icon_name (lun));
  if (out_media_icon != NULL)
    *out_media_icon = g_themed_icon_new_with_default_fallbacks (lun_get_media_icon_name (lun));
}
