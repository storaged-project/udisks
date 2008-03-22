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

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <polkit-dbus/polkit-dbus.h>

#include "devkit-disks-daemon-glue.h"
#include "devkit-disks-device-glue.h"
#include "devkit-disks-marshal.h"

static DBusGConnection     *bus = NULL;
static DBusGProxy          *disks_proxy = NULL;
static GMainLoop           *loop;

static gboolean      opt_inhibit           = FALSE;
static gboolean      opt_enumerate         = FALSE;
static gboolean      opt_monitor           = FALSE;
static gboolean      opt_monitor_detail    = FALSE;
static char         *opt_show_info         = NULL;
static char         *opt_mount             = NULL;
static char         *opt_mount_fstype      = NULL;
static char         *opt_mount_options     = NULL;
static char         *opt_unmount           = NULL;
static char         *opt_unmount_options   = NULL;
static char         *opt_erase             = NULL;
static char         *opt_erase_options     = NULL;
static char         *opt_create_fs         = NULL;
static char         *opt_create_fs_type    = NULL;
static char         *opt_create_fs_options = NULL;

static gboolean do_monitor (void);
static void do_show_info (const char *object_path);

static gboolean
polkit_dbus_gerror_parse (GError *error,
                          PolKitAction **action,
                          PolKitResult *result)
{
        gboolean ret;
        const char *name;

        ret = FALSE;
        if (error->domain != DBUS_GERROR || error->code != DBUS_GERROR_REMOTE_EXCEPTION)
                goto out;

        name = dbus_g_error_get_name (error);

        ret = polkit_dbus_error_parse_from_strings (name,
                                                    error->message,
                                                    action,
                                                    result);
out:
        return ret;
}

static void
do_mount (const char *object_path,
          const char *filesystem_type,
          const char *options)
{
        char *mount_path;
        DBusGProxy *proxy;
        GError *error;
        char **mount_options;

        mount_options = NULL;
        if (options != NULL)
                mount_options = g_strsplit (options, ",", 0);

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           object_path,
                                           "org.freedesktop.DeviceKit.Disks.Device");
try_again:
        error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_Device_mount (proxy,
                                                           filesystem_type,
                                                           (const char **) mount_options,
                                                           &mount_path,
                                                           &error)) {
                PolKitAction *pk_action;
                PolKitResult pk_result;

                if (polkit_dbus_gerror_parse (error, &pk_action, &pk_result)) {
                        if (pk_result != POLKIT_RESULT_NO) {
                                char *action_id;
                                DBusError d_error;

                                polkit_action_get_action_id (pk_action, &action_id);
                                dbus_error_init (&d_error);
                                if (polkit_auth_obtain (action_id,
                                                        0,
                                                        getpid (),
                                                        &d_error)) {
                                        polkit_action_unref (pk_action);
                                        goto try_again;
                                } else {
                                        g_print ("Obtaining authorization failed: %s: %s\n",
                                                 d_error.name, d_error.message);
                                        dbus_error_free (&d_error);
                                        goto out;
                                }
                        }
                        polkit_action_unref (pk_action);
                        g_error_free (error);
                        goto out;
                } else {
                        g_print ("Mount failed: %s\n", error->message);
                        g_error_free (error);
                        goto out;
                }
        }

        g_print ("Mounted %s at %s\n", object_path, mount_path);
        g_free (mount_path);
out:
        g_strfreev (mount_options);
}

static void
do_unmount (const char *object_path,
            const char *options)
{
        DBusGProxy *proxy;
        GError *error;
        char **unmount_options;

        unmount_options = NULL;
        if (options != NULL)
                unmount_options = g_strsplit (options, ",", 0);

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           object_path,
                                           "org.freedesktop.DeviceKit.Disks.Device");

