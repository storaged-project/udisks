
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

        printf ("ID_PART_P%d_TYPE=%s\n", print_number, type != NULL ? type : "");
        printf ("ID_PART_P%d_OFFSET=%lld\n", print_number, offset);
        printf ("ID_PART_P%d_SIZE=%lld\n", print_number, size);
        printf ("ID_PART_P%d_LABEL=%s\n", print_number, label != NULL ? label : "");
        printf ("ID_PART_P%d_UUID=%s\n", print_number, uuid != NULL ? uuid : "");
        printf ("ID_PART_P%d_FLAGS=%s\n", print_number, flags_combined);

        g_free (type);
        g_free (label);
        g_free (uuid);
        g_strfreev (flags);
        g_free (flags_combined);
}

int
main (int argc, char *argv[])
{
        int n;
        int ret;
        char *device_file;
        PartitionTable *table;
        PartitionTable *nested_table;
        int num_entries;
        int num_nested_entries;

        ret = 1;

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

        table = part_table_load_from_disk (device_file);
        if (table == NULL) {
                fprintf (stderr, "%s: unknown partition table type\n", device_file);
                goto out;
        }

        num_entries = part_table_get_num_entries (table);

        /* we only support a single nested partition table */
        num_nested_entries = 0;
        for (n = 0; n < num_entries; n++) {
                nested_table = part_table_entry_get_nested (table, n);
                if (nested_table != NULL) {
                        num_nested_entries = part_table_get_num_entries (nested_table);
                        break;
                }
        }


        printf ("ID_PART_SCHEME=%s\n", part_get_scheme_name (part_table_get_scheme (table)));
        printf ("ID_PART_COUNT=%d\n", num_entries + num_nested_entries);

        for (n = 0; n < num_entries; n++) {
                print_entry (table, n, n + 1);
        }

        for (n = 0; n < num_nested_entries; n++) {
                print_entry (nested_table, n, n + 5);
        }

        ret = 0;

out:
        return ret;
}

