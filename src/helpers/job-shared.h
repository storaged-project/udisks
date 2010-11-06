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
#include <sys/ioctl.h>
#include <stdint.h>
#include <linux/fs.h>

#include <glib.h>

#ifndef __JOB_SHARED_H__
#define __JOB_SHARED_H__

/* TODO: maybe move to private static library if there's a lot of shared stuff */

static inline gboolean
_do_write (int fd,
           void *buf,
           int num)
{
  gboolean ret;
  ret = FALSE;
 again:
  if (write (fd, buf, num) != (int) num)
    {
      if (errno == EAGAIN)
        {
          goto again;
        }
      else
        {
          g_printerr ("%d: error writing %d bytes: %m\n", getpid (), num);
          goto out;
        }
    }
  ret = TRUE;
 out:
  return ret;
}

#if 0
#define ERASE_SIZE (128*1024)
static inline gboolean
_scrub_signatures (int fd, guint64 offset, guint64 size)
{
  gboolean ret;
  guint64 wipe_size;
  char buf[ERASE_SIZE];

  ret = FALSE;

  /* wipe first and last 128KB. Note that btrfs keeps signatures at 0x10000 == 64KB. */
  wipe_size = 128 * 1024;
  g_assert (sizeof (buf) >= wipe_size);

  if (wipe_size > size)
    {
      wipe_size = size;
    }

  if (lseek64 (fd, offset, SEEK_SET) == (off64_t) -1)
    {
      g_printerr ("cannot seek to %" G_GINT64_FORMAT ": %m", offset);
      goto out;
    }

  if (!_do_write (fd, buf, wipe_size))
    goto out;

  if (lseek64 (fd, offset + size - wipe_size, SEEK_SET) == (off64_t) -1)
    {
      g_printerr ("cannot seek to %" G_GINT64_FORMAT ": %m", offset + size - wipe_size);
      goto out;
    }

  if (!_do_write (fd, buf, wipe_size))
    goto out;

  ret = TRUE;

 out:
  return ret;
}
#endif

/**
 * task_zero_device:
 * @device: device to zero
 * @offset: the offset to start zeroing the device
 * @size: if zero is passed, the whole device is zeroed; otherwise size of slice to zero
 *
 * Zeroes (parts of) a device and scrubs all areas containing
 * signatures.
 **/
static inline gboolean
scrub_signatures (const char *device,
                  guint64 offset,
                  guint64 size)
{
  int fd;
  gboolean ret;
  char buf[128 * 1024];
  guint64 wipe_size;

  fd = 0;
  ret = FALSE;

  fd = open (device, O_WRONLY);
  if (fd < 0)
    {
      g_printerr ("cannot open %s: %m\n", device);
      goto out;
    }

  if (size == 0)
    {
      if (ioctl (fd, BLKGETSIZE64, &size) != 0)
        {
          g_printerr ("cannot determine size of %s: %m\n", device);
          goto out;
        }
    }

  memset (buf, '\0', sizeof(buf));

  /* wipe first and last 128KB. Note that btrfs keeps signatures at 0x10000 == 64KB. */
  wipe_size = 128 * 1024;
  memset (buf, '\0', sizeof(buf));

  if (wipe_size > size)
    {
      wipe_size = size;
    }

  if (lseek64 (fd, offset, SEEK_SET) == (off64_t) - 1)
    {
      g_printerr ("cannot seek to %" G_GINT64_FORMAT " on %s: %m", offset, device);
      goto out;
    }

  if (!_do_write (fd, buf, wipe_size))
    goto out;

  if (lseek64 (fd, offset + size - wipe_size, SEEK_SET) == (off64_t) - 1)
    {
      g_printerr ("cannot seek to %" G_GINT64_FORMAT " on %s: %m", offset + size - wipe_size, device);
      goto out;
    }

  if (!_do_write (fd, buf, wipe_size))
    goto out;

  ret = TRUE;

 out:
  if (fd >= 0)
    {
      if (fsync (fd) != 0)
        {
          g_printerr ("Error calling fsync(2) on %s: %m\n", device);
          ret = FALSE;
        }
      close (fd);
    }
  return ret;
}

static inline gboolean
validate_and_escape_label (char **label,
                           int max_len)
{
  int n;
  gboolean ret;
  GString *s;

  ret = FALSE;

  if ((int) strlen (*label) > max_len)
    {
      g_printerr ("given file system label exceeds %d characters\n", max_len);
      goto out;
    }

  /* escape '"' and '\' */
  s = g_string_new (*label);
  for (n = 0; n < (int) s->len; n++)
    {
      if (s->str[n] == '"' || s->str[n] == '\\')
        {
          g_string_insert_c (s, n, '\\');
          n++;
        }
    }
  g_free (*label);
  *label = g_string_free (s, FALSE);

  ret = TRUE;
 out:
  return ret;
}

static inline gboolean
reread_partition_table (const gchar *device_file)
{
  gint fd;
  gboolean ret;
  guint num_retries;

  ret = FALSE;
  num_retries = 0;

  fd = open (device_file, O_RDONLY);
  if (fd < 0)
    {
      g_printerr ("cannot open %s (for BLKRRPART): %m\n", device_file);
      goto out;
    }
 try_again:
  if (ioctl (fd, BLKRRPART) != 0)
    {
      if (errno == EBUSY && num_retries < 20)
        {
          usleep (250 * 1000);
          num_retries++;
          goto try_again;
        }
      close (fd);
      g_printerr ("BLKRRPART ioctl failed for %s: %m\n", device_file);
      goto out;
    }
  close (fd);

  ret = TRUE;

 out:
  return ret;
}

#endif /* __JOB_SHARED_H__ */
