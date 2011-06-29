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

#include <atasmart.h>

#include "udisks-daemon-glue.h"
#include "udisks-device-glue.h"
#include "udisks-marshal.h"

static DBusGConnection *bus = NULL;
static DBusGProxy *disks_proxy = NULL;
static GMainLoop *loop;

static gboolean opt_dump = FALSE;
static gboolean opt_enumerate = FALSE;
static gboolean opt_enumerate_device_files = FALSE;
static gboolean opt_monitor = FALSE;
static gboolean opt_monitor_detail = FALSE;
static char *opt_show_info = NULL;
static char *opt_inhibit_polling = NULL;
static char *opt_poll_for_media = NULL;
static gboolean opt_inhibit = FALSE;
static gboolean opt_inhibit_all_polling = FALSE;
static char *opt_drive_spindown = NULL;
static gboolean opt_drive_spindown_all = FALSE;
static gint opt_spindown_seconds = 0;
static char *opt_mount = NULL;
static char *opt_mount_fstype = NULL;
static char *opt_mount_options = NULL;
static char *opt_unmount = NULL;
static char *opt_unmount_options = NULL;
static char *opt_detach = NULL;
static char *opt_detach_options = NULL;
static char *opt_eject = NULL;
static char *opt_eject_options = NULL;
static char *opt_ata_smart_refresh = NULL;
static gboolean opt_ata_smart_wakeup = FALSE;
static char *opt_ata_smart_simulate = NULL;

static gboolean do_monitor (void);
static void do_show_info (const char *object_path);

