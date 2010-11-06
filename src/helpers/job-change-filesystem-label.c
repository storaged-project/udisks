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
  const char *device;
  const char *fstype;
  char *command_line;
  char *standard_error;
  char *new_label;
  char *c;

  ret = 1;
  command_line = NULL;
  standard_error = NULL;
  new_label = NULL;

  if (argc != 4)
    {
      g_printerr ("wrong usage\n");
      goto out;
    }
  device = argv[1];
  fstype = argv[2];
  new_label = g_strdup (argv[3]);

  if (strcmp (fstype, "ext2") == 0 || strcmp (fstype, "ext3") == 0 || strcmp (fstype, "ext4") == 0)
    {
      if (!validate_and_escape_label (&new_label, 16))
        goto out;
      command_line = g_strdup_printf ("e2label %s \"%s\"", device, new_label);
    }
  else if (strcmp (fstype, "xfs") == 0)
    {
      if (!validate_and_escape_label (&new_label, 12))
        goto out;
      if (strlen (new_label) == 0)
        command_line = g_strdup_printf ("xfs_admin -L -- %s", device);
      else
        command_line = g_strdup_printf ("xfs_admin -L \"%s\" %s", new_label, device);

    }
  else if (strcmp (fstype, "reiserfs") == 0)
    {
      if (!validate_and_escape_label (&new_label, 16))
        goto out;
      command_line = g_strdup_printf ("reiserfstune -l \"%s\" %s", new_label, device);
    }
  else if (strcmp (fstype, "vfat") == 0)
    {
      if (!validate_and_escape_label (&new_label, 254))
        goto out;
      /* VFAT does not allow some characters */
      for (c = "\"*/:<>?\\|"; *c; ++c)
	{
	  if (strchr (new_label, *c) != NULL)
	    {
	      g_printerr ("character '%c' not supported in VFAT labels\n", *c);
	      goto out;
	    }
          }

      g_setenv ("MTOOLS_SKIP_CHECK", "1", TRUE);
      if (strlen (new_label) == 0)
        command_line = g_strdup_printf ("mlabel -c -i %s ::", device);
      else
        command_line = g_strdup_printf ("mlabel -i %s \"::%s\"", device, new_label);

    }
  else if (strcmp (fstype, "ntfs") == 0)
    {
      if (!validate_and_escape_label (&new_label, 128))
        goto out;
      command_line = g_strdup_printf ("ntfslabel %s \"%s\"", device, new_label);
    }
  else if (strcmp (fstype, "nilfs2") == 0)
    {
      if (!validate_and_escape_label (&new_label, 80))
        goto out;
      command_line = g_strdup_printf ("nilfs-tune -L \"%s\" %s", new_label, device);
    }
  else
    {
      g_printerr ("fstype %s not supported\n", fstype);
      goto out;
    }

  if (command_line != NULL)
    {
      error = NULL;
      if (!g_spawn_command_line_sync (command_line, NULL, &standard_error, &exit_status, &error))
        {
          g_printerr ("cannot spawn '%s': %s\n", command_line, error->message);
          g_error_free (error);
          ret = 3; /* indicate FilesystemToolsMissing error */
          goto out;
        }
      if (WEXITSTATUS (exit_status) != 0)
        {
          g_printerr ("helper failed with:\n%s", standard_error);
          goto out;
        }
    }

  ret = 0;

 out:
  g_free (new_label);
  g_free (standard_error);
  g_free (command_line);
  return ret;
}
