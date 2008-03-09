
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "partutil.h"

static void
usage (int argc, char *argv[])
{
        execlp ("man", "man", "part_id", NULL);
        fprintf (stderr, "Cannot show man page: %m\n");
        exit (1);
}

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

        printf ("PART_P%d_TYPE=%s\n", print_number, type != NULL ? type : "");
        printf ("PART_P%d_OFFSET=%lld\n", print_number, offset);
        printf ("PART_P%d_SIZE=%lld\n", print_number, size);
        printf ("PART_P%d_LABEL=%s\n", print_number, label != NULL ? label : "");
        printf ("PART_P%d_UUID=%s\n", print_number, uuid != NULL ? uuid : "");
        printf ("PART_P%d_FLAGS=%s\n", print_number, flags_combined);

        g_free (type);
        g_free (label);
        g_free (uuid);
        g_strfreev (flags);
        g_free (flags_combined);
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
                 * some lipservice, because we're such nice guys, and
                 * run udevinfo to determine the devpath
                 */
                error = NULL;
                command_line = g_strdup_printf ("udevinfo -q path --name %s", device);
                if (!g_spawn_command_line_sync (command_line,
                                                &standard_output,
                                                NULL,
                                                &exit_status,
                                                &error)) {
                        fprintf (stderr, "couldn't run udevinfo to determine node: %s", error->message);
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

static guint64
sysfs_get_uint64 (const char *dir, const char *attribute)
{
        guint64 result;
        char *contents;
        char *filename;

        result = 0;
        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_get_contents (filename, &contents, NULL, NULL)) {
                result = atoll (contents);
                g_free (contents);
        }
        g_free (filename);

        return result;
}

static void
do_entry (const char *device_file)
{
        int n;
        int fd;
        PartitionTable *table;
        char *devpath;
        guint64 entry_start;
        char *devpath_parent;
        char *dev;
        int major;
        int minor;
        char *node_name;
        dev_t node;
        PartitionTable *out_table;
        int out_entry;
        int part_num;
        char *type;
        char *label;
        char *uuid;
        char **flags;
        char *flags_combined;
        guint64 offset;
        guint64 size;

        table = NULL;
        node_name = NULL;
        dev = NULL;
        devpath_parent = NULL;
        devpath = NULL;

        devpath = get_devpath (device_file);
        if (devpath == NULL) {
                fprintf (stderr, "couldn't find devpath\n");
                goto out;
        }

        entry_start = sysfs_get_uint64 (devpath, "start");
        if (entry_start == 0) {

                /* TODO: handle kpartx'ed partitions */

                fprintf (stderr, "couldn't determine where partition starts\n");
                goto out;
        } else {
                /* we're a partition created by the kernel; easy to find the parent */
                devpath_parent = g_strdup (devpath);
                for (n = strlen (devpath_parent); devpath_parent[n] != '/' && n >= 1; n--)
                        devpath_parent[n] = '\0';
                devpath_parent[n] = '\0';

                /* TODO: use the correct block size */
                entry_start *= 512;
        }

        dev = sysfs_get_string (devpath_parent, "dev");
        if (dev == NULL) {
                fprintf (stderr, "couldn't determine dev for enclosing device\n");
                goto out;
        }

        if (sscanf (dev, "%d:%d", &major, &minor) != 2) {
                fprintf (stderr, "major:minor is malformed\n");
                goto out;
        }

        node_name = g_strdup_printf ("/dev/.tmp-part-id-%d", getpid ());
        node = makedev (major, minor);
        if (mknod (node_name, 0400 | S_IFBLK, node) != 0) {
                fprintf (stderr, "mknod failed: %m\n");
                goto out;
        }

        fd = open (node_name, O_RDONLY);
        if (unlink (node_name) != 0) {
                fprintf (stderr, "unlink failed: %m\n");
                goto out;
        }
        if (fd < 0) {
                fprintf (stderr, "open failed: %m\n");
                goto out;
        }

        table = part_table_load_from_disk (fd);
        close (fd);

        if (table == NULL) {
                fprintf (stderr, "%s: unknown partition table type\n", device_file);
                goto out;
        }


        part_table_find (table, entry_start, &out_table, &out_entry);
        if (out_entry == -1) {
                fprintf (stderr, "couldn't find partition\n");
                goto out;
        }

        if (out_table == table)
                part_num = out_entry + 1;
        else
                part_num = out_entry + 5;

        type = part_table_entry_get_type (out_table, out_entry);
        label = part_table_entry_get_label (out_table, out_entry);
        uuid = part_table_entry_get_uuid (out_table, out_entry);
        flags = part_table_entry_get_flags (out_table, out_entry);
        offset = part_table_entry_get_offset (out_table, out_entry);
        size = part_table_entry_get_size (out_table, out_entry);
        flags_combined = g_strjoinv (" ", flags);

        printf ("PART_ENTRY_SCHEME=%s\n", part_get_scheme_name (part_table_get_scheme (table)));
        printf ("PART_ENTRY_NUMBER=%d\n", part_num);
        printf ("PART_ENTRY_TYPE=%s\n", type != NULL ? type : "");
        printf ("PART_ENTRY_OFFSET=%lld\n", offset);
        printf ("PART_ENTRY_SIZE=%lld\n", size);
        printf ("PART_ENTRY_LABEL=%s\n", label != NULL ? label : "");
        printf ("PART_ENTRY_UUID=%s\n", uuid != NULL ? uuid : "");
        printf ("PART_ENTRY_FLAGS=%s\n", flags_combined);
        printf ("PART_ENTRY_SLAVE=%s\n", devpath_parent + sizeof ("/sys") -1);

        g_free (type);
        g_free (label);
        g_free (uuid);
        g_strfreev (flags);
        g_free (flags_combined);
out:
        if (table != NULL)
                part_table_free (table);
        g_free (node_name);
        g_free (dev);
        g_free (devpath_parent);
        g_free (devpath);
}

