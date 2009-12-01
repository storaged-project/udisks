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

#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <glib.h>

static void
usage (void)
{
  g_printerr ("incorrect usage\n");
}

int
main (int argc,
      char *argv[])
{
  int ret;
  const gchar *device_file;
  gboolean is_cdrom;
  int fd, fd2;

  ret = 1;

  if (argc != 2)
    {
      usage ();
      goto out;
    }

  device_file = argv[1];

  /* the device file is the canonical device file from udev */
  is_cdrom = (g_str_has_prefix (device_file, "/dev/sr") || g_str_has_prefix (device_file, "/dev/scd"));

  if (is_cdrom)
    {
      /* optical drives need special care
       *
       *  - use O_NONBLOCK to avoid closing the door
       *  - use O_EXCL to avoid interferring with cd burning software / audio playback / etc
       */
      fd = open (device_file, O_RDONLY | O_NONBLOCK | O_EXCL);
      if (fd != -1)
        close (fd);
    }
  else
    {
      fd = open (device_file, O_RDONLY);
      fd2 = open (device_file, O_RDONLY | O_NONBLOCK);
      if (fd != -1)
        close (fd);
      if (fd2 != -1)
        close (fd2);
    }

  ret = 0;

 out:
  return ret;
}
