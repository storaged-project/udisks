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

#if 0
static void
print_entry (PartitionTable *p, int entry, int print_number)
{
        char *type;
        char *label;
        char *uuid;
        char **flags;
        char *flags_combined;
        guint64 offset;
        guint64 size;

        type = part_table_entry_get_type (p, entry);
        label = part_table_entry_get_label (p, entry);
        uuid = part_table_entry_get_uuid (p, entry);
        flags = part_table_entry_get_flags (p, entry);
        offset = part_table_entry_get_offset (p, entry);
        size = part_table_entry_get_size (p, entry);

        flags_combined = g_strjoinv (" ", flags);

        printf ("DKD_PART_P%d_TYPE=%s\n", print_number, type != NULL ? type : "");
        printf ("DKD_PART_P%d_OFFSET=%" G_GINT64_FORMAT "\n", print_number, offset);
        printf ("DKD_PART_P%d_SIZE=%" G_GINT64_FORMAT "\n", print_number, size);
        printf ("DKD_PART_P%d_LABEL=%s\n", print_number, label != NULL ? label : "");
        printf ("DKD_PART_P%d_UUID=%s\n", print_number, uuid != NULL ? uuid : "");
        printf ("DKD_PART_P%d_FLAGS=%s\n", print_number, flags_combined);

        g_free (type);
        g_free (label);
        g_free (uuid);
        g_strfreev (flags);
        g_free (flags_combined);
}

static GString *e_types;
static GString *e_offsets;
static GString *e_sizes;
static GString *e_labels;
static GString *e_uuids;
static GString *e_flags;

static void
entries_begin (void)
{
        e_types = g_string_new (NULL);
        e_offsets = g_string_new (NULL);
        e_sizes = g_string_new (NULL);
        e_labels = g_string_new (NULL);
        e_uuids = g_string_new (NULL);
        e_flags = g_string_new (NULL);
}

static void
entries_add (PartitionTable *p, int entry, int print_number)
{
        char *type;
        char *label;
        char *uuid;
        char **flags;
        char *flags_combined;
        guint64 offset;
        guint64 size;

        type = part_table_entry_get_type (p, entry);
        label = part_table_entry_get_label (p, entry);
        uuid = part_table_entry_get_uuid (p, entry);
        flags = part_table_entry_get_flags (p, entry);
        offset = part_table_entry_get_offset (p, entry);
        size = part_table_entry_get_size (p, entry);

        flags_combined = g_strjoinv (" ", flags);

        if (e_types->len != 0)
                g_string_append_c (e_types, ' ');
        if (e_offsets->len != 0)
                g_string_append_c (e_offsets, ' ');
        if (e_sizes->len != 0)
                g_string_append_c (e_sizes, ' ');
        if (e_labels->len != 0)
                g_string_append_c (e_labels, ' ');
        if (e_uuids->len != 0)
                g_string_append_c (e_uuids, ' ');
        if (e_flags->len != 0)
                g_string_append_c (e_flags, ' ');

        /* TODO: escape label */

        g_string_append_printf (e_types, "%s", type != NULL ? type : "");
        g_string_append_printf (e_offsets, "%" G_GINT64_FORMAT, offset);
        g_string_append_printf (e_sizes, "%" G_GINT64_FORMAT, size);
        g_string_append_printf (e_labels, "%s", label != NULL ? label : "");
        g_string_append_printf (e_uuids, "%s", uuid != NULL ? uuid : "");
        g_string_append_printf (e_flags, "%s", flags_combined);

        g_free (type);
        g_free (label);
        g_free (uuid);
        g_strfreev (flags);
        g_free (flags_combined);
}

static void
entries_print (void)
{
        printf ("DKD_PART_TYPES=%s\n", e_types->str);
        printf ("DKD_PART_OFFSETS=%s\n", e_offsets->str);
        printf ("DKD_PART_SIZES=%s\n", e_sizes->str);
        printf ("DKD_PART_LABELS=%s\n", e_labels->str);
        printf ("DKD_PART_UUIDS=%s\n", e_uuids->str);
        printf ("DKD_PART_FLAGS=%s\n", e_flags->str);
}

static void
entries_end (void)
{
        g_string_free (e_types, TRUE);
        g_string_free (e_offsets, TRUE);
        g_string_free (e_sizes, TRUE);
        g_string_free (e_labels, TRUE);
        g_string_free (e_uuids, TRUE);
        g_string_free (e_flags, TRUE);
        e_types = NULL;
        e_offsets = NULL;
        e_sizes = NULL;
        e_labels = NULL;
        e_uuids = NULL;
        e_flags = NULL;
}

