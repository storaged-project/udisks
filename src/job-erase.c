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

#include "job-shared.h"

int
main (int argc, char **argv)
{
        int n;
        int ret;
        char *erase;
        int num_erase_passes;
        char **options;
        const char *device;

        ret = 1;
        erase = NULL;

        if (argc < 2) {
                g_printerr ("wrong usage\n");
                goto out;
        }
        device = argv[1];

        options = argv + 2;

        for (n = 0; options[n] != NULL; n++) {
                if (g_str_has_prefix (options[n], "erase=")) {
                        erase = strdup (options[n] + sizeof ("erase=") - 1);
                } else {
                        g_printerr ("option %s not supported\n", options[n]);
                        goto out;
                }
        }


        num_erase_passes = task_zero_device_parse_option (erase);
        if (num_erase_passes == -1) {
                g_printerr ("invalid erase=%s option\n", erase);
                goto out;
        }

        if (!task_zero_device (device, 0, 0, num_erase_passes, 0, num_erase_passes + 1))
                goto out;

        ret = 0;

out:
        g_free (erase);
        return ret;
}
