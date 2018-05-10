/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Dominika Hodovska <dhodovsk@redhat.com>
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
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <gio/gio.h>

#include "udiskszramutil.h"

#define BUFLEN 256

const gchar *zram_policy_action_id = "org.freedesktop.udisks2.zram.manage-zram";

gboolean
set_conf_property (char *filename,
                   const char *key,
                   const char *value,
                   GError **error)
{
  FILE *f = NULL;
  FILE  *tmp = NULL;
  char buff[BUFLEN];
  gchar* tmpfname;
  gboolean newprop = TRUE;
  gint fd;

  f = fopen (filename, "r+");
  if (f == NULL)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),"%m");
      return FALSE;
    }

  tmpfname = g_strdup_printf ("%sXXXXXX", filename);
  fd = g_mkstemp (tmpfname);
  if (fd == -1)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "%m");
      fclose (f);
      g_free (tmpfname);
      return FALSE;
    }

  if (chmod (tmpfname, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 0)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),"%m");
      fclose (f);
      g_free (tmpfname);
      return FALSE;
    }

  tmp = fdopen (fd, "w");
  if (tmp == NULL)
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),"%m");
      fclose (f);
      close (fd);
      g_free (tmpfname);
      return FALSE;
    }

  while (fgets (buff, BUFLEN, f))
    {
      if (! strncmp(key, buff, strlen(key)))
        {
          strncpy (buff+strlen (key)+1, value, BUFLEN-strlen (key)-1);
          buff[strlen (buff)] = '\n';
          newprop = FALSE;
        }
      fputs (buff, tmp);
    }

  if (newprop)
    fprintf (tmp,"%s=%s\n", key, value);
  fclose (f);
  fclose (tmp);

  if (rename (tmpfname, filename))
  {
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),"%m");
    return FALSE;
  }

  return TRUE;
}
