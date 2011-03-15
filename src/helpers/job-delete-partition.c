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

#include "job-shared.h"
#include "partutil.h"

int
main (int argc,
      char **argv)
{
  int ret;
  const char *device;
  char **options;
  int n;
  guint64 offset;
  guint64 size;
  int part_number;
  char *endp;

  ret = 1;

  if (argc < 5)
    {
      g_printerr ("wrong usage\n");
      goto out;
    }
  device = argv[1];
  offset = strtoll (argv[3], &endp, 10);
  if (*endp != '\0')
    {
      g_printerr ("malformed offset '%s'\n", argv[3]);
      goto out;
    }
  size = strtoll (argv[4], &endp, 10);
  if (*endp != '\0')
    {
      g_printerr ("malformed size '%s'\n", argv[4]);
      goto out;
    }
  part_number = strtol (argv[5], &endp, 10);
  if (*endp != '\0')
    {
      g_printerr ("malformed partition number '%s'\n", argv[5]);
      goto out;
    }

  options = argv + 6;

  for (n = 0; options[n] != NULL; n++)
    {
      g_printerr ("option %s not supported\n", options[n]);
      goto out;
    }

  gboolean is_kernel_partition;
  is_kernel_partition = !g_str_has_prefix (device, "/dev/mapper/mpath");

  /* don't ask libparted to poke the kernel - it won't work if other
   * partitions are mounted/busy (unless it's not a kernel partition)
   */
  if (part_del_partition ((char *) device,
                          offset,
                          is_kernel_partition ? FALSE : TRUE))
    {
      /* now, ask the kernel to delete the partition  (but only if we are a kernel partition) */
      if (is_kernel_partition)
        {
          gint fd;
          struct blkpg_ioctl_arg a;
          struct blkpg_partition p;

          fd = open (device, O_RDONLY);
          if (fd < 0)
            {
              g_printerr ("Cannot open %s: %m\n", device);
              goto out;
            }
          memset (&a, '\0', sizeof(struct blkpg_ioctl_arg));
          memset (&p, '\0', sizeof(struct blkpg_partition));
          p.pno = part_number;
          a.op = BLKPG_DEL_PARTITION;
          a.datalen = sizeof(p);
          a.data = &p;
          if (ioctl (fd, BLKPG, &a) == -1)
            {
              g_printerr ("Error doing BLKPG ioctl with BLKPG_DEL_PARTITION for partition %d on %s: %m\n",
                          part_number,
                          device);
              close (fd);
              goto out;
            }
          close (fd);
        }

      /* zero the contents of what was the _partition_
       *
       *... but only after removing it from the partition table
       *    (since it may contain meta data if it's an extended partition)
       */
      /* scrub signatures */
      if (!scrub_signatures (device, offset, size))
        {
          g_printerr ("Cannot scrub filesystem signatures at "
                      "offset=%" G_GINT64_FORMAT " and size=%" G_GINT64_FORMAT "\n",
                      offset, size);
        }
      else
        {
          ret = 0;
        }
    }

 out:
  return ret;
}
