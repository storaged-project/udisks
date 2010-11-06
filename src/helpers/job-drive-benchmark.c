/* -*-  mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
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

#define _GNU_SOURCE /* for O_DIRECT */

#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <string.h>

#include <glib.h>

static guchar *buf = NULL;
static guint64 size = 0;
static const gchar *device_file = NULL;
static gint fd = -1;
static gint page_size = 0;
static guint64 buffer_size = 0;

static void
report_progress (gdouble percent,
                 guint cur_task,
                 guint num_tasks)
{
  gdouble overall;
  static time_t last_report = 0;
  time_t now;

  overall = cur_task * 100.0 / num_tasks;
  overall += percent / num_tasks;

  /* only send out progress updates every 2-3 seconds - we don't want to spam
   * the bus and clients with events
   */
  now = time (NULL);
  if (last_report == 0 || now - last_report > 2)
    {
      g_print ("udisks-helper-progress: %f\n", overall);
      last_report = now;
    }
}

static gboolean
guesstimate_optimal_buffer_size (guint num_samples)
{
  GTimeVal begin_time;
  GTimeVal end_time;
  gboolean ret;
  gssize remaining;
  gssize total_read;
  gdouble duration_secs;
  gssize num_read;

  /* We don't want the benchmark to take forever. So measure the speed in the start and
   * adjust buffer_size such that doing num_samples reads of buffer_size won't take
   * more than 30 seconds.
   *
   * We do this by checking how long it takes to read buffer_size.
   */

  ret = FALSE;

  if (lseek (fd, 0, SEEK_SET) == -1)
    {
      g_printerr ("Error seeking to start of disk for %s when guesstimating buffer size: %m\n", device_file);
      goto out;
    }

  g_get_current_time (&begin_time);
  remaining = buffer_size;
  total_read = 0;
  while (total_read < remaining)
    {
      num_read = read (fd, buf, remaining - total_read);

      if (num_read == 0)
        {
          break;
        }
      else if (num_read < 0)
        {
          g_printerr ("Error reading %" G_GUINT64_FORMAT " bytes at %" G_GSSIZE_FORMAT " from %s "
                      "when guesstimating buffer size: %m\n",
                      buffer_size,
                      (remaining - total_read),
                      device_file);
          goto out;
        }
      else
        {
          total_read += num_read;
        }
    }
  g_get_current_time (&end_time);

  duration_secs = ((end_time.tv_sec * G_USEC_PER_SEC + end_time.tv_usec) - (begin_time.tv_sec * G_USEC_PER_SEC
                                                                            + begin_time.tv_usec)) / ((gdouble) G_USEC_PER_SEC);

  /* duration_secs is (approx) the number of seconds needed to do one sample */
  if (duration_secs * num_samples > 30.0)
    {
      guint64 new_buffer_size;

      new_buffer_size = buffer_size * 30.0 / (duration_secs * num_samples);
      new_buffer_size &= ~(page_size - 1);

      if (new_buffer_size < 1 * 1024 * 1024)
        {
          g_printerr ("Device %s is too slow to benchmark", device_file);
          goto out;
        }

      buffer_size = new_buffer_size;
    }

  ret = TRUE;

 out:
  return ret;
}