try_again:
        error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_Device_unmount (proxy,
                                                             (const char **) unmount_options,
                                                             &error)) {
                PolKitAction *pk_action;
                PolKitResult pk_result;

                if (polkit_dbus_gerror_parse (error, &pk_action, &pk_result)) {
                        if (pk_result != POLKIT_RESULT_NO) {
                                char *action_id;
                                DBusError d_error;

                                polkit_action_get_action_id (pk_action, &action_id);
                                dbus_error_init (&d_error);
                                if (polkit_auth_obtain (action_id,
                                                        0,
                                                        getpid (),
                                                        &d_error)) {
                                        polkit_action_unref (pk_action);
                                        goto try_again;
                                } else {
                                        g_print ("Obtaining authorization failed: %s: %s\n",
                                                 d_error.name, d_error.message);
                                        dbus_error_free (&d_error);
                                        goto out;
                                }
                        }
                        polkit_action_unref (pk_action);
                        g_error_free (error);
                        goto out;
                } else {
                        g_print ("Unmount failed: %s\n", error->message);
                        g_error_free (error);
                        goto out;
                }
        }
out:
        g_strfreev (unmount_options);
}

static void
do_create_fs (const char *object_path,
              const char *fs_type,
              const char *options)
{
        DBusGProxy *proxy;
        GError *error;
        char **split_options;

        split_options = NULL;
        if (options != NULL)
                split_options = g_strsplit (options, ",", 0);

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           object_path,
                                           "org.freedesktop.DeviceKit.Disks.Device");

try_again:
        error = NULL;
        /* Stupid glue don't let me tweak the timeout */
        if (!dbus_g_proxy_call_with_timeout (proxy,
                                             "CreateFilesystem",
                                             INT_MAX,
                                             &error,
                                             G_TYPE_STRING,
                                             fs_type,
                                             G_TYPE_STRV,
                                             split_options,
                                             G_TYPE_INVALID,
                                             G_TYPE_INVALID)) {
                PolKitAction *pk_action;
                PolKitResult pk_result;

                if (polkit_dbus_gerror_parse (error, &pk_action, &pk_result)) {
                        if (pk_result != POLKIT_RESULT_NO) {
                                char *action_id;
                                DBusError d_error;

                                polkit_action_get_action_id (pk_action, &action_id);
                                dbus_error_init (&d_error);
                                if (polkit_auth_obtain (action_id,
                                                        0,
                                                        getpid (),
                                                        &d_error)) {
                                        polkit_action_unref (pk_action);
                                        goto try_again;
                                } else {
                                        g_print ("Obtaining authorization failed: %s: %s\n",
                                                 d_error.name, d_error.message);
                                        dbus_error_free (&d_error);
                                        goto out;
                                }
                        }
                        polkit_action_unref (pk_action);
                        g_error_free (error);
                        goto out;
                } else {
                        g_print ("CreateFilesystem failed: %s\n", error->message);
                        g_error_free (error);
                        goto out;
                }
        }
out:
        g_strfreev (split_options);
}

static void
do_erase (const char *object_path,
          const char *options)
{
        DBusGProxy *proxy;
        GError *error;
        char **split_options;

        split_options = NULL;
        if (options != NULL)
                split_options = g_strsplit (options, ",", 0);

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           object_path,
                                           "org.freedesktop.DeviceKit.Disks.Device");

try_again:
        error = NULL;
        /* Stupid glue don't let me tweak the timeout */
        if (!dbus_g_proxy_call_with_timeout (proxy,
                                             "Erase",
                                             INT_MAX,
                                             &error,
                                             G_TYPE_STRV,
                                             split_options,
                                             G_TYPE_INVALID,
                                             G_TYPE_INVALID)) {
                PolKitAction *pk_action;
                PolKitResult pk_result;

                if (polkit_dbus_gerror_parse (error, &pk_action, &pk_result)) {
                        if (pk_result != POLKIT_RESULT_NO) {
                                char *action_id;
                                DBusError d_error;

                                polkit_action_get_action_id (pk_action, &action_id);
                                dbus_error_init (&d_error);
                                if (polkit_auth_obtain (action_id,
                                                        0,
                                                        getpid (),
                                                        &d_error)) {
                                        polkit_action_unref (pk_action);
                                        goto try_again;
                                } else {
                                        g_print ("Obtaining authorization failed: %s: %s\n",
                                                 d_error.name, d_error.message);
                                        dbus_error_free (&d_error);
                                        goto out;
                                }
                        }
                        polkit_action_unref (pk_action);
                        g_error_free (error);
                        goto out;
                } else {
                        g_print ("Erase failed: %s\n", error->message);
                        g_error_free (error);
                        goto out;
                }
        }
out:
        g_strfreev (split_options);
}

static void
device_added_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
  g_print ("added:     %s\n", object_path);
  if (opt_monitor_detail) {
          do_show_info (object_path);
          g_print ("\n");
  }
}

