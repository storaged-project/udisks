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
#include <signal.h>
#include <gio/gio.h>

#include "gposixsignal.h"

#include "types.h"
#include "linuxblock.h"

/* ---------------------------------------------------------------------------------------------------- */

static GMainLoop *loop = NULL;
static gchar *opt_helper_dir = NULL;
static gboolean opt_replace = FALSE;
static GOptionEntry opt_entries[] = {
  {"replace", 0, 0, G_OPTION_ARG_NONE, &opt_replace, "Replace existing daemon", NULL},
  {"helper-dir", 0, G_OPTION_FLAG_FILENAME, G_OPTION_ARG_STRING, &opt_helper_dir, "Directory for helper tools", NULL},
  {NULL }
};

static GDBusObjectManager *object_manager = NULL;

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  g_print ("Connected to the system bus\n");

  object_manager = g_dbus_object_manager_new (connection, "/org/freedesktop/UDisks");

  linux_block_init (object_manager);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_print ("Lost (or failed to acquire) the name org.freedesktop.UDisks - exiting\n");
  g_main_loop_quit (loop);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_print ("Acquired the name org.freedesktop.UDisks on the system bus\n");
}

static gboolean
on_sigint (gpointer user_data)
{
  g_print ("Handling SIGINT\n");
  g_main_loop_quit (loop);
  return FALSE;
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
  guint sigint_id;

  ret = 1;
  loop = NULL;
  opt_context = NULL;
  name_owner_id = 0;
  sigint_id = 0;

  g_type_init ();

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

  loop = g_main_loop_new (NULL, FALSE);

  sigint_id = _g_posix_signal_watch_add (SIGINT,
                                         G_PRIORITY_DEFAULT,
                                         on_sigint,
                                         NULL,
                                         NULL);

  name_owner_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                                  "org.freedesktop.UDisks",
                                  G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                    (opt_replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
                                  on_bus_acquired,
                                  on_name_acquired,
                                  on_name_lost,
                                  NULL,
                                  NULL);

  g_print ("Entering main event loop\n");

  g_main_loop_run (loop);

  ret = 0;

  g_print ("Shutting down\n");
 out:
  linux_block_shutdown ();
  if (sigint_id > 0)
    g_source_remove (sigint_id);
  if (object_manager != NULL)
    g_object_unref (object_manager);
  if (name_owner_id != 0)
    g_bus_unown_name (name_owner_id);
  if (loop != NULL)
    g_main_loop_unref (loop);
  if (opt_context != NULL)
    g_option_context_free (opt_context);

  g_print ("Exiting with code %d\n", ret);
  return ret;
}
