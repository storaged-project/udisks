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
#include "partutil.h"

int
main (int argc,
      char **argv)
{
  int ret;
  const char *device;
  char *label;
  char *type;
  char *flags_as_string;
  char **flags;
  guint64 offset;
  guint64 size;
  guint64 out_start;
  guint64 out_size;
  char *endp;

  ret = 1;
  flags = NULL;

  if (argc < 7)
    {
      g_printerr ("wrong usage\n");
      goto out;
    }
  device = argv[1];
  offset = strtoll (argv[2], &endp, 10);
  if (*endp != '\0')
    {
      g_printerr ("malformed offset '%s'\n", argv[2]);
      goto out;
    }
  size = strtoll (argv[3], &endp, 10);
  if (*endp != '\0')
    {
      g_printerr ("malformed size '%s'\n", argv[3]);
      goto out;
    }
  type = argv[4];
  label = argv[5];
  flags_as_string = argv[6];

  flags = g_strsplit (flags_as_string, ",", 0);

  /* we trust the caller have verified that the given slice doesn't overlap
   * with existing partitions
   */

  g_print ("type:            '%s'\n", type);
  g_print ("label:           '%s'\n", label);
  g_print ("flags_as_string: '%s'\n", flags_as_string);

  if (part_change_partition ((char *) device,
                             offset,
                             offset,
                             size,
                             &out_start,
                             &out_size,
                             type,
                             strlen (label) > 0 ? label : NULL,
                             flags,
                             -1,
                             -1))
    {
      if (out_start != offset || out_size != size)
        {
          g_printerr ("ugh, offset or size changed\n");
          g_printerr        ("offset:     %" G_GINT64_FORMAT "\n", offset);
          g_printerr ("size:       %" G_GINT64_FORMAT "\n", size);
          g_printerr ("new_offset: %" G_GINT64_FORMAT "\n", out_start);
          g_printerr ("new_size:   %" G_GINT64_FORMAT "\n", out_size);
        }
      else
        {
          /* success */
          ret = 0;
        }
    }

  /* no need to reread partition table as sizes didn't change */

 out:
  g_strfreev (flags);
  return ret;
}
