/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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
#include <linux/fs.h>

#include <glib.h>

#ifndef __JOB_SHARED_H__
#define __JOB_SHARED_H__

/* TODO: maybe move to private static library if there's a lot of shared stuff */

#define ERASE_SIZE (128*1024)

/**
 * parse_passes_from_erase:
 * @str: string to pass, e.g. #NULL, "none", "full", "full3pass",
 * "full7pass", "full35pass".
 *
 * Parses an erase option string and returns the number of passes;
 * #NULL and "none" maps into 0, "full" maps into 1, "full3pass" maps
 * into 3, "full7pass" maps into 7, and "full35pass" maps into 35. If
 * the string cannot be parsed, -1 is returned.
 *
 * Returns: Number of passes or -1 on error.
 **/
static inline int
task_zero_device_parse_option (const char *str)
{
        int ret;

        if (str == NULL) {
                ret = 0;
        } else if (strcmp (str, "none") == 0) {
                ret = 0;
        } else if (strcmp (str, "full") == 0) {
                ret = 1;
        } else if (strcmp (str, "full3pass") == 0) {
                ret = 3;
        } else if (strcmp (str, "full7pass") == 0) {
                ret = 7;
        } else if (strcmp (str, "full35pass") == 0) {
                ret = 35;
        } else {
                ret = -1;
        }

        return ret;
}

static inline gboolean
do_write (int fd, void *buf, int num)
{
        gboolean ret;
        ret = FALSE;
again:
        if (write (fd, buf, num) != (int) num) {
                if (errno == EAGAIN) {
                        goto again;
                } else {
                        g_printerr ("%d: error writing %d bytes: %m\n", getpid (), num);
                        goto out;
                }
        }
        ret = TRUE;
out:
        return ret;
}

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

        if (wipe_size > size) {
                wipe_size = size;
        }

        if (lseek64 (fd, offset, SEEK_SET) == (off64_t) -1) {
                g_printerr ("cannot seek to %" G_GINT64_FORMAT ": %m", offset);
                goto out;
        }

        if (!do_write (fd, buf, wipe_size))
                goto out;

        if (lseek64 (fd, offset + size - wipe_size, SEEK_SET) == (off64_t) -1) {
                g_printerr ("cannot seek to %" G_GINT64_FORMAT ": %m", offset + size - wipe_size);
                goto out;
        }

        if (!do_write (fd, buf, wipe_size))
                goto out;

        ret = TRUE;

out:
        return ret;
}

/**
 * task_zero_device:
 * @device: device to zero
 * @offset: the offset to start zeroing the device
 * @size: if zero is passed, the whole device is zeroed; otherwise size of slice to zero
 * @num_passes: number of passes, 0, 1, 3, 7 and 35 supported. See
 * the function task_zero_device_parse_option() for details.
 * @cur_task: current task
 * @num_tasks: number of tasks
 *
 * Zeroes (parts of) a device. If @num_passes is 0 then only the areas
 * where file system signatures are normally stored are zeroed,
 * otherwise the device is cleared @num_passes times using methods
 * compliant with US DoD 5220.
 *
 * This task will use @num_passes + 1 task slots.
 *
 * See these websites.
 *
 *   http://ask.metafilter.com/83005/How-long-does-a-7-Pass-US-DoD-5220-method-take
 *   http://en.wikipedia.org/wiki/Gutmann_method
 *   http://en.wikipedia.org/wiki/National_Industrial_Security_Program
 *
 * for more details on secure erase.
 *
 * Returns: #TRUE unless erasing failed or an incoming parameter was
 * invalid.
 **/
