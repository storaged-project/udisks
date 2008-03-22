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

static gboolean
validate_and_escape_label (char **label, int max_len)
{
        int n;
        gboolean ret;
        GString *s;

        ret = FALSE;

        if ((int) strlen (*label) > max_len) {
                g_printerr ("given file system label exceeds %d characters\n", max_len);
                goto out;
        }

        /* escape '"' */
        s = g_string_new (*label);
        for (n = 0; n < (int) s->len; n++) {
                if (s->str[n] == '"') {
                        g_string_insert_c (s, n, '\\');
                        n++;
                }
        }
        g_free (*label);
        *label = g_string_free (s, FALSE);

        ret = TRUE;
out:
        return ret;
}


int
main (int argc, char **argv)
{
        int ret;
        int exit_status;
        GError *error;
        const char *fstype;
        const char *device;
        char *command_line;
        char *standard_error;
        char **options;
        GString *s;
        char *label;
        char *erase;
        int num_erase_passes;
        int n;

        ret = 1;
        command_line = NULL;
        standard_error = NULL;
        erase = NULL;

        if (argc < 3) {
                g_printerr ("wrong usage\n");
                goto out;
        }
        fstype = argv[1];
        device = argv[2];
        options = argv + 3;

        /* TODO: clean out metadata in start and beginning */

        if (strcmp (fstype, "vfat") == 0) {

                s = g_string_new ("mkfs.vfat");
                for (n = 0; options[n] != NULL; n++) {
                        if (g_str_has_prefix (options[n], "label=")) {
                                label = strdup (options[n] + sizeof ("label=") - 1);
                                if (!validate_and_escape_label (&label, 11)) {
                                        g_string_free (s, TRUE);
                                        goto out;
                                }
                                g_string_append_printf (s, " -n \"%s\"", label);
                                g_free (label);
                        } else if (g_str_has_prefix (options[n], "erase=")) {
                                erase = strdup (options[n] + sizeof ("erase=") - 1);
                        } else {
                                g_printerr ("option %s not supported\n", options[n]);
                                goto out;
                        }
                }
                g_string_append_printf (s, " %s", device);
                command_line = g_string_free (s, FALSE);

        } else if (strcmp (fstype, "ext3") == 0) {

                s = g_string_new ("mkfs.ext3");
                for (n = 0; options[n] != NULL; n++) {
                        if (g_str_has_prefix (options[n], "label=")) {
                                label = strdup (options[n] + sizeof ("label=") - 1);
                                if (!validate_and_escape_label (&label, 16)) {
                                        g_string_free (s, TRUE);
                                        goto out;
                                }
                                g_string_append_printf (s, " -L \"%s\"", label);
                                g_free (label);
                        } else if (g_str_has_prefix (options[n], "erase=")) {
                                erase = strdup (options[n] + sizeof ("erase=") - 1);
                        } else {
                                g_printerr ("option %s not supported\n", options[n]);
                                goto out;
                        }
                }
                g_string_append_printf (s, " %s", device);
                command_line = g_string_free (s, FALSE);

        } else if (strcmp (fstype, "ntfs") == 0) {

                /* skip zeroing (we do that ourselves) and bad sector checking (will 
                 * eventually be handled on a higher level)
                 */
                s = g_string_new ("mkntfs -f");
                for (n = 0; options[n] != NULL; n++) {
                        if (g_str_has_prefix (options[n], "label=")) {
                                label = strdup (options[n] + sizeof ("label=") - 1);
                                if (!validate_and_escape_label (&label, 255)) {
                                        g_string_free (s, TRUE);
                                        goto out;
                                }
                                g_string_append_printf (s, " -L \"%s\"", label);
                                g_free (label);
                        } else if (g_str_has_prefix (options[n], "erase=")) {
                                erase = strdup (options[n] + sizeof ("erase=") - 1);
                        } else {
                                g_printerr ("option %s not supported\n", options[n]);
                                goto out;
                        }
                }
                g_string_append_printf (s, " %s", device);
                command_line = g_string_free (s, FALSE);

        } else if (strcmp (fstype, "swap") == 0) {

                s = g_string_new ("mkswap");
                for (n = 0; options[n] != NULL; n++) {
                        if (g_str_has_prefix (options[n], "erase=")) {
                                erase = strdup (options[n] + sizeof ("erase=") - 1);
                        } else {
                                g_printerr ("option %s not supported\n", options[n]);
                                goto out;
                        }
                }
                g_string_append_printf (s, " %s", device);
                command_line = g_string_free (s, FALSE);

        } else if (strcmp (fstype, "empty") == 0) {
                command_line = NULL;
                for (n = 0; options[n] != NULL; n++) {
                        if (g_str_has_prefix (options[n], "erase=")) {
                                erase = strdup (options[n] + sizeof ("erase=") - 1);
                        } else {
                                g_printerr ("option %s not supported\n", options[n]);
                                goto out;
                        }
                }
        } else {
                g_printerr ("fstype %s not supported\n", fstype);
                goto out;
        }

        /* TODO: do erase */
        num_erase_passes = task_zero_device_parse_option (erase);
        if (num_erase_passes == -1) {
                g_printerr ("invalid erase=%s option\n", erase);
                goto out;
        }
        if (!task_zero_device (device, 0, 0, num_erase_passes, 0, num_erase_passes + 2))
                goto out;

        g_print ("progress: %d %d -1 mkfs\n", num_erase_passes + 1, num_erase_passes + 2);

        if (command_line != NULL) {
                if (!g_spawn_command_line_sync (command_line,
                                                NULL,
                                                &standard_error,
                                                &exit_status,
                                                &error)) {
                        g_printerr ("cannot spawn '%s'\n", command_line);
                        goto out;
                }
                if (WEXITSTATUS (exit_status) != 0) {
                        g_printerr ("helper failed with:\n%s", standard_error);
                        goto out;
                }
        }

        ret = 0;

out:
        g_free (standard_error);
        g_free (command_line);
        g_free (erase);
        return ret;
}