static void
device_changed_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
  g_print ("changed:     %s\n", object_path);
  if (opt_monitor_detail) {
          /* TODO: would be nice to just show the diff */
          do_show_info (object_path);
          g_print ("\n");
  }
}

static void
print_job (gboolean    job_in_progress,
           const char *job_id,
           gboolean    job_is_cancellable,
           int         job_num_tasks,
           int         job_cur_task,
           const char *job_cur_task_id,
           double      job_cur_task_percentage)
{
        if (job_in_progress) {
                if (job_num_tasks > 0) {
                        g_print ("  job underway:  %s: %d/%d tasks (%s",
                                 job_id,
                                 job_cur_task + 1,
                                 job_num_tasks,
                                 job_cur_task_id);
                        if (job_cur_task_percentage >= 0)
                                g_print (" @ %3.0lf%%", job_cur_task_percentage);
                        if (job_is_cancellable)
                                g_print (", cancellable");
                        g_print (")\n");
                } else {
                        g_print ("  job underway:  %s: unknown progress", job_id);
                        if (job_is_cancellable)
                                g_print (", cancellable");
                        g_print ("\n");
                }
        } else {
                g_print ("  job underway:  no\n");
        }
}

static void
device_job_changed_signal_handler (DBusGProxy *proxy,
                                   const char *object_path,
                                   gboolean    job_in_progress,
                                   const char *job_id,
                                   gboolean    job_is_cancellable,
                                   int         job_num_tasks,
                                   int         job_cur_task,
                                   const char *job_cur_task_id,
                                   double      job_cur_task_percentage,
                                   gpointer    user_data)
{
  g_print ("job-changed: %s\n", object_path);
  if (opt_monitor_detail) {
          print_job (job_in_progress,
                     job_id,
                     job_is_cancellable,
                     job_num_tasks,
                     job_cur_task,
                     job_cur_task_id,
                     job_cur_task_percentage);
  }
}

static void
device_removed_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
  g_print ("removed:   %s\n", object_path);
}

/* --- SUCKY CODE BEGIN --- */

/* This totally sucks; dbus-bindings-tool and dbus-glib should be able
 * to do this for us.
 *
 * TODO: keep in sync with code in tools/devkit-disks in DeviceKit-disks.
 */

typedef struct
{
        char *native_path;

        char    *device_file;
        char   **device_file_by_id;
        char   **device_file_by_path;
        gboolean device_is_partition;
        gboolean device_is_partition_table;
        gboolean device_is_removable;
        gboolean device_is_media_available;
        gboolean device_is_drive;
        gboolean device_is_crypto_cleartext;
        gboolean device_is_mounted;
        char    *device_mount_path;
        guint64  device_size;
        guint64  device_block_size;

        gboolean job_in_progress;
        char    *job_id;
        gboolean job_is_cancellable;
        int      job_num_tasks;
        int      job_cur_task;
        char    *job_cur_task_id;
        double   job_cur_task_percentage;

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
        int      partition_table_max_number;
        GArray  *partition_table_offsets;
        GArray  *partition_table_sizes;

        char    *crypto_cleartext_slave;

        char    *drive_vendor;
        char    *drive_model;
        char    *drive_revision;
        char    *drive_serial;
} DeviceProperties;