static gboolean
measure_transfer_rate (guint num_samples,
                       guint cur_task,
                       guint num_tasks)
{
  gboolean ret;
  guint n;
  guint64 sample_size;

  sample_size = buffer_size;

  ret = FALSE;

  /* First measure read (or TODO: write) performance across the drive */
  for (n = 0; n < num_samples; n++)
    {
      goffset pos;
      gssize remaining;
      gssize total_read;
      GTimeVal begin_time;
      GTimeVal end_time;
      gdouble duration_secs;
      gssize num_read;

      pos = n * size / num_samples;

      /* O_DIRECT also only wants to read from page offsets */
      pos &= ~(page_size - 1);

      if (lseek64 (fd, pos, SEEK_SET) == -1)
        {
          g_printerr ("Error seeking to position %" G_GOFFSET_FORMAT " for %s: %m\n",
                      pos,
                      device_file);
          goto out;
        }

      /* read a single page - otherwise disk spinup + seek time will pollute the result
       * (ignores error checking since that's done below)
       */
      num_read = read (fd, buf, page_size);

      g_get_current_time (&begin_time);
      total_read = 0;
      remaining = sample_size;
      while (total_read < remaining)
        {
          num_read = read (fd, buf, remaining - total_read);

          if (num_read == 0)
            {
              break;
            }
          else if (num_read < 0)
            {
              g_printerr ("Error reading %" G_GUINT64_FORMAT " bytes at %" G_GOFFSET_FORMAT " from %s: %m\n",
                          sample_size,
                          pos + (remaining - total_read),
                          device_file);
              goto out;
            }
          else
            {
              total_read += num_read;
            }

        }
      g_get_current_time (&end_time);

      duration_secs = ((end_time.tv_sec * G_USEC_PER_SEC + end_time.tv_usec) - (begin_time.tv_sec * G_USEC_PER_SEC
                                                                                + begin_time.tv_usec)) / ((gdouble) G_USEC_PER_SEC);

      g_print ("read_transfer_rate: offset %" G_GOFFSET_FORMAT " rate %f\n",
               pos,
               sample_size / duration_secs);

      report_progress (100.0 * n / num_samples, cur_task, num_tasks);
    }

  ret = TRUE;

 out:
  return ret;
}

static gboolean
measure_write_transfer_rate (guint num_samples,
                             guint cur_task,
                             guint num_tasks)
{
  gboolean ret;
  guint n;
  guint64 sample_size;

  sample_size = buffer_size;

  ret = FALSE;

  /* First measure read (or TODO: write) performance across the drive */
  for (n = 0; n < num_samples; n++)
    {
      goffset pos;
      gssize remaining;
      gssize total_written;
      GTimeVal begin_time;
      GTimeVal end_time;
      gdouble duration_secs;
      gssize num_written;

      pos = n * size / num_samples;

      /* O_DIRECT also only wants to read from page offsets */
      pos &= ~(page_size - 1);

      if (lseek64 (fd, pos, SEEK_SET) == -1)
        {
          g_printerr ("Error seeking to position %" G_GOFFSET_FORMAT " for %s: %m\n",
                      pos,
                      device_file);
          goto out;
        }

      /* read a single page - otherwise disk spinup + seek time will pollute the result
       * (ignores error checking since that's done below)
       */
      num_written = read (fd, buf, page_size);

      g_get_current_time (&begin_time);
      total_written = 0;
      remaining = sample_size;
      while (total_written < remaining)
        {
          num_written = write (fd, buf, remaining - total_written);

          if (num_written == 0)
            {
              break;
            }
          else if (num_written < 0)
            {
              g_printerr ("Error writing %" G_GUINT64_FORMAT " bytes at %" G_GOFFSET_FORMAT " to %s: %m\n",
                          sample_size,
                          pos + (remaining - total_written),
                          device_file);
              goto out;
            }
          else
            {
              total_written += num_written;
            }

        }

      if (fsync (fd) != 0)
        {
          g_printerr ("Error fsync()'ing after writing at %" G_GOFFSET_FORMAT " to %s: %m\n",
                      pos + (remaining - total_written),
                      device_file);
          goto out;
        }

      g_get_current_time (&end_time);

      duration_secs = ((end_time.tv_sec * G_USEC_PER_SEC + end_time.tv_usec) - (begin_time.tv_sec * G_USEC_PER_SEC
                                                                                + begin_time.tv_usec)) / ((gdouble) G_USEC_PER_SEC);

      g_print ("write_transfer_rate: offset %" G_GOFFSET_FORMAT " rate %f\n",
               pos,
               sample_size / duration_secs);

      report_progress (100.0 * n / num_samples, cur_task, num_tasks);
    }

  ret = TRUE;

 out:
  return ret;
}

