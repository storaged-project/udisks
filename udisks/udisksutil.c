/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 David Zeuthen <zeuthen@gmail.com>
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

const static struct
{
  const gchar *scheme;
  const gchar *name;
} part_scheme[] =
{
  {"dos", N_("Master Boot Record")},
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
  {"gpt", "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", N_("Basic Data")}, /* same as ms bdp */
  {"gpt", "a19d880f-05fc-4d3b-a006-743f0f84911e", N_("Linux RAID")},
  {"gpt", "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f", N_("Linux Swap")},
  {"gpt", "e6d6d379-f507-44c2-a23c-238f2a3df928", N_("Linux LVM")},
  {"gpt", "8da63339-0007-60c0-c436-083ac8230908", N_("Linux Reserved")},
  /* Not associated with any OS */
  {"gpt", "024dee41-33e7-11d3-9d69-0008c781f39f", N_("MBR Partition Scheme")},
  {"gpt", "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", N_("EFI System")},
  {"gpt", "21686148-6449-6e6f-744e-656564454649", N_("BIOS Boot")},
  /* Microsoft */
  {"gpt", "e3c9e316-0b5c-4db8-817d-f92df00215ae", N_("Microsoft Reserved")},
  {"gpt", "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", N_("Microsoft Basic Data")}, /* same as Linux Basic Data */
  {"gpt", "5808c8aa-7e8f-42e0-85d2-e1e90434cfb3", N_("Microsoft LDM metadata")},
  {"gpt", "af9b60a0-1431-4f62-bc68-3311714a69ad", N_("Microsoft LDM data")},
  {"gpt", "de94bba4-06d1-4d40-a16a-bfd50179d6ac", N_("Microsoft Windows Recovery Environment")},
  /* HP-UX */
  {"gpt", "75894c1e-3aeb-11d3-b7c1-7b03a0000000", N_("HP-UX Data")},
  {"gpt", "e2a1e728-32e3-11d6-a682-7b03a0000000", N_("HP-UX Service")},
  /* FreeBSD */
  {"gpt", "83bd6b9d-7f41-11dc-be0b-001560b84f0f", N_("FreeBSD Boot")},
  {"gpt", "516e7cb4-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD Data")},
  {"gpt", "516e7cb5-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD Swap")},
  {"gpt", "516e7cb6-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD UFS")},
  {"gpt", "516e7cb8-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD Vinum")},
  {"gpt", "516e7cba-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD ZFS")},
  /* Solaris */
  {"gpt", "6a82cb45-1dd2-11b2-99a6-080020736631", N_("Solaris Boot")},
  {"gpt", "6a85cf4d-1dd2-11b2-99a6-080020736631", N_("Solaris Root")},
  {"gpt", "6a87c46f-1dd2-11b2-99a6-080020736631", N_("Solaris Swap")},
  {"gpt", "6a8b642b-1dd2-11b2-99a6-080020736631", N_("Solaris Backup")},
  {"gpt", "6a898cc3-1dd2-11b2-99a6-080020736631", N_("Solaris /usr")}, /* same as Apple ZFS */
  {"gpt", "6a8ef2e9-1dd2-11b2-99a6-080020736631", N_("Solaris /var")},
  {"gpt", "6a90ba39-1dd2-11b2-99a6-080020736631", N_("Solaris /home")},
  {"gpt", "6a9283a5-1dd2-11b2-99a6-080020736631", N_("Solaris Alternate Sector")},
  {"gpt", "6a945a3b-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved")},
  {"gpt", "6a9630d1-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved (2)")},
  {"gpt", "6a980767-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved (3)")},
  {"gpt", "6a96237f-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved (4)")},
  {"gpt", "6a8d2ac7-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved (5)")},
  /* Apple OS X */
  {"gpt", "48465300-0000-11aa-aa11-00306543ecac", N_("Apple HFS/HFS+")},
  {"gpt", "55465300-0000-11aa-aa11-00306543ecac", N_("Apple UFS")},
  {"gpt", "6a898cc3-1dd2-11b2-99a6-080020736631", N_("Apple ZFS")}, /* same as Solaris /usr */
  {"gpt", "52414944-0000-11aa-aa11-00306543ecac", N_("Apple RAID")},
  {"gpt", "52414944-5f4f-11aa-aa11-00306543ecac", N_("Apple RAID (offline)")},
  {"gpt", "426f6f74-0000-11aa-aa11-00306543ecac", N_("Apple Boot")},
  {"gpt", "4c616265-6c00-11aa-aa11-00306543ecac", N_("Apple Label")},
  {"gpt", "5265636f-7665-11aa-aa11-00306543ecac", N_("Apple TV Recovery")},
  /* NetBSD */
  {"gpt", "49f48d32-b10e-11dc-b99b-0019d1879648", N_("NetBSD Swap")},
  {"gpt", "49f48d5a-b10e-11dc-b99b-0019d1879648", N_("NetBSD FFS")},
  {"gpt", "49f48d82-b10e-11dc-b99b-0019d1879648", N_("NetBSD LFS")},
  {"gpt", "49f48daa-b10e-11dc-b99b-0019d1879648", N_("NetBSD RAID")},
  {"gpt", "2db519c4-b10f-11dc-b99b-0019d1879648", N_("NetBSD Concatenated")},
  {"gpt", "2db519ec-b10f-11dc-b99b-0019d1879648", N_("NetBSD Encrypted")},

  /* see http://developer.apple.com/documentation/mac/devices/devices-126.html
   *     http://lists.apple.com/archives/Darwin-drivers/2003/May/msg00021.html */
  {"apm", "Apple_Unix_SVR2", N_("Apple UFS")},
  {"apm", "Apple_HFS", N_("Apple HFS/HFS")},
  {"apm", "Apple_partition_map", N_("Apple Partition Map")},
  {"apm", "Apple_Free", N_("Unused")},
  {"apm", "Apple_Scratch", N_("Empty")},
  {"apm", "Apple_Driver", N_("Driver")},
  {"apm", "Apple_Driver43", N_("Driver 4.3")},
  {"apm", "Apple_PRODOS", N_("ProDOS file system")},
  {"apm", "DOS_FAT_12", N_("FAT 12")},
  {"apm", "DOS_FAT_16", N_("FAT 16")},
  {"apm", "DOS_FAT_32", N_("FAT 32")},
  {"apm", "Windows_FAT_16", N_("FAT 16 (Windows)")},
  {"apm", "Windows_FAT_32", N_("FAT 32 (Windows)")},

  /* see http://www.win.tue.nl/~aeb/partitions/partition_types-1.html */
  {"dos", "0x00",  N_("Empty")},
  {"dos", "0x01",  N_("FAT12")},
  {"dos", "0x04",  N_("FAT16 <32M")},
  {"dos", "0x05",  N_("Extended")},
  {"dos", "0x06",  N_("FAT16")},
  {"dos", "0x07",  N_("HPFS/NTFS")},
  {"dos", "0x0b",  N_("W95 FAT32")},
  {"dos", "0x0c",  N_("W95 FAT32 (LBA)")},
  {"dos", "0x0e",  N_("W95 FAT16 (LBA)")},
  {"dos", "0x0f",  N_("W95 Ext d (LBA)")},
  {"dos", "0x10",  N_("OPUS")},
  {"dos", "0x11",  N_("Hidden FAT12")},
  {"dos", "0x12",  N_("Compaq diagnostics")},
  {"dos", "0x14",  N_("Hidden FAT16 <32M")},
  {"dos", "0x16",  N_("Hidden FAT16")},
  {"dos", "0x17",  N_("Hidden HPFS/NTFS")},
  {"dos", "0x1b",  N_("Hidden W95 FAT32")},
  {"dos", "0x1c",  N_("Hidden W95 FAT32 (LBA)")},
  {"dos", "0x1e",  N_("Hidden W95 FAT16 (LBA)")},
  {"dos", "0x3c",  N_("PartitionMagic")},
  {"dos", "0x81",  N_("Minix")}, /* cf. http://en.wikipedia.org/wiki/MINIX_file_system */
  {"dos", "0x82",  N_("Linux swap")},
  {"dos", "0x83",  N_("Linux")},
  {"dos", "0x84",  N_("Hibernation")},
  {"dos", "0x85",  N_("Linux Extended")},
  {"dos", "0x8e",  N_("Linux LVM")},
  {"dos", "0xa0",  N_("Hibernation")},
  {"dos", "0xa5",  N_("FreeBSD")},
  {"dos", "0xa6",  N_("OpenBSD")},
  {"dos", "0xa8",  N_("Mac OS X")},
  {"dos", "0xaf",  N_("Mac OS X")},
  {"dos", "0xbe",  N_("Solaris boot")},
  {"dos", "0xbf",  N_("Solaris")},
  {"dos", "0xeb",  N_("BeOS BFS")},
  {"dos", "0xec",  N_("SkyOS SkyFS")},
  {"dos", "0xee",  N_("EFI GPT")},
  {"dos", "0xef",  N_("EFI (FAT-12/16/32)")},
  {"dos", "0xfd",  N_("Linux RAID auto")},
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
 * @long_string: Whether to produce a long string.
 *
 * Gets a human readable localized string for @scheme and @type.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
