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
#include <linux/blkpg.h>

#include <glib.h>
#include <gudev/gudev.h>

#include "job-shared.h"
#include "partutil.h"

int
main (int argc,
      char **argv)
{
  int n;
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
  guint out_num;
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

  /* don't ask libparted to poke the kernel - it won't work if other partitions are mounted/busy */
  if (part_add_partition ((char *) device,
                          offset,
                          size,
                          &out_start,
                          &out_size,
                          &out_num,
                          type,
                          strlen (label) > 0 ? label : NULL,
                          flags,
                          -1,
                          -1,
                          FALSE))
    {
      gint fd;
      struct blkpg_ioctl_arg a;
      struct blkpg_partition p;

      /* now clear out file system signatures in the newly created
       * partition... Unless it's an extended partition!
       */
      n = strtoll (type, &endp, 0);
      if (*endp == '\0' && (n == 0x05 || n == 0x0f || n == 0x85))
        {
          /* do nothing */
        }
      else
        {
          if (!scrub_signatures (device, out_start, out_size))
            {
              g_printerr ("Cannot scrub filesystem signatures at "
                          "offset=%" G_GINT64_FORMAT " and size=%" G_GINT64_FORMAT "\n",
                          out_start, out_size);
              goto out;
            }
        }

      /* OK, now tell the kernel about the newly added partition (but only if we are a kernel partition) */
      if (!g_str_has_prefix (device, "/dev/mapper/mpath"))
        {
          fd = open (device, O_RDONLY);
          if (fd < 0)
            {
              g_printerr ("Cannot open %s: %m\n", device);
              goto out;
            }
          memset (&a, '\0', sizeof(struct blkpg_ioctl_arg));
          memset (&p, '\0', sizeof(struct blkpg_partition));
          p.pno = out_num;
          p.start = out_start;
          p.length = out_size;
          a.op = BLKPG_ADD_PARTITION;
          a.datalen = sizeof(p);
          a.data = &p;
          if (ioctl (fd, BLKPG, &a) == -1)
            {
              g_printerr ("Error doing BLKPG ioctl with BLKPG_ADD_PARTITION for partition %d "
                          "of size %" G_GUINT64_FORMAT " at offset %" G_GUINT64_FORMAT " on %s: %m\n",
                          out_num,
                          out_start,
                          out_size,
                          device);
              close (fd);
              goto out;
            }
          close (fd);
        }

      /* send the start and size back to the daemon - it needs to know, to
       * wait for the created partition, because the partition may not have
       * been created exactly where it was requested....
       */
      g_printerr ("job-create-partition-offset: %" G_GINT64_FORMAT "\n", out_start);
      g_printerr ("job-create-partition-size: %" G_GINT64_FORMAT "\n", out_size);

      ret = 0;
    }

 out:
  g_strfreev (flags);
  return ret;
}
