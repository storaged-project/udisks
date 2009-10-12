/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#include "partutil.h"

static void
usage (int argc, char *argv[])
{
        execlp ("man", "man", "part_id", NULL);
        g_printerr ("Cannot show man page: %m\n");
        exit (1);
}

static int
sysfs_get_int (const char *dir, const char *attribute)
{
        int result;
        char *contents;
        char *filename;

        result = 0;
        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_get_contents (filename, &contents, NULL, NULL)) {
                result = strtol (contents, NULL, 0);
                g_free (contents);
        }
        g_free (filename);


        return result;
}

static guint64
sysfs_get_uint64 (const char *dir, const char *attribute)
{
        guint64 result;
        char *contents;
        char *filename;

        result = 0;
        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_get_contents (filename, &contents, NULL, NULL)) {
                result = strtoll (contents, NULL, 0);
                g_free (contents);
        }
        g_free (filename);

        return result;
}

int
main (int argc, char *argv[])
{
        guint n;
        gint fd;
        gint partition_number;
        gchar *devpath;
        const gchar *device_file;
        gchar *partition_table_device_file;
        struct udev *udev;
        PartitionTable *partition_table;

        udev = NULL;
        devpath = NULL;
        partition_table_device_file = NULL;
        partition_table = NULL;

        udev = udev_new ();
        if (udev == NULL) {
                g_printerr ("Error initializing libudev: %m\n");
                goto out;
        }

        device_file = NULL;
        for (n = 1; n < (guint) argc; n++) {
                if (strcmp (argv[n], "--help") == 0) {
                        usage (argc, argv);
                        return 0;
                } else {
                        if (device_file != NULL)
                                usage (argc, argv);
                        device_file = argv[n];
                }
	}

        if (device_file == NULL) {
                g_printerr ("no device\n");
                goto out;
        }

        devpath = getenv ("DEVPATH");
        if (devpath != NULL) {
                devpath = g_build_filename ("/sys", devpath, NULL);
        } else {
                struct udev_device *device;
                struct stat statbuf;

                if (stat (device_file, &statbuf) != 0) {
                        g_printerr ("Error statting %s: %m\n", device_file);
                        goto out;
                }

                device = udev_device_new_from_devnum (udev, 'b', statbuf.st_rdev);
                if (device == NULL) {
                        g_printerr ("Error getting udev device for %s: %m\n", device_file);
                        goto out;
                }
                devpath = g_strdup (udev_device_get_syspath (device));
                udev_device_unref (device);
        }

        partition_number = sysfs_get_int (devpath, "partition");

        /* find device file for partition table device */
        if (partition_number > 0) {
                struct udev_device *device;
                gchar *partition_table_devpath;

                /* partition */
                partition_table_devpath = g_strdup (devpath);
                for (n = strlen (partition_table_devpath) - 1; partition_table_devpath[n] != '/'; n--)
                        partition_table_devpath[n] = '\0';
                partition_table_devpath[n] = '\0';

                device = udev_device_new_from_syspath (udev, partition_table_devpath);
                if (device == NULL) {
                        g_printerr ("Error getting udev device for syspath %s: %m\n", partition_table_devpath);
                        goto out;
                }
                partition_table_device_file = g_strdup (udev_device_get_devnode (device));
                udev_device_unref (device);
                if (partition_table_device_file == NULL) {
			/* This Should Not Happen™, but was reported in a distribution upgrade 
			   scenario, so handle it gracefully */
                        g_printerr ("Error getting devnode from udev device path %s: %m\n", partition_table_devpath);
                        goto out;
                }
                g_free (partition_table_devpath);
        } else {
                /* not partition */
                partition_table_device_file = g_strdup (device_file);
        }

        fd = open (partition_table_device_file, O_RDONLY);

        /* TODO: right now we also use part_id to determine if media is available or not. This
         *       should probably be done elsewhere
         */
        if (partition_number == 0) {
                if (fd < 0) {
                        g_print ("DKD_MEDIA_AVAILABLE=0\n");
                } else {
                        g_print ("DKD_MEDIA_AVAILABLE=1\n");
                }
        }

        if (fd < 0) {
                g_printerr ("Error opening %s: %m\n", partition_table_device_file);
                goto out;
        }
        partition_table = part_table_load_from_disk (fd);
        if (partition_table == NULL) {
                g_printerr ("No partition table found on %s: %m\n", partition_table_device_file);
                goto out;
        }
        close (fd);

        if (partition_number > 0) {
                guint64 partition_offset;
                PartitionTable *partition_table_for_entry;
                gint entry_num;
                gchar *type;
                gchar *label;
                gchar *uuid;
                gchar **flags;
                gchar *flags_combined;
                guint64 size;

                /* partition */
                partition_offset = sysfs_get_uint64 (devpath, "start") * 512;
                part_table_find (partition_table,
                                 partition_offset,
                                 &partition_table_for_entry,
                                 &entry_num);
                if (entry_num == -1) {
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

                g_print ("DKD_PARTITION=1\n");
                g_print ("DKD_PARTITION_SCHEME=%s\n",
                         //part_get_scheme_name (part_table_get_scheme (partition_table_for_entry)));
                         part_get_scheme_name (part_table_get_scheme (partition_table)));
                g_print ("DKD_PARTITION_NUMBER=%d\n", partition_number);
                g_print ("DKD_PARTITION_TYPE=%s\n", type != NULL ? type : "");
                g_print ("DKD_PARTITION_SIZE=%" G_GINT64_FORMAT "\n", size);
                g_print ("DKD_PARTITION_LABEL=%s\n", label != NULL ? label : "");
                g_print ("DKD_PARTITION_UUID=%s\n", uuid != NULL ? uuid : "");
                g_print ("DKD_PARTITION_FLAGS=%s\n", flags_combined);

                g_free (type);
                g_free (label);
                g_free (uuid);
                g_strfreev (flags);
                g_free (flags_combined);
        } else {
                g_print ("DKD_PARTITION_TABLE=1\n");
                g_print ("DKD_PARTITION_TABLE_SCHEME=%s\n",
                         part_get_scheme_name (part_table_get_scheme (partition_table)));
        }

out:
        g_free (devpath);
        g_free (partition_table_device_file);
        if (partition_table != NULL)
                part_table_free (partition_table);
        if (udev != NULL)
                udev_unref (udev);

        return 0;
}

