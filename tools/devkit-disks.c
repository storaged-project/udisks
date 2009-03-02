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
#include <sys/wait.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <locale.h>

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

static gboolean      opt_enumerate         = FALSE;
static gboolean      opt_monitor           = FALSE;
static gboolean      opt_monitor_detail    = FALSE;
static char         *opt_show_info         = NULL;
static char         *opt_inhibit_polling   = NULL;
static gboolean      opt_inhibit           = FALSE;
static gboolean      opt_inhibit_all_polling = FALSE;
static char         *opt_mount             = NULL;
static char         *opt_mount_fstype      = NULL;
static char         *opt_mount_options     = NULL;
static char         *opt_unmount           = NULL;
static char         *opt_unmount_options   = NULL;

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
        if (!org_freedesktop_DeviceKit_Disks_Device_filesystem_mount (proxy,
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
        if (!org_freedesktop_DeviceKit_Disks_Device_filesystem_unmount (proxy,
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
           uid_t       job_initiated_by_uid,
           gboolean    job_is_cancellable,
           int         job_num_tasks,
           int         job_cur_task,
           const char *job_cur_task_id,
           double      job_cur_task_percentage)
{
        if (job_in_progress) {
                if (job_num_tasks > 0) {
                        g_print ("  job underway:            %s: %d/%d tasks (%s",
                                 job_id,
                                 job_cur_task + 1,
                                 job_num_tasks,
                                 job_cur_task_id);
                        if (job_cur_task_percentage >= 0)
                                g_print (" @ %3.0lf%%", job_cur_task_percentage);
                        if (job_is_cancellable)
                                g_print (", cancellable");
                        g_print (", initiated by uid %d", job_initiated_by_uid);
                        g_print (")\n");
                } else {
                        g_print ("  job underway:            %s: unknown progress", job_id);
                        if (job_is_cancellable)
                                g_print (", cancellable");
                        g_print (", initiated by uid %d", job_initiated_by_uid);
                        g_print ("\n");
                }
        } else {
                g_print ("  job underway:            no\n");
        }
}

static void
device_job_changed_signal_handler (DBusGProxy *proxy,
                                   const char *object_path,
                                   gboolean    job_in_progress,
                                   const char *job_id,
                                   guint32     job_initiated_by_uid,
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
                     job_initiated_by_uid,
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

#define SMART_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_STRING,   \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_INT,      \
                                                        G_TYPE_STRING,   \
                                                        G_TYPE_INVALID))

#define HISTORICAL_SMART_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                                   G_TYPE_UINT64, \
                                                                   G_TYPE_DOUBLE, \
                                                                   G_TYPE_UINT64, \
                                                                   G_TYPE_STRING, \
                                                                   G_TYPE_BOOLEAN, \
                                                                   dbus_g_type_get_collection ("GPtrArray", SMART_DATA_STRUCT_TYPE), \
                                                                   G_TYPE_INVALID))

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
        gboolean device_is_system_internal;
        gboolean device_is_partition;
        gboolean device_is_partition_table;
        gboolean device_is_removable;
        gboolean device_is_media_available;
        gboolean device_is_media_change_detected;
        gboolean device_is_media_change_detection_inhibitable;
        gboolean device_is_media_change_detection_inhibited;
        gboolean device_is_read_only;
        gboolean device_is_drive;
        gboolean device_is_optical_disc;
        gboolean device_is_luks;
        gboolean device_is_luks_cleartext;
        gboolean device_is_mounted;
        gboolean device_is_busy;
        gboolean device_is_linux_md_component;
        gboolean device_is_linux_md;
        char    *device_mount_path;
        uid_t    device_mounted_by_uid;
        char    *device_presentation_name;
        char    *device_presentation_icon_name;
        guint64  device_size;
        guint64  device_block_size;

        gboolean job_in_progress;
        char    *job_id;
        uid_t    job_initiated_by_uid;
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

        char    *luks_holder;

        char    *luks_cleartext_slave;
        uid_t    luks_cleartext_unlocked_by_uid;

        char    *drive_vendor;
        char    *drive_model;
        char    *drive_revision;
        char    *drive_serial;
        char    *drive_connection_interface;
        guint64  drive_connection_speed;
        char   **drive_media_compatibility;
        char    *drive_media;
        gboolean drive_is_media_ejectable;
        gboolean drive_requires_eject;

        gboolean optical_disc_is_blank;
        gboolean optical_disc_is_appendable;
        gboolean optical_disc_is_closed;
        guint optical_disc_num_tracks;
        guint optical_disc_num_audio_tracks;
        guint optical_disc_num_sessions;

        gboolean               drive_smart_is_capable;
        gboolean               drive_smart_is_enabled;
        guint64                drive_smart_time_collected;
        gboolean               drive_smart_is_failing;
        double                 drive_smart_temperature;
        guint64                drive_smart_time_powered_on;
        char                  *drive_smart_last_self_test_result;
        GValue                 drive_smart_attributes;

        char    *linux_md_component_level;
        int      linux_md_component_num_raid_devices;
        char    *linux_md_component_uuid;
        char    *linux_md_component_home_host;
        char    *linux_md_component_name;
        char    *linux_md_component_version;
        char    *linux_md_component_holder;
        char   **linux_md_component_state;

        char    *linux_md_state;
        char    *linux_md_level;
        int      linux_md_num_raid_devices;
        char    *linux_md_uuid;
        char    *linux_md_home_host;
        char    *linux_md_name;
        char    *linux_md_version;
        char   **linux_md_slaves;
        gboolean linux_md_is_degraded;
        char    *linux_md_sync_action;
        double   linux_md_sync_percentage;
        guint64  linux_md_sync_speed;
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
        else if (strcmp (key, "device-is-system-internal") == 0)
                props->device_is_system_internal = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-partition") == 0)
                props->device_is_partition = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-partition-table") == 0)
                props->device_is_partition_table = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-removable") == 0)
                props->device_is_removable = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-available") == 0)
                props->device_is_media_available = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-change-detected") == 0)
                props->device_is_media_change_detected = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-change-detection-inhibitable") == 0)
                props->device_is_media_change_detection_inhibitable = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-media-change-detection-inhibited") == 0)
                props->device_is_media_change_detection_inhibited = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-read-only") == 0)
                props->device_is_read_only = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-drive") == 0)
                props->device_is_drive = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-optical-disc") == 0)
                props->device_is_optical_disc = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-luks") == 0)
                props->device_is_luks = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-luks-cleartext") == 0)
                props->device_is_luks_cleartext = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-linux-md-component") == 0)
                props->device_is_linux_md_component = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-linux-md") == 0)
                props->device_is_linux_md = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-mounted") == 0)
                props->device_is_mounted = g_value_get_boolean (value);
        else if (strcmp (key, "device-is-busy") == 0)
                props->device_is_busy = g_value_get_boolean (value);
        else if (strcmp (key, "device-mount-path") == 0)
                props->device_mount_path = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-mounted-by-uid") == 0)
                props->device_mounted_by_uid = g_value_get_uint (value);
        else if (strcmp (key, "device-presentation-name") == 0)
                props->device_presentation_name = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-presentation-icon-name") == 0)
                props->device_presentation_icon_name = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "device-size") == 0)
                props->device_size = g_value_get_uint64 (value);
        else if (strcmp (key, "device-block-size") == 0)
                props->device_block_size = g_value_get_uint64 (value);

        else if (strcmp (key, "job-in-progress") == 0)
                props->job_in_progress = g_value_get_boolean (value);
        else if (strcmp (key, "job-id") == 0)
                props->job_id = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "job-initiated-by-uid") == 0)
                props->job_initiated_by_uid = g_value_get_uint (value);
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

        else if (strcmp (key, "luks-holder") == 0)
                props->luks_holder = g_strdup (g_value_get_boxed (value));

        else if (strcmp (key, "luks-cleartext-slave") == 0)
                props->luks_cleartext_slave = g_strdup (g_value_get_boxed (value));
        else if (strcmp (key, "luks-cleartext-unlocked-by-uid") == 0)
                props->luks_cleartext_unlocked_by_uid = g_value_get_uint (value);

        else if (strcmp (key, "drive-vendor") == 0)
                props->drive_vendor = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-model") == 0)
                props->drive_model = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-revision") == 0)
                props->drive_revision = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-serial") == 0)
                props->drive_serial = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-connection-interface") == 0)
                props->drive_connection_interface = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-connection-speed") == 0)
                props->drive_connection_speed = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-media-compatibility") == 0)
                props->drive_media_compatibility = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "drive-media") == 0)
                props->drive_media = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-is-media-ejectable") == 0)
                props->drive_is_media_ejectable = g_value_get_boolean (value);
        else if (strcmp (key, "drive-requires-eject") == 0)
                props->drive_requires_eject = g_value_get_boolean (value);

        else if (strcmp (key, "optical-disc-is-blank") == 0)
                props->optical_disc_is_blank = g_value_get_boolean (value);
        else if (strcmp (key, "optical-disc-is-appendable") == 0)
                props->optical_disc_is_appendable = g_value_get_boolean (value);
        else if (strcmp (key, "optical-disc-is-closed") == 0)
                props->optical_disc_is_closed = g_value_get_boolean (value);
        else if (strcmp (key, "optical-disc-num-tracks") == 0)
                props->optical_disc_num_tracks = g_value_get_uint (value);
        else if (strcmp (key, "optical-disc-num-audio-tracks") == 0)
                props->optical_disc_num_audio_tracks = g_value_get_uint (value);
        else if (strcmp (key, "optical-disc-num-sessions") == 0)
                props->optical_disc_num_sessions = g_value_get_uint (value);

        else if (strcmp (key, "drive-smart-is-capable") == 0)
                props->drive_smart_is_capable = g_value_get_boolean (value);
        else if (strcmp (key, "drive-smart-is-enabled") == 0)
                props->drive_smart_is_enabled = g_value_get_boolean (value);
        else if (strcmp (key, "drive-smart-time-collected") == 0)
                props->drive_smart_time_collected = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-smart-is-failing") == 0)
                props->drive_smart_is_failing = g_value_get_boolean (value);
        else if (strcmp (key, "drive-smart-temperature") == 0)
                props->drive_smart_temperature = g_value_get_double (value);
        else if (strcmp (key, "drive-smart-time-powered-on") == 0)
                props->drive_smart_time_powered_on = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-smart-last-self-test-result") == 0)
                props->drive_smart_last_self_test_result = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "drive-smart-attributes") == 0) {
                g_value_copy (value, &(props->drive_smart_attributes));
        }

        else if (strcmp (key, "linux-md-component-level") == 0)
                props->linux_md_component_level = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-num-raid-devices") == 0)
                props->linux_md_component_num_raid_devices = g_value_get_int (value);
        else if (strcmp (key, "linux-md-component-uuid") == 0)
                props->linux_md_component_uuid = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-home-host") == 0)
                props->linux_md_component_home_host = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-name") == 0)
                props->linux_md_component_name = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-version") == 0)
                props->linux_md_component_version = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-component-holder") == 0)
                props->linux_md_component_holder = g_strdup (g_value_get_boxed (value));
        else if (strcmp (key, "linux-md-component-state") == 0)
                props->linux_md_component_state = g_strdupv (g_value_get_boxed (value));

        else if (strcmp (key, "linux-md-state") == 0)
                props->linux_md_state = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-level") == 0)
                props->linux_md_level = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-num-raid-devices") == 0)
                props->linux_md_num_raid_devices = g_value_get_int (value);
        else if (strcmp (key, "linux-md-uuid") == 0)
                props->linux_md_uuid = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-home-host") == 0)
                props->linux_md_home_host = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-name") == 0)
                props->linux_md_name = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-version") == 0)
                props->linux_md_version = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-slaves") == 0) {
                int n;
                GPtrArray *object_paths;

                object_paths = g_value_get_boxed (value);

                props->linux_md_slaves = g_new0 (char *, object_paths->len + 1);
                for (n = 0; n < (int) object_paths->len; n++)
                        props->linux_md_slaves[n] = g_strdup (object_paths->pdata[n]);
                props->linux_md_slaves[n] = NULL;
        }
        else if (strcmp (key, "linux-md-is-degraded") == 0)
                props->linux_md_is_degraded = g_value_get_boolean (value);
        else if (strcmp (key, "linux-md-sync-action") == 0)
                props->linux_md_sync_action = g_strdup (g_value_get_string (value));
        else if (strcmp (key, "linux-md-sync-percentage") == 0)
                props->linux_md_sync_percentage = g_value_get_double (value);
        else if (strcmp (key, "linux-md-sync-speed") == 0)
                props->linux_md_sync_speed = g_value_get_uint64 (value);

        else
                handled = FALSE;

        if (!handled)
                g_warning ("unhandled property '%s'", key);
}

