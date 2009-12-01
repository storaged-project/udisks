/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
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

int
main (int argc,
      char **argv)
{
  int ret;
  int exit_status;
  GError *error;
  char *command_line;
  char *standard_error;
  char *device;
  char *slave;
  char *erase;
  char **options;
  int n;

  ret = 1;
  command_line = NULL;
  standard_error = NULL;
  erase = NULL;

  if (argc < 3)
    {
      g_printerr ("wrong usage\n");
      goto out;
    }
  device = argv[1];
  slave = argv[2];
  options = argv + 3;

  for (n = 0; options[n] != NULL; n++)
    {
      if (g_str_has_prefix (options[n], "erase="))
        {
          erase = strdup (options[n] + sizeof("erase=") - 1);
        }
      else
        {
          g_printerr ("option %s not supported\n", options[n]);
          goto out;
        }
    }

  g_print ("device   = '%s'\n", device);
  g_print ("slave    = '%s'\n", slave);
  g_print ("erase    = '%s'\n", erase);

  /* first we fail it... */
  command_line = g_strdup_printf ("mdadm --manage %s --fail %s", device, slave);

  error = NULL;
  if (!g_spawn_command_line_sync (command_line, NULL, &standard_error, &exit_status, &error))
    {
      g_printerr ("cannot spawn '%s': %s\n", command_line, error->message);
      g_error_free (error);
      goto out;
    }
  if (WEXITSTATUS (exit_status) != 0)
    {
      g_printerr ("'%s' failed with: '%s'\n", command_line, standard_error);
      goto out;
    }
  g_free (standard_error);
  g_free (command_line);
  standard_error = NULL;
  command_line = NULL;

  /* TODO: Seems like a kernel bug that you can't immediately remove a component
   *       that you just failed. For now sleeping seems to work.
   */
  sleep (1);

  /* ...then we remove it */
  command_line = g_strdup_printf ("mdadm --manage %s --remove %s", device, slave);

  error = NULL;
  if (!g_spawn_command_line_sync (command_line, NULL, &standard_error, &exit_status, &error))
    {
      g_printerr ("cannot spawn '%s': %s\n", command_line, error->message);
      g_error_free (error);
      goto out;
    }
  if (WEXITSTATUS (exit_status) != 0)
    {
      g_printerr ("'%s' failed with: '%s'\n", command_line, standard_error);
      goto out;
    }
  g_free (standard_error);
  g_free (command_line);
  standard_error = NULL;
  command_line = NULL;

  ret = 0;

 out:
  g_free (standard_error);
  g_free (command_line);
  g_free (erase);
  return ret;
}
