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
        int ret;
        int num_passes;
        const char *device;

        ret = 1;

        if (argc != 2) {
                g_printerr ("wrong usage\n");
                goto out;
        }
        device = argv[1];

        /* TODO: parse options etc. */
        num_passes = 1;
        if (!task_zero_device (device, num_passes, 0, 2))
                goto out;

        ret = 0;

out:
        return ret;
}
