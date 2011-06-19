/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007 David Zeuthen <david@fubar.dk>
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

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <gudev/gudev.h>

#include "poller.h"
#include "device.h"
#include "device-private.h"

#ifdef __linux__
extern char **environ;
static char **argv_buffer = NULL;
static size_t argv_size = 0;
#endif

/*#define POLL_SHOW_DEBUG*/

static void
set_proc_title_init (int argc, char *argv[])
{
#ifdef __linux__
  unsigned int i;
  char **new_environ, *endptr;

  /* This code is really really ugly. We make some memory layout
   * assumptions and reuse the environment array as memory to store
   * our process title in */

  for (i = 0; environ[i] != NULL; i++)
    ;

  endptr = i ? environ[i-1] + strlen (environ[i-1]) : argv[argc-1] + strlen (argv[argc-1]);

  argv_buffer = argv;
  argv_size = endptr - argv_buffer[0];

  /* Make a copy of environ */

  new_environ = malloc (sizeof(char*) * (i + 1));
  for (i = 0; environ[i] != NULL; i++)
    new_environ[i] = strdup (environ[i]);
  new_environ[i] = NULL;

  environ = new_environ;
#endif
}

/* this code borrowed from avahi-daemon's setproctitle.c (LGPL v2) */
static void
set_proc_title (const char *format,
                ...)
{
#ifdef __linux__
  size_t len;
  va_list ap;

  if (argv_buffer == NULL)
    goto out;

  va_start (ap, format);
  vsnprintf (argv_buffer[0], argv_size, format, ap);
  va_end (ap);

  len = strlen (argv_buffer[0]);

  memset (argv_buffer[0] + len, 0, argv_size - len);
  argv_buffer[1] = NULL;
 out:
  ;
#endif
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar **poller_devices_to_poll = NULL;

static guint poller_timeout_id = 0;

static void
poller_poll_device (const gchar *device_file)
{
  gboolean is_cdrom;
  int fd, fd2;

  /* the device file is the canonical device file from udev */
  is_cdrom = (g_str_has_prefix (device_file, "/dev/sr") || g_str_has_prefix (device_file, "/dev/scd"));

#ifdef POLL_SHOW_DEBUG
  g_print ("**** POLLER (%d): polling %s\n", getpid (), device_file);
#endif

  if (is_cdrom)
    {
      /* optical drives need special care
       *
       *  - use O_NONBLOCK to avoid closing the door
       *  - use O_EXCL to avoid interferring with cd burning software / audio playback / etc
       */
      fd = open (device_file, O_RDONLY | O_NONBLOCK | O_EXCL);
      if (fd != -1)
        {
          close (fd);
        }
    }
  else
    {
      fd = open (device_file, O_RDONLY);
      fd2 = open (device_file, O_RDONLY | O_NONBLOCK);
      if (fd != -1)
        {
          close (fd);
        }
      if (fd2 != -1)
        {
          close (fd2);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
poller_timeout_cb (gpointer user_data)
{
  guint n;

  for (n = 0; poller_devices_to_poll != NULL && poller_devices_to_poll[n] != NULL; n++)
    {
      const gchar *device_file = poller_devices_to_poll[n];
      poller_poll_device (device_file);
    }

  /* don't remove the source */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
poller_have_data (GIOChannel *channel,
                  GIOCondition condition,
                  gpointer user_data)
{
  gchar *line;
  gsize line_length;
  GError *error;
  gint status;

  error = NULL;

  /* Exit if parent dies */
  if (condition == G_IO_HUP)
    {
      exit (1);
    }

 read_more:
  status = g_io_channel_read_line (channel, &line, &line_length, NULL, &error);
  if (error != NULL)
    {
      g_warning ("Error reading line from daemon: %s", error->message);
      g_error_free (error);
      goto out;
    }
  if (status == G_IO_STATUS_EOF || status == G_IO_STATUS_AGAIN)
    {
      goto out;
    }

  g_strstrip (line);

#ifdef POLL_SHOW_DEBUG
  g_print ("**** POLLER (%d): polling process read '%s'\n", getpid (), line);
#endif
  if (g_str_has_prefix (line, "set-poll:"))
    {
      g_strfreev (poller_devices_to_poll);
      poller_devices_to_poll = g_strsplit (line + strlen ("set-poll:"), " ", 0);
    }
  else
    {
      g_printerr ("**** POLLER (%d): unknown command '%s'\n", getpid (), line);
    }

  if (g_strv_length (poller_devices_to_poll) == 0)
    {
      if (poller_timeout_id > 0)
        {
          g_source_remove (poller_timeout_id);
          poller_timeout_id = 0;
        }

      set_proc_title ("udisks-daemon: not polling any devices");
    }
  else
    {
      set_proc_title ("udisks-daemon: polling %s", line + strlen ("set-poll:"));

      if (poller_timeout_id == 0)
        {
          poller_timeout_id = g_timeout_add_seconds (2, poller_timeout_cb, NULL);
        }
    }

  g_free (line);
  goto read_more;

 out:
  /* keep the IOChannel around */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
poller_run (gint fd)
{
  GMainLoop *loop;
  GIOChannel *io_channel;

  loop = g_main_loop_new (NULL, FALSE);

  io_channel = g_io_channel_unix_new (fd);
  g_io_channel_set_flags (io_channel, G_IO_FLAG_NONBLOCK, NULL);
  g_io_add_watch (io_channel, G_IO_IN | G_IO_HUP, poller_have_data, NULL);

  g_main_loop_run (loop);
}

/* ---------------------------------------------------------------------------------------------------- */

static gint poller_daemon_write_end_fd;

gboolean
poller_setup (int argc,
              char *argv[])
{
  gint pipefds[2];
  gboolean ret;

  ret = FALSE;

  if (pipe (pipefds) != 0)
    {
      g_warning ("Couldn't set up polling process, pipe() failed: %m");
      goto out;
    }

  switch (fork ())
    {
    case 0:
      /* child */
      close (pipefds[1]); /* close write end */
      set_proc_title_init (argc, argv);
      poller_run (pipefds[0]);
      break;

    default:
      /* parent */
      close (pipefds[0]); /* close read end */
      poller_daemon_write_end_fd = pipefds[1];
      break;

    case -1:
      g_warning ("Couldn't set up polling process, fork() failed: %m");
      goto out;
      break;
    }

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
check_in_kernel_polling (Device* d)
{
  /* only check once */
  if (!d->priv->checked_in_kernel_polling)
    {
      int poll_time;
      int fd;
      char c;

      d->priv->checked_in_kernel_polling = TRUE;

      poll_time = g_udev_device_get_sysfs_attr_as_int (d->priv->d, "events_poll_msecs");
#ifdef POLL_SHOW_DEBUG
      g_print("**** POLLER (%d): per-device poll time for %s: %i\n", getpid (), d->priv->device_file, poll_time);
#endif

      if (poll_time >= 0)
	{
	  d->priv->using_in_kernel_polling = (poll_time > 0);
	  goto out;
	}

      /* -1 means using global polling interval, so check the global default */
      /* check global default */
      fd = open("/sys/module/block/parameters/events_dfl_poll_msecs", O_RDONLY);
      if (fd > 0)
	{
	  if (read (fd, &c, 1) > 0)
	    {
#ifdef POLL_SHOW_DEBUG
	      g_print("**** POLLER (%d): global poll time first char: %c\n", getpid (), c);
#endif
	      /* if this is positive, we use in-kernel polling */
	      d->priv->using_in_kernel_polling = (c != '0' && c != '-');
	    }
	  close (fd);
	}
    }

out:
  return d->priv->using_in_kernel_polling;
}

/* ---------------------------------------------------------------------------------------------------- */

void
poller_set_devices (GList *devices)
{
  GList *l;
  gchar **device_array;
  guint n;
  gchar *joined;
  gchar *devices_to_poll;
  static gchar *devices_currently_polled = NULL;

  device_array = g_new0 (gchar *, g_list_length (devices) + 2);

  for (l = devices, n = 0; l != NULL; l = l->next)
    {
      Device *device = DEVICE (l->data);

      if (check_in_kernel_polling (device))
	{
#ifdef POLL_SHOW_DEBUG
	  g_print("**** POLLER (%d): Kernel is polling %s already, ignoring\n", getpid (), device->priv->device_file);
#endif
	  continue;
	}

      device_array[n++] = device->priv->device_file;
    }

  g_qsort_with_data (device_array, n, sizeof(gchar *), (GCompareDataFunc) g_strcmp0, NULL);

  device_array[n] = "\n";

  joined = g_strjoinv (" ", device_array);
  g_free (device_array);
  devices_to_poll = g_strconcat ("set-poll:", joined, NULL);
  g_free (joined);

  /* only poke the polling process if the list of currently polled devices change */
  if (g_strcmp0 (devices_to_poll, devices_currently_polled) != 0)
    {
      g_free (devices_currently_polled);
      devices_currently_polled = devices_to_poll;

#ifdef POLL_SHOW_DEBUG
      g_print ("**** POLLER (%d): Sending poll command: '%s'\n", getpid (), devices_currently_polled);
#endif
      if (write (poller_daemon_write_end_fd, devices_currently_polled, strlen (devices_currently_polled)) < 0)
        g_error ("**** POLLER (%d): Failed to send polled devices: %s", getpid (), g_strerror (errno));
    }
  else
    {
      g_free (devices_to_poll);
    }
}

/* ---------------------------------------------------------------------------------------------------- */
