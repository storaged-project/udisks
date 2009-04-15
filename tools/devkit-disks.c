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

static gboolean      opt_dump                   = FALSE;
static gboolean      opt_enumerate              = FALSE;
static gboolean      opt_enumerate_device_files = FALSE;
static gboolean      opt_monitor                = FALSE;
static gboolean      opt_monitor_detail         = FALSE;
static char         *opt_show_info              = NULL;
static char         *opt_inhibit_polling        = NULL;
static gboolean      opt_inhibit                = FALSE;
static gboolean      opt_inhibit_all_polling    = FALSE;
static char         *opt_mount                  = NULL;
static char         *opt_mount_fstype           = NULL;
static char         *opt_mount_options          = NULL;
static char         *opt_unmount                = NULL;
static char         *opt_unmount_options        = NULL;
static char         *opt_ata_smart_refresh      = NULL;
static gboolean      opt_ata_smart_wakeup       = FALSE;
static char         *opt_ata_smart_simulate     = NULL;


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
do_ata_smart_refresh (const gchar *object_path,
                      gboolean     wakeup,
                      const gchar *simulate_path)
{
        DBusGProxy *proxy;
        GError *error;
        GPtrArray *options;

        options = g_ptr_array_new ();
        if (!wakeup)
                g_ptr_array_add (options, g_strdup ("nowakeup"));
        if (simulate_path != NULL)
                g_ptr_array_add (options, g_strdup_printf ("simulate=%s", simulate_path));
        g_ptr_array_add (options, NULL);

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           object_path,
                                           "org.freedesktop.DeviceKit.Disks.Device");
        error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_Device_drive_ata_smart_refresh_data (proxy,
                                                                                  (const char **) options->pdata,
                                                                                  &error)) {
                g_print ("Refreshing ATA SMART data failed: %s\n", error->message);
                g_error_free (error);
        } else {
                do_show_info (object_path);
        }

        g_ptr_array_foreach (options, (GFunc) g_free, NULL);
        g_ptr_array_free (options, TRUE);
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
           double      job_percentage)
{
        if (job_in_progress) {
                g_print ("  job underway:            %s", job_id);
                if (job_percentage >= 0)
                        g_print (", %3.0lf%% complete", job_percentage);
                if (job_is_cancellable)
                        g_print (", cancellable");
                g_print (", initiated by uid %d", job_initiated_by_uid);
                g_print ("\n");
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
                                   double      job_percentage,
                                   gpointer    user_data)
{
  g_print ("job-changed: %s\n", object_path);
  if (opt_monitor_detail) {
          print_job (job_in_progress,
                     job_id,
                     job_initiated_by_uid,
                     job_is_cancellable,
                     job_percentage);
  }
}

static void
device_removed_signal_handler (DBusGProxy *proxy, const char *object_path, gpointer user_data)
{
  g_print ("removed:   %s\n", object_path);
}

#define ATA_SMART_ATTRIBUTE_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray", \
                                                                 G_TYPE_UINT, \
                                                                 G_TYPE_STRING, \
                                                                 G_TYPE_UINT, \
                                                                 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, \
                                                                 G_TYPE_UCHAR, G_TYPE_BOOLEAN, \
                                                                 G_TYPE_UCHAR, G_TYPE_BOOLEAN, \
                                                                 G_TYPE_UCHAR, G_TYPE_BOOLEAN, \
                                                                 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, \
                                                                 G_TYPE_UINT, G_TYPE_UINT64, \
                                                                 dbus_g_type_get_collection ("GArray", G_TYPE_UCHAR), \
                                                                 G_TYPE_INVALID))

#define ATA_SMART_HISTORICAL_SMART_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                                             G_TYPE_UINT64, \
                                                                             G_TYPE_BOOLEAN, \
                                                                             G_TYPE_BOOLEAN, \
                                                                             G_TYPE_BOOLEAN, \
                                                                             G_TYPE_BOOLEAN, \
                                                                             G_TYPE_DOUBLE, \
                                                                             G_TYPE_UINT64, \
                                                                             dbus_g_type_get_collection ("GPtrArray", ATA_SMART_DATA_ATTRIBUTE_STRUCT_TYPE), \
                                                                             G_TYPE_INVALID))