static void
do_ata_smart_refresh (const gchar *object_path,
                      gboolean wakeup,
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

  proxy = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.UDisks.Device");
  error = NULL;
  if (!org_freedesktop_UDisks_Device_drive_ata_smart_refresh_data (proxy, (const char **) options->pdata, &error))
    {
      g_print ("Refreshing ATA SMART data failed: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
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

  proxy = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.UDisks.Device");
  error = NULL;
  if (!org_freedesktop_UDisks_Device_filesystem_mount (proxy,
                                                       filesystem_type,
                                                       (const char **) mount_options,
                                                       &mount_path,
                                                       &error))
    {
      g_print ("Mount failed: %s\n", error->message);
      g_error_free (error);
      goto out;
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

  proxy = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.UDisks.Device");

  error = NULL;
  if (!org_freedesktop_UDisks_Device_filesystem_unmount (proxy, (const char **) unmount_options, &error))
    {
      g_print ("Unmount failed: %s\n", error->message);
      g_error_free (error);
    }

  g_strfreev (unmount_options);
}

static void
do_detach (const char *object_path,
           const char *options)
{
  DBusGProxy *proxy;
  GError *error;
  char **unmount_options;

  unmount_options = NULL;
  if (options != NULL)
    unmount_options = g_strsplit (options, ",", 0);

  proxy = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.UDisks.Device");

  error = NULL;
  if (!org_freedesktop_UDisks_Device_drive_detach (proxy, (const char **) options, &error))
    {
      g_print ("Detach failed: %s\n", error->message);
      g_error_free (error);
    }

  g_strfreev (unmount_options);
}

static void
do_eject (const char *object_path,
           const char *options)
{
  DBusGProxy *proxy;
  GError *error;
  char **eject_options;

  eject_options = NULL;
  if (options != NULL)
    eject_options = g_strsplit (options, ",", 0);

  proxy = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.UDisks.Device");

  error = NULL;
  if (!org_freedesktop_UDisks_Device_drive_eject (proxy, (const char **) eject_options, &error))
    {
      g_print ("Eject failed: %s\n", error->message);
      g_error_free (error);
    }

  g_strfreev (eject_options);
}

static void
device_added_signal_handler (DBusGProxy *proxy,
                             const char *object_path,
                             gpointer user_data)
{
  g_print ("added:     %s\n", object_path);
  if (opt_monitor_detail)
    {
      do_show_info (object_path);
      g_print ("\n");
    }
}

static void
device_changed_signal_handler (DBusGProxy *proxy,
                               const char *object_path,
                               gpointer user_data)
{
  g_print ("changed:     %s\n", object_path);
  if (opt_monitor_detail)
    {
      /* TODO: would be nice to just show the diff */
      do_show_info (object_path);
      g_print ("\n");
    }
}

static void
print_job (gboolean job_in_progress,
           const char *job_id,
           uid_t job_initiated_by_uid,
           gboolean job_is_cancellable,
           double job_percentage)
{
  if (job_in_progress)
    {
      g_print ("  job underway:                %s", job_id);
      if (job_percentage >= 0)
        g_print (", %3.0lf%% complete", job_percentage);
      if (job_is_cancellable)
        g_print (", cancellable");
      g_print (", initiated by uid %d", job_initiated_by_uid);
      g_print ("\n");
    }
  else
    {
      g_print ("  job underway:                no\n");
    }
}

static void
device_job_changed_signal_handler (DBusGProxy *proxy,
                                   const char *object_path,
                                   gboolean job_in_progress,
                                   const char *job_id,
                                   guint32 job_initiated_by_uid,
                                   gboolean job_is_cancellable,
                                   double job_percentage,
                                   gpointer user_data)
{
  g_print ("job-changed: %s\n", object_path);
  if (opt_monitor_detail)
    {
      print_job (job_in_progress, job_id, job_initiated_by_uid, job_is_cancellable, job_percentage);
    }
}

static void
device_removed_signal_handler (DBusGProxy *proxy,
                               const char *object_path,
                               gpointer user_data)
{
  g_print ("removed:   %s\n", object_path);
}

#define LSOF_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                       G_TYPE_UINT,     \
                                                       G_TYPE_UINT,     \
                                                       G_TYPE_STRING,   \
                                                       G_TYPE_INVALID))

/* --- SUCKY CODE BEGIN --- */

/* This totally sucks; dbus-bindings-tool and dbus-glib should be able
 * to do this for us.
 *
 * TODO: keep in sync with code in tools/udisks in udisks.
 */

typedef struct
{
  char *native_path;

  guint64 device_detection_time;
  guint64 device_media_detection_time;
  gint64 device_major;
  gint64 device_minor;
  char *device_file;
  char *device_file_presentation;
  char **device_file_by_id;
  char **device_file_by_path;
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
  gboolean device_is_linux_lvm2_lv;
  gboolean device_is_linux_lvm2_pv;
  gboolean device_is_linux_dmmp;
  gboolean device_is_linux_dmmp_component;
  gboolean device_is_linux_loop;
  char **device_mount_paths;
  uid_t device_mounted_by_uid;
  gboolean device_presentation_hide;
  gboolean device_presentation_nopolicy;
  char *device_presentation_name;
  char *device_presentation_icon_name;
  char *device_automount_hint;
  guint64 device_size;
  guint64 device_block_size;

  gboolean job_in_progress;
  char *job_id;
  uid_t job_initiated_by_uid;
  gboolean job_is_cancellable;
  double job_percentage;

  char *id_usage;
  char *id_type;
  char *id_version;
  char *id_uuid;
  char *id_label;

  char *partition_slave;
  char *partition_scheme;
  int partition_number;
  char *partition_type;
  char *partition_label;
  char *partition_uuid;
  char **partition_flags;
  guint64 partition_offset;
  guint64 partition_size;
  guint64 partition_alignment_offset;

  char *partition_table_scheme;
  int partition_table_count;

  char *luks_holder;

  char *luks_cleartext_slave;
  uid_t luks_cleartext_unlocked_by_uid;

  char *drive_vendor;
  char *drive_model;
  char *drive_revision;
  char *drive_serial;
  char *drive_wwn;
  char *drive_connection_interface;
  guint64 drive_connection_speed;
  char **drive_media_compatibility;
  char *drive_media;
  gboolean drive_is_media_ejectable;
  gboolean drive_can_detach;
  gboolean drive_can_spindown;
  gboolean drive_is_rotational;
  guint drive_rotation_rate;
  char *drive_write_cache;
  char *drive_adapter;
  char **drive_ports;
  char **drive_similar_devices;

  gboolean optical_disc_is_blank;
  gboolean optical_disc_is_appendable;
  gboolean optical_disc_is_closed;
  guint optical_disc_num_tracks;
  guint optical_disc_num_audio_tracks;
  guint optical_disc_num_sessions;

  gboolean drive_ata_smart_is_available;
  guint64 drive_ata_smart_time_collected;
  gchar *drive_ata_smart_status;
  gchar *drive_ata_smart_blob;
  gsize drive_ata_smart_blob_size;

  char *linux_md_component_level;
  int linux_md_component_position;
  int linux_md_component_num_raid_devices;
  char *linux_md_component_uuid;
  char *linux_md_component_home_host;
  char *linux_md_component_name;
  char *linux_md_component_version;
  char *linux_md_component_holder;
  char **linux_md_component_state;

  char *linux_md_state;
  char *linux_md_level;
  int linux_md_num_raid_devices;
  char *linux_md_uuid;
  char *linux_md_home_host;
  char *linux_md_name;
  char *linux_md_version;
  char **linux_md_slaves;
  gboolean linux_md_is_degraded;
  char *linux_md_sync_action;
  double linux_md_sync_percentage;
  guint64 linux_md_sync_speed;

  gchar *linux_lvm2_lv_name;
  gchar *linux_lvm2_lv_uuid;
  gchar *linux_lvm2_lv_group_name;
  gchar *linux_lvm2_lv_group_uuid;

  gchar *linux_lvm2_pv_uuid;
  guint  linux_lvm2_pv_num_metadata_areas;
  gchar *linux_lvm2_pv_group_name;
  gchar *linux_lvm2_pv_group_uuid;
  guint64 linux_lvm2_pv_group_size;
  guint64 linux_lvm2_pv_group_unallocated_size;
  guint64 linux_lvm2_pv_group_sequence_number;
  guint64 linux_lvm2_pv_group_extent_size;
  char **linux_lvm2_pv_group_physical_volumes;
  char **linux_lvm2_pv_group_logical_volumes;

  gchar *linux_dmmp_component_holder;

  gchar *linux_dmmp_name;
  gchar **linux_dmmp_slaves;
  gchar *linux_dmmp_parameters;

  gchar *linux_loop_filename;

} DeviceProperties;

static void
collect_props (const char *key,
               const GValue *value,
               DeviceProperties *props)
{
  gboolean handled = TRUE;

  if (strcmp (key, "NativePath") == 0)
    props->native_path = g_strdup (g_value_get_string (value));

  else if (strcmp (key, "DeviceDetectionTime") == 0)
    props->device_detection_time = g_value_get_uint64 (value);
  else if (strcmp (key, "DeviceMediaDetectionTime") == 0)
    props->device_media_detection_time = g_value_get_uint64 (value);
  else if (strcmp (key, "DeviceMajor") == 0)
    props->device_major = g_value_get_int64 (value);
  else if (strcmp (key, "DeviceMinor") == 0)
    props->device_minor = g_value_get_int64 (value);
  else if (strcmp (key, "DeviceFile") == 0)
    props->device_file = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DeviceFilePresentation") == 0)
    props->device_file_presentation = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DeviceFileById") == 0)
    props->device_file_by_id = g_strdupv (g_value_get_boxed (value));
  else if (strcmp (key, "DeviceFileByPath") == 0)
    props->device_file_by_path = g_strdupv (g_value_get_boxed (value));
  else if (strcmp (key, "DeviceIsSystemInternal") == 0)
    props->device_is_system_internal = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsPartition") == 0)
    props->device_is_partition = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsPartitionTable") == 0)
    props->device_is_partition_table = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsRemovable") == 0)
    props->device_is_removable = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsMediaAvailable") == 0)
    props->device_is_media_available = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsMediaChangeDetected") == 0)
    props->device_is_media_change_detected = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsMediaChangeDetectionPolling") == 0)
    props->device_is_media_change_detection_polling = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsMediaChangeDetectionInhibitable") == 0)
    props->device_is_media_change_detection_inhibitable = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsMediaChangeDetectionInhibited") == 0)
    props->device_is_media_change_detection_inhibited = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsReadOnly") == 0)
    props->device_is_read_only = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsDrive") == 0)
    props->device_is_drive = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsOpticalDisc") == 0)
    props->device_is_optical_disc = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsLuks") == 0)
    props->device_is_luks = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsLuksCleartext") == 0)
    props->device_is_luks_cleartext = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsLinuxMdComponent") == 0)
    props->device_is_linux_md_component = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsLinuxMd") == 0)
    props->device_is_linux_md = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsLinuxLvm2LV") == 0)
    props->device_is_linux_lvm2_lv = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsLinuxLvm2PV") == 0)
    props->device_is_linux_lvm2_pv = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsLinuxDmmp") == 0)
    props->device_is_linux_dmmp = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsLinuxDmmpComponent") == 0)
    props->device_is_linux_dmmp_component = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsLinuxLoop") == 0)
    props->device_is_linux_loop = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceIsMounted") == 0)
    props->device_is_mounted = g_value_get_boolean (value);
  else if (strcmp (key, "DeviceMountPaths") == 0)
    props->device_mount_paths = g_strdupv (g_value_get_boxed (value));
  else if (strcmp (key, "DeviceMountedByUid") == 0)
    props->device_mounted_by_uid = g_value_get_uint (value);
  else if (strcmp (key, "DevicePresentationHide") == 0)
    props->device_presentation_hide = g_value_get_boolean (value);
  else if (strcmp (key, "DevicePresentationNopolicy") == 0)
    props->device_presentation_nopolicy = g_value_get_boolean (value);
  else if (strcmp (key, "DevicePresentationName") == 0)
    props->device_presentation_name = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DevicePresentationIconName") == 0)
    props->device_presentation_icon_name = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DeviceAutomountHint") == 0)
    props->device_automount_hint = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DeviceSize") == 0)
    props->device_size = g_value_get_uint64 (value);
  else if (strcmp (key, "DeviceBlockSize") == 0)
    props->device_block_size = g_value_get_uint64 (value);

  else if (strcmp (key, "JobInProgress") == 0)
    props->job_in_progress = g_value_get_boolean (value);
  else if (strcmp (key, "JobId") == 0)
    props->job_id = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "JobInitiatedByUid") == 0)
    props->job_initiated_by_uid = g_value_get_uint (value);
  else if (strcmp (key, "JobIsCancellable") == 0)
    props->job_is_cancellable = g_value_get_boolean (value);
  else if (strcmp (key, "JobPercentage") == 0)
    props->job_percentage = g_value_get_double (value);

  else if (strcmp (key, "IdUsage") == 0)
    props->id_usage = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "IdType") == 0)
    props->id_type = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "IdVersion") == 0)
    props->id_version = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "IdUuid") == 0)
    props->id_uuid = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "IdLabel") == 0)
    props->id_label = g_strdup (g_value_get_string (value));

  else if (strcmp (key, "PartitionSlave") == 0)
    props->partition_slave = g_strdup (g_value_get_boxed (value));
  else if (strcmp (key, "PartitionScheme") == 0)
    props->partition_scheme = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "PartitionNumber") == 0)
    props->partition_number = g_value_get_int (value);
  else if (strcmp (key, "PartitionType") == 0)
    props->partition_type = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "PartitionLabel") == 0)
    props->partition_label = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "PartitionUuid") == 0)
    props->partition_uuid = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "PartitionFlags") == 0)
    props->partition_flags = g_strdupv (g_value_get_boxed (value));
  else if (strcmp (key, "PartitionOffset") == 0)
    props->partition_offset = g_value_get_uint64 (value);
  else if (strcmp (key, "PartitionSize") == 0)
    props->partition_size = g_value_get_uint64 (value);
  else if (strcmp (key, "PartitionAlignmentOffset") == 0)
    props->partition_alignment_offset = g_value_get_uint64 (value);

  else if (strcmp (key, "PartitionTableScheme") == 0)
    props->partition_table_scheme = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "PartitionTableCount") == 0)
    props->partition_table_count = g_value_get_int (value);

  else if (strcmp (key, "LuksHolder") == 0)
    props->luks_holder = g_strdup (g_value_get_boxed (value));

  else if (strcmp (key, "LuksCleartextSlave") == 0)
    props->luks_cleartext_slave = g_strdup (g_value_get_boxed (value));
  else if (strcmp (key, "LuksCleartextUnlockedByUid") == 0)
    props->luks_cleartext_unlocked_by_uid = g_value_get_uint (value);

  else if (strcmp (key, "DriveVendor") == 0)
    props->drive_vendor = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DriveModel") == 0)
    props->drive_model = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DriveRevision") == 0)
    props->drive_revision = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DriveSerial") == 0)
    props->drive_serial = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DriveWwn") == 0)
    props->drive_wwn = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DriveConnectionInterface") == 0)
    props->drive_connection_interface = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DriveConnectionSpeed") == 0)
    props->drive_connection_speed = g_value_get_uint64 (value);
  else if (strcmp (key, "DriveMediaCompatibility") == 0)
    props->drive_media_compatibility = g_strdupv (g_value_get_boxed (value));
  else if (strcmp (key, "DriveMedia") == 0)
    props->drive_media = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DriveIsMediaEjectable") == 0)
    props->drive_is_media_ejectable = g_value_get_boolean (value);
  else if (strcmp (key, "DriveCanDetach") == 0)
    props->drive_can_detach = g_value_get_boolean (value);
  else if (strcmp (key, "DriveCanSpindown") == 0)
    props->drive_can_spindown = g_value_get_boolean (value);
  else if (strcmp (key, "DriveIsRotational") == 0)
    props->drive_is_rotational = g_value_get_boolean (value);
  else if (strcmp (key, "DriveRotationRate") == 0)
    props->drive_rotation_rate = g_value_get_uint (value);
  else if (strcmp (key, "DriveWriteCache") == 0)
    props->drive_write_cache = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DriveAdapter") == 0)
    props->drive_adapter = g_strdup (g_value_get_boxed (value));
  else if (strcmp (key, "DrivePorts") == 0)
    {
      guint n;
      GPtrArray *object_paths;

      object_paths = g_value_get_boxed (value);

      props->drive_ports = g_new0 (char *, object_paths->len + 1);
      for (n = 0; n < object_paths->len; n++)
        props->drive_ports[n] = g_strdup (object_paths->pdata[n]);
      props->drive_ports[n] = NULL;
    }
  else if (strcmp (key, "DriveSimilarDevices") == 0)
    {
      guint n;
      GPtrArray *object_paths;

      object_paths = g_value_get_boxed (value);

      props->drive_similar_devices = g_new0 (char *, object_paths->len + 1);
      for (n = 0; n < object_paths->len; n++)
        props->drive_similar_devices[n] = g_strdup (object_paths->pdata[n]);
      props->drive_similar_devices[n] = NULL;
    }

  else if (strcmp (key, "OpticalDiscIsBlank") == 0)
    props->optical_disc_is_blank = g_value_get_boolean (value);
  else if (strcmp (key, "OpticalDiscIsAppendable") == 0)
    props->optical_disc_is_appendable = g_value_get_boolean (value);
  else if (strcmp (key, "OpticalDiscIsClosed") == 0)
    props->optical_disc_is_closed = g_value_get_boolean (value);
  else if (strcmp (key, "OpticalDiscNumTracks") == 0)
    props->optical_disc_num_tracks = g_value_get_uint (value);
  else if (strcmp (key, "OpticalDiscNumAudioTracks") == 0)
    props->optical_disc_num_audio_tracks = g_value_get_uint (value);
  else if (strcmp (key, "OpticalDiscNumSessions") == 0)
    props->optical_disc_num_sessions = g_value_get_uint (value);

  else if (strcmp (key, "DriveAtaSmartIsAvailable") == 0)
    props->drive_ata_smart_is_available = g_value_get_boolean (value);
  else if (strcmp (key, "DriveAtaSmartTimeCollected") == 0)
    props->drive_ata_smart_time_collected = g_value_get_uint64 (value);
  else if (strcmp (key, "DriveAtaSmartStatus") == 0)
    props->drive_ata_smart_status = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "DriveAtaSmartBlob") == 0)
    {
      GArray *a = g_value_get_boxed (value);
      g_free (props->drive_ata_smart_blob);
      props->drive_ata_smart_blob = g_memdup (a->data, a->len);
      props->drive_ata_smart_blob_size = a->len;
    }

  else if (strcmp (key, "LinuxMdComponentLevel") == 0)
    props->linux_md_component_level = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdComponentPosition") == 0)
    props->linux_md_component_position = g_value_get_int (value);
  else if (strcmp (key, "LinuxMdComponentNumRaidDevices") == 0)
    props->linux_md_component_num_raid_devices = g_value_get_int (value);
  else if (strcmp (key, "LinuxMdComponentUuid") == 0)
    props->linux_md_component_uuid = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdComponentHomeHost") == 0)
    props->linux_md_component_home_host = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdComponentName") == 0)
    props->linux_md_component_name = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdComponentVersion") == 0)
    props->linux_md_component_version = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdComponentHolder") == 0)
    props->linux_md_component_holder = g_strdup (g_value_get_boxed (value));
  else if (strcmp (key, "LinuxMdComponentState") == 0)
    props->linux_md_component_state = g_strdupv (g_value_get_boxed (value));

  else if (strcmp (key, "LinuxMdState") == 0)
    props->linux_md_state = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdLevel") == 0)
    props->linux_md_level = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdNumRaidDevices") == 0)
    props->linux_md_num_raid_devices = g_value_get_int (value);
  else if (strcmp (key, "LinuxMdUuid") == 0)
    props->linux_md_uuid = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdHomeHost") == 0)
    props->linux_md_home_host = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdName") == 0)
    props->linux_md_name = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdVersion") == 0)
    props->linux_md_version = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdSlaves") == 0)
    {
      guint n;
      GPtrArray *object_paths;

      object_paths = g_value_get_boxed (value);

      props->linux_md_slaves = g_new0 (char *, object_paths->len + 1);
      for (n = 0; n < object_paths->len; n++)
        props->linux_md_slaves[n] = g_strdup (object_paths->pdata[n]);
      props->linux_md_slaves[n] = NULL;
    }
  else if (strcmp (key, "LinuxMdIsDegraded") == 0)
    props->linux_md_is_degraded = g_value_get_boolean (value);
  else if (strcmp (key, "LinuxMdSyncAction") == 0)
    props->linux_md_sync_action = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxMdSyncPercentage") == 0)
    props->linux_md_sync_percentage = g_value_get_double (value);
  else if (strcmp (key, "LinuxMdSyncSpeed") == 0)
    props->linux_md_sync_speed = g_value_get_uint64 (value);

  else if (strcmp (key, "LinuxLvm2LVName") == 0)
    props->linux_lvm2_lv_name = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxLvm2LVUuid") == 0)
    props->linux_lvm2_lv_uuid = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxLvm2LVGroupName") == 0)
    props->linux_lvm2_lv_group_name = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxLvm2LVGroupUuid") == 0)
    props->linux_lvm2_lv_group_uuid = g_strdup (g_value_get_string (value));

  else if (strcmp (key, "LinuxLvm2PVUuid") == 0)
    props->linux_lvm2_pv_uuid = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxLvm2PVNumMetadataAreas") == 0)
    props->linux_lvm2_pv_num_metadata_areas = g_value_get_uint (value);
  else if (strcmp (key, "LinuxLvm2PVGroupName") == 0)
    props->linux_lvm2_pv_group_name = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxLvm2PVGroupUuid") == 0)
    props->linux_lvm2_pv_group_uuid = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxLvm2PVGroupSize") == 0)
    props->linux_lvm2_pv_group_size = g_value_get_uint64 (value);
  else if (strcmp (key, "LinuxLvm2PVGroupUnallocatedSize") == 0)
    props->linux_lvm2_pv_group_unallocated_size = g_value_get_uint64 (value);
  else if (strcmp (key, "LinuxLvm2PVGroupSequenceNumber") == 0)
    props->linux_lvm2_pv_group_sequence_number = g_value_get_uint64 (value);
  else if (strcmp (key, "LinuxLvm2PVGroupExtentSize") == 0)
    props->linux_lvm2_pv_group_extent_size = g_value_get_uint64 (value);
  else if (strcmp (key, "LinuxLvm2PVGroupPhysicalVolumes") == 0)
    props->linux_lvm2_pv_group_physical_volumes = g_strdupv (g_value_get_boxed (value));
  else if (strcmp (key, "LinuxLvm2PVGroupLogicalVolumes") == 0)
    props->linux_lvm2_pv_group_logical_volumes = g_strdupv (g_value_get_boxed (value));

  else if (strcmp (key, "LinuxDmmpComponentHolder") == 0)
    props->linux_dmmp_component_holder = g_strdup (g_value_get_boxed (value));

  else if (strcmp (key, "LinuxDmmpName") == 0)
    props->linux_dmmp_name = g_strdup (g_value_get_string (value));
  else if (strcmp (key, "LinuxDmmpSlaves") == 0)
    {
      guint n;
      GPtrArray *object_paths;

      object_paths = g_value_get_boxed (value);

      props->linux_dmmp_slaves = g_new0 (char *, object_paths->len + 1);
      for (n = 0; n < object_paths->len; n++)
        props->linux_dmmp_slaves[n] = g_strdup (object_paths->pdata[n]);
      props->linux_dmmp_slaves[n] = NULL;
    }
  else if (strcmp (key, "LinuxDmmpParameters") == 0)
    props->linux_dmmp_parameters = g_strdup (g_value_get_string (value));

  else if (strcmp (key, "LinuxLoopFilename") == 0)
    props->linux_loop_filename = g_strdup (g_value_get_string (value));

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
  g_free (props->device_file_presentation);
  g_strfreev (props->device_file_by_id);
  g_strfreev (props->device_file_by_path);
  g_strfreev (props->device_mount_paths);
  g_free (props->device_presentation_name);
  g_free (props->device_presentation_icon_name);
  g_free (props->device_automount_hint);
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
  g_free (props->drive_wwn);
  g_free (props->drive_connection_interface);
  g_strfreev (props->drive_media_compatibility);
  g_free (props->drive_media);
  g_free (props->drive_write_cache);
  g_free (props->drive_adapter);
  g_strfreev (props->drive_ports);
  g_strfreev (props->drive_similar_devices);

  g_free (props->drive_ata_smart_status);
  g_free (props->drive_ata_smart_blob);

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

  g_free (props->linux_lvm2_lv_name);
  g_free (props->linux_lvm2_lv_uuid);
  g_free (props->linux_lvm2_lv_group_name);
  g_free (props->linux_lvm2_lv_group_uuid);

  g_free (props->linux_lvm2_pv_uuid);
  g_free (props->linux_lvm2_pv_group_name);
  g_free (props->linux_lvm2_pv_group_uuid);
  g_strfreev (props->linux_lvm2_pv_group_physical_volumes);
  g_strfreev (props->linux_lvm2_pv_group_logical_volumes);

  g_free (props->linux_dmmp_component_holder);

  g_free (props->linux_dmmp_name);
  g_strfreev (props->linux_dmmp_slaves);
  g_free (props->linux_dmmp_parameters);

  g_free (props->linux_loop_filename);

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
  const char *ifname = "org.freedesktop.UDisks.Device";

  props = g_new0 (DeviceProperties, 1);

  prop_proxy
    = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.DBus.Properties");
  error = NULL;
  if (!dbus_g_proxy_call (prop_proxy,
                          "GetAll",
                          &error,
                          G_TYPE_STRING,
                          ifname,
                          G_TYPE_INVALID,
                          dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
                          &hash_table,
                          G_TYPE_INVALID))
    {
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
  g_print ("Monitoring activity from the disks daemon. Press Ctrl+C to cancel.\n");

  dbus_g_proxy_connect_signal (disks_proxy, "DeviceAdded", G_CALLBACK (device_added_signal_handler), NULL, NULL);
  dbus_g_proxy_connect_signal (disks_proxy, "DeviceRemoved", G_CALLBACK (device_removed_signal_handler), NULL, NULL);
  dbus_g_proxy_connect_signal (disks_proxy, "DeviceChanged", G_CALLBACK (device_changed_signal_handler), NULL, NULL);
  dbus_g_proxy_connect_signal (disks_proxy,
                               "DeviceJobChanged",
                               G_CALLBACK (device_job_changed_signal_handler),
                               NULL,
                               NULL);
  g_main_loop_run (loop);

  return FALSE;
}

