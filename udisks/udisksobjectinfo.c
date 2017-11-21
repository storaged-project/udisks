/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 David Zeuthen <zeuthen@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "udisksobjectinfo.h"
#include "udisksclient.h"
#include "udisks-generated.h"

/**
 * SECTION:udisksobjectinfo
 * @title: UDisksObjectInfo
 * @short_description: Detailed information about objects
 *
 * Detailed information about the D-Bus interfaces (such as
 * #UDisksBlock and #UDisksDrive) on a #UDisksObject that is suitable
 * to display in an user interface. Use
 * udisks_client_get_object_info() to get #UDisksObjectInfo objects.
 * Note that #UDisksObjectInfo is an immutable object; once it has
 * been created it cannot be modified further.
 *
 * The <link
 * linkend="gdbus-property-org-freedesktop-UDisks2-Block.HintName">HintName</link>
 * and/or <link
 * linkend="gdbus-property-org-freedesktop-UDisks2-Block.HintName">HintIconName</link>
 * propreties on associated #UDisksBlock interfaces (if any) may
 * influence what udisks_object_info_get_icon() and
 * udisks_object_info_get_media_icon() returns.
 *
 * The value return by udisks_object_info_get_one_liner() is designed
 * to contain enough information such that it is all that needs to be
 * shown about the object. As a result for e.g.  block devices or
 * drives it contains the special device device
 * e.g. <filename>/dev/sda</filename>.
 *
 * Since: 2.1
 */

/**
 * UDisksObjectInfo:
 *
 * The #UDisksObjectInfo structure contains only private data and
 * should only be accessed using the provided API.
 *
 * Since: 2.1
 */
struct _UDisksObjectInfo
{
  GObject parent_instance;

  UDisksObject *object;
  gchar *name;
  gchar *description;
  GIcon *icon;
  GIcon *icon_symbolic;
  gchar *media_description;
  GIcon *media_icon;
  GIcon *media_icon_symbolic;
  gchar *one_liner;
  gchar *sort_key;
};

typedef struct _UDisksObjectInfoClass UDisksObjectInfoClass;

