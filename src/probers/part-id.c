/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2009 David Zeuthen <david@fubar.dk>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define LIBUDEV_I_KNOW_THE_API_IS_SUBJECT_TO_CHANGE
#include <libudev.h>

#include "helpers/partutil.h"

static void
usage (int argc,
       char *argv[])
{
  execlp ("man", "man", "part_id", NULL);
  g_printerr ("Cannot show man page: %m\n");
  exit (1);
}

static gchar *
decode_udev_encoded_string (const gchar *str)
{
  GString *s;
  gchar *ret;
  const gchar *end_valid;
  guint n;

  s = g_string_new (NULL);
  for (n = 0; str[n] != '\0'; n++)
    {
      if (str[n] == '\\')
        {
          gint val;

          if (str[n + 1] != 'x' || str[n + 2] == '\0' || str[n + 3] == '\0')
            {
              g_print ("**** NOTE: malformed encoded string '%s'\n", str);
              break;
            }

          val = (g_ascii_xdigit_value (str[n + 2]) << 4) | g_ascii_xdigit_value (str[n + 3]);

          g_string_append_c (s, val);

          n += 3;
        }
      else
        {
          g_string_append_c (s, str[n]);
        }
    }

  if (!g_utf8_validate (s->str, -1, &end_valid))
    {
      g_print ("**** NOTE: The string '%s' is not valid UTF-8. Invalid characters begins at '%s'\n", s->str, end_valid);
      ret = g_strndup (s->str, end_valid - s->str);
      g_string_free (s, TRUE);
    }
  else
    {
      ret = g_string_free (s, FALSE);
    }

  return ret;
}

static int
sysfs_get_int (const char *dir,
               const char *attribute)
{
  int result;
  char *contents;
  char *filename;

  result = 0;
  filename = g_build_filename (dir, attribute, NULL);
  if (g_file_get_contents (filename, &contents, NULL, NULL))
    {
      result = strtol (contents, NULL, 0);
      g_free (contents);
    }
  g_free (filename);

  return result;
}

