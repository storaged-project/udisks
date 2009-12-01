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

#include <glib.h>

#include "job-shared.h"

int
main (int argc,
      char **argv)
{
  int ret;
  int exit_status;
  GError *error;
  const char *action;
  const char *device;
  uid_t uid;
  char *command_line;
  char *standard_error;
  char *endp;
  gboolean do_mount;
  gboolean do_unmount;
  gboolean do_force_unmount;

  ret = 1;
  command_line = NULL;
  standard_error = NULL;
  do_mount = FALSE;
  do_unmount = FALSE;
  do_force_unmount = FALSE;

  if (argc != 4)
    {
      g_printerr ("wrong usage: expected 3 parameters, got %d\n", argc);
      goto out;
    }
  action = argv[1];
  device = argv[2];
  uid = strtol (argv[3], &endp, 10);
  if (endp == NULL || *endp != '\0')
    {
      g_printerr ("wrong usage: malformed uid '%s'\n", argv[3]);
      goto out;
    }
  if (strcmp (action, "mount") == 0)
    {
      do_mount = TRUE;
    }
  else if (strcmp (action, "unmount") == 0)
    {
      do_unmount = TRUE;
    }
  else if (strcmp (action, "force_unmount") == 0)
    {
      do_force_unmount = TRUE;
    }
  else
    {
      g_printerr ("wrong usage: malformed action '%s'\n", action);
      goto out;
    }

  /* become the user; right now we are uid 0.. so after the setuid() call we
   * can never gain root again
   */
  if (uid != 0)
    {
      if (setuid (uid) != 0)
        {
          g_printerr ("cannot switch to uid %d: %m\n", uid);
          goto out;
        }
    }

  if (do_mount)
    {
      command_line = g_strdup_printf ("mount %s", device);
    }
  else if (do_unmount)
    {
      command_line = g_strdup_printf ("umount %s", device);
    }
  else if (do_force_unmount)
    {
      command_line = g_strdup_printf ("umount -l %s", device);
    }
  else
    {
      g_assert_not_reached ();
    }

  error = NULL;
  if (!g_spawn_command_line_sync (command_line, NULL, &standard_error, &exit_status, &error))
    {
      g_printerr ("cannot spawn '%s': %s\n", command_line, error->message);
      g_error_free (error);
      goto out;
    }
  if (WEXITSTATUS (exit_status) != 0)
    {
      g_printerr ("helper failed with:\n%s", standard_error);
      goto out;
    }
  ret = 0;

 out:
  g_free (standard_error);
  g_free (command_line);
  return ret;
}