static void
device_properties_free (DeviceProperties *props)
{
        g_free (props->native_path);
        g_free (props->device_file);
        g_strfreev (props->device_file_by_id);
        g_strfreev (props->device_file_by_path);
        g_free (props->device_mount_path);
        g_free (props->device_presentation_name);
        g_free (props->device_presentation_icon_name);
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
        if (props->partition_table_offsets != NULL)
                g_array_free (props->partition_table_offsets, TRUE);
        if (props->partition_table_sizes != NULL)
                g_array_free (props->partition_table_sizes, TRUE);
        g_free (props->luks_holder);
        g_free (props->luks_cleartext_slave);
        g_free (props->drive_model);
        g_free (props->drive_vendor);
        g_free (props->drive_revision);
        g_free (props->drive_serial);
        g_free (props->drive_connection_interface);
        g_strfreev (props->drive_media_compatibility);
        g_free (props->drive_media);
        g_free (props->drive_smart_last_self_test_result);
        g_value_unset (&(props->drive_smart_attributes));
        g_free (props->linux_md_component_level);
        g_free (props->linux_md_component_uuid);
        g_free (props->linux_md_component_home_host);
        g_free (props->linux_md_component_name);
        g_free (props->linux_md_component_version);
        g_free (props->linux_md_component_holder);
        g_strfreev (props->linux_md_component_state);

        g_free (props->linux_md_state);
        g_free (props->linux_md_level);
        g_free (props->linux_md_uuid);
        g_free (props->linux_md_home_host);
        g_free (props->linux_md_name);
        g_free (props->linux_md_version);
        g_strfreev (props->linux_md_slaves);
        g_free (props->linux_md_sync_action);
        g_free (props);
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
        g_value_init (&(props->drive_smart_attributes),
                      dbus_g_type_get_collection ("GPtrArray", SMART_DATA_STRUCT_TYPE));

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

                device_properties_free (props);
                props = NULL;
                goto out;
        }

        g_hash_table_foreach (hash_table, (GHFunc) collect_props, props);

        g_hash_table_unref (hash_table);