udisks_util_get_part_type_for_display (const gchar *scheme,
                                       const gchar *type,
                                       gboolean     long_string)
{
  guint n;
  gchar *ret;

  for (n = 0; part_type[n].name != NULL; n++)
    {
      if (g_strcmp0 (part_type[n].scheme, scheme) == 0 &&
          g_strcmp0 (part_type[n].type, type) == 0)
        {
          if (long_string)
            {
              /* Translators: First %s is the detailed partition type (e.g. "FAT16 (0x16)") and
               * second %s is the partition type (e.g. 0x16)
               */
              ret = g_strdup_printf ("%s (%s)", _(part_type[n].name), type);
            }
          else
            {
              ret = g_strdup (_(part_type[n].name));
            }
          goto out;
        }
    }

  if (long_string)
    {
      /* Translators: Shown for unknown partition types.
       * First %s is the partition type.
       */
      ret = g_strdup_printf (_("Unknown (%s)"), type);
    }
  else
    {
      ret = g_strdup (_("Unknown"));
    }

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
  {"raid",       "LVM2_member",       "*",     N_("LVM2 Physical Volume (%s)"),         N_("LVM2 PV")},
  {"raid",       "LVM2_member",       NULL,    N_("LVM2 Physical Volume"),              N_("LVM2 PV")},
  {"raid",       "linux_raid_member", "*",     N_("Software RAID Component (version %s)"), N_("MD Raid")},
  {"raid",       "linux_raid_member", NULL,    N_("Software RAID Component"),           N_("MD Raid")},
  {"raid",       "zfs_member",        "*",     N_("ZFS Device (ZPool version %s)"),     N_("ZFS (v%s)")},
  {"raid",       "zfs_member",        NULL,    N_("ZFS Device"),                        N_("ZFS")},
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
