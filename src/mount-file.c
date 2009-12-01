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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "mount-file.h"
#include "device.h"
#include "device-private.h"

enum
  {
    MOUNT_FILE_DEVICE_FILE,
    MOUNT_FILE_MOUNT_PATH,
    MOUNT_FILE_MOUNTED_BY_UID,
    MOUNT_FILE_REMOVE_DIR_ON_UNMOUNT,
    MOUNT_FILE_NUM_TOKENS,
  };

gboolean
mount_file_has_device (const gchar *device_file,
                       uid_t *mounted_by_uid,
                       gboolean *remove_dir_on_unmount)
{
  gboolean ret;
  char *contents;
  char **lines;
  GError *error;
  int n;
  char *device_file_escaped;

  contents = NULL;
  ret = FALSE;
  device_file_escaped = g_uri_escape_string (device_file, NULL, TRUE);

  error = NULL;
  if (!g_file_get_contents (PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab",
                            &contents,
                            NULL,
                            &error))
    {
      /* not having the file is fine */
      if (error->code != G_FILE_ERROR_NOENT)
        {
          g_warning ("Error reading "
                     PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab: %s",
                     error->message);
        }
      g_error_free (error);
      goto out;
    }

  lines = g_strsplit (contents, "\n", 0);
  for (n = 0; lines[n] != NULL; n++)
    {
      const char *line = lines[n];
      char **tokens;

      tokens = g_strsplit (line, " ", 0);
      if (g_strv_length (tokens) == MOUNT_FILE_NUM_TOKENS)
        {
          const char *line_device_file = tokens[MOUNT_FILE_DEVICE_FILE];

          if (strcmp (line_device_file, device_file_escaped) == 0)
            {
              ret = TRUE;

              if (mounted_by_uid != NULL)
                *mounted_by_uid = atoi (tokens[MOUNT_FILE_MOUNTED_BY_UID]);
              if (remove_dir_on_unmount != NULL)
                if (strcmp (tokens[MOUNT_FILE_REMOVE_DIR_ON_UNMOUNT], "1") == 0)
                  *remove_dir_on_unmount = TRUE;

              g_strfreev (lines);
              g_strfreev (tokens);
              goto out;
            }
        }
      g_strfreev (tokens);
    }
  g_strfreev (lines);

 out:
  g_free (contents);
  g_free (device_file_escaped);
  return ret;
}