static void
collect_props (const char *key, const GValue *value, DeviceProperties *props)
{
        gboolean handled = TRUE;

        if (strcmp (key, "native-path") == 0)
                props->native_path = g_strdup (g_value_get_string (value));

        else if (strcmp (key, "device-file") == 0)
                props->device_file = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-file-by-id") == 0)
                props->device_file_by_id = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "device-file-by-path") == 0)
                props->device_file_by_path = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "device-is-partition") == 0)
                props->device_is_partition = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-partition-table") == 0)
                props->device_is_partition_table = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-removable") == 0)
                props->device_is_removable = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-available") == 0)
                props->device_is_media_available = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-drive") == 0)
                props->device_is_drive = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-crypto-cleartext") == 0)
                props->device_is_crypto_cleartext = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-mounted") == 0)
                props->device_is_mounted = g_value_get_boolean (value);
        else if (strcmp (key, "device-mount-path") == 0)
                props->device_mount_path = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-size") == 0)
                props->device_size = g_value_get_uint64 (value);
        else if (strcmp (key, "device-block-size") == 0)
                props->device_block_size = g_value_get_uint64 (value);

        else if (strcmp (key, "job-in-progress") == 0)
                props->job_in_progress = g_value_get_boolean (value);
        else if (strcmp (key, "job-id") == 0)
                props->job_id = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "job-is-cancellable") == 0)
                props->job_is_cancellable = g_value_get_boolean (value);
        else if (strcmp (key, "job-num-tasks") == 0)
                props->job_num_tasks = g_value_get_int (value);
        else if (strcmp (key, "job-cur-task") == 0)
                props->job_cur_task = g_value_get_int (value);
        else if (strcmp (key, "job-cur-task-id") == 0)
                props->job_cur_task_id = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "job-cur-task-percentage") == 0)
                props->job_cur_task_percentage = g_value_get_double (value);

        else if (strcmp (key, "id-usage") == 0)
                props->id_usage = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "id-type") == 0)
                props->id_type = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "id-version") == 0)
                props->id_version = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "id-uuid") == 0)
                props->id_uuid = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "id-label") == 0)
                props->id_label = g_strdup (g_value_get_string (value));

        else if (strcmp (key, "partition-slave") == 0)
                props->partition_slave = g_strdup (g_value_get_boxed (value));
        else if (strcmp (key, "partition-scheme") == 0)
                props->partition_scheme = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-number") == 0)
                props->partition_number = g_value_get_int (value);
        else if (strcmp (key, "partition-type") == 0)
                props->partition_type = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-label") == 0)
                props->partition_label = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-uuid") == 0)
                props->partition_uuid = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-flags") == 0)
                props->partition_flags = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "partition-offset") == 0)
                props->partition_offset = g_value_get_uint64 (value);
        else if (strcmp (key, "partition-size") == 0)
                props->partition_size = g_value_get_uint64 (value);

        else if (strcmp (key, "partition-table-scheme") == 0)
                props->partition_table_scheme = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "partition-table-count") == 0)
                props->partition_table_count = g_value_get_int (value);
        else if (strcmp (key, "partition-table-max-number") == 0)
                props->partition_table_max_number = g_value_get_int (value);
        else if (strcmp (key, "partition-table-offsets") == 0) {
                GValue dest_value = {0,};
                g_value_init (&dest_value, dbus_g_type_get_collection ("GArray", G_TYPE_UINT64));
                g_value_copy (value, &dest_value);
                props->partition_table_offsets = g_value_get_boxed (&dest_value);
        } else if (strcmp (key, "partition-table-sizes") == 0) {
                GValue dest_value = {0,};
                g_value_init (&dest_value, dbus_g_type_get_collection ("GArray", G_TYPE_UINT64));
                g_value_copy (value, &dest_value);
                props->partition_table_sizes = g_value_get_boxed (&dest_value);
        }

        else if (strcmp (key, "crypto-cleartext-slave") == 0)
                props->crypto_cleartext_slave = g_strdup (g_value_get_boxed (value));

        else if (strcmp (key, "drive-vendor") == 0)
                props->drive_vendor = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-model") == 0)
                props->drive_model = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-revision") == 0)
                props->drive_revision = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-serial") == 0)
                props->drive_serial = g_strdup (g_value_get_string (value));

        else
                handled = FALSE;

        if (!handled)
                g_warning ("unhandled property '%s'", key);
}

static DeviceProperties *
device_properties_get (DBusGConnection *bus,
                       const char *object_path)
{
        DeviceProperties *props;
        GError *error;
        GHashTable *hash_table;
        DBusGProxy *prop_proxy;
        const char *ifname = "org.freedesktop.DeviceKit.Disks.Device";

        props = g_new0 (DeviceProperties, 1);

	prop_proxy = dbus_g_proxy_new_for_name (bus,
                                                "org.freedesktop.DeviceKit.Disks",
                                                object_path,
                                                "org.freedesktop.DBus.Properties");
        error = NULL;
        if (!dbus_g_proxy_call (prop_proxy,
                                "GetAll",
                                &error,
                                G_TYPE_STRING,
                                ifname,
                                G_TYPE_INVALID,
                                dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
                                &hash_table,
                                G_TYPE_INVALID)) {
                g_warning ("Couldn't call GetAll() to get properties for %s: %s", object_path, error->message);
                g_error_free (error);
                goto out;
        }

        g_hash_table_foreach (hash_table, (GHFunc) collect_props, props);

        g_hash_table_unref (hash_table);

out:
        g_object_unref (prop_proxy);
        return props;
}

