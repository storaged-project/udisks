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
#include <signal.h>

#include <glib.h>

#include "job-shared.h"

static const char *device;
static gboolean cancelled = FALSE;

static void
sigterm_handler (int signum)
{
        cancelled = TRUE;
}

static gboolean
abort_test (void)
{
        gboolean ret;
        int exit_status;
        char *standard_error;
        char *command_line;
        GError *error;

        ret = FALSE;

        command_line = g_strdup_printf ("smartctl -X %s", device);

        error = NULL;
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
        g_free (standard_error);

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
        char *command_line;
        char *standard_output;
        char *standard_error;
        const char *test;
        gboolean captive;
        const char *result;

        ret = 1;
        command_line = NULL;
        standard_error = NULL;
        result = NULL;

        if (argc != 4) {
                g_printerr ("wrong usage\n");
                goto out;
        }
        device = argv[1];
        test = argv[2];
        captive = (strcmp (argv[3], "1") == 0);

        g_print ("device   = '%s'\n", device);
        g_print ("test     = '%s'\n", test);
        g_print ("captive  = %d\n", captive);

        command_line = g_strdup_printf ("smartctl -t %s %s %s",
                                        test,
                                        captive ? "-C" : "",
                                        device);

        error = NULL;
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
        g_free (standard_error);
        standard_error = NULL;

        signal (SIGTERM, sigterm_handler);

        /* ok, now poll every five secs via 'smartctl -c' until the test is done */

        g_free (command_line);
        command_line = g_strdup_printf ("smartctl -c %s", device);


        /* progress at 0% initially */
        g_print ("progress: 0 1 0 smartselftest\n");

        while (TRUE) {
                int exec_status = -1;
                int percentage_done;

                sleep (5);

                if (cancelled) {
                        g_printerr ("Abort test and exiting since we caught SIGTERM\n");
                        abort_test ();
                        goto out;
                }

                error = NULL;
                if (!g_spawn_command_line_sync (command_line,
                                                &standard_output,
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
                g_free (standard_error);
                standard_error = NULL;

                int n;
                char **lines;

                lines = g_strsplit (standard_output, "\n", 0);
                for (n = 0; lines[n] != NULL; n++) {
                        const char *line = (const char *) lines[n];
                        if (g_str_has_prefix (line, "Self-test execution status:")) {
                                int m;

                                for (m = 0; line[m] != '\0'; m++) {
                                        if (g_ascii_isdigit (line[m]))
                                                break;
                                }
                                if (line[m] != '\0') {
                                        char *endp;

                                        exec_status = strtol (line + m, &endp, 10);
                                        if (*endp == ')') {
                                                /* good */
                                        } else {
                                                exec_status = -1;
                                        }
                                }
                        }
                }
                g_strfreev (lines);

                /* didn't manage to parse output */
                if (exec_status == -1) {
                        g_printerr ("Unexpected output polling drive for selftest completion\n");
                        abort_test ();
                        goto out;
                }

                /* see ataprint.cpp:ataPrintSelectiveSelfTestLog() in smartmontools */

                if ((exec_status >> 4) == 15) {
                        percentage_done = 100 - (exec_status & 0x0f) * 10;
                        g_print ("progress: 0 1 %d smartselftest\n", percentage_done);
                } else {
                        switch ((exec_status)>>4){
                        case  0:
                                result = "Completed";
                                break;
                        case  1:
                                result = "Aborted_by_host";
                                break;
                        case  2:
                                result = "Interrupted";
                                break;
                        case  3:
                                result = "Fatal_error";
                                break;
                        case  4:
                                result = "Completed_unknown_failure";
                                break;
                        case  5:
                                result = "Completed_electrical_failure";
                                break;
                        case  6:
                                result = "Completed_servo/seek_failure";
                                break;
                        case  7:
                                result = "Completed_read_failure";
                                break;
                        case  8:
                                result = "Completed_handling_damage??";
                                break;
                        default:
                                g_printerr ("Unexpected status %d polling drive for selftest completion\n",
                                            exec_status);
                                abort_test ();
                                goto out;
                        }
                        goto test_complete;
                }
        }

test_complete:
        /* send the result of the test back to the daemon */
        /* g_printerr ("job-smart-selftest: %s\n", result); */
        ret = 0;

out:
        g_free (standard_error);
        g_free (command_line);
        return ret;
}