out:
        g_object_unref (prop_proxy);
        return props;
}

/* --- SUCKY CODE END --- */

static gboolean
do_monitor (void)
{
        GError *error;

        g_print ("Monitoring activity from the disks daemon. Press Ctrl+C to cancel.\n");

        error = NULL;

        dbus_g_proxy_connect_signal (disks_proxy, "DeviceAdded",
                                     G_CALLBACK (device_added_signal_handler), NULL, NULL);
        dbus_g_proxy_connect_signal (disks_proxy, "DeviceRemoved",
                                     G_CALLBACK (device_removed_signal_handler), NULL, NULL);
        dbus_g_proxy_connect_signal (disks_proxy, "DeviceChanged",
                                     G_CALLBACK (device_changed_signal_handler), NULL, NULL);
        dbus_g_proxy_connect_signal (disks_proxy, "DeviceJobChanged",
                                     G_CALLBACK (device_job_changed_signal_handler), NULL, NULL);
        g_main_loop_run (loop);

        return FALSE;
}

static void
do_show_info (const char *object_path)
{
        guint n;
        guint m;
        DeviceProperties *props;

        props = device_properties_get (bus, object_path);
        if (props == NULL)
                return;

        g_print ("Showing information for %s\n", object_path);
        g_print ("  native-path:             %s\n", props->native_path);
        g_print ("  device-file:             %s\n", props->device_file);
        for (n = 0; props->device_file_by_id[n] != NULL; n++)
                g_print ("    by-id:                 %s\n", (char *) props->device_file_by_id[n]);
        for (n = 0; props->device_file_by_path[n] != NULL; n++)
                g_print ("    by-path:               %s\n", (char *) props->device_file_by_path[n]);
        g_print ("  system internal:         %d\n", props->device_is_system_internal);
        g_print ("  removable:               %d\n", props->device_is_removable);
        g_print ("  has media:               %d\n", props->device_is_media_available);
        g_print ("    detects change:        %d\n", props->device_is_media_change_detected);
        g_print ("    detection inhibitable: %d\n", props->device_is_media_change_detection_inhibitable);
        g_print ("    detection inhibited:   %d\n", props->device_is_media_change_detection_inhibited);
        g_print ("  is read only:            %d\n", props->device_is_read_only);
        g_print ("  is mounted:              %d\n", props->device_is_mounted);
        g_print ("  is busy:                 %d\n", props->device_is_busy);
        g_print ("  mount path:              %s\n", props->device_mount_path);
        g_print ("  mounted by uid:          %d\n", props->device_mounted_by_uid);
        g_print ("  presentation name:       %s\n", props->device_presentation_name);
        g_print ("  presentation icon:       %s\n", props->device_presentation_icon_name);
        g_print ("  size:                    %" G_GUINT64_FORMAT "\n", props->device_size);
        g_print ("  block size:              %" G_GUINT64_FORMAT "\n", props->device_block_size);

        print_job (props->job_in_progress,
                   props->job_id,
                   props->job_initiated_by_uid,
                   props->job_is_cancellable,
                   props->job_num_tasks,
                   props->job_cur_task,
                   props->job_cur_task_id,
                   props->job_cur_task_percentage);
        g_print ("  usage:                   %s\n", props->id_usage);
        g_print ("  type:                    %s\n", props->id_type);
        g_print ("  version:                 %s\n", props->id_version);
        g_print ("  uuid:                    %s\n", props->id_uuid);
        g_print ("  label:                   %s\n", props->id_label);
        if (props->device_is_linux_md_component) {
                g_print ("  linux md component:\n");
                g_print ("    RAID level:            %s\n", props->linux_md_component_level);
                g_print ("    num comp:              %d\n", props->linux_md_component_num_raid_devices);
                g_print ("    uuid:                  %s\n", props->linux_md_component_uuid);
                g_print ("    home host:             %s\n", props->linux_md_component_home_host);
                g_print ("    name:                  %s\n", props->linux_md_component_name);
                g_print ("    version:               %s\n", props->linux_md_component_version);
                g_print ("    holder:                %s\n",
                         g_strcmp0 (props->linux_md_component_holder, "/") == 0 ? "(none)" : props->linux_md_component_holder);
                g_print ("    state:                 ");
                for (n = 0;
                     props->linux_md_component_state != NULL && props->linux_md_component_state[n] != NULL;
                     n++) {
                        if (n > 0)
                                g_print (", ");
                        g_print ("%s", props->linux_md_component_state[n]);
                }
                g_print ("\n");
        }
        if (props->device_is_linux_md) {
                g_print ("  linux md:\n");
                g_print ("    state:                 %s\n", props->linux_md_state);
                g_print ("    RAID level:            %s\n", props->linux_md_level);
                g_print ("    uuid:                  %s\n", props->linux_md_uuid);
                g_print ("    home host:             %s\n", props->linux_md_home_host);
                g_print ("    name:                  %s\n", props->linux_md_name);
                g_print ("    num comp:              %d\n", props->linux_md_num_raid_devices);
                g_print ("    version:               %s\n", props->linux_md_version);
                g_print ("    degraded:              %d\n", props->linux_md_is_degraded);
                g_print ("    sync action:           %s\n", props->linux_md_sync_action);
                if (strcmp (props->linux_md_sync_action, "idle") != 0) {
                        g_print ("      complete:            %3.01f%%\n", props->linux_md_sync_percentage);
                        g_print ("      speed:               %" G_GINT64_FORMAT " bytes/sec\n", props->linux_md_sync_speed);
                }
                g_print ("    slaves:\n");
                for (n = 0; props->linux_md_slaves[n] != NULL; n++)
                        g_print ("                  %s\n", props->linux_md_slaves[n]);
        }
        if (props->device_is_luks) {
                g_print ("  luks device:\n");
                g_print ("    holder:                %s\n", props->luks_holder);
        }
        if (props->device_is_luks_cleartext) {
                g_print ("  cleartext luks device:\n");
                g_print ("    backed by:             %s\n", props->luks_cleartext_slave);
                g_print ("    unlocked by:           uid %d\n", props->luks_cleartext_unlocked_by_uid);
        }
        if (props->device_is_partition_table) {
                g_print ("  partition table:\n");
                g_print ("    scheme:                %s\n", props->partition_table_scheme);
                g_print ("    count:                 %d\n", props->partition_table_count);
                g_print ("    max number:            %d\n", props->partition_table_max_number);
        }
        if (props->device_is_partition) {
                g_print ("  partition:\n");
                g_print ("    part of:               %s\n", props->partition_slave);
                g_print ("    scheme:                %s\n", props->partition_scheme);
                g_print ("    number:                %d\n", props->partition_number);
                g_print ("    type:                  %s\n", props->partition_type);
                g_print ("    flags:                ");
                for (n = 0; props->partition_flags[n] != NULL; n++)
                        g_print (" %s", (char *) props->partition_flags[n]);
                g_print ("\n");
                g_print ("    offset:                %" G_GINT64_FORMAT "\n", props->partition_offset);
                g_print ("    size:                  %" G_GINT64_FORMAT "\n", props->partition_size);
                g_print ("    label:                 %s\n", props->partition_label);
                g_print ("    uuid:                  %s\n", props->partition_uuid);
        }
        if (props->device_is_optical_disc) {
                g_print ("  optical disc:\n");
                g_print ("    blank:                 %d\n", props->optical_disc_is_blank);
                g_print ("    appendable:            %d\n", props->optical_disc_is_appendable);
                g_print ("    closed:                %d\n", props->optical_disc_is_closed);
                g_print ("    num tracks:            %d\n", props->optical_disc_num_tracks);
                g_print ("    num audio tracks:      %d\n", props->optical_disc_num_audio_tracks);
                g_print ("    num sessions:          %d\n", props->optical_disc_num_sessions);
        }
        if (props->device_is_drive) {
                g_print ("  drive:\n");
                g_print ("    vendor:                %s\n", props->drive_vendor);
                g_print ("    model:                 %s\n", props->drive_model);
                g_print ("    revision:              %s\n", props->drive_revision);
                g_print ("    serial:                %s\n", props->drive_serial);
                g_print ("    ejectable:             %d\n", props->drive_is_media_ejectable);
                g_print ("    require eject:         %d\n", props->drive_requires_eject);
                g_print ("    media:                 %s\n", props->drive_media);
                g_print ("      compat:             ");
                for (n = 0; props->drive_media_compatibility[n] != NULL; n++)
                        g_print (" %s", (char *) props->drive_media_compatibility[n]);
                g_print ("\n");
                if (props->drive_connection_interface == NULL || strlen (props->drive_connection_interface) == 0)
                        g_print ("    interface:     (unknown)\n");
                else
                        g_print ("    interface:             %s\n", props->drive_connection_interface);
                if (props->drive_connection_speed == 0)
                        g_print ("    if speed:              (unknown)\n");
                else
                        g_print ("    if speed:              %" G_GINT64_FORMAT " bits/s\n", props->drive_connection_speed);

                if (!props->drive_smart_is_capable) {
                        g_print ("    S.M.A.R.T.:            not capable\n");
                } else if (props->drive_smart_time_collected == 0) {
                        g_print ("    S.M.A.R.T.:            not collected\n");
                } else {
                        struct tm *time_tm;
                        time_t time;
                        char time_buf[256];

                        time = (time_t) props->drive_smart_time_collected;
                        time_tm = localtime (&time);
                        strftime (time_buf, sizeof time_buf, "%c", time_tm);

                        g_print ("    S.M.A.R.T.:            Information collected at %s\n", time_buf);
                        if (!props->drive_smart_is_capable) {
                                g_print ("              not capable\n");
                        } else if (!props->drive_smart_is_enabled) {
                                g_print ("              not enabled\n");
                        } else {

                                g_print ("      assessment:          %s\n",
                                         props->drive_smart_is_failing ? "FAILING" : "Passed");
                                g_print ("      temperature:         %g\302\260 C / %g\302\260 F\n",
                                         props->drive_smart_temperature,
                                         9 * props->drive_smart_temperature / 5 + 32);
                                g_print ("      powered on:          %" G_GUINT64_FORMAT " hours\n", props->drive_smart_time_powered_on / 3600);
                                //g_print ("      196  Reallocated_Event_Count      0x0032 100   100           0 443023360\n",
                                g_print ("      =========================================================================\n");
                                g_print ("      Id   Description                   Flags Value Worst Threshold       Raw\n");
                                g_print ("      =========================================================================\n");
                                GPtrArray *p;
                                p = g_value_get_boxed (&(props->drive_smart_attributes));
                                for (m = 0; m < p->len; m++) {
                                        gint attr_id;
                                        gchar *attr_desc;
                                        gint attr_flags;
                                        gint attr_value;
                                        gint attr_worst;
                                        gint attr_threshold;
                                        gchar *attr_raw;
                                        GValue elem = {0};

                                        g_value_init (&elem, SMART_DATA_STRUCT_TYPE);
                                        g_value_set_static_boxed (&elem, p->pdata[m]);

                                        dbus_g_type_struct_get (&elem,
                                                                0, &(attr_id),
                                                                1, &(attr_desc),
                                                                2, &(attr_flags),
                                                                3, &(attr_value),
                                                                4, &(attr_worst),
                                                                5, &(attr_threshold),
                                                                6, &(attr_raw),
                                                                G_MAXUINT);

                                        g_print ("      %3d  %-28s 0x%04x %5d %5d %9d %s\n",
                                                 attr_id,
                                                 attr_desc,
                                                 attr_flags,
                                                 attr_value,
                                                 attr_worst,
                                                 attr_threshold,
                                                 attr_raw);

                                        g_free (attr_desc);
                                        g_free (attr_raw);

                                        g_value_unset (&elem);
                                }
                        }
                }

        }
        device_properties_free (props);
}

