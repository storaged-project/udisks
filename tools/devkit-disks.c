/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#include "devkit-disks-daemon-glue.h"
#include "devkit-disks-device-glue.h"

static void
device_added_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
  g_print ("added:   %s\n", object_path);
}

static void
device_removed_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
  g_print ("removed: %s\n", object_path);
}

/* --- SUCKY CODE BEGIN --- */

/* This totally sucks; dbus-bindings-tool and dbus-glib should be able
 * to do this for us.
 */

static char *
get_property_string (DBusGConnection *bus,
                     const char *svc_name,
                     const char *obj_path,
                     const char *if_name,
                     const char *prop_name)
{
        char *ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = NULL;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (char *) g_value_get_string (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static gboolean
get_property_boolean (DBusGConnection *bus,
                      const char *svc_name,
                      const char *obj_path,
                      const char *if_name,
                      const char *prop_name)
{
        gboolean ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = FALSE;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (gboolean) g_value_get_boolean (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static guint64
get_property_uint64 (DBusGConnection *bus,
                     const char *svc_name,
                     const char *obj_path,
                     const char *if_name,
                     const char *prop_name)
{
        guint64 ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = 0;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (guint64) g_value_get_uint64 (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static int
get_property_int (DBusGConnection *bus,
                  const char *svc_name,
                  const char *obj_path,
                  const char *if_name,
                  const char *prop_name)
{
        int ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = 0;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (guint64) g_value_get_int (&value);

out:
        g_object_unref (proxy);
        return ret;
}

static char **
get_property_strlist (DBusGConnection *bus,
                      const char *svc_name,
                      const char *obj_path,
                      const char *if_name,
                      const char *prop_name)
{
        char **ret;
        DBusGProxy *proxy;
        GValue value = { 0 };
        GError *error = NULL;

        ret = NULL;
	proxy = dbus_g_proxy_new_for_name (bus,
                                           svc_name,
                                           obj_path,
                                           "org.freedesktop.DBus.Properties");
        if (!dbus_g_proxy_call (proxy,
                                "Get",
                                &error,
                                G_TYPE_STRING,
                                if_name,
                                G_TYPE_STRING,
                                prop_name,
                                G_TYPE_INVALID,
                                G_TYPE_VALUE,
                                &value,
                                G_TYPE_INVALID)) {
                g_warning ("error: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        ret = (char **) g_value_get_boxed (&value);

out:
        /* don't crash; return an empty list */
        if (ret == NULL) {
                ret = g_new0 (char *,  1);
                *ret = NULL;
        }

        g_object_unref (proxy);
        return ret;
}

typedef struct
{
        char *native_path;

        char    *device_file;
        char   **device_file_by_id;
        char   **device_file_by_path;
        char   **device_holders;
        char   **device_slaves;
        gboolean device_is_partition;
        gboolean device_is_partition_table;

        char    *id_usage;
        char    *id_type;
        char    *id_version;
        char    *id_uuid;
        char    *id_label;

        char    *partition_slave;
        char    *partition_scheme;
        int      partition_number;
        char    *partition_type;
        char    *partition_label;
        char    *partition_uuid;
        char   **partition_flags;
        guint64  partition_offset;
        guint64  partition_size;

        char    *partition_table_scheme;
        int      partition_table_count;
        char   **partition_table_holders;
} DeviceProperties;

static DeviceProperties *
device_properties_get (DBusGConnection *bus, const char *object_path)
{
        DeviceProperties *props;

        props = g_new0 (DeviceProperties, 1);
        props->native_path = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "native-path");

        props->device_file = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-file");
        props->device_file_by_id = get_property_strlist (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-file-by-id");
        props->device_file_by_path = get_property_strlist (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-file-by-path");
        props->device_holders = get_property_strlist (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-holders");
        props->device_slaves = get_property_strlist (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-slaves");
        props->device_is_partition = get_property_boolean (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-is-partition");
        props->device_is_partition_table = get_property_boolean (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "device-is-partition-table");

        props->id_usage = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-usage");
        props->id_type = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-type");
        props->id_version = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-version");
        props->id_uuid = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-uuid");
        props->id_label = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "id-label");

        props->partition_slave = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-slave");
        props->partition_scheme = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-scheme");
        props->partition_number = get_property_int (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-number");
        props->partition_type = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-type");
        props->partition_label = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-label");
        props->partition_uuid = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-uuid");
        props->partition_flags = get_property_strlist (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-flags");
        props->partition_offset = get_property_uint64 (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-offset");
        props->partition_size = get_property_uint64 (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-size");

        props->partition_table_scheme = get_property_string (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-table-scheme");
        props->partition_table_count = get_property_int (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-table-count");
        props->partition_table_holders = get_property_strlist (
                bus,
                "org.freedesktop.DeviceKit.Disks",
                object_path,
                "org.freedesktop.DeviceKit.Disks.Device",
                "partition-table-holders");
        return props;
}

static void
device_properties_free (DeviceProperties *props)
{
        g_free (props->native_path);
        g_free (props->device_file);
        g_strfreev (props->device_file_by_id);
        g_strfreev (props->device_file_by_path);
        g_strfreev (props->device_holders);
        g_strfreev (props->device_slaves);
        g_free (props->id_usage);
        g_free (props->id_type);
        g_free (props->id_version);
        g_free (props->id_uuid);
        g_free (props->id_label);
        g_free (props->partition_slave);
        g_free (props->partition_type);
        g_free (props->partition_label);
        g_free (props->partition_uuid);
        g_strfreev (props->partition_flags);
        g_free (props->partition_table_scheme);
        g_strfreev (props->partition_table_holders);
        g_free (props);
}

/* --- SUCKY CODE END --- */

int
main (int argc, char **argv)
{
        int                  ret;
        GOptionContext      *context;
        GError              *error = NULL;
        DBusGConnection     *bus = NULL;
        DBusGProxy          *disks_proxy = NULL;
        GMainLoop           *loop;
        unsigned int         n;
        static gboolean      inhibit      = FALSE;
        static gboolean      enumerate    = FALSE;
        static gboolean      monitor      = FALSE;
        static char         *show_info    = NULL;
        static GOptionEntry  entries []   = {
                { "inhibit", 0, 0, G_OPTION_ARG_NONE, &inhibit, "Inhibit the disks daemon from exiting", NULL },
                { "enumerate", 0, 0, G_OPTION_ARG_NONE, &enumerate, "Enumerate objects paths for devices", NULL },
                { "monitor", 0, 0, G_OPTION_ARG_NONE, &monitor, "Monitor activity from the disk daemon", NULL },
                { "show-info", 0, 0, G_OPTION_ARG_STRING, &show_info, "Show information about object path", NULL },
                { NULL }
        };

        ret = 1;

        g_type_init ();

        context = g_option_context_new ("DeviceKit-disks tool");
        g_option_context_add_main_entries (context, entries, NULL);
        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        loop = g_main_loop_new (NULL, FALSE);

        bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
                g_error_free (error);
                goto out;
        }


	disks_proxy = dbus_g_proxy_new_for_name (bus,
                                                 "org.freedesktop.DeviceKit.Disks",
                                                 "/",
                                                 "org.freedesktop.DeviceKit.Disks");
        dbus_g_proxy_add_signal (disks_proxy, "DeviceAdded", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (disks_proxy, "DeviceRemoved", G_TYPE_STRING, G_TYPE_INVALID);

        if (inhibit) {
                char *cookie;
                if (!org_freedesktop_DeviceKit_Disks_inhibit_shutdown (disks_proxy, &cookie, &error)) {
                        g_warning ("Couldn't inhibit disk daemon: %s", error->message);
                        g_error_free (error);
                        goto out;
                }
                g_free (cookie);
                g_print ("Disks daemon is now inhibited from exiting. Press Ctrl+C to cancel.\n");
                /* spin forever */
                g_main_loop_run (loop);
        } else if (enumerate) {
                GPtrArray *devices;
                if (!org_freedesktop_DeviceKit_Disks_enumerate_devices (disks_proxy, &devices, &error)) {
                        g_warning ("Couldn't enumerate devices: %s", error->message);
                        g_error_free (error);
                        goto out;
                }
                for (n = 0; n < devices->len; n++) {
                        char *object_path = devices->pdata[n];
                        g_print ("%s\n", object_path);
                }
                g_ptr_array_foreach (devices, (GFunc) g_free, NULL);
                g_ptr_array_free (devices, TRUE);
        } else if (monitor) {
                char *cookie;

                g_print ("Monitoring activity from the disks daemon. Press Ctrl+C to cancel.\n");

                if (!org_freedesktop_DeviceKit_Disks_inhibit_shutdown (disks_proxy, &cookie, &error)) {
                        g_warning ("Couldn't inhibit shutdown on disks daemon: %s", error->message);
                        g_error_free (error);
                        goto out;
                }
                g_free (cookie);

                dbus_g_proxy_connect_signal (disks_proxy, "DeviceAdded",
                                             G_CALLBACK (device_added_signal_handler), NULL, NULL);
                dbus_g_proxy_connect_signal (disks_proxy, "DeviceRemoved",
                                             G_CALLBACK (device_removed_signal_handler), NULL, NULL);
                g_main_loop_run (loop);
        } else if (show_info != NULL) {
                DeviceProperties *props;

                props = device_properties_get (bus, show_info);
                g_print ("Showing information for %s\n", show_info);
                g_print ("  native-path:   %s\n", props->native_path);
                for (n = 0; props->device_holders[n] != NULL; n++)
                        g_print ("  holder:        %s\n", (char *) props->device_holders[n]);
                for (n = 0; props->device_slaves[n] != NULL; n++)
                        g_print ("  slave:         %s\n", (char *) props->device_slaves[n]);
                g_print ("  device-file:   %s\n", props->device_file);
                for (n = 0; props->device_file_by_id[n] != NULL; n++)
                        g_print ("    by-id:       %s\n", (char *) props->device_file_by_id[n]);
                for (n = 0; props->device_file_by_path[n] != NULL; n++)
                        g_print ("    by-path:     %s\n", (char *) props->device_file_by_path[n]);
                g_print ("  usage:         %s\n", props->id_usage);
                g_print ("  type:          %s\n", props->id_type);
                g_print ("  version:       %s\n", props->id_version);
                g_print ("  uuid:          %s\n", props->id_uuid);
                g_print ("  label:         %s\n", props->id_label);
                if (props->device_is_partition_table) {
                        g_print ("  partition table:\n");
                        g_print ("    scheme:      %s\n", props->partition_table_scheme);
                        g_print ("    count:       %d\n", props->partition_table_count);
                        for (n = 0; props->partition_table_holders[n] != NULL; n++)
                                g_print ("    partition:   %s\n", (char *) props->partition_table_holders[n]);

                }
                if (props->device_is_partition) {
                        g_print ("  partition:\n");
                        g_print ("    part of:     %s\n", props->partition_slave);
                        g_print ("    scheme:      %s\n", props->partition_scheme);
                        g_print ("    number:      %d\n", props->partition_number);
                        g_print ("    type:        %s\n", props->partition_type);
                        g_print ("    flags:      ");
                        for (n = 0; props->partition_flags[n] != NULL; n++)
                                g_print (" %s", (char *) props->partition_flags[n]);
                        g_print ("\n");
                        g_print ("    offset:      %lld\n", props->partition_offset);
                        g_print ("    size:        %lld\n", props->partition_size);
                        g_print ("    label:       %s\n", props->partition_label);
                        g_print ("    uuid:        %s\n", props->partition_uuid);

                }
                device_properties_free (props);
        }

        ret = 0;

out:
        if (disks_proxy != NULL)
                g_object_unref (disks_proxy);
        if (bus != NULL)
                dbus_g_connection_unref (bus);

        return ret;
}