static gboolean
measure_access_time (guint num_samples,
                     guint cur_task,
                     guint num_tasks)
{
  gboolean ret;
  guint n;
  GRand *rand;
  GTimeVal begin_time;
  GTimeVal end_time;
  gdouble duration_secs;

  ret = FALSE;

  /* we want this to be deterministic (per size) to make benchmarks repeatable */
  rand = g_rand_new_with_seed (42);

  for (n = 0; n < num_samples; n++)
    {
      guint64 pos;

      pos = (guint64) g_rand_double_range (rand, 0, (gdouble) (size - page_size));
      pos &= ~(page_size - 1);

      g_get_current_time (&begin_time);
      if (lseek64 (fd, pos, SEEK_SET) == -1)
        {
          g_printerr ("Error seeking to position %" G_GUINT64_FORMAT " for %s: %m\n",
                      pos,
                      device_file);
          goto out;
        }
      if (read (fd, buf, page_size) < 0)
        {
          g_printerr ("Error reading %d bytes at %" G_GUINT64_FORMAT " from %s: %m\n",
                      page_size,
                      pos,
                      device_file);
          goto out;
        }
      g_get_current_time (&end_time);

      duration_secs = ((end_time.tv_sec * G_USEC_PER_SEC + end_time.tv_usec) - (begin_time.tv_sec * G_USEC_PER_SEC
                                                                                + begin_time.tv_usec)) / ((gdouble) G_USEC_PER_SEC);

      g_print ("access_time: offset %" G_GUINT64_FORMAT " time %f\n",
               pos,
               duration_secs);

      report_progress (100.0 * n / num_samples, cur_task, num_tasks);
    }

  ret = TRUE;

 out:
  if (rand != NULL)
    g_rand_free (rand);

  return ret;
}

int
main (int argc,
      char *argv[])
{
  gint ret;
  guchar *buf_unaligned;
  guint num_transfer_rate_samples;
  guint num_access_time_samples;
  gboolean do_write_benchmark;
  guint cur_task;
  guint num_tasks;

  ret = 1;
  fd = -1;
  buf_unaligned = NULL;

  if (argc != 3)
    {
      g_printerr ("incorrect usage\n");
      goto out;
    }

  /* TODO: should these be configurable? */
  num_transfer_rate_samples = 200;
  num_access_time_samples = 1000;

  device_file = argv[1];

  do_write_benchmark = atoi (argv[2]);

  if (do_write_benchmark)
    {
      fd = open (device_file, O_RDWR | O_DIRECT);
    }
  else
    {
      fd = open (device_file, O_RDONLY | O_DIRECT);
    }
  if (fd < 0)
    {
      g_printerr ("Error opening %s: %m\n", device_file);
      goto out;
    }

  if (ioctl (fd, BLKGETSIZE64, &size) != 0)
    {
      g_printerr ("Error finding size of %s: %m\n", device_file);
      goto out;
    }

  page_size = sysconf (_SC_PAGESIZE);
  if (page_size < 1)
    {
      g_printerr ("Error getting page size: %m\n");
      goto out;
    }

  /* upper bound for buffer size */
  buffer_size = 100 * 1024 * 1024;

  buf_unaligned = g_new0 (guchar, buffer_size + page_size);

  /* O_DIRECT needs a page aligned buffer */
  buf = (guchar*) (((gintptr) (buf_unaligned + page_size)) & (~(page_size - 1)));

  g_print ("udisks-helper-progress: 0.0\n");

  if (!guesstimate_optimal_buffer_size (num_transfer_rate_samples))
    goto out;

  /* TODO: report back chosen buffer_size? g_print ("buffer_size: %" G_GUINT64_FORMAT "\n", buffer_size); */

  cur_task = 0;
  if (do_write_benchmark)
    num_tasks = 3;
  else
    num_tasks = 2;

  if (!measure_transfer_rate (num_transfer_rate_samples, cur_task++, num_tasks))
    goto out;

  if (do_write_benchmark)
    {
      if (!measure_write_transfer_rate (num_transfer_rate_samples, cur_task++, num_tasks))
        goto out;
    }

  if (!measure_access_time (num_access_time_samples, cur_task++, num_tasks))
    goto out;

  ret = 0;

 out:
  if (fd != -1)
    close (fd);
  g_free (buf_unaligned);

  return ret;
}