static void
device_properties_free (DeviceProperties *props)
{
        g_free (props->native_path);
        g_free (props->device_file);
        g_strfreev (props->device_file_by_id);
        g_strfreev (props->device_file_by_path);
        g_free (props->device_mount_path);
        g_free (props->job_id);
        g_free (props->job_cur_task_id);
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
        g_array_free (props->partition_table_offsets, TRUE);
        g_array_free (props->partition_table_sizes, TRUE);
        g_free (props->crypto_cleartext_slave);
        g_free (props->drive_model);
        g_free (props->drive_vendor);
        g_free (props->drive_revision);
        g_free (props->drive_serial);
        g_free (props);
}

/* --- SUCKY CODE END --- */

static gboolean
do_monitor (void)
{
        char *cookie;
        GError *error;

        g_print ("Monitoring activity from the disks daemon. Press Ctrl+C to cancel.\n");

        error = NULL;
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
        dbus_g_proxy_connect_signal (disks_proxy, "DeviceChanged",
                                     G_CALLBACK (device_changed_signal_handler), NULL, NULL);
        dbus_g_proxy_connect_signal (disks_proxy, "DeviceJobChanged",
                                     G_CALLBACK (device_job_changed_signal_handler), NULL, NULL);
        g_main_loop_run (loop);

out:
        return FALSE;
}

