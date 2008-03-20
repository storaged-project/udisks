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
        char **options;
        char *erase;
        int num_erase_passes;
        int n;
        guint64 offset;
        char *endp;

        ret = 1;
        erase = NULL;


        if (argc < 3) {
                g_printerr ("wrong usage\n");
                goto out;
        }
        device = argv[1];
        offset = strtoll (argv[2], &endp, 10);
        if (*endp != '\0') {
                g_printerr ("malformed offset '%s'\n", argv[2]);
                goto out;
        }
        options = argv + 3;

        for (n = 0; options[n] != NULL; n++) {
                if (g_str_has_prefix (options[n], "erase=")) {
                        erase = strdup (options[n] + sizeof ("erase=") - 1);
                } else {
                        g_printerr ("option %s not supported\n", options[n]);
                        goto out;
                }
        }

        num_erase_passes = -1;
#if 0
        /* TODO: zero the _partition_, not the disk */
        num_erase_passes = task_zero_device_parse_option (erase);
        if (num_erase_passes == -1) {
                g_printerr ("invalid erase=%s option\n", erase);
                goto out;
        }
        if (!task_zero_device (device, num_erase_passes, 0, num_erase_passes + 2))
                goto out;
#endif

        g_print ("progress: %d %d -1 partitioning\n", num_erase_passes + 1, num_erase_passes + 2);

        if (part_del_partition ((char *) device, offset))
                ret = 0;

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
