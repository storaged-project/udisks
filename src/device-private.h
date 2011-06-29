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

#ifndef __DEVICE_PRIVATE_H__
#define __DEVICE_PRIVATE_H__

#include <dbus/dbus-glib.h>
#include <gudev/gudev.h>
#include <atasmart.h>

#include "types.h"

G_BEGIN_DECLS

struct Job;
typedef struct Job Job;

#define LSOF_DATA_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                       G_TYPE_UINT,     \
                                                       G_TYPE_UINT,     \
                                                       G_TYPE_STRING,   \
                                                       G_TYPE_INVALID))

struct DevicePrivate
{
  DBusGConnection *system_bus_connection;
  Daemon *daemon;
  GUdevDevice *d;

  Job *job;

  char *object_path;
  char *native_path;
  guint64 device_detection_time;
  guint64 device_media_detection_time;

  gboolean removed;

  gboolean job_in_progress;
  char *job_id;
  uid_t job_initiated_by_uid;
  gboolean job_is_cancellable;
  double job_percentage;

  gboolean checked_in_kernel_polling;
  gboolean using_in_kernel_polling;

  guint linux_md_poll_timeout_id;

  /* A list of current polling inhibitors (Inhibitor objects) */
  GList *polling_inhibitors;

  /* if non-zero, the id of the idle for emitting a 'change' signal */
  guint emit_changed_idle_id;

  /*****************/
  /* Disk spindown */
  /*****************/

  /* A list of current spindown configurators (Inhibitor objects)
   *
   * Each object will have a data element, @spindown-timeout-seconds, that is
   * the requested timeout for the inhibitor in question. It can be read via
   *
   *  GPOINTER_TO_INT (g_object_get_data (G_OBJECT (inhibitor), "spindown-timeout-seconds"));
   */
  GList *spindown_inhibitors;

  /* The timeout the disk is currently configured with, in seconds. This is 0 if spindown
   * is not enabled. Depending on the command-set used, a slightly different rounded value
   * may have been sent to the disk - for example, the ATA command-set has a rather peculiar
   * mapping, see the hdparm(1) man-page, option -S.
   *
   * This value is computed by considering all per-disk spindown inhibitors (set
   * via the DriveSetSpindownTimeout() method on the device) and all global spindown
   * inhibitors (set via the DriveSetAllSpindownTimeouts() method on the daemon).
   */
  gint spindown_timeout;

  /**************/
  /* Properties */
  /**************/

  char *device_file;
  char *device_file_presentation;
  dev_t dev;
  GPtrArray *device_file_by_id;
  GPtrArray *device_file_by_path;
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
  gboolean device_is_linux_md_component;
  gboolean device_is_linux_md;
  gboolean device_is_linux_lvm2_lv;
  gboolean device_is_linux_lvm2_pv;
  gboolean device_is_linux_dmmp;
  gboolean device_is_linux_dmmp_component;
  gboolean device_is_linux_loop;
  guint64 device_size;
  guint64 device_block_size;
  gboolean device_is_mounted;
  GPtrArray *device_mount_paths;
  uid_t device_mounted_by_uid;
  gboolean device_presentation_hide;
  gboolean device_presentation_nopolicy;
  char *device_presentation_name;
  char *device_presentation_icon_name;
  char *device_automount_hint;

  char *id_usage;
  char *id_type;
  char *id_version;
  char *id_uuid;
  char *id_label;

  char *partition_slave;
  char *partition_scheme;
  char *partition_type;
  char *partition_label;
  char *partition_uuid;
  GPtrArray *partition_flags;
  int partition_number;
  guint64 partition_offset;
  guint64 partition_size;
  guint64 partition_alignment_offset;

  char *partition_table_scheme;
  int partition_table_count;

  char *drive_vendor;
  char *drive_model;
  char *drive_revision;
  char *drive_serial;
  char *drive_wwn;
  char *drive_connection_interface;
  guint drive_connection_speed;
  GPtrArray *drive_media_compatibility;
  char *drive_media;
  gboolean drive_is_media_ejectable;
  gboolean drive_can_detach;
  gboolean drive_can_spindown;
  gboolean drive_is_rotational;
  guint drive_rotation_rate;
  char *drive_write_cache;
  char *drive_adapter;
  GPtrArray *drive_ports;
  GPtrArray *drive_similar_devices;