static inline gboolean
task_zero_device (const char *device, guint64 offset, guint64 size, int num_passes, int cur_task, int num_tasks)
{
        int fd;
        gboolean ret;
        guint64 cursor;
        int percent;
        int old_percent;
        char buf[ERASE_SIZE];

        fd = 0;
        ret = FALSE;

        fd = open (device, O_WRONLY);
        if (fd < 0) {
                g_printerr ("cannot open device: %m\n");
                goto out;
        }

        if (size == 0) {
                if (ioctl (fd, BLKGETSIZE64, &size) != 0) {
                        g_printerr ("cannot determine size of device: %m\n");
                        goto out;
                }
        }

        memset (buf, '\0', sizeof (buf));


        if (num_passes == 0) {
                g_print ("progress: %d %d 0 zeroing\n", cur_task, num_tasks);

                if (!_scrub_signatures (fd, offset, size))
                        goto out;

        } else if (num_passes == 1) {
                /* first do a quick scrub of the signatures */
                if (!_scrub_signatures (fd, offset, size))
                        goto out;

                /* now all signatures should be gone.. TODO: poke the kernel so the volume is
                 * tagged as unrecognized
                 */

                if (lseek64 (fd, offset, SEEK_SET) == (off64_t) -1) {
                        g_printerr ("cannot seek to %" G_GINT64_FORMAT ": %m", offset);
                        goto out;
                }

                cursor = 0;
                old_percent = 0;
                g_print ("progress: %d %d 0 zeroing\n", cur_task, num_tasks);
                while (cursor < size) {
                        guint64 num;

                        num = sizeof (buf);
                        if (size - cursor < num)
                                num = size - cursor;
                        if (!do_write (fd, buf, num))
                                goto out;

                        cursor += num;

                        percent = 100 * cursor / size;
                        if (percent > old_percent) {
                                g_print ("progress: %d %d %d zeroing\n", cur_task, num_tasks, percent);
                                old_percent = percent;
                        }
                }
                g_print ("progress: %d %d -1 sync\n", cur_task + 1, num_tasks);
                fsync (fd);
        } else if (num_passes == 3) {
                g_printerr ("only 0 and 1 erase passes is implemented for now\n");
                goto out;
        } else if (num_passes == 7) {
                g_printerr ("only 0 and 1 erase passes is implemented for now\n");
                goto out;
        } else if (num_passes == 35) {
                g_printerr ("only 0 and 1 erase passes is implemented for now\n");
                goto out;
        }

        ret = TRUE;

out:
        if (fd >= 0)
                close (fd);
        return ret;
}

static inline gboolean
zero_signatures (const char *device, guint64 offset, guint64 size, int cur_task, int num_tasks)
{
        int fd;
        gboolean ret;
        guint64 wipe_size;
        char buf[ERASE_SIZE];

        ret = FALSE;

        fd = open (device, O_WRONLY);
        if (fd < 0) {
                g_printerr ("cannot open device: %m\n");
                goto out;
        }

        g_print ("progress: %d %d 0 zeroing\n", cur_task, num_tasks);

        /* wipe first and last 16kb. TODO: check 16kb is the right number */
        wipe_size = 16 * 1024;
        g_assert (sizeof (buf) >= wipe_size);

        if (wipe_size > size) {
                wipe_size = size;
        }

        if (lseek64 (fd, offset, SEEK_SET) == (off64_t) -1) {
                g_printerr ("cannot seek to %" G_GINT64_FORMAT ": %m", offset + size - wipe_size);
                goto out;
        }

        if (!do_write (fd, buf, wipe_size))
                goto out;

        if (lseek64 (fd, offset + size - wipe_size, SEEK_SET) == (off64_t) -1) {
                g_printerr ("cannot seek to %" G_GINT64_FORMAT ": %m", size - wipe_size);
                goto out;
        }

        if (!do_write (fd, buf, wipe_size))
                goto out;

        ret = TRUE;

out:
        if (fd >= 0)
                close (fd);
        return ret;
}

static inline gboolean
validate_and_escape_label (char **label, int max_len)
{
        int n;
        gboolean ret;
        GString *s;

        ret = FALSE;

        if ((int) strlen (*label) > max_len) {
                g_printerr ("given file system label exceeds %d characters\n", max_len);
                goto out;
        }

        /* escape '"' */
        s = g_string_new (*label);
        for (n = 0; n < (int) s->len; n++) {
                if (s->str[n] == '"') {
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
        if (fd < 0) {
                g_printerr ("cannot open %s (for BLKRRPART): %m\n", device_file);
                goto out;
        }
 try_again:
        if (ioctl (fd, BLKRRPART) != 0) {
                if (errno == EBUSY && num_retries < 20) {
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