static void
do_table (int fd)
{
        gint n;
        gint max_number;
        PartitionTable *table;
        PartitionTable *nested_table;
        int num_entries;
        int num_used_entries;
        int num_nested_entries;

        table = NULL;

        table = part_table_load_from_disk (fd);

        if (table == NULL) {
                fprintf (stderr, "unknown partition table type\n");
                goto out;
        }

        num_entries = part_table_get_num_entries (table);
        num_used_entries = num_entries;

        /* don't lie about number of entries */
        for (n = 0; n < num_entries; n++) {
                if (!part_table_entry_is_in_use (table, n)) {
                        num_used_entries--;
                        continue;
                }
        }

        /* we only support a single nested partition table */
        num_nested_entries = 0;
        nested_table = NULL;
        for (n = 0; n < num_entries; n++) {
                if (!part_table_entry_is_in_use (table, n))
                        continue;
                nested_table = part_table_entry_get_nested (table, n);
                if (nested_table != NULL) {
                        num_nested_entries = part_table_get_num_entries (nested_table);
                        break;
                }
        }

        printf ("DKD_PART_SCHEME=%s\n", part_get_scheme_name (part_table_get_scheme (table)));
        printf ("DKD_PART_COUNT=%d\n", num_used_entries + num_nested_entries);

        entries_begin ();

        max_number = 0;
        for (n = 0; n < num_entries; n++) {
                if (!part_table_entry_is_in_use (table, n))
                        continue;
                print_entry (table, n, n + 1);
                entries_add (table, n, n + 1);
                if (n + 1 >= max_number)
                         max_number = n + 1;
        }

        for (n = 0; n < num_nested_entries; n++) {
                print_entry (nested_table, n, n + 5);
                entries_add (table, n, n + 1);
                if (n + 5 >= max_number)
                         max_number = n + 5;
        }

        //entries_print ();
        entries_end ();

        printf ("DKD_PART_MAX_NUMBER=%d\n", max_number);
out:
        if (table != NULL)
                part_table_free (table);
}

static gboolean
sysfs_file_exists (const char *dir, const char *attribute)
{
        gboolean result;
        char *filename;

        result = FALSE;
        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
                result = TRUE;
        }
        g_free (filename);

        return result;
}

static char *
sysfs_get_string (const char *dir, const char *attribute)
{
        char *contents;
        char *filename;

        contents = NULL;
        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_get_contents (filename, &contents, NULL, NULL)) {
                ;
        }
        g_free (filename);

        return contents;
}

static char *
get_devpath (const char *device)
{
        char *s;
        char *result;

        s = getenv ("DEVPATH");
        if (s == NULL) {
                GError *error;
                char *command_line;
                char *standard_output;
                int exit_status;

                /* ok, so this means we're not invoked from udev; do
                 * some lipservice, because we're such nice and
                 * gullibe guys..  we run udevadm to determine the
                 * devpath
                 */
                error = NULL;
                command_line = g_strdup_printf ("udevadm info -q path --name %s", device);
                if (!g_spawn_command_line_sync (command_line,
                                                &standard_output,
                                                NULL,
                                                &exit_status,
                                                &error)) {
                        fprintf (stderr, "error running udevadm to determine node: %s", error->message);
                        g_error_free (error);
                        g_free (command_line);
                        goto out;
                }
                g_free (command_line);

                if (WEXITSTATUS (exit_status) != 0) {
                        fprintf (stderr, "udevinfo returned %d\n", WEXITSTATUS (exit_status));
                        g_free (standard_output);
                        goto out;
                }
                g_strchomp (standard_output);
                result = g_build_filename ("/sys", standard_output, NULL);
                g_free (standard_output);

                goto out;
        }

        result = g_build_filename ("/sys", s, NULL);
out:
        return result;
}

static int
open_device (const char *given_device_file, gboolean *is_part)
{
        int fd;
        const char *devpath;
        char *device_file;
        char *s;
        char *dev;
        int major;
        int minor;
        char *node_name;
        dev_t node;
        int n;

        fd = -1;
        device_file = NULL;
        *is_part = FALSE;

        /* if we're called for a partition by udev, scan the parent */
        devpath = get_devpath (given_device_file);
        if (devpath == NULL)
                goto not_part;

        if (!sysfs_file_exists (devpath, "start"))
                goto not_part;

        /* we're a partition */

        *is_part = TRUE;

        s = g_strdup (devpath);
        for (n = strlen (s) - 1; n >= 0 && s[n] != '/'; n--)
                s[n] = '\0';
        s[n] = '\0';

        dev = sysfs_get_string (s, "dev");
        g_free (s);

        if (dev == NULL) {
                fprintf (stderr, "couldn't determine dev for enclosing device\n");
                goto out;
        }

        if (sscanf (dev, "%d:%d", &major, &minor) != 2) {
                fprintf (stderr, "major:minor is malformed\n");
                goto out;
        }
        g_free (dev);

        node_name = g_strdup_printf ("/dev/.tmp-part-id-%d", getpid ());
        node = makedev (major, minor);
        if (mknod (node_name, 0400 | S_IFBLK, node) != 0) {
                fprintf (stderr, "mknod failed: %m\n");
                g_free (node_name);
                goto out;
        }

        fd = open (node_name, O_RDONLY);
        if (unlink (node_name) != 0) {
                fprintf (stderr, "unlink failed: %m\n");
                g_free (node_name);
                goto out;
        }
        g_free (node_name);


out:
        if (fd < 0) {
                printf ("DKD_MEDIA_AVAILABLE=0\n");
        } else {
                printf ("DKD_MEDIA_AVAILABLE=1\n");
        }
        return fd;

not_part:
        fd = open (given_device_file, O_RDONLY);
        if (fd < 0) {
                printf ("DKD_MEDIA_AVAILABLE=0\n");
        } else {
                printf ("DKD_MEDIA_AVAILABLE=1\n");
        }
        return fd;
}
#endif

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
                guint64 offset;
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
                offset = part_table_entry_get_offset (partition_table_for_entry, entry_num);
                size = part_table_entry_get_size (partition_table_for_entry, entry_num);

                flags_combined = g_strjoinv (" ", flags);

                g_print ("DKD_PARTITION=1\n");
                g_print ("DKD_PARTITION_SCHEME=%s\n",
                         part_get_scheme_name (part_table_get_scheme (partition_table_for_entry)));
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