/* ---------------------------------------------------------------------------------------------------- */

static gint
do_inhibit_polling (const char *object_path,
                    gint         argc,
                    gchar       *argv[])
{
        char *cookie;
        DBusGProxy *proxy;
        GError *error;
        char **options;
        gint ret;

        options = NULL;
        cookie = NULL;
        ret = 127;

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           object_path,
                                           "org.freedesktop.DeviceKit.Disks.Device");

        error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_Device_drive_inhibit_polling (proxy,
                                                                           (const char **) options,
                                                                           &cookie,
                                                                           &error)) {
                g_print ("Inhibit polling failed: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        if (argc == 0) {
                g_print ("Inhibiting polling on %s. Press Ctrl+C to exit.\n", object_path);
                while (TRUE)
                        sleep (100000000);
        } else {
                GError *error;
                gint exit_status;

                error = NULL;
                if (!g_spawn_sync (NULL,  /* working dir */
                                   argv,
                                   NULL,  /* envp */
                                   G_SPAWN_SEARCH_PATH,
                                   NULL, /* child_setup */
                                   NULL, /* user_data */
                                   NULL, /* standard_output */
                                   NULL, /* standard_error */
                                   &exit_status, /* exit_status */
                                   &error)) {
                        g_printerr ("Error launching program: %s\n", error->message);
                        g_error_free (error);
                        ret = 126;
                        goto out;
                }

                if (WIFEXITED (exit_status))
                        ret = WEXITSTATUS (exit_status);
                else
                        ret = 125;
        }

out:
        g_free (cookie);
        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gint
do_inhibit_all_polling (gint         argc,
                        gchar       *argv[])
{
        char *cookie;
        DBusGProxy *proxy;
        GError *error;
        char **options;
        gint ret;

        options = NULL;
        cookie = NULL;
        ret = 127;

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           "/",
                                           "org.freedesktop.DeviceKit.Disks");

        error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_drive_inhibit_all_polling (proxy,
                                                                        (const char **) options,
                                                                        &cookie,
                                                                        &error)) {
                g_print ("Inhibit all polling failed: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        if (argc == 0) {
                g_print ("Inhibiting polling on all devices. Press Ctrl+C to exit.\n");
                while (TRUE)
                        sleep (100000000);
        } else {
                GError *error;
                gint exit_status;

                error = NULL;
                if (!g_spawn_sync (NULL,  /* working dir */
                                   argv,
                                   NULL,  /* envp */
                                   G_SPAWN_SEARCH_PATH,
                                   NULL, /* child_setup */
                                   NULL, /* user_data */
                                   NULL, /* standard_output */
                                   NULL, /* standard_error */
                                   &exit_status, /* exit_status */
                                   &error)) {
                        g_printerr ("Error launching program: %s\n", error->message);
                        g_error_free (error);
                        ret = 126;
                        goto out;
                }

                if (WIFEXITED (exit_status))
                        ret = WEXITSTATUS (exit_status);
                else
                        ret = 125;
        }

out:
        g_free (cookie);
        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gint
do_inhibit (gint         argc,
            gchar       *argv[])
{
        char *cookie;
        DBusGProxy *proxy;
        GError *error;
        gint ret;

        cookie = NULL;
        ret = 127;

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           "/",
                                           "org.freedesktop.DeviceKit.Disks");

        error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_inhibit (proxy,
                                                      &cookie,
                                                      &error)) {
                g_print ("Inhibit all polling failed: %s\n", error->message);
                g_error_free (error);
                goto out;
        }

        if (argc == 0) {
                g_print ("Inhibiting the daemon. Press Ctrl+C to exit.\n");
                while (TRUE)
                        sleep (100000000);
        } else {
                GError *error;
                gint exit_status;

                error = NULL;
                if (!g_spawn_sync (NULL,  /* working dir */
                                   argv,
                                   NULL,  /* envp */
                                   G_SPAWN_SEARCH_PATH,
                                   NULL, /* child_setup */
                                   NULL, /* user_data */
                                   NULL, /* standard_output */
                                   NULL, /* standard_error */
                                   &exit_status, /* exit_status */
                                   &error)) {
                        g_printerr ("Error launching program: %s\n", error->message);
                        g_error_free (error);
                        ret = 126;
                        goto out;
                }

                if (WIFEXITED (exit_status))
                        ret = WEXITSTATUS (exit_status);
                else
                        ret = 125;
        }

