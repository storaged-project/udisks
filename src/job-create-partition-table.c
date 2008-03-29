/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <glib.h>

#include "job-shared.h"
#include "partutil.h"

int
main (int argc, char **argv)
{
        int fd;
        int ret;
        const char *device;
        const char *scheme;
        char **options;
        char *erase;
        int num_erase_passes;
        int n;
        PartitionScheme pscheme;
        gboolean no_partition_table;

        ret = 1;
        erase = NULL;
        no_partition_table = FALSE;
        pscheme = PART_TYPE_MSDOS;

        if (argc < 3) {
                g_printerr ("wrong usage\n");
                goto out;
        }
        device = argv[1];
        scheme = argv[2];
        options = argv + 3;

        for (n = 0; options[n] != NULL; n++) {
                if (g_str_has_prefix (options[n], "erase=")) {
                        erase = strdup (options[n] + sizeof ("erase=") - 1);
                } else {
                        g_printerr ("option %s not supported\n", options[n]);
                        goto out;
                }
        }

        if (strcmp (scheme, "mbr") == 0) {
                pscheme = PART_TYPE_MSDOS;
        } else if (strcmp (scheme, "gpt") == 0) {
                pscheme = PART_TYPE_GPT;
        } else if (strcmp (scheme, "apm") == 0) {
                pscheme = PART_TYPE_APPLE;
        } else if (strcmp (scheme, "none") == 0) {
                no_partition_table = TRUE;
        } else {
                g_printerr ("partitioning scheme %s not supported\n", scheme);
                goto out;
        }

        num_erase_passes = -1;

        num_erase_passes = task_zero_device_parse_option (erase);
        if (num_erase_passes == -1) {
                g_printerr ("invalid erase=%s option\n", erase);
                goto out;
        }
        if (!task_zero_device (device, 0, 0, num_erase_passes, 0, num_erase_passes + 2))
                goto out;

        g_print ("progress: %d %d -1 partitioning\n", num_erase_passes + 1, num_erase_passes + 2);

        if (no_partition_table) {
                ret = 0;
        } else {
                if (part_create_partition_table ((char *) device, pscheme))
                        ret = 0;
        }

        /* either way, we've got this far.. signal the kernel to reread the partition table */
        fd = open (device, O_RDONLY);
        if (fd < 0) {
                g_printerr ("cannot open %s (for BLKRRPART): %m\n", device);
                ret = 1;
                goto out;
        }
        if (ioctl (fd, BLKRRPART) != 0) {
                close (fd);
                g_printerr ("BLKRRPART ioctl failed for %s: %m\n", device);
                ret = 1;
                goto out;
        }
        close (fd);

out:
        g_free (erase);
        return ret;
}
