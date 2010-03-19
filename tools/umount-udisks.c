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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "udisks-daemon-glue.h"
#include "udisks-device-glue.h"

static DBusGConnection *bus;

static int
do_unmount (const char *object_path,
            const char *options)
{
  DBusGProxy *proxy;
  GError *error;
  char **unmount_options;
  int ret = 1;

  unmount_options = NULL;
  if (options != NULL)
    unmount_options = g_strsplit (options, ",", 0);

  proxy = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.UDisks.Device");

  error = NULL;
  if (!org_freedesktop_UDisks_Device_filesystem_unmount (proxy, (const char **) unmount_options, &error))
    {
      g_print ("Unmount failed: %s\n", error->message);
      g_error_free (error);
      goto out;
    }
  ret = 0; /* success */
 out:
  g_strfreev (unmount_options);
  return ret;
}

int
main (int argc,
      char **argv)
{
  GError *error;
  DBusGProxy *disks_proxy;
  int ret;
  char *object_path;
  struct stat st;
  char *path;

  ret = 1;
  bus = NULL;
  path = NULL;
  disks_proxy = NULL;

  g_type_init ();

  if (argc < 2 || strlen (argv[1]) == 0)
    {
      fprintf (stderr, "%s: this program is only supposed to be invoked by umount(8).\n", argv[0]);
      goto out;
    }

  error = NULL;
  bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
  if (bus == NULL)
    {
      g_warning ("Couldn't connect to system bus: %s", error->message);
      g_error_free (error);
      goto out;
    }

  disks_proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.UDisks",
                                           "/org/freedesktop/UDisks",
                                           "org.freedesktop.UDisks");

  error = NULL;

  if (stat (argv[1], &st) < 0)
    {
      fprintf (stderr, "%s: could not stat %s: %s\n", argv[0], argv[1], strerror (errno));
      goto out;
    }

  if (S_ISBLK (st.st_mode))
    {
      path = g_strdup (argv[1]);
    }
  else
    {
      path = g_strdup_printf ("/dev/block/%d:%d", major (st.st_dev), minor (st.st_dev));
    }

  if (!org_freedesktop_UDisks_find_device_by_device_file (disks_proxy, path, &object_path, &error))
    {
      fprintf (stderr, "%s: no device for %s: %s\n", argv[0], argv[1], error->message);
      g_error_free (error);
      goto out;
    }
  ret = do_unmount (object_path, NULL);
  g_free (object_path);

 out:
  g_free (path);
  if (disks_proxy != NULL)
    g_object_unref (disks_proxy);
  if (bus != NULL)
    dbus_g_connection_unref (bus);
  return ret;
}