  gboolean optical_disc_is_blank;
  gboolean optical_disc_is_appendable;
  gboolean optical_disc_is_closed;
  guint optical_disc_num_tracks;
  guint optical_disc_num_audio_tracks;
  guint optical_disc_num_sessions;

  char *luks_holder;

  char *luks_cleartext_slave;
  uid_t luks_cleartext_unlocked_by_uid;

  char *linux_md_component_level;
  int linux_md_component_position;
  int linux_md_component_num_raid_devices;
  char *linux_md_component_uuid;
  char *linux_md_component_home_host;
  char *linux_md_component_name;
  char *linux_md_component_version;
  char *linux_md_component_holder;
  GPtrArray *linux_md_component_state;

  char *linux_md_state;
  char *linux_md_level;
  int linux_md_num_raid_devices;
  char *linux_md_uuid;
  char *linux_md_home_host;
  char *linux_md_name;
  char *linux_md_version;
  GPtrArray *linux_md_slaves;
  GPtrArray *linux_md_slaves_state;
  gboolean linux_md_is_degraded;
  char *linux_md_sync_action;
  double linux_md_sync_percentage;
  guint64 linux_md_sync_speed;

  gchar *linux_lvm2_lv_name;
  gchar *linux_lvm2_lv_uuid;
  gchar *linux_lvm2_lv_group_name;
  gchar *linux_lvm2_lv_group_uuid;

  gchar *linux_lvm2_pv_uuid;
  guint linux_lvm2_pv_num_metadata_areas;
  gchar *linux_lvm2_pv_group_name;
  gchar *linux_lvm2_pv_group_uuid;
  guint64 linux_lvm2_pv_group_size;
  guint64 linux_lvm2_pv_group_unallocated_size;
  guint64 linux_lvm2_pv_group_sequence_number;
  guint64 linux_lvm2_pv_group_extent_size;
  GPtrArray *linux_lvm2_pv_group_physical_volumes;
  GPtrArray *linux_lvm2_pv_group_logical_volumes;

  gboolean drive_ata_smart_is_available;
  guint64 drive_ata_smart_time_collected;
  SkSmartOverall drive_ata_smart_status;
  void *drive_ata_smart_blob;
  gsize drive_ata_smart_blob_size;

  gchar *linux_dmmp_component_holder;

  gchar *linux_dmmp_name;
  GPtrArray *linux_dmmp_slaves;
  gchar *linux_dmmp_parameters;

  gchar *linux_loop_filename;

  /* the following properties are not (yet) exported */
  char *dm_name;
  GPtrArray *slaves_objpath;
  GPtrArray *holders_objpath;
};

/* property setters */

void device_set_job_in_progress (Device *device, gboolean value);
void device_set_job_id (Device *device, const gchar *value);
void device_set_job_initiated_by_uid (Device *device, guint value);
void device_set_job_is_cancellable (Device *device, gboolean value);
void device_set_job_percentage (Device *device, gdouble value);