static void
do_table (const char *device_file)
{
        int n;
        int fd;
        PartitionTable *table;
        PartitionTable *nested_table;
        int num_entries;
        int num_used_entries;
        int num_nested_entries;

        table = NULL;

        fd = open (device_file, O_RDONLY);
        if (fd < 0) {
                printf ("MEDIA_AVAILABLE=0\n");
                goto out;
        }
        printf ("MEDIA_AVAILABLE=1\n");
        table = part_table_load_from_disk (fd);
        close (fd);

        if (table == NULL) {
                fprintf (stderr, "%s: unknown partition table type\n", device_file);
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

        printf ("PART_SCHEME=%s\n", part_get_scheme_name (part_table_get_scheme (table)));
        printf ("PART_COUNT=%d\n", num_used_entries + num_nested_entries);

        for (n = 0; n < num_entries; n++) {
                if (!part_table_entry_is_in_use (table, n))
                        continue;
                print_entry (table, n, n + 1);
        }

        for (n = 0; n < num_nested_entries; n++) {
                print_entry (nested_table, n, n + 5);
        }
out:
        if (table != NULL)
                part_table_free (table);
}

int
main (int argc, char *argv[])
{
        int n;
        char *device_file;
        gboolean opt_entry;

        device_file = NULL;
        opt_entry = FALSE;
        for (n = 1; n < argc; n++) {
                if (strcmp (argv[n], "--help") == 0) {
                        usage (argc, argv);
                        return 0;
                } else if (strcmp (argv[n], "--entry") == 0) {
                        opt_entry = TRUE;
                } else {
                        if (device_file != NULL)
                                usage (argc, argv);
                        device_file = argv[n];
                }
	}

        if (device_file == NULL) {
                fprintf (stderr, "no device\n");
                goto out;
        }

        if (opt_entry) {
                do_entry (device_file);
        } else {
                do_table (device_file);
        }

out:
        return 0;
}

