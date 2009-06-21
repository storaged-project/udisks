/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
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
#include <stdlib.h>
#include <time.h>

#include <scsi/sg_lib.h>
#include <scsi/sg_cmds.h>

#include <glib.h>

static void
usage (void)
{
  g_printerr ("incorrect usage\n");
}

int
main (int argc, char *argv[])
{
  int rc;
  int ret;
  int sg_fd;
  const gchar *device;

  ret = 1;
  sg_fd = -1;

  if (argc != 2)
    {
      usage ();
      goto out;
    }

  device = argv[1];

  sg_fd = sg_cmds_open_device (device, 1 /* read_only */, 1);
  if (sg_fd < 0)
    {
      g_printerr ("Cannot open %s: %m\n", device);
      goto out;
    }

  if ((rc = sg_ll_sync_cache_10 (sg_fd,
                                 0,  /* sync_nv */
                                 0,  /* immed */
                                 0,  /* group */
                                 0,  /* lba */
                                 0,  /* count */
                                 1,  /* noisy */
                                 0   /* verbose */
                                 )) != 0)
    {
      g_printerr ("Error SYNCHRONIZE CACHE for %s: %s\n", device, safe_strerror (rc));
      /* this is not a catastrophe, carry on */
    }

  if (sg_ll_start_stop_unit (sg_fd,
                             0,  /* immed */
                             0,  /* pc_mod__fl_num */
                             0,  /* power_cond */
                             0,  /* noflush__fl */
                             0,  /* loej */
                             0,  /* start */
                             1,  /* noisy */
                             0   /* verbose */
                             ) != 0)
    {
      g_printerr ("Error STOP UNIT for %s: %s\n", device, safe_strerror (rc));
      goto out;
    }

  /* OK, close the device */
  sg_cmds_close_device (sg_fd);
  sg_fd = -1;

  ret = 0;

 out:
  if (sg_fd > 0)
    sg_cmds_close_device (sg_fd);
  return ret;
}