static void
do_show_info (const char *object_path)
{
        unsigned int n;
        DeviceProperties *props;

        props = device_properties_get (bus, object_path);
        g_print ("Showing information for %s\n", object_path);
        g_print ("  native-path:   %s\n", props->native_path);
        g_print ("  device-file:   %s\n", props->device_file);
        for (n = 0; props->device_file_by_id[n] != NULL; n++)
                g_print ("    by-id:       %s\n", (char *) props->device_file_by_id[n]);
        for (n = 0; props->device_file_by_path[n] != NULL; n++)
                g_print ("    by-path:     %s\n", (char *) props->device_file_by_path[n]);
        g_print ("  removable:     %d\n", props->device_is_removable);
        g_print ("  has media:     %d\n", props->device_is_media_available);
        g_print ("  is mounted:    %d\n", props->device_is_mounted);
        g_print ("  mount path:    %s\n", props->device_mount_path);
        g_print ("  size:          %lld\n", props->device_size);
        g_print ("  block size:    %lld\n", props->device_block_size);

        print_job (props->job_in_progress,
                   props->job_id,
                   props->job_is_cancellable,
                   props->job_num_tasks,
                   props->job_cur_task,
                   props->job_cur_task_id,
                   props->job_cur_task_percentage);
        g_print ("  usage:         %s\n", props->id_usage);
        g_print ("  type:          %s\n", props->id_type);
        g_print ("  version:       %s\n", props->id_version);
        g_print ("  uuid:          %s\n", props->id_uuid);
        g_print ("  label:         %s\n", props->id_label);
        if (props->device_is_crypto_cleartext) {
                g_print ("  cleartext crypto device:\n");
                g_print ("    backed by:   %s\n", props->crypto_cleartext_slave);
        }
        if (props->device_is_partition_table) {
                g_print ("  partition table:\n");
                g_print ("    scheme:      %s\n", props->partition_table_scheme);
                g_print ("    count:       %d\n", props->partition_table_count);
                g_print ("    max number:  %d\n", props->partition_table_max_number);
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
        if (props->device_is_drive) {
                g_print ("  drive:\n");
                g_print ("    vendor:      %s\n", props->drive_vendor);
                g_print ("    model:       %s\n", props->drive_model);
                g_print ("    revision:    %s\n", props->drive_revision);
                g_print ("    serial:      %s\n", props->drive_serial);
        }
        device_properties_free (props);
}

int
main (int argc, char **argv)
{
        int                  ret;
        GOptionContext      *context;
        GError              *error = NULL;
        unsigned int         n;
        static GOptionEntry  entries []     = {
                { "inhibit", 0, 0, G_OPTION_ARG_NONE, &opt_inhibit, "Inhibit the disks daemon from exiting", NULL },
                { "enumerate", 0, 0, G_OPTION_ARG_NONE, &opt_enumerate, "Enumerate objects paths for devices", NULL },
                { "monitor", 0, 0, G_OPTION_ARG_NONE, &opt_monitor, "Monitor activity from the disk daemon", NULL },
                { "monitor-detail", 0, 0, G_OPTION_ARG_NONE, &opt_monitor_detail, "Monitor with detail", NULL },
                { "show-info", 0, 0, G_OPTION_ARG_STRING, &opt_show_info, "Show information about object path", NULL },

                { "mount", 0, 0, G_OPTION_ARG_STRING, &opt_mount, "Mount the device given by the object path", NULL },
                { "mount-fstype", 0, 0, G_OPTION_ARG_STRING, &opt_mount_fstype, "Specify file system type", NULL },
                { "mount-options", 0, 0, G_OPTION_ARG_STRING, &opt_mount_options, "Mount options separated by comma", NULL },

                { "unmount", 0, 0, G_OPTION_ARG_STRING, &opt_unmount, "Unmount the device given by the object path", NULL },
                { "unmount-options", 0, 0, G_OPTION_ARG_STRING, &opt_unmount_options, "Unmount options separated by comma", NULL },

                { "erase", 0, 0, G_OPTION_ARG_STRING, &opt_erase, "Erase a device", NULL },
                { "erase-options", 0, 0, G_OPTION_ARG_STRING, &opt_erase_options, "Erase options", NULL },

                { "create-fs", 0, 0, G_OPTION_ARG_STRING, &opt_create_fs, "Create a file system", NULL },
                { "create-fs-type", 0, 0, G_OPTION_ARG_STRING, &opt_create_fs_type, "File system type to create", NULL },
                { "create-fs-options", 0, 0, G_OPTION_ARG_STRING, &opt_create_fs_options, "File system create options", NULL },
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

        dbus_g_object_register_marshaller (
                devkit_disks_marshal_VOID__STRING_BOOLEAN_STRING_BOOLEAN_INT_INT_STRING_DOUBLE,
                G_TYPE_NONE,
                G_TYPE_STRING,
                G_TYPE_BOOLEAN,
                G_TYPE_STRING,
                G_TYPE_BOOLEAN,
                G_TYPE_INT,
                G_TYPE_INT,
                G_TYPE_STRING,
                G_TYPE_DOUBLE,
                G_TYPE_INVALID);

	disks_proxy = dbus_g_proxy_new_for_name (bus,
                                                 "org.freedesktop.DeviceKit.Disks",
                                                 "/",
                                                 "org.freedesktop.DeviceKit.Disks");
        dbus_g_proxy_add_signal (disks_proxy, "DeviceAdded", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (disks_proxy, "DeviceRemoved", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (disks_proxy, "DeviceChanged", G_TYPE_STRING, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (disks_proxy,
                                 "DeviceJobChanged",
                                 G_TYPE_STRING,
                                 G_TYPE_BOOLEAN,
                                 G_TYPE_STRING,
                                 G_TYPE_BOOLEAN,
                                 G_TYPE_INT,
                                 G_TYPE_INT,
                                 G_TYPE_STRING,
                                 G_TYPE_DOUBLE,
                                 G_TYPE_INVALID);

        if (opt_inhibit) {
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
        } else if (opt_enumerate) {
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
        } else if (opt_monitor || opt_monitor_detail) {
                if (!do_monitor ())
                        goto out;
        } else if (opt_show_info != NULL) {
                do_show_info (opt_show_info);
        } else if (opt_mount != NULL) {
                do_mount (opt_mount, opt_mount_fstype, opt_mount_options);
        } else if (opt_unmount != NULL) {
                do_unmount (opt_unmount, opt_unmount_options);
        } else if (opt_erase != NULL) {
                do_erase (opt_erase, opt_erase_options);
        } else if (opt_create_fs != NULL && opt_create_fs_type != NULL) {
                do_create_fs (opt_create_fs, opt_create_fs_type, opt_create_fs_options);
        }

        ret = 0;

out:
        if (disks_proxy != NULL)
                g_object_unref (disks_proxy);
        if (bus != NULL)
                dbus_g_connection_unref (bus);

        return ret;
}
