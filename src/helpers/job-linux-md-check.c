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

static gboolean
sysfs_put_string (const gchar *sysfs_path,
                  const gchar *file,
                  const gchar *value)
{
  FILE *f;
  gchar *filename;
  gboolean ret;

  ret = FALSE;
  filename = NULL;

  filename = g_build_filename (sysfs_path, file, NULL);
  f = fopen (filename, "w");
  if (f == NULL)
    {
      g_printerr ("error opening %s for writing: %m\n", filename);
      goto out;
    }
  else
    {
      if (fputs (value, f) == EOF)
        {
          g_printerr ("error writing '%s' to %s: %m\n", value, filename);
          fclose (f);
          goto out;
        }
      fclose (f);
    }

  ret = TRUE;
 out:
  g_free (filename);
  return ret;
}

static char *
sysfs_get_string (const gchar *sysfs_path,
                  const gchar *file)
{
  gchar *result;
  gchar *filename;

  result = NULL;
  filename = g_build_filename (sysfs_path, file, NULL);
  if (!g_file_get_contents (filename, &result, NULL, NULL))
    {
      result = g_strdup ("");
    }
  g_free (filename);

  return result;
}

static gboolean cancelled = FALSE;

static void
sigterm_handler (int signum)
{
  cancelled = TRUE;
}

int
main (int argc,
      char **argv)
{
  gint ret;
  const gchar *device;
  const gchar *sysfs_path;
  gchar *sync_action;
  gboolean repair;
  gchar **options;
  gint n;

  ret = 1;
  repair = FALSE;
  sync_action = NULL;

  if (argc < 3)
    {
      g_printerr ("wrong usage\n");
      goto out;
    }
  device = argv[1];
  sysfs_path = argv[2];
  options = argv + 3;

  for (n = 0; options[n] != NULL; n++)
    {
      if (strcmp (options[n], "repair") == 0)
        {
          repair = TRUE;
        }
      else
        {
          g_printerr ("option %s not supported\n", options[n]);
          goto out;
        }
    }

  g_print ("device   = '%s'\n", device);
  g_print ("repair   = %d\n", repair);

  sync_action = sysfs_get_string (sysfs_path, "md/sync_action");
  g_strstrip (sync_action);
  if (g_strcmp0 (sync_action, "idle") != 0)
    {
      g_printerr ("device %s is not idle\n", device);
      goto out;
    }

  /* if the user cancels, catch that and make the array idle */
  signal (SIGTERM, sigterm_handler);

  if (!sysfs_put_string (sysfs_path, "md/sync_action", repair ? "repair" : "check"))
    {
      goto out;
    }

  g_print ("udisks-helper-progress: 0\n");
  while (!cancelled)
    {
      guint64 done;
      guint64 remaining;
      gchar *s;

      sleep (2);

      sync_action = sysfs_get_string (sysfs_path, "md/sync_action");
      g_strstrip (sync_action);
      if (g_strcmp0 (sync_action, "idle") == 0)
        {
          break;
        }
      g_free (sync_action);
      sync_action = NULL;

      s = g_strstrip (sysfs_get_string (sysfs_path, "md/sync_completed"));
      if (sscanf (s, "%" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "", &done, &remaining) == 2)
        {
          g_print ("udisks-helper-progress: %d\n", (gint) (100L * done / remaining));
        }
      else
        {
          g_printerr ("Cannot parse md/sync_completed: '%s'", s);
          goto out;
        }
      g_free (s);
    }

  if (cancelled)
    {
      sysfs_put_string (sysfs_path, "md/sync_action", "idle");
      goto out;
    }

  ret = 0;

 out:
  g_free (sync_action);
  return ret;
}