#define LSOF_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                       G_TYPE_UINT,     \
                                                       G_TYPE_UINT,     \
                                                       G_TYPE_STRING,   \
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

        gint64   device_major;
        gint64   device_minor;
        char    *device_file;
        char   **device_file_by_id;
        char   **device_file_by_path;
        gboolean device_is_system_internal;
        gboolean device_is_partition;
        gboolean device_is_partition_table;
        gboolean device_is_removable;
        gboolean device_is_media_available;
        gboolean device_is_media_change_detected;
        gboolean device_is_media_change_detection_polling;
        gboolean device_is_media_change_detection_inhibitable;
        gboolean device_is_media_change_detection_inhibited;
        gboolean device_is_read_only;
        gboolean device_is_drive;
        gboolean device_is_optical_disc;
        gboolean device_is_luks;
        gboolean device_is_luks_cleartext;
        gboolean device_is_mounted;
        gboolean device_is_linux_md_component;
        gboolean device_is_linux_md;
        char   **device_mount_paths;
        uid_t    device_mounted_by_uid;
        gboolean device_presentation_hide;
        char    *device_presentation_name;
        char    *device_presentation_icon_name;
        guint64  device_size;
        guint64  device_block_size;

        gboolean job_in_progress;
        char    *job_id;
        uid_t    job_initiated_by_uid;
        gboolean job_is_cancellable;
        double   job_percentage;

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

        gboolean drive_ata_smart_is_available;
        gboolean drive_ata_smart_is_failing;
        gboolean drive_ata_smart_is_failing_valid;
        gboolean drive_ata_smart_has_bad_sectors;
        gboolean drive_ata_smart_has_bad_attributes;
        gdouble drive_ata_smart_temperature_kelvin;
        guint64 drive_ata_smart_power_on_seconds;
        guint64 drive_ata_smart_time_collected;
        guint drive_ata_smart_offline_data_collection_status;
        guint drive_ata_smart_offline_data_collection_seconds;
        guint drive_ata_smart_self_test_execution_status;
        guint drive_ata_smart_self_test_execution_percent_remaining;
        gboolean drive_ata_smart_short_and_extended_self_test_available;
        gboolean drive_ata_smart_conveyance_self_test_available;
        gboolean drive_ata_smart_start_self_test_available;
        gboolean drive_ata_smart_abort_self_test_available;
        guint drive_ata_smart_short_self_test_polling_minutes;
        guint drive_ata_smart_extended_self_test_polling_minutes;
        guint drive_ata_smart_conveyance_self_test_polling_minutes;
        GValue drive_ata_smart_attributes;

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

        else if (strcmp (key, "device-major") == 0)
                props->device_major = g_value_get_int64 (value);
        else if (strcmp (key, "device-minor") == 0)
                props->device_minor = g_value_get_int64 (value);
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
        else if (strcmp (key, "device-is-media-change-detection-polling") == 0)
                props->device_is_media_change_detection_polling = g_value_get_boolean (value);
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
        else if (strcmp (key, "device-mount-paths") == 0)
                props->device_mount_paths = g_strdupv (g_value_get_boxed (value));
        else if (strcmp (key, "device-mounted-by-uid") == 0)
                props->device_mounted_by_uid = g_value_get_uint (value);
        else if (strcmp (key, "device-presentation-hide") == 0)
                props->device_presentation_hide = g_value_get_boolean (value);
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
        else if (strcmp (key, "job-percentage") == 0)
                props->job_percentage = g_value_get_double (value);

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

        else if (strcmp (key, "drive-ata-smart-is-available") == 0)
                props->drive_ata_smart_is_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-is-failing") == 0)
                props->drive_ata_smart_is_failing = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-is-failing-valid") == 0)
                props->drive_ata_smart_is_failing_valid = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-has-bad-sectors") == 0)
                props->drive_ata_smart_has_bad_sectors = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-has-bad-attributes") == 0)
                props->drive_ata_smart_has_bad_attributes = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-temperature-kelvin") == 0)
                props->drive_ata_smart_temperature_kelvin = g_value_get_double (value);
        else if (strcmp (key, "drive-ata-smart-power-on-seconds") == 0)
                props->drive_ata_smart_power_on_seconds = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-ata-smart-time-collected") == 0)
                props->drive_ata_smart_time_collected = g_value_get_uint64 (value);
        else if (strcmp (key, "drive-ata-smart-offline-data-collection-status") == 0)
                props->drive_ata_smart_offline_data_collection_status = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-offline-data-collection-seconds") == 0)
                props->drive_ata_smart_offline_data_collection_seconds = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-self-test-execution-status") == 0)
                props->drive_ata_smart_self_test_execution_status = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-self-test-execution-percent-remaining") == 0)
                props->drive_ata_smart_self_test_execution_percent_remaining = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-short-and-extended-self-test-available") == 0)
                props->drive_ata_smart_short_and_extended_self_test_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-conveyance-self-test-available") == 0)
                props->drive_ata_smart_conveyance_self_test_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-start-self-test-available") == 0)
                props->drive_ata_smart_start_self_test_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-abort-self-test-available") == 0)
                props->drive_ata_smart_abort_self_test_available = g_value_get_boolean (value);
        else if (strcmp (key, "drive-ata-smart-short-self-test-polling-minutes") == 0)
                props->drive_ata_smart_short_self_test_polling_minutes = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-extended-self-test-polling-minutes") == 0)
                props->drive_ata_smart_extended_self_test_polling_minutes = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-conveyance-self-test-polling-minutes") == 0)
                props->drive_ata_smart_conveyance_self_test_polling_minutes = g_value_get_uint (value);
        else if (strcmp (key, "drive-ata-smart-attributes") == 0) {
                g_value_copy (value, &(props->drive_ata_smart_attributes));
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
        g_strfreev (props->device_mount_paths);
        g_free (props->device_presentation_name);
        g_free (props->device_presentation_icon_name);
        g_free (props->job_id);
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
        g_free (props->luks_holder);
        g_free (props->luks_cleartext_slave);
        g_free (props->drive_model);
        g_free (props->drive_vendor);
        g_free (props->drive_revision);
        g_free (props->drive_serial);
        g_free (props->drive_connection_interface);
        g_strfreev (props->drive_media_compatibility);
        g_free (props->drive_media);

        g_value_unset (&(props->drive_ata_smart_attributes));

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
        g_value_init (&(props->drive_ata_smart_attributes),
                      dbus_g_type_get_collection ("GPtrArray", ATA_SMART_ATTRIBUTE_STRUCT_TYPE));

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

static const gchar *
print_available (gboolean available)
{
        if (available)
                return "available";
        else
                return "not available";
}

static const gchar *
get_ata_smart_offline_status (guint offline_status)
{
        const gchar *ret;

        switch (offline_status) {
        case 0: ret = "never collected"; break;
        case 1: ret = "successful"; break;
        case 2: ret = "in progress"; break;
        case 3: ret = "suspended"; break;
        case 4: ret = "aborted"; break;
        case 5: ret = "fatal"; break;
        default: ret = "unknown"; break;
        }

        return ret;
}

static const gchar *
get_ata_smart_self_test_status (guint self_test_status)
{
        const gchar *ret;

        switch (self_test_status) {
        case 0: ret = "success or never"; break;
        case 1: ret = "aborted"; break;
        case 2: ret = "interrupted"; break;
        case 3: ret = "fatal"; break;
        case 4: ret = "error (unknown)"; break;
        case 5: ret = "error (electrical)"; break;
        case 6: ret = "error (servo)"; break;
        case 7: ret = "error (read)"; break;
        case 8: ret = "error (handling)"; break;
        case 15: ret = "in progress"; break;
        default: ret = "unknown"; break;
        }

        return ret;
}

static gchar *
get_ata_smart_unit (guint unit, guint64 pretty_value)
{
        gchar *ret;

        switch (unit) {
        default:
        case 0:
        case 1:
                ret = g_strdup_printf ("%" G_GUINT64_FORMAT, pretty_value);
                break;

        case 2:
                if (pretty_value > 1000 * 60 * 60 * 24) {
                        ret = g_strdup_printf ("%.3g days", pretty_value / 1000.0 / 60.0 / 60.0 / 24.0);
                } else if (pretty_value > 1000 * 60 * 60) {
                        ret = g_strdup_printf ("%.3g hours", pretty_value / 1000.0 / 60.0 / 60.0);
                } else if (pretty_value > 1000 * 60) {
                        ret = g_strdup_printf ("%.3g mins", pretty_value / 1000.0 / 60.0);
                } else if (pretty_value > 1000) {
                        ret = g_strdup_printf ("%.3g secs", pretty_value / 1000.0);
                } else {
                        ret = g_strdup_printf ("%d msec", (gint) pretty_value);
                }
                break;

        case 3:
                ret = g_strdup_printf ("%" G_GUINT64_FORMAT " sectors", pretty_value);
                break;

        case 4:
                ret = g_strdup_printf ("%.3gC / %.3gF",
                                       pretty_value / 1000.0 - 273.15,
                                       (pretty_value / 1000.0 - 273.15) * 9.0 / 5.0 + 32.0);
                break;
        }

        return ret;
}

static gboolean
has_colors (void)
{
        static gboolean ret = FALSE;
        static gboolean checked = FALSE;

        if (checked)
                return ret;

        if (isatty (STDOUT_FILENO)) {
                ret = TRUE;
        }

        checked = TRUE;

        return ret;
}

static void
begin_highlight (void)
{
        if (has_colors ())
                g_print ("\x1B[1;31m");
}

static void
end_highlight (void)
{
        if (has_colors ())
                g_print ("\x1B[0m");
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
        g_print ("  device:                  %" G_GINT64_MODIFIER "d:%" G_GINT64_MODIFIER "d\n",
                 props->device_major,
                 props->device_minor);
        g_print ("  device-file:             %s\n", props->device_file);
        for (n = 0; props->device_file_by_id[n] != NULL; n++)
                g_print ("    by-id:                 %s\n", (char *) props->device_file_by_id[n]);
        for (n = 0; props->device_file_by_path[n] != NULL; n++)
                g_print ("    by-path:               %s\n", (char *) props->device_file_by_path[n]);
        g_print ("  system internal:         %d\n", props->device_is_system_internal);
        g_print ("  removable:               %d\n", props->device_is_removable);
        g_print ("  has media:               %d\n", props->device_is_media_available);
        g_print ("    detects change:        %d\n", props->device_is_media_change_detected);
        g_print ("    detection by polling:  %d\n", props->device_is_media_change_detection_polling);
        g_print ("    detection inhibitable: %d\n", props->device_is_media_change_detection_inhibitable);
        g_print ("    detection inhibited:   %d\n", props->device_is_media_change_detection_inhibited);
        g_print ("  is read only:            %d\n", props->device_is_read_only);
        g_print ("  is mounted:              %d\n", props->device_is_mounted);
        g_print ("  mount paths:             ");
        for (n = 0; props->device_mount_paths != NULL && props->device_mount_paths[n] != NULL; n++) {
                if (n != 0)
                        g_print (", ");
                g_print ("%s", props->device_mount_paths[n]);
        }
        g_print ("\n");
        g_print ("  mounted by uid:          %d\n", props->device_mounted_by_uid);
        g_print ("  presentation hide:       %d\n", props->device_presentation_hide);
        g_print ("  presentation name:       %s\n", props->device_presentation_name);
        g_print ("  presentation icon:       %s\n", props->device_presentation_icon_name);
        g_print ("  size:                    %" G_GUINT64_FORMAT "\n", props->device_size);
        g_print ("  block size:              %" G_GUINT64_FORMAT "\n", props->device_block_size);

        print_job (props->job_in_progress,
                   props->job_id,
                   props->job_initiated_by_uid,
                   props->job_is_cancellable,
                   props->job_percentage);
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

                /* ------------------------------------------------------------------------------------------------- */

                if (!props->drive_ata_smart_is_available) {
                        g_print ("    ATA SMART:             not available\n");
                } else if (props->drive_ata_smart_time_collected == 0) {
                        g_print ("    ATA SMART:             Data not collected\n");
                } else {
                        struct tm *time_tm;
                        time_t time;
                        char time_buf[256];
                        GPtrArray *p;

                        time = (time_t) props->drive_ata_smart_time_collected;
                        time_tm = localtime (&time);
                        strftime (time_buf, sizeof time_buf, "%c", time_tm);


                        g_print ("    ATA SMART:             Updated at %s\n", time_buf);
                        if (props->drive_ata_smart_is_failing_valid) {
                                if (!props->drive_ata_smart_is_failing)
                                        g_print ("      assessment:          PASSED\n");
                                else {
                                        g_print ("      assessment:          ");
                                        begin_highlight ();
                                        g_print ("FAILING");
                                        end_highlight ();
                                        g_print ("\n");
                                }

                        } else {
                                g_print ("      assessment:          Unknown\n");
                        }

                        if (props->drive_ata_smart_has_bad_sectors) {
                                begin_highlight ();
                                g_print ("      bad sectors:         Yes\n");
                                end_highlight ();
                        } else {
                                g_print ("      bad sectors:         None\n");
                        }

                        if (props->drive_ata_smart_has_bad_attributes) {
                                begin_highlight ();
                                g_print ("      attributes:          One ore more attributes exceed threshold\n");
                                end_highlight ();
                        } else {
                                g_print ("      attributes:          Within threshold\n");
                        }

                        if (props->drive_ata_smart_temperature_kelvin < 0.1) {
                                g_print ("      temperature:         Unknown\n");
                        } else {
                                gdouble celcius;
                                gdouble fahrenheit;
                                celcius = props->drive_ata_smart_temperature_kelvin - 273.15;
                                fahrenheit = 9.0 * celcius / 5.0 + 32.0;
                                g_print ("      temperature:         %.3g\302\260 C / %.3g\302\260 F\n", celcius, fahrenheit);
                        }

                        if (props->drive_ata_smart_power_on_seconds == 0) {
                                g_print ("      power on hours:      Unknown\n");
                                g_print ("      powered on:          Unknown\n");
                        } else {
                                gchar *power_on_text;
                                guint val;

                                val = props->drive_ata_smart_power_on_seconds;

                                if (val > 60 * 60 * 24) {
                                        power_on_text = g_strdup_printf ("%.3g days", val / 60.0 / 60.0 / 24.0);
                                } else {
                                        power_on_text = g_strdup_printf ("%.3g hours", val / 60.0 / 60.0);
                                }

                                g_print ("      powered on:          %s\n", power_on_text);
                                g_free (power_on_text);
                        }


                        g_print ("      offline data:        %s (%d second(s) to complete)\n", get_ata_smart_offline_status (props->drive_ata_smart_offline_data_collection_status), props->drive_ata_smart_offline_data_collection_seconds);
                        g_print ("      self-test status:    %s (%d%% remaining)\n", get_ata_smart_self_test_status (props->drive_ata_smart_self_test_execution_status), props->drive_ata_smart_self_test_execution_percent_remaining);
                        g_print ("      ext./short test:     %s\n", print_available (props->drive_ata_smart_short_and_extended_self_test_available));
                        g_print ("      conveyance test:     %s\n", print_available (props->drive_ata_smart_conveyance_self_test_available));
                        g_print ("      start test:          %s\n", print_available (props->drive_ata_smart_start_self_test_available));
                        g_print ("      abort test:          %s\n", print_available (props->drive_ata_smart_abort_self_test_available));
                        g_print ("      short test:          %3d minute(s) recommended polling time\n", props->drive_ata_smart_short_self_test_polling_minutes);
                        g_print ("      ext. test:           %3d minute(s) recommended polling time\n", props->drive_ata_smart_extended_self_test_polling_minutes);
                        g_print ("      conveyance test:     %3d minute(s) recommended polling time\n", props->drive_ata_smart_conveyance_self_test_polling_minutes);
                        g_print ("===============================================================================\n");
                        g_print (" Attribute       Current/Worst/Threshold  Status   Value       Type     Updates\n");
                        g_print ("===============================================================================\n");
                        p = g_value_get_boxed (&(props->drive_ata_smart_attributes));
                        for (m = 0; m < p->len; m++) {
                                GValue elem = {0};
                                guint id;
                                gchar *name;
                                guint flags;
                                gboolean online, prefailure;
                                guchar current;
                                gboolean current_valid;
                                guchar worst;
                                gboolean worst_valid;
                                guchar threshold;
                                gboolean threshold_valid;
                                gboolean good, good_valid;
                                guint pretty_unit;
                                guint64 pretty_value;
                                gchar *pretty;
                                const gchar *assessment;
                                const gchar *type;
                                const gchar *updates;
                                gboolean do_highlight;
                                GArray *raw_data;

                                g_value_init (&elem, ATA_SMART_ATTRIBUTE_STRUCT_TYPE);
                                g_value_set_static_boxed (&elem, p->pdata[m]);

                                dbus_g_type_struct_get (&elem,
                                                         0, &id,
                                                         1, &name,
                                                         2, &flags,
                                                         3, &online,
                                                         4, &prefailure,
                                                         5, &current,
                                                         6, &current_valid,
                                                         7, &worst,
                                                         8, &worst_valid,
                                                         9, &threshold,
                                                        10, &threshold_valid,
                                                        11, &good,
                                                        12, &good_valid,
                                                        13, &pretty_unit,
                                                        14, &pretty_value,
                                                        15, &raw_data,
                                                        G_MAXUINT);

                                pretty = get_ata_smart_unit (pretty_unit, pretty_value);

                                do_highlight = FALSE;
                                if (!good_valid)
                                        assessment = " n/a";
                                else if (good)
                                        assessment = "good";
                                else {
                                        assessment = "FAIL";
                                        do_highlight = TRUE;
                                }

                                if (online)
                                        updates = "Online ";
                                else
                                        updates = "Offline";

                                if (prefailure)
                                        type = "Prefail";
                                else
                                        type = "Old-age";

                                if (do_highlight)
                                        begin_highlight ();

                                g_print (" %-27s %3d/%3d/%3d   %s    %-11s %s  %s\n",
                                         name,
                                         current,
                                         worst,
                                         threshold,
                                         assessment,
                                         pretty,
                                         type,
                                         updates);

                                if (do_highlight)
                                        end_highlight ();


                                g_free (pretty);

                                g_array_free (raw_data, TRUE);
                                g_value_unset (&elem);
                        }
                }

                /* ------------------------------------------------------------------------------------------------- */


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

	if (argc > 0 && strcmp (argv[0], "--") == 0) {
		argv++;
		argc--;
	}

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

	if (argc > 0 && strcmp (argv[0], "--") == 0) {
		argv++;
		argc--;
	}

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           "/org/freedesktop/DeviceKit/Disks",
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

	if (argc > 0 && strcmp (argv[0], "--") == 0) {
		argv++;
		argc--;
	}

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           "/org/freedesktop/DeviceKit/Disks",
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

static gint
ptr_str_array_compare (const gchar **a, const gchar **b)
{
        return g_strcmp0 (*a, *b);
}

static gchar *
device_file_to_object_path (const gchar *device_file)
{
        gchar *object_path;
        DBusGProxy *proxy;
        GError *error;
        struct stat statbuf;

        object_path = NULL;
        error = NULL;

        if (stat (device_file, &statbuf) != 0) {
                g_print ("Cannot stat device file %s: %m\n", device_file);
                goto out;
        }

        if (!S_ISBLK (statbuf.st_mode)) {
                g_print ("Device file %s is not a block device: %m\n", device_file);
                goto out;
        }

	proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.DeviceKit.Disks",
                                           "/org/freedesktop/DeviceKit/Disks",
                                           "org.freedesktop.DeviceKit.Disks");

        error = NULL;
        if (!org_freedesktop_DeviceKit_Disks_find_device_by_major_minor (proxy,
                                                                         major (statbuf.st_rdev),
                                                                         minor (statbuf.st_rdev),
                                                                         &object_path,
                                                                         &error)) {
                g_print ("Cannot find device with major:minor %d:%d: %s\n",
                         major (statbuf.st_rdev),
                         minor (statbuf.st_rdev),
                         error->message);
                g_error_free (error);
                goto out;
        }

 out:
        return object_path;
}


int
main (int argc, char **argv)
{
        int                  ret;
        GOptionContext      *context;
        GError              *error = NULL;
        unsigned int         n;
        gchar               *device_file;
        static GOptionEntry  entries []     = {
                { "enumerate", 0, 0, G_OPTION_ARG_NONE, &opt_enumerate, "Enumerate objects paths for devices", NULL },
                { "enumerate-device-files", 0, 0, G_OPTION_ARG_NONE, &opt_enumerate_device_files, "Enumerate device files for devices", NULL },
                { "dump", 0, 0, G_OPTION_ARG_NONE, &opt_dump, "Dump all information about all devices", NULL },
                { "monitor", 0, 0, G_OPTION_ARG_NONE, &opt_monitor, "Monitor activity from the disk daemon", NULL },
                { "monitor-detail", 0, 0, G_OPTION_ARG_NONE, &opt_monitor_detail, "Monitor with detail", NULL },
                { "show-info", 0, 0, G_OPTION_ARG_STRING, &opt_show_info, "Show information about a device file", NULL },
                { "inhibit-polling", 0, 0, G_OPTION_ARG_STRING, &opt_inhibit_polling, "Inhibit polling", NULL },
                { "inhibit-all-polling", 0, 0, G_OPTION_ARG_NONE, &opt_inhibit_all_polling, "Inhibit all polling", NULL },
                { "inhibit", 0, 0, G_OPTION_ARG_NONE, &opt_inhibit, "Inhibit the daemon", NULL },

                { "mount", 0, 0, G_OPTION_ARG_STRING, &opt_mount, "Mount the device given by the object path", NULL },
                { "mount-fstype", 0, 0, G_OPTION_ARG_STRING, &opt_mount_fstype, "Specify file system type", NULL },
                { "mount-options", 0, 0, G_OPTION_ARG_STRING, &opt_mount_options, "Mount options separated by comma", NULL },

                { "unmount", 0, 0, G_OPTION_ARG_STRING, &opt_unmount, "Unmount the device given by the object path", NULL },
                { "unmount-options", 0, 0, G_OPTION_ARG_STRING, &opt_unmount_options, "Unmount options separated by comma", NULL },
                { "ata-smart-refresh", 0, 0, G_OPTION_ARG_STRING, &opt_ata_smart_refresh, "Refresh ATA SMART data", NULL },
                { "ata-smart-wakeup", 0, 0, G_OPTION_ARG_NONE, &opt_ata_smart_wakeup, "Wake up the disk if it is not awake", NULL },
                { "ata-smart-simulate", 0, 0, G_OPTION_ARG_STRING, &opt_ata_smart_simulate, "Inject libatasmart BLOB for testing", NULL },
                { NULL }
        };

        setlocale (LC_ALL, "");

        ret = 1;
        device_file = NULL;

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
                devkit_disks_marshal_VOID__BOXED_BOOLEAN_STRING_UINT_BOOLEAN_DOUBLE,
                G_TYPE_NONE,
                DBUS_TYPE_G_OBJECT_PATH,
                G_TYPE_BOOLEAN,
                G_TYPE_STRING,
                G_TYPE_UINT,
                G_TYPE_BOOLEAN,
                G_TYPE_DOUBLE,
                G_TYPE_INVALID);

	disks_proxy = dbus_g_proxy_new_for_name (bus,
                                                 "org.freedesktop.DeviceKit.Disks",
                                                 "/org/freedesktop/DeviceKit/Disks",
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
                                 G_TYPE_DOUBLE,
                                 G_TYPE_INVALID);

        if (opt_dump) {
                GPtrArray *devices;
                if (!org_freedesktop_DeviceKit_Disks_enumerate_devices (disks_proxy, &devices, &error)) {
                        g_warning ("Couldn't enumerate devices: %s", error->message);
                        g_error_free (error);
                        goto out;
                }
                g_ptr_array_sort (devices, (GCompareFunc) ptr_str_array_compare);
                g_print ("========================================================================\n");
                for (n = 0; n < devices->len; n++) {
                        char *object_path = devices->pdata[n];
                        do_show_info (object_path);
                        g_print ("\n"
                                 "========================================================================\n");
                }
                g_ptr_array_foreach (devices, (GFunc) g_free, NULL);
                g_ptr_array_free (devices, TRUE);
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
        } else if (opt_enumerate_device_files) {
                gchar **device_files;
                if (!org_freedesktop_DeviceKit_Disks_enumerate_device_files (disks_proxy, &device_files, &error)) {
                        g_warning ("Couldn't enumerate device files: %s", error->message);
                        g_error_free (error);
                        goto out;
                }
                for (n = 0; device_files != NULL && device_files[n] != NULL; n++) {
                        g_print ("%s\n", device_files[n]);
                }
                g_strfreev (device_files);
        } else if (opt_monitor || opt_monitor_detail) {
                if (!do_monitor ())
                        goto out;
        } else if (opt_show_info != NULL) {
                device_file = device_file_to_object_path (opt_show_info);
                if (device_file == NULL)
                        goto out;
                do_show_info (device_file);
        } else if (opt_inhibit_polling != NULL) {
                device_file = device_file_to_object_path (opt_inhibit_polling);
                if (device_file == NULL)
                        goto out;
                ret = do_inhibit_polling (device_file, argc - 1, argv + 1);
                goto out;
        } else if (opt_inhibit_all_polling) {
                ret = do_inhibit_all_polling (argc - 1, argv + 1);
                goto out;
        } else if (opt_inhibit) {
                ret = do_inhibit (argc - 1, argv + 1);
                goto out;
        } else if (opt_mount != NULL) {
                device_file = device_file_to_object_path (opt_mount);
                if (device_file == NULL)
                        goto out;
                do_mount (device_file, opt_mount_fstype, opt_mount_options);
        } else if (opt_unmount != NULL) {
                device_file = device_file_to_object_path (opt_unmount);
                if (device_file == NULL)
                        goto out;
                do_unmount (device_file, opt_unmount_options);
        } else if (opt_ata_smart_refresh != NULL) {
                device_file = device_file_to_object_path (opt_ata_smart_refresh);
                if (device_file == NULL)
                        goto out;
                do_ata_smart_refresh (device_file, opt_ata_smart_wakeup, opt_ata_smart_simulate);
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
        g_free (device_file);
        if (disks_proxy != NULL)
                g_object_unref (disks_proxy);
        if (bus != NULL)
                dbus_g_connection_unref (bus);
        g_option_context_free (context);

        return ret;
}