void device_set_device_automount_hint (Device *device, const gchar *value);
void device_set_device_detection_time (Device *device, guint64 value);
void device_set_device_media_detection_time (Device *device, guint64 value);
void device_set_device_file (Device *device, const gchar *value);
void device_set_device_file_presentation (Device *device, const gchar *value);
void device_set_device_file_by_id (Device *device, GStrv value);
void device_set_device_file_by_path (Device *device, GStrv value);
void device_set_device_is_system_internal (Device *device, gboolean value);
void device_set_device_is_partition (Device *device, gboolean value);
void device_set_device_is_partition_table (Device *device, gboolean value);
void device_set_device_is_removable (Device *device, gboolean value);
void device_set_device_is_media_available (Device *device, gboolean value);
void device_set_device_is_media_change_detected (Device *device, gboolean value);
void device_set_device_is_media_change_detection_polling (Device *device, gboolean value);
void device_set_device_is_media_change_detection_inhibitable (Device *device, gboolean value);
void device_set_device_is_media_change_detection_inhibited (Device *device, gboolean value);
void device_set_device_is_read_only (Device *device, gboolean value);
void device_set_device_is_drive (Device *device, gboolean value);
void device_set_device_is_optical_disc (Device *device, gboolean value);
void device_set_device_is_luks (Device *device, gboolean value);
void device_set_device_is_luks_cleartext (Device *device, gboolean value);
void device_set_device_is_linux_md_component (Device *device, gboolean value);
void device_set_device_is_linux_md (Device *device, gboolean value);
void device_set_device_is_linux_lvm2_lv (Device *device, gboolean value);
void device_set_device_is_linux_lvm2_pv (Device *device, gboolean value);
void device_set_device_is_linux_dmmp (Device *device, gboolean value);
void device_set_device_is_linux_dmmp_component (Device *device, gboolean value);
void device_set_device_is_linux_loop (Device *device, gboolean value);
void device_set_device_size (Device *device, guint64 value);
void device_set_device_block_size (Device *device, guint64 value);
void device_set_device_is_mounted (Device *device, gboolean value);
void device_set_device_mount_paths (Device *device, GStrv value);
void device_set_device_mounted_by_uid (Device *device, guint value);
void device_set_device_presentation_hide (Device *device, gboolean value);
void device_set_device_presentation_nopolicy (Device *device, gboolean value);
void device_set_device_presentation_name (Device *device, const gchar *value);
void device_set_device_presentation_icon_name (Device *device, const gchar *value);

void device_set_id_usage (Device *device, const gchar *value);
void device_set_id_type (Device *device, const gchar *value);
void device_set_id_version (Device *device, const gchar *value);
void device_set_id_uuid (Device *device, const gchar *value);
void device_set_id_label (Device *device, const gchar *value);

void device_set_partition_slave (Device *device, const gchar *value);
void device_set_partition_scheme (Device *device, const gchar *value);
void device_set_partition_type (Device *device, const gchar *value);
void device_set_partition_label (Device *device, const gchar *value);
void device_set_partition_uuid (Device *device, const gchar *value);
void device_set_partition_flags (Device *device, GStrv value);
void device_set_partition_number (Device *device, gint value);
void device_set_partition_offset (Device *device, guint64 value);
void device_set_partition_size (Device *device, guint64 value);
void device_set_partition_alignment_offset (Device *device, guint64 value);

void device_set_partition_table_scheme (Device *device, const gchar *value);
void device_set_partition_table_count (Device *device, gint value);

void device_set_drive_vendor (Device *device, const gchar *value);
void device_set_drive_model (Device *device, const gchar *value);
void device_set_drive_revision (Device *device, const gchar *value);
void device_set_drive_serial (Device *device, const gchar *value);
void device_set_drive_wwn (Device *device, const gchar *value);
void device_set_drive_connection_interface (Device *device, const gchar *value);
void device_set_drive_connection_speed (Device *device, guint value);
void device_set_drive_media_compatibility (Device *device, GStrv value);
void device_set_drive_media (Device *device, const gchar *value);
void device_set_drive_is_media_ejectable (Device *device, gboolean value);
void device_set_drive_can_detach (Device *device, gboolean value);
void device_set_drive_can_spindown (Device *device, gboolean value);
void device_set_drive_is_rotational (Device *device, gboolean value);
void device_set_drive_rotation_rate (Device *device, guint value);
void device_set_drive_write_cache (Device *device, const gchar *value);
void device_set_drive_adapter (Device *device, const gchar *value);
void device_set_drive_ports (Device *device, GStrv value);
void device_set_drive_similar_devices (Device *device, GStrv value);

void device_set_optical_disc_is_blank (Device *device, gboolean value);
void device_set_optical_disc_is_appendable (Device *device, gboolean value);
void device_set_optical_disc_is_closed (Device *device, gboolean value);
void device_set_optical_disc_num_tracks (Device *device, guint value);
void device_set_optical_disc_num_audio_tracks (Device *device, guint value);
void device_set_optical_disc_num_sessions (Device *device, guint value);

