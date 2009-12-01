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
#include <unistd.h>

#include <glib.h>
#include <atasmart.h>

static void
usage (void)
{
  g_printerr ("incorrect usage\n");
}

static const gchar *device;
static gboolean cancelled = FALSE;

static void
sigterm_handler (int signum)
{
  cancelled = TRUE;
}

int
main (int argc,
      char *argv[])
{
  int ret;
  SkDisk *d;
  SkBool smart_is_available;
  SkSmartSelfTest test;

  d = NULL;
  ret = 1;

  if (argc != 3)
    {
      usage ();
      goto out;
    }

  device = argv[1];

  if (strcmp (argv[2], "short") == 0)
    {
      test = SK_SMART_SELF_TEST_SHORT;
    }
  else if (strcmp (argv[2], "extended") == 0)
    {
      test = SK_SMART_SELF_TEST_EXTENDED;
    }
  else if (strcmp (argv[2], "conveyance") == 0)
    {
      test = SK_SMART_SELF_TEST_CONVEYANCE;
    }
  else
    {
      g_printerr ("Unknown test '%s'\n", argv[2]);
      goto out;
    }

  if (sk_disk_open (device, &d) != 0)
    {
      g_printerr ("Failed to open disk %s: %s\n", device, strerror (errno));
      goto out;
    }

  if (sk_disk_smart_is_available (d, &smart_is_available) != 0)
    {
      g_printerr ("Failed to determine if smart is available for %s: %s\n", device, strerror (errno));
      goto out;
    }

  /* if the user cancels, catch that and abort the test */
  signal (SIGTERM, sigterm_handler);

  /* progress at 0% initially */
  g_print ("udisks-helper-progress: 0\n");

  /* start the test */
  if (sk_disk_smart_self_test (d, test) != 0)
    {
      g_printerr ("Error initiating test on disk %s: %s\n", device, strerror (errno));
      goto out;
    }

  /* poll for completion */
  while (TRUE && !cancelled)
    {
      const SkSmartParsedData *data;

      sleep (2);

      if (sk_disk_smart_read_data (d) != 0)
        {
          g_printerr ("Failed to read smart data for %s: %s\n", device, strerror (errno));
          goto out;
        }
      if (sk_disk_smart_parse (d, &data) != 0)
        {
          g_printerr ("Failed to parse smart data for %s: %s\n", device, strerror (errno));
          goto out;
        }

      if (data->self_test_execution_status != SK_SMART_SELF_TEST_EXECUTION_STATUS_INPROGRESS)
        break;

      /* update progress */
      g_print ("udisks-helper-progress: %d\n", 100 - data->self_test_execution_percent_remaining);
    }

  /* abort test if cancelled */
  if (cancelled)
    {
      if (sk_disk_smart_self_test (d, SK_SMART_SELF_TEST_ABORT) != 0)
        {
          g_printerr ("Error cancelling test on disk %s: %s\n", device, strerror (errno));
          goto out;
        }
    }

  ret = 0;

 out:
  if (d != NULL)
    sk_disk_free (d);
  return ret;
}