out:
        g_free (cookie);
        return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
        int                  ret;
        GOptionContext      *context;
        GError              *error = NULL;
        unsigned int         n;
        static GOptionEntry  entries []     = {
                { "enumerate", 0, 0, G_OPTION_ARG_NONE, &opt_enumerate, "Enumerate objects paths for devices", NULL },
                { "monitor", 0, 0, G_OPTION_ARG_NONE, &opt_monitor, "Monitor activity from the disk daemon", NULL },
                { "monitor-detail", 0, 0, G_OPTION_ARG_NONE, &opt_monitor_detail, "Monitor with detail", NULL },
                { "show-info", 0, 0, G_OPTION_ARG_STRING, &opt_show_info, "Show information about object path", NULL },
                { "inhibit-polling", 0, 0, G_OPTION_ARG_STRING, &opt_inhibit_polling, "Inhibit polling", NULL },
                { "inhibit-all-polling", 0, 0, G_OPTION_ARG_NONE, &opt_inhibit_all_polling, "Inhibit all polling", NULL },
                { "inhibit", 0, 0, G_OPTION_ARG_NONE, &opt_inhibit, "Inhibit the daemon", NULL },

                { "mount", 0, 0, G_OPTION_ARG_STRING, &opt_mount, "Mount the device given by the object path", NULL },
                { "mount-fstype", 0, 0, G_OPTION_ARG_STRING, &opt_mount_fstype, "Specify file system type", NULL },
                { "mount-options", 0, 0, G_OPTION_ARG_STRING, &opt_mount_options, "Mount options separated by comma", NULL },

                { "unmount", 0, 0, G_OPTION_ARG_STRING, &opt_unmount, "Unmount the device given by the object path", NULL },
                { "unmount-options", 0, 0, G_OPTION_ARG_STRING, &opt_unmount_options, "Unmount options separated by comma", NULL },

                { NULL }
        };

        ret = 1;

        setlocale (LC_ALL, "");

        g_type_init ();

        context = g_option_context_new ("DeviceKit-disks tool");
        g_option_context_set_description (context, "See the devkit-disks man page for details.");
        g_option_context_add_main_entries (context, entries, NULL);
        g_option_context_parse (context, &argc, &argv, NULL);

        loop = g_main_loop_new (NULL, FALSE);

        bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
                g_error_free (error);
                goto out;
        }

        dbus_g_object_register_marshaller (
                devkit_disks_marshal_VOID__BOXED_BOOLEAN_STRING_UINT_BOOLEAN_INT_INT_STRING_DOUBLE,
                G_TYPE_NONE,
                DBUS_TYPE_G_OBJECT_PATH,
                G_TYPE_BOOLEAN,
                G_TYPE_STRING,
                G_TYPE_UINT,
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
        dbus_g_proxy_add_signal (disks_proxy, "DeviceAdded", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (disks_proxy, "DeviceRemoved", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (disks_proxy, "DeviceChanged", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
        dbus_g_proxy_add_signal (disks_proxy,
                                 "DeviceJobChanged",
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 G_TYPE_BOOLEAN,
                                 G_TYPE_STRING,
                                 G_TYPE_UINT,
                                 G_TYPE_BOOLEAN,
                                 G_TYPE_INT,
                                 G_TYPE_INT,
                                 G_TYPE_STRING,
                                 G_TYPE_DOUBLE,
                                 G_TYPE_INVALID);

        if (opt_enumerate) {
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
        } else if (opt_inhibit_polling != NULL) {
                ret = do_inhibit_polling (opt_inhibit_polling, argc - 1, argv + 1);
                goto out;
        } else if (opt_inhibit_all_polling) {
                ret = do_inhibit_all_polling (argc - 1, argv + 1);
                goto out;
        } else if (opt_inhibit) {
                ret = do_inhibit (argc - 1, argv + 1);
                goto out;
        } else if (opt_mount != NULL) {
                do_mount (opt_mount, opt_mount_fstype, opt_mount_options);
        } else if (opt_unmount != NULL) {
                do_unmount (opt_unmount, opt_unmount_options);
        } else {
                gchar *usage;

                usage = g_option_context_get_help (context, FALSE, NULL);
                g_printerr ("%s", usage);
                g_free (usage);

                ret = 1;
                goto out;
        }

        ret = 0;

out:
        if (disks_proxy != NULL)
                g_object_unref (disks_proxy);
        if (bus != NULL)
                dbus_g_connection_unref (bus);
        g_option_context_free (context);

        return ret;
}