static gboolean
has_colors (void)
{
  static gboolean ret = FALSE;
  static gboolean checked = FALSE;

  if (checked)
    return ret;

  if (isatty (STDOUT_FILENO))
    {
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

static const gchar *
ata_smart_status_to_desc (const gchar *status,
                          gboolean *out_highlight)
{
  const gchar *desc;
  gboolean highlight;

  highlight = FALSE;
  if (g_strcmp0 (status, "GOOD") == 0)
    {
      desc = "Good";
    }
  else if (g_strcmp0 (status, "BAD_ATTRIBUTE_IN_THE_PAST") == 0)
    {
      desc = "Disk was used outside of design parameters in the past";
    }
  else if (g_strcmp0 (status, "BAD_SECTOR") == 0)
    {
      desc = "Disk has a few bad sectors";
    }
  else if (g_strcmp0 (status, "BAD_ATTRIBUTE_NOW") == 0)
    {
      desc = "Disk is being used outside of design parameters";
      highlight = TRUE;
    }
  else if (g_strcmp0 (status, "BAD_SECTOR_MANY") == 0)
    {
      desc = "Disk reports many bad sectors";
      highlight = TRUE;
    }
  else if (g_strcmp0 (status, "BAD_STATUS") == 0)
    {
      desc = "Disk failure is imminent";
      highlight = TRUE;
    }
  else
    {
      desc = status;
    }

  if (out_highlight != NULL)
    *out_highlight = highlight;

  return desc;
}

static gchar *
get_ata_smart_unit (guint unit,
                    guint64 pretty_value)
{
  gchar *ret;

  switch (unit)
    {
    default:
    case SK_SMART_ATTRIBUTE_UNIT_UNKNOWN:
    case SK_SMART_ATTRIBUTE_UNIT_NONE:
      ret = g_strdup_printf ("%" G_GUINT64_FORMAT, pretty_value);
      break;

    case SK_SMART_ATTRIBUTE_UNIT_MSECONDS:
      if (pretty_value > 1000 * 60 * 60 * 24)
        {
          ret = g_strdup_printf ("%3.1f days", pretty_value / 1000.0 / 60.0 / 60.0 / 24.0);
        }
      else if (pretty_value > 1000 * 60 * 60)
        {
          ret = g_strdup_printf ("%3.1f hours", pretty_value / 1000.0 / 60.0 / 60.0);
        }
      else if (pretty_value > 1000 * 60)
        {
          ret = g_strdup_printf ("%3.1f mins", pretty_value / 1000.0 / 60.0);
        }
      else if (pretty_value > 1000)
        {
          ret = g_strdup_printf ("%3.1f secs", pretty_value / 1000.0);
        }
      else
        {
          ret = g_strdup_printf ("%d msec", (gint) pretty_value);
        }
      break;

    case SK_SMART_ATTRIBUTE_UNIT_SECTORS:
      ret = g_strdup_printf ("%" G_GUINT64_FORMAT " sectors", pretty_value);
      break;

    case SK_SMART_ATTRIBUTE_UNIT_MKELVIN:
      ret = g_strdup_printf ("%.3gC / %.3gF", pretty_value / 1000.0 - 273.15, (pretty_value / 1000.0 - 273.15) * 9.0
                             / 5.0 + 32.0);
      break;
    }

  return ret;
}

static void
print_ata_smart_attr (SkDisk *d,
                      const SkSmartAttributeParsedData *a,
                      void *user_data)
{
  const gchar *assessment;
  const gchar *type;
  const gchar *updates;
  gchar *current_str;
  gchar *worst_str;
  gchar *threshold_str;
  gchar *pretty;

  pretty = get_ata_smart_unit (a->pretty_unit, a->pretty_value);

  if (!a->good_now_valid)
    {
      assessment = "   n/a   ";
    }
  else
    {
      if (!a->good_now)
        {
          assessment = "  FAIL   ";
        }
      else
        {
          if (a->good_in_the_past_valid && !a->good_in_the_past)
            {
              assessment = "FAIL_PAST";
            }
          else
            {
              assessment = "  good   ";
            }
        }
    }

  if (a->online)
    updates = "Online ";
  else
    updates = "Offline";

  if (a->prefailure)
    type = "Pre-fail";
  else
    type = "Old-age ";

  if (a->current_value_valid)
    current_str = g_strdup_printf ("%3d", a->current_value);
  else
    current_str = g_strdup ("n/a");

  if (a->worst_value_valid)
    worst_str = g_strdup_printf ("%3d", a->worst_value);
  else
    worst_str = g_strdup ("n/a");

  if (a->threshold_valid)
    threshold_str = g_strdup_printf ("%3d", a->threshold);
  else
    threshold_str = g_strdup ("n/a");

  if (a->warn)
    begin_highlight ();

  g_print (" %-27s %s|%s|%s %s %-11s %s %s\n",
           a->name,
           current_str,
           worst_str,
           threshold_str,
           assessment,
           pretty,
           type,
           updates);

  if (a->warn)
    end_highlight ();

  g_free (current_str);
  g_free (worst_str);
  g_free (threshold_str);
  g_free (pretty);
}

static void
do_show_info (const char *object_path)
{
  guint n;
  DeviceProperties *props;
  struct tm *time_tm;
  time_t time;
  char time_buf[256];

  props = device_properties_get (bus, object_path);
  if (props == NULL)
    return;

  time = (time_t) props->device_detection_time;
  time_tm = localtime (&time);
  strftime (time_buf, sizeof time_buf, "%c", time_tm);

  g_print ("Showing information for %s\n", object_path);
  g_print ("  native-path:                 %s\n", props->native_path);
  g_print ("  device:                      %" G_GINT64_MODIFIER "d:%" G_GINT64_MODIFIER "d\n",
           props->device_major,
           props->device_minor);
  g_print ("  device-file:                 %s\n", props->device_file);
  g_print ("    presentation:              %s\n",
           (props->device_file_presentation != NULL && strlen (props->device_file_presentation) > 0) ?
           props->device_file_presentation : "(not set)");
  for (n = 0; props->device_file_by_id[n] != NULL; n++)
    g_print ("    by-id:                     %s\n", (char *) props->device_file_by_id[n]);
  for (n = 0; props->device_file_by_path[n] != NULL; n++)
    g_print ("    by-path:                   %s\n", (char *) props->device_file_by_path[n]);
  g_print ("  detected at:                 %s\n", time_buf);
  g_print ("  system internal:             %d\n", props->device_is_system_internal);
  g_print ("  removable:                   %d\n", props->device_is_removable);
  g_print ("  has media:                   %d", props->device_is_media_available);
  if (props->device_media_detection_time != 0)
    {
      time = (time_t) props->device_media_detection_time;
      time_tm = localtime (&time);
      strftime (time_buf, sizeof time_buf, "%c", time_tm);
      g_print (" (detected at %s)", time_buf);
    }
  g_print ("\n");
  g_print ("    detects change:            %d\n", props->device_is_media_change_detected);
  g_print ("    detection by polling:      %d\n", props->device_is_media_change_detection_polling);
  g_print ("    detection inhibitable:     %d\n", props->device_is_media_change_detection_inhibitable);
  g_print ("    detection inhibited:       %d\n", props->device_is_media_change_detection_inhibited);
  g_print ("  is read only:                %d\n", props->device_is_read_only);
  g_print ("  is mounted:                  %d\n", props->device_is_mounted);
  g_print ("  mount paths:             ");
  for (n = 0; props->device_mount_paths != NULL && props->device_mount_paths[n] != NULL; n++)
    {
      if (n != 0)
        g_print (", ");
      g_print ("%s", props->device_mount_paths[n]);
    }
  g_print ("\n");
  g_print ("  mounted by uid:              %d\n", props->device_mounted_by_uid);
  g_print ("  presentation hide:           %d\n", props->device_presentation_hide);
  g_print ("  presentation nopolicy:       %d\n", props->device_presentation_nopolicy);
  g_print ("  presentation name:           %s\n", props->device_presentation_name);
  g_print ("  presentation icon:           %s\n", props->device_presentation_icon_name);
  g_print ("  automount hint:              %s\n", props->device_automount_hint);
  g_print ("  size:                        %" G_GUINT64_FORMAT "\n", props->device_size);
  g_print ("  block size:                  %" G_GUINT64_FORMAT "\n", props->device_block_size);

  print_job (props->job_in_progress,
             props->job_id,
             props->job_initiated_by_uid,
             props->job_is_cancellable,
             props->job_percentage);
  g_print ("  usage:                       %s\n", props->id_usage);
  g_print ("  type:                        %s\n", props->id_type);
  g_print ("  version:                     %s\n", props->id_version);
  g_print ("  uuid:                        %s\n", props->id_uuid);
  g_print ("  label:                       %s\n", props->id_label);
  if (props->device_is_linux_md_component)
    {
      g_print ("  linux md component:\n");
      g_print ("    RAID level:                %s\n", props->linux_md_component_level);
      g_print ("    position:                  %d\n", props->linux_md_component_position);
      g_print ("    num components:            %d\n", props->linux_md_component_num_raid_devices);
      g_print ("    uuid:                      %s\n", props->linux_md_component_uuid);
      g_print ("    home host:                 %s\n", props->linux_md_component_home_host);
      g_print ("    name:                      %s\n", props->linux_md_component_name);
      g_print ("    version:                   %s\n", props->linux_md_component_version);
      g_print ("    holder:                    %s\n", g_strcmp0 (props->linux_md_component_holder, "/") == 0 ? "(none)"
               : props->linux_md_component_holder);
      g_print ("    state:                     ");
      for (n = 0; props->linux_md_component_state != NULL && props->linux_md_component_state[n] != NULL; n++)
        {
          if (n > 0)
            g_print (", ");
          g_print ("%s", props->linux_md_component_state[n]);
        }
      g_print ("\n");
    }
  if (props->device_is_linux_md)
    {
      g_print ("  linux md:\n");
      g_print ("    state:                     %s\n", props->linux_md_state);
      g_print ("    RAID level:                %s\n", props->linux_md_level);
      g_print ("    uuid:                      %s\n", props->linux_md_uuid);
      g_print ("    home host:                 %s\n", props->linux_md_home_host);
      g_print ("    name:                      %s\n", props->linux_md_name);
      g_print ("    num comp:                  %d\n", props->linux_md_num_raid_devices);
      g_print ("    version:                   %s\n", props->linux_md_version);
      g_print ("    degraded:                  %d\n", props->linux_md_is_degraded);
      g_print ("    sync action:               %s\n", props->linux_md_sync_action);
      if (strcmp (props->linux_md_sync_action, "idle") != 0)
        {
          g_print ("      complete:                %3.01f%%\n", props->linux_md_sync_percentage);
          g_print ("      speed:                   %" G_GINT64_FORMAT " bytes/sec\n", props->linux_md_sync_speed);
        }
      g_print ("    slaves:\n");
      for (n = 0; props->linux_md_slaves[n] != NULL; n++)
        g_print ("                      %s\n", props->linux_md_slaves[n]);
    }
  if (props->device_is_linux_lvm2_lv)
    {
      g_print ("  LVM2 Logical Volume:\n");
      g_print ("    LV name:                   %s\n", props->linux_lvm2_lv_name);
      g_print ("    LV uuid:                   %s\n", props->linux_lvm2_lv_uuid);
      g_print ("    VG name:                   %s\n", props->linux_lvm2_lv_group_name);
      g_print ("    VG uuid:                   %s\n", props->linux_lvm2_lv_group_uuid);
    }
  if (props->device_is_linux_lvm2_pv)
    {
      g_print ("  LVM2 Physical Volume:\n");
      g_print ("    PV uuid:                   %s\n", props->linux_lvm2_pv_uuid);
      g_print ("    PV num mda:                %d\n", props->linux_lvm2_pv_num_metadata_areas);
      g_print ("    VG name:                   %s\n", props->linux_lvm2_pv_group_name);
      g_print ("    VG uuid:                   %s\n", props->linux_lvm2_pv_group_uuid);
      g_print ("    VG size:                   %" G_GUINT64_FORMAT "\n", props->linux_lvm2_pv_group_size);
      g_print ("    VG unallocated size:       %" G_GUINT64_FORMAT "\n", props->linux_lvm2_pv_group_unallocated_size);
      g_print ("    VG extent size:            %" G_GUINT64_FORMAT "\n", props->linux_lvm2_pv_group_extent_size);
      g_print ("    VG sequence number:        %" G_GUINT64_FORMAT "\n", props->linux_lvm2_pv_group_sequence_number);
      g_print ("    Physical Volumes bound to the VG:\n");
      for (n = 0; props->linux_lvm2_pv_group_physical_volumes[n] != NULL; n++)
        g_print ("      %s\n", props->linux_lvm2_pv_group_physical_volumes[n]);
      g_print ("    Logical Volumes that are part of the VG:\n");
      for (n = 0; props->linux_lvm2_pv_group_logical_volumes[n] != NULL; n++)
        g_print ("      %s\n", props->linux_lvm2_pv_group_logical_volumes[n]);
    }
  if (props->device_is_linux_dmmp)
    {
      g_print ("  dm-multipath:\n");
      g_print ("    name:                      %s\n", props->linux_dmmp_name);
      g_print ("    parameters:                %s\n", props->linux_dmmp_parameters);
      g_print ("    components:\n");
      for (n = 0; props->linux_dmmp_slaves[n] != NULL; n++)
        g_print ("      %s\n", props->linux_dmmp_slaves[n]);
    }
  if (props->device_is_linux_dmmp_component)
    {
      g_print ("  dm-multipath component:\n");
      g_print ("    multipath device:          %s\n", props->linux_dmmp_component_holder);
    }
  if (props->device_is_linux_loop)
    {
      g_print ("  loop:\n");
      g_print ("    filename:                  %s\n", props->linux_loop_filename);
    }

  if (props->device_is_luks)
    {
      g_print ("  luks device:\n");
      g_print ("    holder:                    %s\n", props->luks_holder);
    }
  if (props->device_is_luks_cleartext)
    {
      g_print ("  cleartext luks device:\n");
      g_print ("    backed by:                 %s\n", props->luks_cleartext_slave);
      g_print ("    unlocked by:               uid %d\n", props->luks_cleartext_unlocked_by_uid);
    }
  if (props->device_is_partition_table)
    {
      g_print ("  partition table:\n");
      g_print ("    scheme:                    %s\n", props->partition_table_scheme);
      g_print ("    count:                     %d\n", props->partition_table_count);
    }
  if (props->device_is_partition)
    {
      g_print ("  partition:\n");
      g_print ("    part of:                   %s\n", props->partition_slave);
      g_print ("    scheme:                    %s\n", props->partition_scheme);
      g_print ("    number:                    %d\n", props->partition_number);
      g_print ("    type:                      %s\n", props->partition_type);
      g_print ("    flags:                    ");
      for (n = 0; props->partition_flags[n] != NULL; n++)
        g_print (" %s", (char *) props->partition_flags[n]);
      g_print ("\n");
      g_print ("    offset:                    %" G_GINT64_FORMAT "\n", props->partition_offset);
      if (props->partition_alignment_offset != 0)
        begin_highlight ();
      g_print ("    alignment offset:          %" G_GINT64_FORMAT "\n", props->partition_alignment_offset);
      if (props->partition_alignment_offset != 0)
        end_highlight ();
      g_print ("    size:                      %" G_GINT64_FORMAT "\n", props->partition_size);
      g_print ("    label:                     %s\n", props->partition_label);
      g_print ("    uuid:                      %s\n", props->partition_uuid);
    }
  if (props->device_is_optical_disc)
    {
      g_print ("  optical disc:\n");
      g_print ("    blank:                     %d\n", props->optical_disc_is_blank);
      g_print ("    appendable:                %d\n", props->optical_disc_is_appendable);
      g_print ("    closed:                    %d\n", props->optical_disc_is_closed);
      g_print ("    num tracks:                %d\n", props->optical_disc_num_tracks);
      g_print ("    num audio tracks:          %d\n", props->optical_disc_num_audio_tracks);
      g_print ("    num sessions:              %d\n", props->optical_disc_num_sessions);
    }
  if (props->device_is_drive)
    {
      g_print ("  drive:\n");
      g_print ("    vendor:                    %s\n", props->drive_vendor);
      g_print ("    model:                     %s\n", props->drive_model);
      g_print ("    revision:                  %s\n", props->drive_revision);
      g_print ("    serial:                    %s\n", props->drive_serial);
      g_print ("    WWN:                       %s\n", props->drive_wwn);
      g_print ("    detachable:                %d\n", props->drive_can_detach);
      g_print ("    can spindown:              %d\n", props->drive_can_spindown);
      if (props->drive_is_rotational)
        {
          if (props->drive_rotation_rate > 0)
            {
              g_print ("    rotational media:          Yes, at %d RPM\n", props->drive_rotation_rate);
            }
          else
            {
              g_print ("    rotational media:          Yes, unknown rate\n");
            }
        }
      else
        {
          g_print ("    rotational media:          No\n");
        }
      if (props->drive_write_cache == NULL || strlen (props->drive_write_cache) == 0)
        {
          g_print ("    write-cache:               unknown\n");
        }
      else
        {
          g_print ("    write-cache:               %s\n", props->drive_write_cache);
        }
      g_print ("    ejectable:                 %d\n", props->drive_is_media_ejectable);
      g_print ("    adapter:                   %s\n", strlen (props->drive_adapter) > 1 ? props->drive_adapter : "Unknown");
      g_print ("    ports:\n");
      for (n = 0; props->drive_ports != NULL && props->drive_ports[n] != NULL; n++)
        {
          g_print ("      %s\n", props->drive_ports[n]);
        }
      g_print ("    similar devices:\n");
      for (n = 0; props->drive_similar_devices != NULL && props->drive_similar_devices[n] != NULL; n++)
        {
          g_print ("      %s\n", props->drive_similar_devices[n]);
        }
      if (props->drive_similar_devices != NULL && g_strv_length (props->drive_similar_devices) > 0)
        {
          if (!props->device_is_linux_dmmp_component && !props->device_is_linux_dmmp)
            {
              begin_highlight ();
              g_print ("      WARNING: Multiple devices with this serial and/or WWN has been detected\n"
                       "               but dm-multipath is not active for these devices.\n");
              end_highlight ();
            }
        }
      g_print ("    media:                     %s\n", props->drive_media);
      g_print ("      compat:                 ");
      for (n = 0; props->drive_media_compatibility[n] != NULL; n++)
        g_print (" %s", (char *) props->drive_media_compatibility[n]);
      g_print ("\n");
      if (props->drive_connection_interface == NULL || strlen (props->drive_connection_interface) == 0)
        g_print ("    interface:                 (unknown)\n");
      else
        g_print ("    interface:                 %s\n", props->drive_connection_interface);
      if (props->drive_connection_speed == 0)
        g_print ("    if speed:                  (unknown)\n");
      else
        g_print ("    if speed:                  %" G_GINT64_FORMAT " bits/s\n", props->drive_connection_speed);

      /* ------------------------------------------------------------------------------------------------- */

      if (!props->drive_ata_smart_is_available)
        {
          g_print ("    ATA SMART:                 not available\n");
        }
      else if (props->drive_ata_smart_time_collected == 0)
        {
          g_print ("    ATA SMART:                 Data not collected\n");
        }
      else
        {
          SkDisk *d;

          time = (time_t) props->drive_ata_smart_time_collected;
          time_tm = localtime (&time);
          strftime (time_buf, sizeof time_buf, "%c", time_tm);

          g_print ("    ATA SMART:                 Updated at %s\n", time_buf);

          if (props->drive_ata_smart_status == NULL || strlen (props->drive_ata_smart_status) == 0)
            {
              g_print ("      overall assessment:      UNKNOWN\n");
            }
          else
            {
              const gchar *status_desc;
              gboolean do_highlight;

              status_desc = ata_smart_status_to_desc (props->drive_ata_smart_status, &do_highlight);

              if (do_highlight)
                begin_highlight ();
              g_print ("      overall assessment:      %s\n", status_desc);
              if (do_highlight)
                end_highlight ();
            }

          if (sk_disk_open (NULL, &d) == 0)
            {
              if (sk_disk_set_blob (d,
                                    props->drive_ata_smart_blob,
                                    props->drive_ata_smart_blob_size) == 0)
                {
                  g_print ("===============================================================================\n");
                  g_print (" Attribute       Current|Worst|Threshold  Status   Value       Type     Updates\n");
                  g_print ("===============================================================================\n");

                  sk_disk_smart_parse_attributes (d, print_ata_smart_attr, NULL);
                }
              sk_disk_free (d);
            }

        }

      /* ------------------------------------------------------------------------------------------------- */

    }
  device_properties_free (props);
}

/* ---------------------------------------------------------------------------------------------------- */

static gint
do_poll_for_media (const char *object_path)
{
  DBusGProxy *proxy;
  GError *error;
  gint ret;

  ret = 1;

  proxy = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.UDisks.Device");

  error = NULL;
  if (!org_freedesktop_UDisks_Device_drive_poll_media (proxy, &error))
    {
      g_print ("Poll for media failed: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  ret = 0;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gint
do_inhibit_polling (const char *object_path,
                    gint argc,
                    gchar *argv[])
{
  char *cookie;
  DBusGProxy *proxy;
  GError *error;
  char **options;
  gint ret;

  options = NULL;
  cookie = NULL;
  ret = 127;

  if (argc > 0 && strcmp (argv[0], "--") == 0)
    {
      argv++;
      argc--;
    }

  proxy = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.UDisks.Device");

  error = NULL;
  if (!org_freedesktop_UDisks_Device_drive_inhibit_polling (proxy, (const char **) options, &cookie, &error))
    {
      g_print ("Inhibit polling failed: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  if (argc == 0)
    {
      g_print ("Inhibiting polling on %s. Press Ctrl+C to exit.\n", object_path);
      while (TRUE)
        sleep (100000000);
    }
  else
    {
      GError * error;
      gint exit_status;

      error = NULL;
      if (!g_spawn_sync (NULL, /* working dir */
                         argv, NULL, /* envp */
                         G_SPAWN_SEARCH_PATH, NULL, /* child_setup */
                         NULL, /* user_data */
                         NULL, /* standard_output */
                         NULL, /* standard_error */
                         &exit_status, /* exit_status */
                         &error))
        {
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
do_inhibit_all_polling (gint argc,
                        gchar *argv[])
{
  char *cookie;
  DBusGProxy *proxy;
  GError *error;
  char **options;
  gint ret;

  options = NULL;
  cookie = NULL;
  ret = 127;

  if (argc > 0 && strcmp (argv[0], "--") == 0)
    {
      argv++;
      argc--;
    }

  proxy
    = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", "/org/freedesktop/UDisks", "org.freedesktop.UDisks");

  error = NULL;
  if (!org_freedesktop_UDisks_drive_inhibit_all_polling (proxy, (const char **) options, &cookie, &error))
    {
      g_print ("Inhibit all polling failed: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  if (argc == 0)
    {
      g_print ("Inhibiting polling on all devices. Press Ctrl+C to exit.\n");
      while (TRUE)
        sleep (100000000);
    }
  else
    {
      GError * error;
      gint exit_status;

      error = NULL;
      if (!g_spawn_sync (NULL, /* working dir */
                         argv, NULL, /* envp */
                         G_SPAWN_SEARCH_PATH, NULL, /* child_setup */
                         NULL, /* user_data */
                         NULL, /* standard_output */
                         NULL, /* standard_error */
                         &exit_status, /* exit_status */
                         &error))
        {
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
do_set_spindown (const char *object_path,
                 gint argc,
                 gchar *argv[])
{
  char *cookie;
  DBusGProxy *proxy;
  GError *error;
  char **options;
  gint ret;

  options = NULL;
  cookie = NULL;
  ret = 127;

  if (argc > 0 && strcmp (argv[0], "--") == 0)
    {
      argv++;
      argc--;
    }

  proxy = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", object_path, "org.freedesktop.UDisks.Device");

  error = NULL;
  if (!org_freedesktop_UDisks_Device_drive_set_spindown_timeout (proxy,
                                                                 opt_spindown_seconds,
                                                                 (const char **) options,
                                                                 &cookie,
                                                                 &error))
    {
      g_print ("Setting spindown failed: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  if (argc == 0)
    {
      g_print ("Set spindown on %s to %d seconds. Press Ctrl+C to exit.\n", object_path, opt_spindown_seconds);
      while (TRUE)
        sleep (100000000);
    }
  else
    {
      GError * error;
      gint exit_status;

      error = NULL;
      if (!g_spawn_sync (NULL, /* working dir */
                         argv, NULL, /* envp */
                         G_SPAWN_SEARCH_PATH, NULL, /* child_setup */
                         NULL, /* user_data */
                         NULL, /* standard_output */
                         NULL, /* standard_error */
                         &exit_status, /* exit_status */
                         &error))
        {
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
do_set_spindown_all (gint argc,
                     gchar *argv[])
{
  char *cookie;
  DBusGProxy *proxy;
  GError *error;
  char **options;
  gint ret;

  options = NULL;
  cookie = NULL;
  ret = 127;

  if (argc > 0 && strcmp (argv[0], "--") == 0)
    {
      argv++;
      argc--;
    }

  proxy
    = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", "/org/freedesktop/UDisks", "org.freedesktop.UDisks");

  error = NULL;
  if (!org_freedesktop_UDisks_drive_set_all_spindown_timeouts (proxy,
                                                               opt_spindown_seconds,
                                                               (const char **) options,
                                                               &cookie,
                                                               &error))
    {
      g_print ("Setting spindown failed: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  if (argc == 0)
    {
      g_print ("Set spindown for all drives to %d seconds. Press Ctrl+C to exit.\n", opt_spindown_seconds);
      while (TRUE)
        sleep (100000000);
    }
  else
    {
      GError * error;
      gint exit_status;

      error = NULL;
      if (!g_spawn_sync (NULL, /* working dir */
                         argv, NULL, /* envp */
                         G_SPAWN_SEARCH_PATH, NULL, /* child_setup */
                         NULL, /* user_data */
                         NULL, /* standard_output */
                         NULL, /* standard_error */
                         &exit_status, /* exit_status */
                         &error))
        {
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
do_inhibit (gint argc,
            gchar *argv[])
{
  char *cookie;
  DBusGProxy *proxy;
  GError *error;
  gint ret;

  cookie = NULL;
  ret = 127;

  if (argc > 0 && strcmp (argv[0], "--") == 0)
    {
      argv++;
      argc--;
    }

  proxy
    = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", "/org/freedesktop/UDisks", "org.freedesktop.UDisks");

  error = NULL;
  if (!org_freedesktop_UDisks_inhibit (proxy, &cookie, &error))
    {
      g_print ("Inhibit all polling failed: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  if (argc == 0)
    {
      g_print ("Inhibiting the daemon. Press Ctrl+C to exit.\n");
      while (TRUE)
        sleep (100000000);
    }
  else
    {
      GError * error;
      gint exit_status;

      error = NULL;
      if (!g_spawn_sync (NULL, /* working dir */
                         argv, NULL, /* envp */
                         G_SPAWN_SEARCH_PATH, NULL, /* child_setup */
                         NULL, /* user_data */
                         NULL, /* standard_output */
                         NULL, /* standard_error */
                         &exit_status, /* exit_status */
                         &error))
        {
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
ptr_str_array_compare (const gchar **a,
                       const gchar **b)
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

  if (stat (device_file, &statbuf) != 0)
    {
      g_print ("Cannot stat device file %s: %m\n", device_file);
      goto out;
    }

  if (!S_ISBLK (statbuf.st_mode))
    {
      g_print ("Device file %s is not a block device: %m\n", device_file);
      goto out;
    }

  proxy
    = dbus_g_proxy_new_for_name (bus, "org.freedesktop.UDisks", "/org/freedesktop/UDisks", "org.freedesktop.UDisks");

  error = NULL;
  if (!org_freedesktop_UDisks_find_device_by_major_minor (proxy,
                                                          major (statbuf.st_rdev),
                                                          minor (statbuf.st_rdev),
                                                          &object_path,
                                                          &error))
    {
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
main (int argc,
      char **argv)
{
  int ret;
  GOptionContext *context;
  GError *error = NULL;
  unsigned int n;
  gchar *device_file;
  static GOptionEntry
    entries[] =
    {
      { "enumerate", 0, 0, G_OPTION_ARG_NONE, &opt_enumerate, "Enumerate objects paths for devices", NULL },
      { "enumerate-device-files", 0, 0, G_OPTION_ARG_NONE, &opt_enumerate_device_files,
        "Enumerate device files for devices", NULL },
      { "dump", 0, 0, G_OPTION_ARG_NONE, &opt_dump, "Dump all information about all devices", NULL },
      { "monitor", 0, 0, G_OPTION_ARG_NONE, &opt_monitor, "Monitor activity from the disk daemon", NULL },
      { "monitor-detail", 0, 0, G_OPTION_ARG_NONE, &opt_monitor_detail, "Monitor with detail", NULL },
      { "show-info", 0, 0, G_OPTION_ARG_STRING, &opt_show_info, "Show information about a device file", NULL },
      { "inhibit-polling", 0, 0, G_OPTION_ARG_STRING, &opt_inhibit_polling, "Inhibit polling", NULL },
      { "inhibit-all-polling", 0, 0, G_OPTION_ARG_NONE, &opt_inhibit_all_polling, "Inhibit all polling", NULL },
      { "poll-for-media", 0, 0, G_OPTION_ARG_STRING, &opt_poll_for_media, "Poll for media", NULL },
      { "set-spindown", 0, 0, G_OPTION_ARG_STRING, &opt_drive_spindown, "Set spindown timeout for drive", NULL },
      { "set-spindown-all", 0, 0, G_OPTION_ARG_NONE, &opt_drive_spindown_all,
        "Set spindown timeout for all drives", NULL },
      { "spindown-timeout", 0, 0, G_OPTION_ARG_INT, &opt_spindown_seconds, "Spindown timeout in seconds", NULL },
      { "inhibit", 0, 0, G_OPTION_ARG_NONE, &opt_inhibit, "Inhibit the daemon", NULL },

      { "mount", 0, 0, G_OPTION_ARG_STRING, &opt_mount, "Mount the given device", NULL },
      { "mount-fstype", 0, 0, G_OPTION_ARG_STRING, &opt_mount_fstype, "Specify file system type", NULL },
      { "mount-options", 0, 0, G_OPTION_ARG_STRING, &opt_mount_options, "Mount options separated by comma",
        NULL },

      { "unmount", 0, 0, G_OPTION_ARG_STRING, &opt_unmount, "Unmount the given device", NULL },
      { "unmount-options", 0, 0, G_OPTION_ARG_STRING, &opt_unmount_options,
        "Unmount options separated by comma", NULL },
      { "detach", 0, 0, G_OPTION_ARG_STRING, &opt_detach, "Detach the given device", NULL },
      { "detach-options", 0, 0, G_OPTION_ARG_STRING, &opt_detach_options, "Detach options separated by comma",
        NULL },
      { "eject", 0, 0, G_OPTION_ARG_STRING, &opt_eject, "Eject the given device", NULL },
      { "eject-options", 0, 0, G_OPTION_ARG_STRING, &opt_eject_options, "Eject options separated by comma", NULL },
      { "ata-smart-refresh", 0, 0, G_OPTION_ARG_STRING, &opt_ata_smart_refresh, "Refresh ATA SMART data", NULL },
      { "ata-smart-wakeup", 0, 0, G_OPTION_ARG_NONE, &opt_ata_smart_wakeup,
        "Wake up the disk if it is not awake", NULL },
      { "ata-smart-simulate", 0, 0, G_OPTION_ARG_STRING, &opt_ata_smart_simulate,
        "Inject libatasmart BLOB for testing", NULL },
      { NULL } };

  setlocale (LC_ALL, "");

  ret = 1;
  device_file = NULL;

  g_type_init ();

  context = g_option_context_new ("udisks commandline tool");
  g_option_context_set_description (context, "See the udisks man page for details.");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
  if (bus == NULL)
    {
      g_warning ("Couldn't connect to system bus: %s", error->message);
      g_error_free (error);
      goto out;
    }

  dbus_g_object_register_marshaller (udisks_marshal_VOID__BOXED_BOOLEAN_STRING_UINT_BOOLEAN_DOUBLE,
                                     G_TYPE_NONE,
                                     DBUS_TYPE_G_OBJECT_PATH,
                                     G_TYPE_BOOLEAN,
                                     G_TYPE_STRING,
                                     G_TYPE_UINT,
                                     G_TYPE_BOOLEAN,
                                     G_TYPE_DOUBLE,
                                     G_TYPE_INVALID);

  disks_proxy = dbus_g_proxy_new_for_name (bus,
                                           "org.freedesktop.UDisks",
                                           "/org/freedesktop/UDisks",
                                           "org.freedesktop.UDisks");
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

  if (opt_dump)
    {
      GPtrArray *devices;
      if (!org_freedesktop_UDisks_enumerate_devices (disks_proxy, &devices, &error))
        {
          g_warning ("Couldn't enumerate devices: %s", error->message);
          g_error_free (error);
          goto out;
        }
      g_ptr_array_sort (devices, (GCompareFunc) ptr_str_array_compare);
      g_print ("========================================================================\n");
      for (n = 0; n < devices->len; n++)
        {
          char *object_path = devices->pdata[n];
          do_show_info (object_path);
          g_print ("\n"
                   "========================================================================\n");
        }
      g_ptr_array_foreach (devices, (GFunc) g_free, NULL);
      g_ptr_array_free (devices, TRUE);
    }
  else if (opt_enumerate)
    {
      GPtrArray *devices;
      if (!org_freedesktop_UDisks_enumerate_devices (disks_proxy, &devices, &error))
        {
          g_warning ("Couldn't enumerate devices: %s", error->message);
          g_error_free (error);
          goto out;
        }
      for (n = 0; n < devices->len; n++)
        {
          char *object_path = devices->pdata[n];
          g_print ("%s\n", object_path);
        }
      g_ptr_array_foreach (devices, (GFunc) g_free, NULL);
      g_ptr_array_free (devices, TRUE);
    }
  else if (opt_enumerate_device_files)
    {
      gchar **device_files;
      if (!org_freedesktop_UDisks_enumerate_device_files (disks_proxy, &device_files, &error))
        {
          g_warning ("Couldn't enumerate device files: %s", error->message);
          g_error_free (error);
          goto out;
        }
      for (n = 0; device_files != NULL && device_files[n] != NULL; n++)
        {
          g_print ("%s\n", device_files[n]);
        }
      g_strfreev (device_files);
    }
  else if (opt_monitor || opt_monitor_detail)
    {
      if (!do_monitor ())
        goto out;
    }
  else if (opt_show_info != NULL)
    {
      device_file = device_file_to_object_path (opt_show_info);
      if (device_file == NULL)
        goto out;
      do_show_info (device_file);
    }
  else if (opt_inhibit_polling != NULL)
    {
      device_file = device_file_to_object_path (opt_inhibit_polling);
      if (device_file == NULL)
        goto out;
      ret = do_inhibit_polling (device_file, argc - 1, argv + 1);
      goto out;
    }
  else if (opt_poll_for_media != NULL)
    {
      device_file = device_file_to_object_path (opt_poll_for_media);
      if (device_file == NULL)
        goto out;
      ret = do_poll_for_media (device_file);
      goto out;
    }
  else if (opt_inhibit_all_polling)
    {
      ret = do_inhibit_all_polling (argc - 1, argv + 1);
      goto out;
    }
  else if (opt_drive_spindown != NULL)
    {
      device_file = device_file_to_object_path (opt_drive_spindown);
      if (device_file == NULL)
        goto out;
      ret = do_set_spindown (device_file, argc - 1, argv + 1);
      goto out;
    }
  else if (opt_drive_spindown_all)
    {
      ret = do_set_spindown_all (argc - 1, argv + 1);
      goto out;
    }
  else if (opt_inhibit)
    {
      ret = do_inhibit (argc - 1, argv + 1);
      goto out;
    }
  else if (opt_mount != NULL)
    {
      device_file = device_file_to_object_path (opt_mount);
      if (device_file == NULL)
        goto out;
      do_mount (device_file, opt_mount_fstype, opt_mount_options);
    }
  else if (opt_unmount != NULL)
    {
      device_file = device_file_to_object_path (opt_unmount);
      if (device_file == NULL)
        goto out;
      do_unmount (device_file, opt_unmount_options);
    }
  else if (opt_detach != NULL)
    {
      device_file = device_file_to_object_path (opt_detach);
      if (device_file == NULL)
        goto out;
      do_detach (device_file, opt_detach_options);
    }
  else if (opt_eject != NULL)
    {
      device_file = device_file_to_object_path (opt_eject);
      if (device_file == NULL)
        goto out;
      do_eject (device_file, opt_eject_options);
    }
  else if (opt_ata_smart_refresh != NULL)
    {
      device_file = device_file_to_object_path (opt_ata_smart_refresh);
      if (device_file == NULL)
        goto out;
      do_ata_smart_refresh (device_file, opt_ata_smart_wakeup, opt_ata_smart_simulate);
    }
  else
    {
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
