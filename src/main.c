/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <david@fubar.dk>
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

#include "config.h"

#include <gio/gio.h>

#include "profile.h"

#include "linuxdaemon.h"

static Daemon *the_daemon = NULL;
static GMainLoop *loop = NULL;
static gchar *opt_helper_dir = NULL;
static gboolean opt_replace = FALSE;
static GOptionEntry opt_entries[] = {
  {"replace", 0, 0, G_OPTION_ARG_NONE, &opt_replace, "Replace existing daemon", NULL},
  {"helper-dir", 0, G_OPTION_FLAG_FILENAME, G_OPTION_ARG_STRING, &opt_helper_dir, "Directory for helper tools", NULL},
  {NULL }
};

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  GError *error;

  PROFILE ("Connected to the system bus");
  g_print ("Connected to the system bus\n");

  g_assert (the_daemon == NULL);
  the_daemon = g_object_new (TYPE_LINUX_DAEMON,
                             "daemon-version", PACKAGE_VERSION,
                             NULL);

  error = NULL;
  daemon_register_object (the_daemon,
                          connection,
                          "/org/freedesktop/UDisks",
                          &error);
  if (error != NULL)
    {
      g_printerr ("Error registering object at /org/freedesktop/UDisks: %s", error->message);
      g_error_free (error);
      g_main_loop_quit (loop); /* exit */
    }
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  PROFILE ("Lost the name org.freedesktop.UDisks - exiting");
  g_print ("Lost the name org.freedesktop.UDisks - exiting\n");
  g_main_loop_quit (loop);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  PROFILE ("Acquired the name org.freedesktop.UDisks on the system bus");
  g_print ("Acquired the name org.freedesktop.UDisks on the system bus\n");
}

int
main (int    argc,
      char **argv)
{
  GError *error;
  GOptionContext *opt_context;
  gchar *s;
  gint ret;
  guint name_owner_id;

  PROFILE ("main(): start");

  ret = 1;
  loop = NULL;
  opt_context = NULL;
  name_owner_id = 0;

  g_type_init ();

#if 0
  /* fork the polling process early */
  if (!poller_setup (argc, argv))
    {
      goto out;
    }
#endif

  /* avoid gvfs (http://bugzilla.gnome.org/show_bug.cgi?id=526454) */
  if (!g_setenv ("GIO_USE_VFS", "local", TRUE))
    {
      g_printerr ("Error setting GIO_USE_GVFS");
      goto out;
    }

  opt_context = g_option_context_new ("udisks storage daemon");
  g_option_context_add_main_entries (opt_context, opt_entries, NULL);
  error = NULL;
  if (!g_option_context_parse (opt_context, &argc, &argv, &error))
    {
      g_printerr ("Error parsing options: %s", error->message);
      g_error_free (error);
      goto out;
    }

  /* run with a controlled path */
  if (opt_helper_dir != NULL)
    s = g_strdup_printf ("%s:" PACKAGE_LIBEXEC_DIR ":/sbin:/bin:/usr/sbin:/usr/bin", opt_helper_dir);
  else
    s = g_strdup (PACKAGE_LIBEXEC_DIR ":/sbin:/bin:/usr/sbin:/usr/bin");
  if (!g_setenv ("PATH", s, TRUE))
    {
      g_printerr ("Error setting PATH\n");
      goto out;
    }
  g_free (s);

  name_owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                                  "org.freedesktop.UDisks",
                                  G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                    (opt_replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
                                  on_bus_acquired,
                                  on_name_acquired,
                                  on_name_lost,
                                  NULL,
                                  NULL);

  PROFILE ("main(): starting main loop");

  loop = g_main_loop_new (NULL, FALSE);

  g_main_loop_run (loop);

  ret = 0;

 out:
  if (name_owner_id != 0)
    g_bus_unown_name (name_owner_id);
  if (the_daemon != NULL)
    g_object_unref (the_daemon);
  if (loop != NULL)
    g_main_loop_unref (loop);
  if (opt_context != NULL)
    g_option_context_free (opt_context);
  return ret;
}
