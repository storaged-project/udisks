/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#ifndef __DEVKIT_DISKS_DEVICE_PRIVATE_H__
#define __DEVKIT_DISKS_DEVICE_PRIVATE_H__

#include <glib-object.h>
#include <polkit-dbus/polkit-dbus.h>

#include "devkit-disks-device.h"

G_BEGIN_DECLS

struct Job;
typedef struct Job Job;

struct DevkitDisksDevicePrivate
{
        DBusGConnection *system_bus_connection;
        DBusGProxy      *system_bus_proxy;
        DevkitDisksDaemon *daemon;

        Job *job;

        char *object_path;
        char *native_path;

        gboolean job_in_progress;
        char *job_id;
        gboolean job_is_cancellable;
        int job_num_tasks;
        int job_cur_task;
        char *job_cur_task_id;
        double job_cur_task_percentage;

        struct {
                char *device_file;
                GPtrArray *device_file_by_id;
                GPtrArray *device_file_by_path;
                gboolean device_is_partition;
                gboolean device_is_partition_table;
                gboolean device_is_removable;
                gboolean device_is_media_available;
                gboolean device_is_read_only;
                gboolean device_is_drive;
                gboolean device_is_crypto_cleartext;
                guint64 device_size;
                guint64 device_block_size;
                gboolean device_is_mounted;
                char *device_mount_path;

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

                char *partition_table_scheme;
                int partition_table_count;
                int partition_table_max_number;
                GArray *partition_table_offsets;
                GArray *partition_table_sizes;

                char *drive_vendor;
                char *drive_model;
                char *drive_revision;
                char *drive_serial;
                char *drive_connection_interface;
                guint drive_connection_speed;
                GPtrArray *drive_media;

                char *crypto_cleartext_slave;

                /* the following properties are not (yet) exported */
                char *dm_name;
                GPtrArray *slaves_objpath;
                GPtrArray *holders_objpath;
        } info;
};


G_END_DECLS

#endif /* __DEVKIT_DISKS_DEVICE_PRIVATE_H__ */
