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
  const char *scheme;
  char **options;
  int n;
  PartitionScheme pscheme;
  gboolean no_partition_table;

  ret = 1;
  no_partition_table = FALSE;
  pscheme = PART_TYPE_MSDOS;

  if (argc < 3)
    {
      g_printerr ("wrong usage\n");
      goto out;
    }
  device = argv[1];
  scheme = argv[2];
  options = argv + 3;

  for (n = 0; options[n] != NULL; n++)
    {
      g_printerr ("option %s not supported\n", options[n]);
      goto out;
    }

  if (strcmp (scheme, "mbr") == 0)
    {
      pscheme = PART_TYPE_MSDOS;
    }
  else if (strcmp (scheme, "gpt") == 0)
    {
      pscheme = PART_TYPE_GPT;
    }
  else if (strcmp (scheme, "apm") == 0)
    {
      pscheme = PART_TYPE_APPLE;
    }
  else if (strcmp (scheme, "none") == 0)
    {
      no_partition_table = TRUE;
    }
  else
    {
      g_printerr ("partitioning scheme %s not supported\n", scheme);
      goto out;
    }

  /* scrub signatures */
  if (!scrub_signatures (device, 0, 0))
    goto out;

  if (no_partition_table)
    {
      ret = 0;
    }
  else
    {
      if (part_create_partition_table ((char *) device, pscheme))
        ret = 0;
    }

  /* tell kernel reread partition table (but only if we are a kernel partition) */
  if (!g_str_has_prefix (device, "/dev/mapper/mpath"))
    {
      if (!reread_partition_table (device))
        {
          ret = 1;
          goto out;
        }
    }

 out:
  return ret;
}
