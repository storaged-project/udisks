
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

static void
do_table (int fd)
{
        int n;
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
                 * gullibe guys..  we run udevinfo to determine the
                 * devpath
                 */
                error = NULL;
                command_line = g_strdup_printf ("udevinfo -q path --name %s",device);
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

static int
open_device (const char *given_device_file)
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

        /* if we're called for a partition by udev, scan the parent */
        devpath = get_devpath (given_device_file);
        if (devpath == NULL)
                goto not_part;

        if (!sysfs_file_exists (devpath, "start"))
                goto not_part;

        /* we're a partition */

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
                printf ("MEDIA_AVAILABLE=0\n");
        } else {
                printf ("MEDIA_AVAILABLE=1\n");
        }
        return fd;

not_part:
        fd = open (given_device_file, O_RDONLY);
        if (fd < 0) {
                printf ("MEDIA_AVAILABLE=0\n");
        } else {
                printf ("MEDIA_AVAILABLE=1\n");
        }
        return fd;
}

int
main (int argc, char *argv[])
{
        int n;
        int fd;
        char *device_file;

        device_file = NULL;
        for (n = 1; n < argc; n++) {
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
                fprintf (stderr, "no device\n");
                goto out;
        }

        fd = open_device (device_file);
        if (fd >= 0) {
                if (getenv ("ID_FS_USAGE") == NULL) {
                        do_table (fd);
                }
                close (fd);
        }

out:
        return 0;
}