void
mount_file_add (const gchar *device_file,
                const gchar *mount_path,
                uid_t mounted_by_uid,
                gboolean remove_dir_on_unmount)
{
  char *contents;
  char *new_contents;
  char *added_line;
  GError *error;
  char *device_file_escaped;
  char *mount_path_escaped;

  g_return_if_fail (device_file != NULL);
  g_return_if_fail (mount_path != NULL);

  /* on error resp. contents and size will be set to resp. NULL and 0 */
  error = NULL;
  if (!g_file_get_contents (PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab",
                            &contents,
                            NULL,
                            &error))
    {
      /* not having the file is fine */
      if (error->code != G_FILE_ERROR_NOENT)
        {
          g_warning ("Error reading "
                     PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab: %s",
                     error->message);
        }
      g_error_free (error);
    }

  device_file_escaped = g_uri_escape_string (device_file, NULL, TRUE);
  mount_path_escaped = g_uri_escape_string (mount_path, NULL, TRUE);
  added_line = g_strdup_printf ("%s %s %d %d\n",
                                device_file_escaped,
                                mount_path_escaped,
                                mounted_by_uid,
                                remove_dir_on_unmount);
  g_free (device_file_escaped);
  g_free (mount_path_escaped);

  if (contents == NULL)
    {
      new_contents = added_line;
    }
  else
    {
      new_contents = g_strconcat (contents, added_line, NULL);
      g_free (contents);
      g_free (added_line);
    }

  error = NULL;
  if (!g_file_set_contents (PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab",
                            new_contents,
                            strlen (new_contents),
                            &error))
    {
      g_warning ("Error writing " PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab: %s", error->message);
      g_error_free (error);
    }

  g_free (new_contents);
}

void
mount_file_remove (const gchar *device_file,
                   const gchar *mount_path)
{
  char *contents;
  char **lines;
  char *new_contents;
  GString *str;
  GError *error;
  char *device_file_escaped;
  char *mount_path_escaped;
  int n;

  contents = NULL;
  new_contents = NULL;
  device_file_escaped = NULL;
  mount_path_escaped = NULL;

  device_file_escaped = g_uri_escape_string (device_file, NULL, TRUE);
  mount_path_escaped = g_uri_escape_string (mount_path, NULL, TRUE);

  error = NULL;
  if (!g_file_get_contents (PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab",
                            &contents,
                            NULL,
                            &error))
    {
      g_warning ("Error reading "
                 PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab: %s",
                 error->message);
      g_error_free (error);
      goto out;
    }

  str = g_string_new ("");
  lines = g_strsplit (contents, "\n", 0);
  for (n = 0; lines[n] != NULL; n++)
    {
      const char *line = lines[n];
      char **tokens;

      tokens = g_strsplit (line, " ", 0);
      if (g_strv_length (tokens) == MOUNT_FILE_NUM_TOKENS)
        {
          const char *line_device_file = tokens[MOUNT_FILE_DEVICE_FILE];
          const char *line_mount_path = tokens[MOUNT_FILE_MOUNT_PATH];

          if (strcmp (line_device_file, device_file_escaped) == 0 && strcmp (line_mount_path, mount_path_escaped) == 0)
            {
              /* this is the one... ignore this line and pile on with the others */
            }
          else
            {
              g_string_append_printf (str, "%s\n", line);
            }
        }
      g_strfreev (tokens);
    }
  g_strfreev (lines);

  new_contents = g_string_free (str, FALSE);

  error = NULL;
  if (!g_file_set_contents (PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab",
                            new_contents,
                            strlen (new_contents),
                            &error))
    {
      g_warning ("Error writing " PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab: %s", error->message);
      g_error_free (error);
    }

 out:
  g_free (new_contents);
  g_free (contents);
  g_free (device_file_escaped);
  g_free (mount_path_escaped);
}

void
mount_file_clean_stale (GList *existing_devices)
{
  GList *l;
  char *contents;
  char **lines;
  GError *error;
  int n;
  char *new_contents;
  GString *str;

  contents = NULL;
  new_contents = NULL;

  error = NULL;
  if (!g_file_get_contents (PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab",
                            &contents,
                            NULL,
                            &error))
    {
      /* not having the file is fine */
      if (error->code != G_FILE_ERROR_NOENT)
        {
          g_warning ("Error reading "
                     PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab: %s",
                     error->message);
        }
      g_error_free (error);
      goto out;
    }

  str = g_string_new ("");
  lines = g_strsplit (contents, "\n", 0);
  for (n = 0; lines[n] != NULL; n++)
    {
      const char *line = lines[n];
      char **tokens;

      tokens = g_strsplit (line, " ", 0);
      if (g_strv_length (tokens) == MOUNT_FILE_NUM_TOKENS)
        {
          const char *line_mount_path = tokens[MOUNT_FILE_MOUNT_PATH];
          gboolean entry_is_valid;

          entry_is_valid = FALSE;

          for (l = existing_devices; l != NULL && !entry_is_valid; l = l->next)
            {
              Device *device = l->data;
              char *mount_path_escaped;

              if (!device->priv->device_is_mounted ||
                  device->priv->device_mount_paths == NULL)
                continue;

              mount_path_escaped = g_uri_escape_string (((gchar **) device->priv->device_mount_paths->pdata)[0], NULL, TRUE);

              if (strcmp (line_mount_path, mount_path_escaped) == 0)
                {
                  entry_is_valid = TRUE;
                }
              g_free (mount_path_escaped);
            }

          if (entry_is_valid)
            {
              g_string_append_printf (str, "%s\n", line);
            }
          else
            {
              char *line_mount_path_unescaped;

              line_mount_path_unescaped = g_uri_unescape_string (line_mount_path, NULL);

              g_print ("Removing stale mounts entry and directory for '%s'\n",
                       line_mount_path_unescaped);

              if (g_rmdir (line_mount_path_unescaped) != 0)
                {
                  g_warning ("Error removing dir '%s' in stale cleanup: %m",
                             line_mount_path_unescaped);
                }

            }
        }
      g_strfreev (tokens);
    }
  g_strfreev (lines);

  new_contents = g_string_free (str, FALSE);

  error = NULL;
  if (!g_file_set_contents (PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab",
                            new_contents,
                            strlen (new_contents),
                            &error))
    {
      g_warning ("Error writing " PACKAGE_LOCALSTATE_DIR "/lib/udisks/mtab: %s", error->message);
      g_error_free (error);
    }

 out:
  g_free (new_contents);
  g_free (contents);
}

