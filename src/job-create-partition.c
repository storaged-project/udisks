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
        int n;
        int fd;
        int ret;
        const char *device;
        char **options;
        char *label;
        char *type;
        char *flags_as_string;
        char **flags;
        guint64 offset;
        guint64 size;
        guint64 out_start;
        guint64 out_size;
        char *endp;

        ret = 1;
        flags = NULL;

        if (argc < 7) {
                g_printerr ("wrong usage\n");
                goto out;
        }
        device = argv[1];
        offset = strtoll (argv[2], &endp, 10);
        if (*endp != '\0') {
                g_printerr ("malformed offset '%s'\n", argv[2]);
                goto out;
        }
        size = strtoll (argv[3], &endp, 10);
        if (*endp != '\0') {
                g_printerr ("malformed size '%s'\n", argv[3]);
                goto out;
        }
        type = argv[4];
        label = argv[5];
        flags_as_string = argv[6];
        options = argv + 7;

        flags = g_strsplit (flags_as_string, ",", 0);

        /* we trust the caller have verified that the given slice doesn't overlap
         * with existing partitions
         */

        g_print ("type:            '%s'\n", type);
        g_print ("label:           '%s'\n", label);
        g_print ("flags_as_string: '%s'\n", flags_as_string);

        g_print ("progress: %d %d -1 partitioning\n", 0, 2);

        if (part_add_partition ((char *) device,
                                offset,
                                size,
                                &out_start,
                                &out_size,
                                type,
                                strlen (label) > 0 ? label : NULL,
                                flags,
                                -1,
                                -1,
                                FALSE)) {
                /* now clear out file system signatures in the newly created
                 * partition... Unless it's an extended partition!
                 */
                n = strtoll (type, &endp, 0);
                if (*endp == '\0' && (n == 0x05 || n == 0x0f || n == 0x85)) {
                        ret = 0;
                } else {
                        if (!zero_signatures (device, out_start, out_size, 1, 2)) {
                                g_printerr ("Cannot wipe file system signatures @ offset=%lld and size=%lld\n",
                                            out_start, out_size);
                        } else {
                                ret = 0;
                        }
                }

                /* send the start and size back to the daemon - it needs to know, to
                 * wait for the created partition, because the partition may not have
                 * been created exactly where it was requested....
                 */
                g_printerr ("job-create-partition-offset: %lld\n", out_start);
                g_printerr ("job-create-partition-size: %lld\n", out_size);
        }

        /* TODO: replace blkrrpart with partx-ish ioctls */

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
        g_strfreev (flags);
        return ret;
}