void device_set_luks_holder (Device *device, const gchar *value);

void device_set_luks_cleartext_slave (Device *device, const gchar *value);
void device_set_luks_cleartext_unlocked_by_uid (Device *device, guint value);

void device_set_linux_md_component_level (Device *device, const gchar *value);
void device_set_linux_md_component_position (Device *device, gint value);
void device_set_linux_md_component_num_raid_devices (Device *device, gint value);
void device_set_linux_md_component_uuid (Device *device, const gchar *value);
void device_set_linux_md_component_home_host (Device *device, const gchar *value);
void device_set_linux_md_component_name (Device *device, const gchar *value);
void device_set_linux_md_component_version (Device *device, const gchar *value);
void device_set_linux_md_component_holder (Device *device, const gchar *value);
void device_set_linux_md_component_state (Device *device, GStrv value);

void device_set_linux_md_state (Device *device, const gchar *value);
void device_set_linux_md_level (Device *device, const gchar *value);
void device_set_linux_md_num_raid_devices (Device *device, gint value);
void device_set_linux_md_uuid (Device *device, const gchar *value);
void device_set_linux_md_home_host (Device *device, const gchar *value);
void device_set_linux_md_name (Device *device, const gchar *value);
void device_set_linux_md_version (Device *device, const gchar *value);
void device_set_linux_md_slaves (Device *device, GStrv value);
void device_set_linux_md_is_degraded (Device *device, gboolean value);
void device_set_linux_md_sync_action (Device *device, const gchar *value);
void device_set_linux_md_sync_percentage (Device *device, gdouble value);
void device_set_linux_md_sync_speed (Device *device, guint64 value);

void device_set_linux_lvm2_lv_name (Device *device, const gchar *value);
void device_set_linux_lvm2_lv_uuid (Device *device, const gchar *value);
void device_set_linux_lvm2_lv_group_name (Device *device, const gchar *value);
void device_set_linux_lvm2_lv_group_uuid (Device *device, const gchar *value);

void device_set_linux_lvm2_pv_uuid (Device *device, const gchar *value);
void device_set_linux_lvm2_pv_num_metadata_areas (Device *device, guint value);
void device_set_linux_lvm2_pv_group_name (Device *device, const gchar *value);
void device_set_linux_lvm2_pv_group_uuid (Device *device, const gchar *value);
void device_set_linux_lvm2_pv_group_size (Device *device, guint64 value);
void device_set_linux_lvm2_pv_group_unallocated_size (Device *device, guint64 value);
void device_set_linux_lvm2_pv_group_sequence_number (Device *device, guint64 value);
void device_set_linux_lvm2_pv_group_extent_size (Device *device, guint64 value);
void device_set_linux_lvm2_pv_group_physical_volumes (Device *device, GStrv value);
void device_set_linux_lvm2_pv_group_logical_volumes (Device *device, GStrv value);

void device_set_linux_dmmp_component_holder (Device *device, const gchar *value);

void device_set_linux_dmmp_name (Device *device, const gchar *value);
void device_set_linux_dmmp_slaves (Device *device, GStrv value);
void device_set_linux_dmmp_parameters (Device *device, const gchar *value);

void device_set_linux_loop_filename (Device *device, const gchar *value);

void device_set_dm_name (Device *device, const gchar *value);
void device_set_slaves_objpath (Device *device, GStrv value);
void device_set_holders_objpath (Device *device, GStrv value);

void device_set_drive_ata_smart_is_available (Device *device, gboolean value);
void device_set_drive_ata_smart_time_collected (Device *device, guint64 value);
void device_set_drive_ata_smart_status (Device *device, SkSmartOverall value);
void device_set_drive_ata_smart_blob_steal (Device *device, gchar *blob, gsize blob_size);

G_END_DECLS

#endif /* __DEVICE_PRIVATE_H__ */
