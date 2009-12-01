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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include <glib.h>
#include <atasmart.h>

static void
usage (void)
{
  fprintf (stderr, "incorrect usage\n");
}

int
main (int argc,
      char *argv[])
{
  int ret;
  const char *device;
  SkDisk *d;
  SkBool smart_is_available;
  SkBool awake;
  gboolean nowakeup;
  const void *blob;
  size_t blob_size;
  gchar *encoded_blob;

  d = NULL;
  ret = 1;

  if (argc != 3)
    {
      usage ();
      goto out;
    }

  device = argv[1];

  nowakeup = atoi (argv[2]);

  if (sk_disk_open (device, &d) != 0)
    {
      g_printerr ("Failed to open disk %s: %m\n", device);
      goto out;
    }

  if (sk_disk_check_sleep_mode (d, &awake) != 0)
    {
      g_printerr ("Failed to check if disk %s is awake: %m\n", device);
      goto out;
    }

  /* don't wake up disk unless specically asked to */
  if (nowakeup && !awake)
    {
      g_printerr ("Disk %s is asleep and nowakeup option was passed\n", device);
      ret = 2;
      goto out;
    }

  if (sk_disk_smart_is_available (d, &smart_is_available) != 0)
    {
      g_printerr ("Failed to determine if smart is available for %s: %m\n", device);
      goto out;
    }

  /* main smart data */
  if (sk_disk_smart_read_data (d) != 0)
    {
      g_printerr ("Failed to read smart data for %s: %m\n", device);
      goto out;
    }

  if (sk_disk_get_blob (d, &blob, &blob_size) != 0)
    {
      g_printerr ("Failed to read smart data for %s: %m\n", device);
      goto out;
    }

  encoded_blob = g_base64_encode ((const guchar *) blob, (gsize) blob_size);
  g_print ("%s\n", encoded_blob);
  g_free (encoded_blob);

  ret = 0;

 out:

  if (d != NULL)
    sk_disk_free (d);
  return ret;
}