static guint64
sysfs_get_uint64 (const char *dir,
                  const char *attribute)
{
  guint64 result;
  char *contents;
  char *filename;

  result = 0;
  filename = g_build_filename (dir, attribute, NULL);
  if (g_file_get_contents (filename, &contents, NULL, NULL))
    {
      result = strtoll (contents, NULL, 0);
      g_free (contents);
    }
  g_free (filename);

  return result;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_syspath (struct udev *udev,
             const gchar *device_file)
{
  struct udev_device *device;
  struct stat statbuf;
  gchar *ret;

  ret = NULL;

  if (stat (device_file, &statbuf) != 0)
    {
      g_printerr ("Error statting %s: %m\n", device_file);
      goto out;
    }

  device = udev_device_new_from_devnum (udev, 'b', statbuf.st_rdev);
  if (device == NULL)
    {
      g_printerr ("Error getting udev device for %s: %m\n", device_file);
      goto out;
    }
  ret = g_strdup (udev_device_get_syspath (device));
  udev_device_unref (device);

 out:
  return ret;
}

/**
 * get_part_table_device_file:
 * @udev: An udev context.
 * @given_device_file: The device file given on the command line.
 * @out_partition_table_syspath: Return location for sysfs path of the slave the partition is for.
 * @out_offset: Return location for offset or %NULL.
 * @out_partition_number: Return location for partition number or %NULL.
 *
 * If @given_device_file is not a partition, returns a copy of it and
 * sets @out_offset and @out_partition_number to 0 and
 * @out_partition_table_syspath to the sysfs path for
 * @given_device_file.
 *
 * Otherwise, returns the device file for the block device for which
 * @given_device_file is a partition of and returns the offset of the
 * partition in @out_offset and the partition number in
 * @out_partition_number. The sysfs path is set in
 * @out_partition_table_syspath.
 *
 * If something goes wrong, %NULL is returned.
 */
static gchar *
get_part_table_device_file (struct udev  *udev,
                            const gchar  *given_device_file,
                            gchar       **out_partition_table_syspath,
                            guint64      *out_offset,
                            guint        *out_partition_number)
{
  gchar *ret;
  guint64 offset;
  guint partition_number;
  gchar *devpath;
  gchar *partition_table_syspath;

  devpath = NULL;
  offset = 0;
  ret = NULL;
  partition_table_syspath = NULL;

  devpath = get_syspath (udev, given_device_file);
  if (devpath == NULL)
    goto out;

  partition_number = sysfs_get_int (devpath, "partition");

  /* find device file for partition table device */
  if (partition_number > 0)
    {
      struct udev_device *device;
      guint n;

      /* partition */
      partition_table_syspath = g_strdup (devpath);
      for (n = strlen (partition_table_syspath) - 1; partition_table_syspath[n] != '/'; n--)
        partition_table_syspath[n] = '\0';
      partition_table_syspath[n] = '\0';

      device = udev_device_new_from_syspath (udev, partition_table_syspath);
      if (device == NULL)
        {
          g_printerr ("Error getting udev device for syspath %s: %m\n", partition_table_syspath);
          goto out;
        }
      ret = g_strdup (udev_device_get_devnode (device));
      udev_device_unref (device);
      if (ret == NULL)
        {
          /* This Should Not Happen™, but was reported in a distribution upgrade
             scenario, so handle it gracefully */
          g_printerr ("Error getting devnode from udev device path %s: %m\n", partition_table_syspath);
          goto out;
        }
      offset = sysfs_get_uint64 (devpath, "start") * 512;
    }
  else
    {
      struct udev_device *device;
      const char *targets_type;
      const char *encoded_targets_params;

      device = udev_device_new_from_syspath (udev, devpath);
      g_printerr ("device=%p' for devpath=%s\n", device, devpath);
      if (device == NULL)
        {
          g_printerr ("Error getting udev device for syspath %s: %m\n", devpath);
          goto out;
        }

#if 0
      targets_type = udev_device_get_property_value (device, "UDISKS_DM_TARGETS_TYPE");
      encoded_targets_params = udev_device_get_property_value (device, "UDISKS_DM_TARGETS_PARAMS");
#else
      targets_type = g_getenv ("UDISKS_DM_TARGETS_TYPE");
      encoded_targets_params = g_getenv ("UDISKS_DM_TARGETS_PARAMS");
#endif
      g_printerr ("targets_type=%s and env is %s\n", targets_type, g_getenv ("UDISKS_DM_TARGETS_TYPE"));
      g_printerr ("encoded_targets_params=%s and env var is %s\n", encoded_targets_params, g_getenv ("UDISKS_DM_TARGETS_PARAMS"));

      if (g_strcmp0 (targets_type, "linear") == 0)
        {
          gint partition_slave_major;
          gint partition_slave_minor;
          guint64 offset_sectors;
          gchar *targets_params;

          targets_params = decode_udev_encoded_string (encoded_targets_params);
          if (sscanf (targets_params,
                      "%d:%d\x20%" G_GUINT64_FORMAT,
                      &partition_slave_major,
                      &partition_slave_minor,
                      &offset_sectors) == 3)
            {
              struct udev_device *mp_device;

              mp_device = udev_device_new_from_devnum (udev, 'b', makedev (partition_slave_major,
                                                                           partition_slave_minor));
              if (mp_device != NULL)
                {
                  const char *dm_uuid;

                  ret = g_strdup (udev_device_get_devnode (mp_device));
                  offset = offset_sectors * 512;

                  /* now figure out partition_number
                   *
                   * this is kind of a hack.. but works since UUID is of the
                   * form part2-mpath-3600508b400105df70000e00000d80000
                   */
                  partition_number = 0;
#if 0
                  dm_uuid = udev_device_get_property_value (device, "DM_UUID");
#else
                  dm_uuid = g_getenv ("DM_UUID");
#endif
                  if (dm_uuid != NULL && g_str_has_prefix (dm_uuid, "part"))
                    partition_number = atoi (dm_uuid + 4);

                  if (partition_number < 1)
                    {
                      g_free (ret);
                      ret = NULL;
                      goto out;
                    }

                  partition_table_syspath = g_strdup (udev_device_get_syspath (mp_device));

                  udev_device_unref (mp_device);
                  g_free (targets_params);
                  udev_device_unref (device);
                  goto out;
                }
            }
          g_free (targets_params);
        }
      udev_device_unref (device);

      /* not a kernel partition */
      partition_table_syspath = g_strdup (devpath);
      ret = g_strdup (given_device_file);
      partition_number = 0;
    }

 out:
  if (out_offset != NULL)
    *out_offset = offset;
  if (out_partition_number != NULL)
    *out_partition_number = partition_number;
  if (out_partition_table_syspath != NULL)
    *out_partition_table_syspath = partition_table_syspath;
  else
    g_free (partition_table_syspath);

  g_free (devpath);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static guint
count_entries (PartitionTable *pt)
{
  guint ret;
  guint num_top_level;
  guint n;

  ret = 0;

  num_top_level = part_table_get_num_entries (pt);
  for (n = 0; n < num_top_level; n++)
    {
      PartitionTable *nested;

      if (part_table_entry_is_in_use (pt, n))
        ret++;

      nested = part_table_entry_get_nested (pt, n);
      if (nested != NULL)
        {
          ret += part_table_get_num_entries (nested);
        }
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int argc,
      char *argv[])
{
  guint n;
  gint fd;
  gchar *devpath;
  const gchar *device_file;
  gchar *partition_table_device_file;
  struct udev *udev;
  PartitionTable *partition_table;
  guint64 partition_offset;
  guint partition_number;
  gchar *partition_table_syspath;

  udev = NULL;
  devpath = NULL;
  partition_table_device_file = NULL;
  partition_table_syspath = NULL;
  partition_table = NULL;

  udev = udev_new ();
  if (udev == NULL)
    {
      g_printerr ("Error initializing libudev: %m\n");
      goto out;
    }

  device_file = NULL;
  for (n = 1; n < (guint) argc; n++)
    {
      if (strcmp (argv[n], "--help") == 0)
        {
          usage (argc, argv);
          return 0;
        }
      else
        {
          if (device_file != NULL)
            usage (argc, argv);
          device_file = argv[n];
        }
    }

  if (device_file == NULL)
    {
      g_printerr ("no device\n");
      goto out;
    }

  partition_table_device_file = get_part_table_device_file (udev,
                                                            device_file,
                                                            &partition_table_syspath,
                                                            &partition_offset,
                                                            &partition_number);
  g_printerr ("using device_file=%s syspath=%s, offset=%" G_GUINT64_FORMAT " and number=%d for %s\n",
              partition_table_device_file,
              partition_table_syspath,
              partition_offset,
              partition_number,
              device_file);

  fd = open (partition_table_device_file, O_RDONLY);

  /* TODO: right now we also use part_id to determine if media is available or not. This
   *       should probably be done elsewhere
   */
  if (partition_offset == 0)
    {
      if (fd < 0)
        {
          g_print ("UDISKS_MEDIA_AVAILABLE=0\n");
        }
      else
        {
          g_print ("UDISKS_MEDIA_AVAILABLE=1\n");
        }
    }

  if (fd < 0)
    {
      g_printerr ("Error opening %s: %m\n", partition_table_device_file);
      goto out;
    }
  partition_table = part_table_load_from_disk (fd);
  if (partition_table == NULL)
    {
      g_printerr ("No partition table found on %s: %m\n", partition_table_device_file);
      goto out;
    }
  close (fd);

  if (partition_offset > 0)
    {
      PartitionTable *partition_table_for_entry;
      gint entry_num;
      gchar *type;
      gchar *label;
      gchar *uuid;
      gchar **flags;
      gchar *flags_combined;
      guint64 size;

      /* partition */
      part_table_find (partition_table, partition_offset, &partition_table_for_entry, &entry_num);
      if (entry_num == -1)
        {
          g_printerr ("Error finding partition at offset %" G_GUINT64_FORMAT " on %s\n",
                      partition_offset,
                      partition_table_device_file);
          goto out;
        }

      type = part_table_entry_get_type (partition_table_for_entry, entry_num);
      label = part_table_entry_get_label (partition_table_for_entry, entry_num);
      uuid = part_table_entry_get_uuid (partition_table_for_entry, entry_num);
      flags = part_table_entry_get_flags (partition_table_for_entry, entry_num);
      size = part_table_entry_get_size (partition_table_for_entry, entry_num);

      flags_combined = g_strjoinv (" ", flags);

      g_print ("UDISKS_PARTITION=1\n");
      g_print ("UDISKS_PARTITION_SCHEME=%s\n",
               //part_get_scheme_name (part_table_get_scheme (partition_table_for_entry)));
               part_get_scheme_name (part_table_get_scheme (partition_table)));
      g_print ("UDISKS_PARTITION_NUMBER=%d\n", partition_number);
      g_print ("UDISKS_PARTITION_TYPE=%s\n", type != NULL ? type : "");
      g_print ("UDISKS_PARTITION_SIZE=%" G_GINT64_FORMAT "\n", size);
      g_print ("UDISKS_PARTITION_LABEL=%s\n", label != NULL ? label : "");
      g_print ("UDISKS_PARTITION_UUID=%s\n", uuid != NULL ? uuid : "");
      g_print ("UDISKS_PARTITION_FLAGS=%s\n", flags_combined);
      g_print ("UDISKS_PARTITION_SLAVE=%s\n", partition_table_syspath);
      g_print ("UDISKS_PARTITION_OFFSET=%" G_GUINT64_FORMAT "\n", partition_offset);

      g_free (type);
      g_free (label);
      g_free (uuid);
      g_strfreev (flags);
      g_free (flags_combined);
    }
  else
    {
      g_print ("UDISKS_PARTITION_TABLE=1\n");
      g_print ("UDISKS_PARTITION_TABLE_SCHEME=%s\n", part_get_scheme_name (part_table_get_scheme (partition_table)));
      g_print ("UDISKS_PARTITION_TABLE_COUNT=%d\n", count_entries (partition_table));
    }

 out:
  g_free (devpath);
  g_free (partition_table_device_file);
  g_free (partition_table_syspath);
  if (partition_table != NULL)
    part_table_free (partition_table);
  if (udev != NULL)
    udev_unref (udev);

  return 0;
}