struct _UDisksObjectInfoClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (UDisksObjectInfo, udisks_object_info, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_object_info_finalize (GObject *object)
{
  UDisksObjectInfo *info = UDISKS_OBJECT_INFO (object);

  g_clear_object (&info->object);
  g_free (info->name);
  g_free (info->description);
  g_clear_object (&info->icon);
  g_clear_object (&info->icon_symbolic);
  g_free (info->media_description);
  g_clear_object (&info->media_icon);
  g_clear_object (&info->media_icon_symbolic);
  g_free (info->one_liner);
  g_free (info->sort_key);

  G_OBJECT_CLASS (udisks_object_info_parent_class)->finalize (object);
}

static void
udisks_object_info_init (UDisksObjectInfo *info)
{
}

static void
udisks_object_info_class_init (UDisksObjectInfoClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_object_info_finalize;
}


static UDisksObjectInfo *
udisks_object_info_new (UDisksObject *object)
{
  UDisksObjectInfo *ret;

  g_return_val_if_fail (object == NULL || UDISKS_IS_OBJECT (object), NULL);

  ret = g_object_new (UDISKS_TYPE_OBJECT_INFO, NULL);
  ret->object = object != NULL ? g_object_ref (object) : NULL;
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef enum
{
  DRIVE_TYPE_UNSET,
  DRIVE_TYPE_DRIVE,
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
  const gchar *media_icon_symbolic;
  DriveType    media_type;
  const gchar *drive_icon;
  const gchar *drive_icon_symbolic;
} media_data[] =
{
  /* Translators: 'Thumb' here refers to "USB thumb drive", see http://en.wikipedia.org/wiki/Thumb_drive */
  {"thumb",      NC_("media-type", "Thumb"),        NC_("media-type", "Thumb"),        "media-removable", "media-removable-symbolic", DRIVE_TYPE_DRIVE, "media-removable", "media-removable-symbolic"},

  {"floppy",     NC_("media-type", "Floppy"),       NC_("media-type", "Floppy"), "media-floppy", "media-floppy-symbolic",      DRIVE_TYPE_DISK, "drive-removable-media-floppy", "drive-removable-media-symbolic"},
  {"floppy_zip", NC_("media-type", "Zip"),          NC_("media-type", "Zip"),    "media-floppy-jaz", "media-floppy-symbolic",  DRIVE_TYPE_DISK, "drive-removable-media-floppy-jaz", "drive-removable-media-symbolic"},
  {"floppy_jaz", NC_("media-type", "Jaz"),          NC_("media-type", "Jaz"),    "media-floppy-zip", "media-floppy-symbolic",  DRIVE_TYPE_DISK, "drive-removable-media-floppy-zip", "drive-removable-media-symbolic"},

  {"flash",      NC_("media-type", "Flash"),        NC_("media-type", "Flash"),        "media-flash", "media-flash-symbolic",       DRIVE_TYPE_CARD, "drive-removable-media-flash", "drive-removable-media-symbolic"},
  {"flash_ms",   NC_("media-type", "MemoryStick"),  NC_("media-type", "MemoryStick"),  "media-flash-ms", "media-flash-symbolic",    DRIVE_TYPE_CARD, "drive-removable-media-flash-ms", "drive-removable-media-symbolic"},
  {"flash_sm",   NC_("media-type", "SmartMedia"),   NC_("media-type", "SmartMedia"),   "media-flash-sm", "media-flash-symbolic",    DRIVE_TYPE_CARD, "drive-removable-media-flash-sm", "drive-removable-media-symbolic"},
  {"flash_cf",   NC_("media-type", "CompactFlash"), NC_("media-type", "CompactFlash"), "media-flash-cf", "media-flash-symbolic",    DRIVE_TYPE_CARD, "drive-removable-media-flash-cf", "drive-removable-media-symbolic"},
  {"flash_mmc",  NC_("media-type", "MMC"),          NC_("media-type", "SD"),           "media-flash-mmc", "media-flash-symbolic",   DRIVE_TYPE_CARD, "drive-removable-media-flash-mmc", "drive-removable-media-symbolic"},
  {"flash_sd",   NC_("media-type", "SD"),           NC_("media-type", "SD"),           "media-flash-sd", "media-flash-symbolic",    DRIVE_TYPE_CARD, "drive-removable-media-flash-sd", "drive-removable-media-symbolic"},
  {"flash_sdxc", NC_("media-type", "SDXC"),         NC_("media-type", "SD"),           "media-flash-sd-xc", "media-flash-symbolic", DRIVE_TYPE_CARD, "drive-removable-media-flash-sd-xc", "drive-removable-media-symbolic"},
  {"flash_sdhc", NC_("media-type", "SDHC"),         NC_("media-type", "SD"),           "media-flash-sd-hc", "media-flash-symbolic", DRIVE_TYPE_CARD, "drive-removable-media-flash-sd-hc", "drive-removable-media-symbolic"},

  {"optical_cd",             NC_("media-type", "CD-ROM"),    NC_("media-type", "CD"),      "media-optical-cd-rom", "media-optical-symbolic",        DRIVE_TYPE_DISC, "drive-optical", "drive-optical-symbolic"},
  {"optical_cd_r",           NC_("media-type", "CD-R"),      NC_("media-type", "CD"),      "media-optical-cd-r", "media-optical-symbolic",          DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_cd_rw",          NC_("media-type", "CD-RW"),     NC_("media-type", "CD"),      "media-optical-cd-rw", "media-optical-symbolic",         DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_dvd",            NC_("media-type", "DVD"),       NC_("media-type", "DVD"),     "media-optical-dvd-rom", "media-optical-symbolic",       DRIVE_TYPE_DISC, "drive-optical", "drive-optical-symbolic"},
  {"optical_dvd_r",          NC_("media-type", "DVD-R"),     NC_("media-type", "DVD"),     "media-optical-dvd-r", "media-optical-symbolic",         DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_dvd_rw",         NC_("media-type", "DVD-RW"),    NC_("media-type", "DVD"),     "media-optical-dvd-rw", "media-optical-symbolic",        DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_dvd_ram",        NC_("media-type", "DVD-RAM"),   NC_("media-type", "DVD"),     "media-optical-dvd-ram", "media-optical-symbolic",       DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_dvd_plus_r",     NC_("media-type", "DVD+R"),     NC_("media-type", "DVD"),     "media-optical-dvd-r-plus", "media-optical-symbolic",    DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_dvd_plus_rw",    NC_("media-type", "DVD+RW"),    NC_("media-type", "DVD"),     "media-optical-dvd-rw-plus", "media-optical-symbolic",   DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_dvd_plus_r_dl",  NC_("media-type", "DVD+R DL"),  NC_("media-type", "DVD"),     "media-optical-dvd-dl-r-plus", "media-optical-symbolic", DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_dvd_plus_rw_dl", NC_("media-type", "DVD+RW DL"), NC_("media-type", "DVD"),     "media-optical-dvd-dl-r-plus", "media-optical-symbolic", DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_bd",             NC_("media-type", "BD-ROM"),    NC_("media-type", "Blu-Ray"), "media-optical-bd-rom", "media-optical-symbolic",        DRIVE_TYPE_DISC, "drive-optical", "drive-optical-symbolic"},
  {"optical_bd_r",           NC_("media-type", "BD-R"),      NC_("media-type", "Blu-Ray"), "media-optical-bd-r", "media-optical-symbolic",          DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_bd_re",          NC_("media-type", "BD-RE"),     NC_("media-type", "Blu-Ray"), "media-optical-bd-re", "media-optical-symbolic",         DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_hddvd",          NC_("media-type", "HDDVD"),     NC_("media-type", "HDDVD"),   "media-optical-hddvd-rom", "media-optical-symbolic",     DRIVE_TYPE_DISC, "drive-optical", "drive-optical-symbolic"},
  {"optical_hddvd_r",        NC_("media-type", "HDDVD-R"),   NC_("media-type", "HDDVD"),   "media-optical-hddvd-r", "media-optical-symbolic",       DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_hddvd_rw",       NC_("media-type", "HDDVD-RW"),  NC_("media-type", "HDDVD"),   "media-optical-hddvd-rw", "media-optical-symbolic",      DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_mo",             NC_("media-type", "MO"),        NC_("media-type", "CD"),      "media-optical-mo", "media-optical-symbolic",            DRIVE_TYPE_DISC, "drive-optical", "drive-optical-symbolic"},
  {"optical_mrw",            NC_("media-type", "MRW"),       NC_("media-type", "CD"),      "media-optical-mrw", "media-optical-symbolic",           DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
  {"optical_mrw_w",          NC_("media-type", "MRW-W"),     NC_("media-type", "CD"),      "media-optical-mrw-w", "media-optical-symbolic",         DRIVE_TYPE_DISC, "drive-optical-recorder", "drive-optical-symbolic"},
};

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
last_segment (const gchar *str)
{
  const gchar *ret = NULL;
  gint n;
  gint len;

  if (str == NULL)
    goto out;

  len = strlen (str);
  if (len == 0)
    {
      ret = str;
      goto out;
    }

  for (n = len - 1; n >= 0; --n)
    {
      if (str[n] == '/' && n < len - 1)
        {
          ret = str + n + 1;
          goto out;
        }
    }

  ret = str;

 out:
  return ret;
}

static void
udisks_client_get_object_info_for_block (UDisksClient     *client,
                                         UDisksBlock      *block,
                                         UDisksPartition  *partition,
                                         UDisksObjectInfo *info)
{
  guint64 size = 0;
  gchar *size_str = NULL;
  gchar *s;

  size = udisks_block_get_size (block);
  if (size > 0)
    size_str = udisks_client_get_size_for_display (client, size, FALSE, FALSE);

  info->icon = g_themed_icon_new_with_default_fallbacks ("drive-removable-media");
  info->icon_symbolic = g_themed_icon_new_with_default_fallbacks ("drive-removable-media-symbolic");
  info->name = udisks_block_dup_preferred_device (block);
  if (size_str != NULL)
    {
      info->description = g_strdup_printf (_("%s Block Device"), size_str);
    }
  else
    {
      info->description = g_strdup (_("Block Device"));
    }

  if (partition != NULL)
    {
      /* Translators: Used to describe a partition of a block device.
       *              The %u is the partition number.
       *              The %s is the description for the block device (e.g. "5 GB Block Device").
       */
      s = g_strdup_printf (C_("part-block", "Partition %u of %s"),
                           udisks_partition_get_number (partition), info->description);
      g_free (info->description);
      info->description = s;
    }

  /* Translators: String used for one-liner description of a block device.
   *              The first %s is the description of the object (e.g. "50 GB Block Device").
   *              The second %s is the special device file (e.g. "/dev/sda2").
   */
  info->one_liner = g_strdup_printf (C_("one-liner-block", "%s (%s)"),
                                     info->description,
                                     udisks_block_get_preferred_device (block));

  info->sort_key = g_strdup_printf ("02_block_%s_%u",
                                    last_segment (g_dbus_object_get_object_path (G_DBUS_OBJECT (info->object))),
                                    partition != NULL ? udisks_partition_get_number (partition) : 0);

  g_free (size_str);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_client_get_object_info_for_loop (UDisksClient     *client,
                                        UDisksLoop       *loop,
                                        UDisksBlock      *block,
                                        UDisksPartition  *partition,
                                        UDisksObjectInfo *info)
{
  guint64 size = 0;
  gchar *size_str = NULL;
  gchar *s;

  size = udisks_block_get_size (block);
  if (size > 0)
    size_str = udisks_client_get_size_for_display (client, size, FALSE, FALSE);

  info->icon = g_themed_icon_new_with_default_fallbacks ("drive-removable-media");
  info->icon_symbolic = g_themed_icon_new_with_default_fallbacks ("drive-removable-media-symbolic");
  info->name = udisks_loop_dup_backing_file (loop);
  if (size_str != NULL)
    {
      info->description = g_strdup_printf (_("%s Loop Device"), size_str);
    }
  else
    {
      info->description = g_strdup (_("Loop Device"));
    }

  if (partition != NULL)
    {
      /* Translators: Used to describe a partition of a loop device.
       *              The %u is the partition number.
       *              The %s is the description for the loop device (e.g. "5 GB Loop Device").
       */
      s = g_strdup_printf (C_("part-loop", "Partition %u of %s"),
                           udisks_partition_get_number (partition), info->description);
      g_free (info->description);
      info->description = s;
    }

  /* Translators: String used for one-liner description of a loop device.
   *              The first %s is the description of the object (e.g. "2 GB Loop Device").
   *              The second %s is the name of the backing file (e.g. "/home/davidz/file.iso").
   *              The third %s is the special device file (e.g. "/dev/loop2").
   */
  info->one_liner = g_strdup_printf (C_("one-liner-loop", "%s — %s (%s)"),
                                     info->description,
                                     info->name,
                                     udisks_block_get_preferred_device (block));

  info->sort_key = g_strdup_printf ("03_loop_%s_%u",
                                    last_segment (g_dbus_object_get_object_path (G_DBUS_OBJECT (info->object))),
                                    partition != NULL ? udisks_partition_get_number (partition) : 0);

  g_free (size_str);
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
format_mdraid_level (const gchar *level)
{
  const gchar *ret = NULL;

  if (g_strcmp0 (level, "raid0") == 0)
    ret = C_("mdraid-desc", "RAID-0 Array");
  else if (g_strcmp0 (level, "raid1") == 0)
    ret = C_("mdraid-desc", "RAID-1 Array");
  else if (g_strcmp0 (level, "raid4") == 0)
    ret = C_("mdraid-desc", "RAID-4 Array");
  else if (g_strcmp0 (level, "raid5") == 0)
    ret = C_("mdraid-desc", "RAID-5 Array");
  else if (g_strcmp0 (level, "raid6") == 0)
    ret = C_("mdraid-desc", "RAID-6 Array");
  else if (g_strcmp0 (level, "raid10") == 0)
    ret = C_("mdraid-desc", "RAID-10 Array");
  else
    ret = C_("mdraid-desc", "RAID Array");

  return ret;
}

static void
udisks_client_get_object_info_for_mdraid (UDisksClient     *client,
                                          UDisksMDRaid     *mdraid,
                                          UDisksPartition  *partition,
                                          UDisksObjectInfo *info)
{
  UDisksBlock *block = NULL;
  guint64 size = 0;
  gchar *size_str = NULL;
  const gchar *name;
  const gchar *level;
  gchar *s;

  block = udisks_client_get_block_for_mdraid (client, mdraid);

  size = udisks_mdraid_get_size (mdraid);
  if (size > 0)
    size_str = udisks_client_get_size_for_display (client, size, FALSE, FALSE);

  name = udisks_mdraid_get_name (mdraid);
  s = strstr (name, ":");
  if (s != NULL && strlen (s) > 1)
    info->name = g_strdup (s + 1);
  else
    info->name = g_strdup (name);
  info->icon = g_themed_icon_new_with_default_fallbacks ("drive-multidisk");
  info->icon_symbolic = g_themed_icon_new_with_default_fallbacks ("drive-multidisk-symbolic");

  level = udisks_mdraid_get_level (mdraid);
  if (size_str != NULL)
    {
      /* Translators: Used to format the description for a RAID array.
       *              The first %s is the size (e.g. '42.0 GB').
       *              The second %s is the level (e.g. 'RAID-5 Array').
       */
      info->description = g_strdup_printf (C_("mdraid-desc", "%s %s"),
                                           size_str,
                                           format_mdraid_level (level));
    }
  else
    {
      info->description = g_strdup (format_mdraid_level (level));
    }

  if (partition != NULL)
    {
      /* Translators: Used to describe a partition of a RAID Array.
       *              The %u is the partition number.
       *              The %s is the description for the drive (e.g. "2 TB RAID-5").
       */
      s = g_strdup_printf (C_("part-raid", "Partition %u of %s"),
                           udisks_partition_get_number (partition), info->description);
      g_free (info->description);
      info->description = s;
    }

  if (strlen (info->name) > 0)
    {
      if (block != NULL)
        {
          /* Translators: String used for one-liner description of running RAID array.
           *              The first %s is the array name (e.g. "AlphaGo").
           *              The second %s is the size and level (e.g. "2 TB RAID-5").
           *              The third %s is the special device file (e.g. "/dev/sda").
           */
          info->one_liner = g_strdup_printf (C_("one-liner-mdraid-running", "%s — %s (%s)"),
                                             info->name,
                                             info->description,
                                             udisks_block_get_preferred_device (block));
        }
      else
        {
          /* Translators: String used for one-liner description of non-running RAID array.
           *              The first %s is the array name (e.g. "AlphaGo").
           *              The second %s is the size and level (e.g. "2 TB RAID-5").
           */
          info->one_liner = g_strdup_printf (C_("one-liner-mdraid-not-running", "%s — %s"),
                                             info->name,
                                             info->description);
        }
    }
  else
    {
      if (block != NULL)
        {
          /* Translators: String used for one-liner description of running RAID array w/o a name.
           *              The first %s is the array name (e.g. "AlphaGo").
           *              The second %s is the size and level (e.g. "2 TB RAID-5").
           *              The third %s is the special device file (e.g. "/dev/sda").
           */
          info->one_liner = g_strdup_printf (C_("one-liner-mdraid-no-name-running", "%s — %s"),
                                             info->description,
                                             udisks_block_get_preferred_device (block));
        }
      else
        {
          /* Translators: String used for one-liner description of non-running RAID array w/o a name.
           *              The first %s is the array name (e.g. "AlphaGo").
           *              The second %s is the size and level (e.g. "2 TB RAID-5").
           */
          info->one_liner = g_strdup_printf (C_("one-liner-mdraid-no-name-not-running", "%s"),
                                             info->description);
        }
    }

  g_clear_object (&block);

  info->sort_key = g_strdup_printf ("01_mdraid_%s_%u", udisks_mdraid_get_uuid (mdraid),
                                    partition != NULL ? udisks_partition_get_number (partition) : 0);

  g_free (size_str);
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

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_client_get_object_info_for_drive (UDisksClient     *client,
                                         UDisksDrive      *drive,
                                         UDisksPartition  *partition,
                                         UDisksObjectInfo *info)
{
  const gchar *vendor;
  const gchar *model;
  const gchar *media;
  const gchar *const *media_compat;
  gboolean media_available;
  gboolean media_removable;
  gint rotation_rate;
  guint64 size;
  gchar *size_str;
  guint n;
  GString *desc_str;
  DriveType desc_type;
  gchar *hyphenated_connection_bus;
  const gchar *connection_bus;
  UDisksBlock *block = NULL;
  gchar *s;
  const gchar *cs;
  UDisksBlock *block_for_partition = NULL;

  g_return_if_fail (UDISKS_IS_DRIVE (drive));

  size_str = NULL;

  vendor = udisks_drive_get_vendor (drive);
  model = udisks_drive_get_model (drive);
  size = udisks_drive_get_size (drive);
  media_removable = udisks_drive_get_media_removable (drive);
  media_available = udisks_drive_get_media_available (drive);
  rotation_rate = udisks_drive_get_rotation_rate (drive);
  if (size > 0)
    size_str = udisks_client_get_size_for_display (client, size, FALSE, FALSE);
  media = udisks_drive_get_media (drive);
  media_compat = udisks_drive_get_media_compatibility (drive);
  connection_bus = udisks_drive_get_connection_bus (drive);
  if (strlen (connection_bus) > 0)
    hyphenated_connection_bus = g_strdup_printf ("-%s", connection_bus);
  else
    hyphenated_connection_bus = g_strdup ("");

  /* Name is easy - that's just "$vendor $model" */
  if (strlen (vendor) == 0)
    vendor = NULL;
  if (strlen (model) == 0)
    model = NULL;
  info->name = g_strdup_printf ("%s%s%s",
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
          if (info->icon == NULL)
            info->icon = g_themed_icon_new_with_default_fallbacks (media_data[n].drive_icon);
          if (info->icon_symbolic == NULL)
            info->icon_symbolic = g_themed_icon_new_with_default_fallbacks (media_data[n].drive_icon_symbolic);
          if (strstr (desc_str->str, media_data[n].media_family) == NULL)
            {
              if (desc_str->len > 0)
                g_string_append (desc_str, "/");
              g_string_append (desc_str, g_dpgettext2 (GETTEXT_PACKAGE, "media-type", media_data[n].media_family));
            }
          desc_type = media_data[n].media_type;
        }

      if (media_removable && media_available)
        {
          /* media */
          if (g_strcmp0 (media, media_data[n].id) == 0)
            {
              if (info->media_description == NULL)
                {
                  switch (media_data[n].media_type)
                    {
                    case DRIVE_TYPE_UNSET:
                      g_assert_not_reached ();
                      break;
                    case DRIVE_TYPE_DRIVE:
                      /* Translators: Used to describe drive without removable media. The %s is the type, e.g. 'Thumb' */
                      info->media_description = g_strdup_printf (C_("drive-with-fixed-media", "%s Drive"), g_dpgettext2 (GETTEXT_PACKAGE, "media-type", media_data[n].media_name));
                      break;
                    case DRIVE_TYPE_DISK:
                      /* Translators: Used to describe generic media. The %s is the type, e.g. 'Zip' or 'Floppy' */
                      info->media_description = g_strdup_printf (C_("drive-with-generic-media", "%s Disk"), g_dpgettext2 (GETTEXT_PACKAGE, "media-type", media_data[n].media_name));
                      break;
                    case DRIVE_TYPE_CARD:
                      /* Translators: Used to describe flash media. The %s is the type, e.g. 'SD' or 'CompactFlash' */
                      info->media_description = g_strdup_printf (C_("flash-media", "%s Card"), g_dpgettext2 (GETTEXT_PACKAGE, "media-type", media_data[n].media_name));
                      break;
                    case DRIVE_TYPE_DISC:
                      /* Translators: Used to describe optical discs. The %s is the type, e.g. 'CD-R' or 'DVD-ROM' */
                      info->media_description = g_strdup_printf (C_("optical-media", "%s Disc"), g_dpgettext2 (GETTEXT_PACKAGE, "media-type", media_data[n].media_name));
                      break;
                    }
                }
              if (info->media_icon == NULL)
                info->media_icon = g_themed_icon_new_with_default_fallbacks (media_data[n].media_icon);
              if (info->media_icon_symbolic == NULL)
                info->media_icon_symbolic = g_themed_icon_new_with_default_fallbacks (media_data[n].media_icon_symbolic);
            }
        }
    }

  switch (desc_type)
    {
    case DRIVE_TYPE_UNSET:
      if (media_removable)
        {
          if (size_str != NULL)
            {
              /* Translators: Used to describe a drive. The %s is the size, e.g. '20 GB' */
              info->description = g_strdup_printf (C_("drive-with-size", "%s Drive"), size_str);
            }
          else
            {
              /* Translators: Used to describe a drive we know very little about (removable media or size not known) */
              info->description = g_strdup (C_("generic-drive", "Drive"));
            }
        }
      else
        {
          if (rotation_rate == 0)
            {
              if (size_str != NULL)
                {
                  /* Translators: Used to describe a non-rotating drive (rotation rate either unknown
                   * or it's a solid-state drive). The %s is the size, e.g. '20 GB'.  */
                  info->description = g_strdup_printf (C_("disk-non-rotational", "%s Disk"), size_str);
                }
              else
                {
                  /* Translators: Used to describe a non-rotating drive (rotation rate either unknown
                   * or it's a solid-state drive). The drive is either using removable media or its
                   * size not known. */
                  info->description = g_strdup (C_("disk-non-rotational", "Disk"));
                }
            }
          else
            {
              if (size_str != NULL)
                {
                  /* Translators: Used to describe a hard-disk drive (HDD). The %s is the size, e.g. '20 GB'.  */
                  info->description = g_strdup_printf (C_("disk-hdd", "%s Hard Disk"), size_str);
                }
              else
                {
                  /* Translators: Used to describe a hard-disk drive (HDD) (removable media or size not known) */
                  info->description = g_strdup (C_("disk-hdd", "Hard Disk"));
                }
            }
        }
      break;

    case DRIVE_TYPE_CARD:
      /* Translators: Used to describe a card reader. The %s is the card type e.g. 'CompactFlash'.  */
      info->description = g_strdup_printf (C_("drive-card-reader", "%s Card Reader"), desc_str->str);
      break;

    case DRIVE_TYPE_DRIVE: /* explicit fall-through */
    case DRIVE_TYPE_DISK: /* explicit fall-through */
    case DRIVE_TYPE_DISC:
      if (!media_removable && size_str != NULL)
        {
          /* Translators: Used to describe drive. The first %s is the size e.g. '20 GB' and the
           * second %s is the drive type e.g. 'Thumb'.
           */
          info->description = g_strdup_printf (C_("drive-with-size-and-type", "%s %s Drive"), size_str, desc_str->str);
        }
      else
        {
          /* Translators: Used to describe drive. The first %s is the drive type e.g. 'Thumb'.
           */
          info->description = g_strdup_printf (C_("drive-with-type", "%s Drive"), desc_str->str);
        }
      break;
    }
  g_string_free (desc_str, TRUE);

  /* fallback for icon */
  if (info->icon == NULL)
    {
      if (media_removable)
        {
          s = g_strdup_printf ("drive-removable-media%s", hyphenated_connection_bus);
        }
      else
        {
          if (rotation_rate == 0)
            s = g_strdup_printf ("drive-harddisk-solidstate%s", hyphenated_connection_bus);
          else
            s = g_strdup_printf ("drive-harddisk%s", hyphenated_connection_bus);
        }
      info->icon = g_themed_icon_new_with_default_fallbacks (s);
      g_free (s);
    }
  /* fallback for icon_symbolic */
  if (info->icon_symbolic == NULL)
    {
      if (media_removable)
        {
          s = g_strdup_printf ("drive-removable-media%s-symbolic", hyphenated_connection_bus);
        }
      else
        {
          if (rotation_rate == 0)
            s = g_strdup_printf ("drive-harddisk-solidstate%s-symbolic", hyphenated_connection_bus);
          else
            s = g_strdup_printf ("drive-harddisk%s-symbolic", hyphenated_connection_bus);
        }
      info->icon_symbolic = g_themed_icon_new_with_default_fallbacks (s);
      g_free (s);
    }
  /* fallback for media_icon */
  if (media_available && info->media_icon == NULL)
    {
      if (media_removable)
        {
          s = g_strdup_printf ("drive-removable-media%s", hyphenated_connection_bus);
        }
      else
        {
          if (rotation_rate == 0)
            s = g_strdup_printf ("drive-harddisk-solidstate%s", hyphenated_connection_bus);
          else
            s = g_strdup_printf ("drive-harddisk%s", hyphenated_connection_bus);
        }
      info->media_icon = g_themed_icon_new_with_default_fallbacks (s);
      g_free (s);
    }
  /* fallback for media_icon_symbolic */
  if (media_available && info->media_icon_symbolic == NULL)
    {
      if (media_removable)
        {
          s = g_strdup_printf ("drive-removable-media%s-symbolic", hyphenated_connection_bus);
        }
      else
        {
          if (rotation_rate == 0)
            s = g_strdup_printf ("drive-harddisk-solidstate%s-symbolic", hyphenated_connection_bus);
          else
            s = g_strdup_printf ("drive-harddisk%s-symbolic", hyphenated_connection_bus);
        }
      info->media_icon_symbolic = g_themed_icon_new_with_default_fallbacks (s);
      g_free (s);
    }

  /* prepend a qualifier to the media description, based on the disc state */
  if (udisks_drive_get_optical_blank (drive))
    {
      /* Translators: String used for a blank disc. The %s is the disc type e.g. "CD-RW Disc" */
      s = g_strdup_printf (C_("optical-media", "Blank %s"), info->media_description);
      g_free (info->media_description);
      info->media_description = s;
    }
  else if (udisks_drive_get_optical_num_audio_tracks (drive) > 0 &&
           udisks_drive_get_optical_num_data_tracks (drive) > 0)
    {
      /* Translators: String used for a mixed disc. The %s is the disc type e.g. "CD-ROM Disc" */
      s = g_strdup_printf (C_("optical-media", "Mixed %s"), info->media_description);
      g_free (info->media_description);
      info->media_description = s;
    }
  else if (udisks_drive_get_optical_num_audio_tracks (drive) > 0 &&
           udisks_drive_get_optical_num_data_tracks (drive) == 0)
    {
      /* Translators: String used for an audio disc. The %s is the disc type e.g. "CD-ROM Disc" */
      s = g_strdup_printf (C_("optical-media", "Audio %s"), info->media_description);
      g_free (info->media_description);
      info->media_description = s;
    }

  /* Apply UDISKS_NAME, UDISKS_ICON_NAME, UDISKS_SYMBOLIC_ICON_NAME hints, if available */
  block = udisks_client_get_block_for_drive (client, drive, TRUE);
  if (block != NULL)
    {
      cs = udisks_block_get_hint_name (block);
      if (cs != NULL && strlen (cs) > 0)
        {
          g_free (info->description);
          g_free (info->media_description);
          info->description = g_strdup (cs);
          info->media_description = g_strdup (cs);
        }

      cs = udisks_block_get_hint_icon_name (block);
      if (cs != NULL && strlen (cs) > 0)
        {
          g_clear_object (&info->icon);
          g_clear_object (&info->media_icon);
          info->icon = g_themed_icon_new_with_default_fallbacks (cs);
          info->media_icon = g_themed_icon_new_with_default_fallbacks (cs);
        }

      cs = udisks_block_get_hint_symbolic_icon_name (block);
      if (cs != NULL && strlen (cs) > 0)
        {
          g_clear_object (&info->icon_symbolic);
          g_clear_object (&info->media_icon_symbolic);
          info->icon_symbolic = g_themed_icon_new_with_default_fallbacks (cs);
          info->media_icon_symbolic = g_themed_icon_new_with_default_fallbacks (cs);
        }
    }

  if (partition != NULL)
    {
      GDBusObject *object_for_partition;
      object_for_partition = g_dbus_interface_get_object (G_DBUS_INTERFACE (partition));
      if (object_for_partition != NULL)
        block_for_partition = udisks_object_peek_block (UDISKS_OBJECT (object_for_partition));
    }
  if (block_for_partition == NULL)
    block_for_partition = block;

  if (partition != NULL)
    {
      /* Translators: Used to describe a partition of a drive.
       *              The %u is the partition number.
       *              The %s is the description for the drive (e.g. "2 GB Thumb Drive").
       */
      s = g_strdup_printf (C_("part-drive", "Partition %u of %s"),
                           udisks_partition_get_number (partition), info->description);
      g_free (info->description);
      info->description = s;
    }

  /* calculate and set one-liner */
  if (block != NULL)
    {
      const gchar *drive_revision = udisks_drive_get_revision (drive);
      if (strlen (drive_revision) > 0)
        {
          /* Translators: String used for one-liner description of drive.
           *              The first %s is the description of the object (e.g. "80 GB Disk" or "Partition 2 of 2 GB Thumb Drive").
           *              The second %s is the name of the object (e.g. "INTEL SSDSA2MH080G1GC").
           *              The third %s is the fw revision (e.g "45ABX21").
           *              The fourth %s is the special device file (e.g. "/dev/sda").
           */
          info->one_liner = g_strdup_printf (C_("one-liner-drive", "%s — %s [%s] (%s)"),
                                             info->description,
                                             info->name,
                                             drive_revision,
                                             udisks_block_get_preferred_device (block_for_partition));
        }
      else
        {
          /* Translators: String used for one-liner description of drive w/o known fw revision.
           *              The first %s is the description of the object (e.g. "80 GB Disk").
           *              The second %s is the name of the object (e.g. "INTEL SSDSA2MH080G1GC").
           *              The third %s is the special device file (e.g. "/dev/sda").
           */
          info->one_liner = g_strdup_printf (C_("one-liner-drive", "%s — %s (%s)"),
                                             info->description,
                                             info->name,
                                             udisks_block_get_preferred_device (block_for_partition));
        }
    }

  g_free (hyphenated_connection_bus);
  g_free (size_str);

  info->sort_key = g_strdup_printf ("00_drive_%s", udisks_drive_get_sort_key (drive));

  g_clear_object (&block);
}

/**
 * udisks_client_get_object_info:
 * @client: A #UDisksClient.
 * @object: A #UDisksObject.
 *
 * Gets information about a #UDisksObject instance that is suitable to
 * present in an user interface. Information is returned in the
 * #UDisksObjectInfo object and is localized.
 *
 * Returns: (transfer full): A #UDisksObjectInfo instance that should be freed with g_object_unref().
 *
 * Since: 2.1
 */
UDisksObjectInfo *
udisks_client_get_object_info (UDisksClient        *client,
                               UDisksObject        *object)
{
  UDisksObjectInfo *ret = NULL;
  UDisksDrive *drive = NULL;
  UDisksBlock *block = NULL;
  UDisksPartition *partition = NULL;
  UDisksMDRaid *mdraid = NULL;
  UDisksLoop *loop = NULL;

  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);
  g_return_val_if_fail (UDISKS_IS_OBJECT (object), NULL);

  ret = udisks_object_info_new (object);
  drive = udisks_object_get_drive (object);
  block = udisks_object_get_block (object);
  loop = udisks_object_get_loop (object);
  partition = udisks_object_get_partition (object);
  mdraid = udisks_object_get_mdraid (object);
  if (drive != NULL)
    {
      udisks_client_get_object_info_for_drive (client, drive, NULL, ret);
    }
  else if (mdraid != NULL)
    {
      udisks_client_get_object_info_for_mdraid (client, mdraid, NULL, ret);
    }
  else if (block != NULL)
    {
      drive = udisks_client_get_drive_for_block (client, block);
      if (drive != NULL)
        {
          udisks_client_get_object_info_for_drive (client, drive, partition, ret);
          goto out;
        }

      mdraid = udisks_client_get_mdraid_for_block (client, block);
      if (mdraid != NULL)
        {
          udisks_client_get_object_info_for_mdraid (client, mdraid, partition, ret);
          goto out;
        }

      if (loop != NULL)
        udisks_client_get_object_info_for_loop (client, loop, block, partition, ret);
      else
        udisks_client_get_object_info_for_block (client, block, partition, ret);
    }
 out:
  g_clear_object (&loop);
  g_clear_object (&mdraid);
  g_clear_object (&partition);
  g_clear_object (&block);
  g_clear_object (&drive);

#if 0
  /* for debugging */
  g_print ("%s -> dd='%s', md='%s', ol='%s' and di='%s', mi='%s' sk='%s'\n",
           g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
           ret->description,
           ret->media_description,
           ret->one_liner,
           ret->icon == NULL ? "" : g_icon_to_string (ret->icon),
           ret->media_icon == NULL ? "" : g_icon_to_string (ret->media_icon),
           ret->sort_key);
#endif

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gpointer
_g_object_ref0 (gpointer object)
{
  if (object != NULL)
    return g_object_ref (G_OBJECT (object));
  else
    return NULL;
}

/**
 * udisks_client_get_drive_info:
 * @client: A #UDisksClient.
 * @drive: A #UDisksDrive.
 * @out_name: (out) (allow-none): Return location for name or %NULL.
 * @out_description: (out) (allow-none): Return location for description or %NULL.
 * @out_drive_icon: (out) (allow-none): Return location for icon representing the drive or %NULL.
 * @out_media_description: (out) (allow-none): Return location for description of the media or %NULL.
 * @out_media_icon: (out) (allow-none): Return location for icon representing the media or %NULL.
 *
 * Gets information about a #UDisksDrive object that is suitable to
 * present in an user interface. The returned strings are localized.
 *
 * Deprecated: 2.1: Use udisks_client_get_object_info() instead.
 */
void
udisks_client_get_drive_info (UDisksClient  *client,
                              UDisksDrive   *drive,
                              gchar        **out_name,
                              gchar        **out_description,
                              GIcon        **out_icon,
                              gchar        **out_media_description,
                              GIcon        **out_media_icon)
{
  UDisksObjectInfo *info;

  g_return_if_fail (UDISKS_IS_CLIENT (client));
  g_return_if_fail (UDISKS_IS_DRIVE (drive));

  info = udisks_object_info_new (NULL);
  udisks_client_get_object_info_for_drive (client, drive, NULL, info);

  if (out_name != NULL)
    *out_name = g_strdup (info->name);

  if (out_description != NULL)
    *out_description = g_strdup (info->description);

  if (out_icon != NULL)
    *out_icon = _g_object_ref0 (info->icon);

  if (out_media_description != NULL)
    *out_media_description = g_strdup (info->media_description);

  if (out_media_icon != NULL)
    *out_media_icon = _g_object_ref0 (info->media_icon);

  g_object_unref (info);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_object_info_get_object:
 * @info: A #UDisksObjectInfo.
 *
 * Gets the #UDisksObject that @info is for
 *
 * Returns: (transfer none): The object - do not free or unref, the reference belongs to @info.
 *
 * Since: 2.1
 */
UDisksObject *
udisks_object_info_get_object (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->object;
}

/**
 * udisks_object_info_get_name:
 * @info: A #UDisksObjectInfo.
 *
 * Gets the name.
 *
 * Returns: (transfer none): The value or %NULL. Do not free or unref, the value belongs to @info.
 *
 * Since: 2.1
 */
const gchar *
udisks_object_info_get_name (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->name;
}

/**
 * udisks_object_info_get_description:
 * @info: A #UDisksObjectInfo.
 *
 * Gets the description.
 *
 * Returns: (transfer none): The value or %NULL. Do not free or unref, the value belongs to @info.
 *
 * Since: 2.1
 */
const gchar *
udisks_object_info_get_description (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->description;
}

/**
 * udisks_object_info_get_icon:
 * @info: A #UDisksObjectInfo.
 *
 * Gets the icon.
 *
 * Returns: (transfer none): The value or %NULL. Do not free or unref, the value belongs to @info.
 *
 * Since: 2.1
 */
GIcon *
udisks_object_info_get_icon (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->icon;
}

/**
 * udisks_object_info_get_icon_symbolic:
 * @info: A #UDisksObjectInfo.
 *
 * Gets the symbolic icon.
 *
 * Returns: (transfer none): The value or %NULL. Do not free or unref, the value belongs to @info.
 *
 * Since: 2.1
 */
GIcon *
udisks_object_info_get_icon_symbolic (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->icon_symbolic;
}

/**
 * udisks_object_info_get_media_description:
 * @info: A #UDisksObjectInfo.
 *
 * Gets the media description.
 *
 * Returns: (transfer none): The value or %NULL. Do not free or unref, the value belongs to @info.
 *
 * Since: 2.1
 */
const gchar *
udisks_object_info_get_media_description (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->media_description;
}

/**
 * udisks_object_info_get_media_icon:
 * @info: A #UDisksObjectInfo.
 *
 * Gets the media icon.
 *
 * Returns: (transfer none): The value or %NULL. Do not free or unref, the value belongs to @info.
 *
 * Since: 2.1
 */
GIcon *
udisks_object_info_get_media_icon (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->media_icon;
}

/**
 * udisks_object_info_get_media_icon_symbolic:
 * @info: A #UDisksObjectInfo.
 *
 * Gets the symbolic media icon.
 *
 * Returns: (transfer none): The value or %NULL. Do not free or unref, the value belongs to @info.
 *
 * Since: 2.1
 */
GIcon *
udisks_object_info_get_media_icon_symbolic (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->media_icon_symbolic;
}

/**
 * udisks_object_info_get_one_liner:
 * @info: A #UDisksObjectInfo.
 *
 * Gets a one-line description.
 *
 * Returns: (transfer none): The value or %NULL. Do not free or unref, the value belongs to @info.
 *
 * Since: 2.1
 */
const gchar *
udisks_object_info_get_one_liner (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->one_liner;
}

/**
 * udisks_object_info_get_sort_key:
 * @info: A #UDisksObjectInfo.
 *
 * Gets the sort-key for @info. This can be used with g_strcmp0() to
 * sort objects.
 *
 * Returns: (transfer none): The sort key or %NULL. Do not free or unref, the value belongs to @info.
 *
 * Since: 2.1
 */
const gchar *
udisks_object_info_get_sort_key (UDisksObjectInfo *info)
{
  g_return_val_if_fail (UDISKS_IS_OBJECT_INFO (info), NULL);
  return info->sort_key;
}
