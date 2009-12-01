/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2009 David Zeuthen <david@fubar.dk>
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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

/* yeah, this is a bit cheesy with the spawning and the base64 encoding/decoding... once
 * smp_utils ships a library we'll just use that instead
 */

#include <glib.h>

static guchar *
do_smp_command (const gchar *smp_command_line,
                gsize *out_smp_response_len)
{
  guchar *ret;
  gchar *command_line;
  gchar *prog_stdout;
  gint prog_exit_status;
  GError *error;

  ret = NULL;
  error = NULL;

  command_line = g_strdup_printf ("sh -c '%s |base64 -w0'", smp_command_line);
  if (!g_spawn_command_line_sync (command_line, &prog_stdout, NULL, &prog_exit_status, &error))
    {
      g_printerr ("Error spawning `%s': %s", command_line, error->message);
      g_error_free (error);
      goto out;
    }

  if (!WIFEXITED (prog_exit_status))
    goto out;

  if (WEXITSTATUS (prog_exit_status) != 0)
    goto out;

  if (prog_stdout == NULL || strlen (prog_stdout) == 0)
    goto out;

  ret = g_base64_decode (prog_stdout, out_smp_response_len);
  if (ret == NULL)
    {
      g_printerr ("Error decoding SMP response\n");
      goto out;
    }

 out:
  g_free (command_line);
  g_free (prog_stdout);
  return ret;
}

gint
main (int argc,
      char *argv[])
{
  gint ret;
  gchar *basename;
  const gchar *sysfs_path;
  gchar *bsg_name;
  guchar *smp_response;
  gsize smp_response_len;
  gchar *s;

  ret = 1;
  basename = NULL;
  bsg_name = NULL;
  smp_response = NULL;

  if (argc != 2)
    {
      g_printerr ("Usage: %s <devpath>\n", argv[0]);
      goto out;
    }
  sysfs_path = argv[1];

  basename = g_path_get_basename (sysfs_path);
  bsg_name = g_strdup_printf ("/dev/bsg/%s", basename);

  s = g_strdup_printf ("smp_rep_manufacturer -r %s", bsg_name);
  smp_response = do_smp_command (s, &smp_response_len);
  g_free (s);
  if (smp_response == NULL)
    goto out;

  gchar *vendor;
  gchar *model;
  gchar *revision;

  /* 9.4.3.5 REPORT MANUFACTURER INFORMATION function:
   *
   * VENDOR IDENTIFICATION is 8 bytes of ASCII from bytes 12 through 19
   * PRODUCT IDENTIFICATION is 16 bytes of ASCII from bytes 20 through 35
   * PRODUCT REVISION LEVEL is 4 bytes of ASCII from bytes 36 through 39
   */
  vendor = g_strndup ((const gchar *) smp_response + 12, 8);
  model = g_strndup ((const gchar *) smp_response + 20, 16);
  revision = g_strndup ((const gchar *) smp_response + 36, 4);

  g_print ("ID_VENDOR=%s\n", vendor);
  g_print ("ID_MODEL=%s\n", model);
  g_print ("ID_REVISION=%s\n", revision);

  ret = 0;

 out:
  g_free (basename);
  g_free (bsg_name);
  g_free (smp_response);
  return ret;
}
