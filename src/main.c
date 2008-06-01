/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 David Zeuthen <david@fubar.dk>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
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

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <devkit-gobject.h>

#include "devkit-disks-daemon.h"


#define NAME_TO_CLAIM "org.freedesktop.DeviceKit.Disks"

static GMainLoop *loop;

static void
name_lost (DBusGProxy *system_bus_proxy, const char *name_which_was_lost, gpointer user_data)
{
        g_warning ("got NameLost, exiting");
        g_main_loop_quit (loop);
}

static gboolean
acquire_name_on_proxy (DBusGProxy *system_bus_proxy, gboolean replace)
{
        GError     *error;
        guint       result;
        gboolean    res;
        gboolean    ret;
        guint       flags;

        ret = FALSE;

        flags = DBUS_NAME_FLAG_ALLOW_REPLACEMENT;
        if (replace)
                flags |= DBUS_NAME_FLAG_REPLACE_EXISTING;

        if (system_bus_proxy == NULL) {
                goto out;
        }

        error = NULL;
	res = dbus_g_proxy_call (system_bus_proxy,
                                 "RequestName",
                                 &error,
                                 G_TYPE_STRING, NAME_TO_CLAIM,
                                 G_TYPE_UINT, flags,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, &result,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", NAME_TO_CLAIM, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", NAME_TO_CLAIM);
                }
                goto out;
	}

 	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", NAME_TO_CLAIM, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", NAME_TO_CLAIM);
                }
                goto out;
        }

        dbus_g_proxy_add_signal (system_bus_proxy, "NameLost", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (system_bus_proxy, "NameLost", G_CALLBACK (name_lost), NULL, NULL);

        ret = TRUE;

 out:
        return ret;
}

int
main (int argc, char **argv)
{
        GError              *error;
        DevkitDisksDaemon   *disks_daemon;
        GOptionContext      *context;
        DBusGProxy          *system_bus_proxy;
        DBusGConnection     *bus;
        int                  ret;
        static gboolean      replace;
        static GOptionEntry  entries []   = {
                { "replace", 0, 0, G_OPTION_ARG_NONE, &replace, "Replace existing daemon", NULL },
                { NULL }
        };

        ret = 1;

        /* run with a controlled path */
        if (!g_setenv ("PATH", PACKAGE_LIBEXEC_DIR ":/sbin:/bin:/usr/sbin:/usr/bin", TRUE)) {
                g_warning ("Couldn't set PATH");
                goto out;
        }

        /* avoid gvfs (http://bugzilla.gnome.org/show_bug.cgi?id=526454) */
        if (!g_setenv ("GIO_USE_VFS", "local", TRUE)) {
                g_warning ("Couldn't set GIO_USE_GVFS");
                goto out;
        }

        g_type_init ();

        context = g_option_context_new ("DeviceKit Disks Daemon");
        g_option_context_add_main_entries (context, entries, NULL);
        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        error = NULL;
        bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
                g_error_free (error);
                goto out;
        }

	system_bus_proxy = dbus_g_proxy_new_for_name (bus,
                                               DBUS_SERVICE_DBUS,
                                               DBUS_PATH_DBUS,
                                               DBUS_INTERFACE_DBUS);
        if (system_bus_proxy == NULL) {
                g_warning ("Could not construct system_bus_proxy object; bailing out");
                goto out;
        }

        if (!acquire_name_on_proxy (system_bus_proxy, replace) ) {
                g_warning ("Could not acquire name; bailing out");
                goto out;
        }

        g_debug ("Starting devkit-disks-daemon version %s", VERSION);

        disks_daemon = devkit_disks_daemon_new ();

        if (disks_daemon == NULL) {
                goto out;
        }

        loop = g_main_loop_new (NULL, FALSE);

        g_main_loop_run (loop);

        g_object_unref (disks_daemon);
        g_main_loop_unref (loop);
        ret = 0;

out:
        return ret;
}
