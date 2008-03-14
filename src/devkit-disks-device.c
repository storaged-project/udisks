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
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <gio/gunixmounts.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <devkit/devkit.h>
#include <polkit-dbus/polkit-dbus.h>

#include "devkit-disks-device.h"
#include "devkit-disks-device-private.h"
#include "mounts-file.h"

/*--------------------------------------------------------------------------------------------------------------*/
#include "devkit-disks-device-glue.h"

static void     devkit_disks_device_class_init  (DevkitDisksDeviceClass *klass);
static void     devkit_disks_device_init        (DevkitDisksDevice      *seat);
static void     devkit_disks_device_finalize    (GObject     *object);

static void     init_info                  (DevkitDisksDevice *device);
static void     free_info                  (DevkitDisksDevice *device);
static gboolean update_info                (DevkitDisksDevice *device);


enum
{
        PROP_0,
        PROP_NATIVE_PATH,

        PROP_DEVICE_FILE,
        PROP_DEVICE_FILE_BY_ID,
        PROP_DEVICE_FILE_BY_PATH,
        PROP_DEVICE_IS_PARTITION,
        PROP_DEVICE_IS_PARTITION_TABLE,
        PROP_DEVICE_IS_REMOVABLE,
        PROP_DEVICE_IS_MEDIA_AVAILABLE,
        PROP_DEVICE_IS_DRIVE,
        PROP_DEVICE_SIZE,
        PROP_DEVICE_BLOCK_SIZE,
        PROP_DEVICE_IS_MOUNTED,
        PROP_DEVICE_MOUNT_PATH,

        PROP_ID_USAGE,
        PROP_ID_TYPE,
        PROP_ID_VERSION,
        PROP_ID_UUID,
        PROP_ID_LABEL,

        PROP_PARTITION_SLAVE,
        PROP_PARTITION_SCHEME,
        PROP_PARTITION_TYPE,
        PROP_PARTITION_LABEL,
        PROP_PARTITION_UUID,
        PROP_PARTITION_FLAGS,
        PROP_PARTITION_NUMBER,
        PROP_PARTITION_OFFSET,
        PROP_PARTITION_SIZE,

        PROP_PARTITION_TABLE_SCHEME,
        PROP_PARTITION_TABLE_COUNT,
        PROP_PARTITION_TABLE_MAX_NUMBER,
        PROP_PARTITION_TABLE_OFFSETS,
        PROP_PARTITION_TABLE_SIZES,

        PROP_DRIVE_VENDOR,
        PROP_DRIVE_MODEL,
        PROP_DRIVE_REVISION,
        PROP_DRIVE_SERIAL,
};

enum
{
        CHANGED_SIGNAL,
        LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (DevkitDisksDevice, devkit_disks_device, G_TYPE_OBJECT)

#define DEVKIT_DISKS_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_TYPE_DISKS_DEVICE, DevkitDisksDevicePrivate))

GQuark
devkit_disks_device_error_quark (void)
{
        static GQuark ret = 0;

        if (ret == 0) {
                ret = g_quark_from_static_string ("devkit_disks_device_error");
        }

        return ret;
}


#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
devkit_disks_device_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0)
        {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_GENERAL, "GeneralError"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_SUPPORTED, "NotSupported"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTABLE, "NotMountable"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_ALREADY_MOUNTED, "AlreadyMounted"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTED, "NotMounted"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTED_BY_DK, "NotMountedByDeviceKit"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_FSTAB_ENTRY, "FstabEntry"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_MOUNT_OPTION_NOT_ALLOWED, "MountOptionNotAllowed"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_FILESYSTEM_BUSY, "FilesystemBusy"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_CANNOT_REMOUNT, "CannotRemount"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_UNMOUNT_OPTION_NOT_ALLOWED, "UnmountOptionNotAllowed"),
                        { 0, 0, 0 }
                };
                g_assert (DEVKIT_DISKS_DEVICE_NUM_ERRORS == G_N_ELEMENTS (values) - 1);
                etype = g_enum_register_static ("DevkitDisksDeviceError", values);
        }
        return etype;
}


static GObject *
devkit_disks_device_constructor (GType                  type,
                                 guint                  n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
        DevkitDisksDevice      *device;
        DevkitDisksDeviceClass *klass;

        klass = DEVKIT_DISKS_DEVICE_CLASS (g_type_class_peek (DEVKIT_TYPE_DISKS_DEVICE));

        device = DEVKIT_DISKS_DEVICE (
                G_OBJECT_CLASS (devkit_disks_device_parent_class)->constructor (type,
                                                                                n_construct_properties,
                                                                                construct_properties));
        return G_OBJECT (device);
}

static void
get_property (GObject         *object,
              guint            prop_id,
              GValue          *value,
              GParamSpec      *pspec)
{
        DevkitDisksDevice *device = DEVKIT_DISKS_DEVICE (object);

        switch (prop_id) {
        case PROP_NATIVE_PATH:
                g_value_set_string (value, device->priv->native_path);
                break;

        case PROP_DEVICE_FILE:
                g_value_set_string (value, device->priv->info.device_file);
                break;
        case PROP_DEVICE_FILE_BY_ID:
                g_value_set_boxed (value, device->priv->info.device_file_by_id);
                break;
        case PROP_DEVICE_FILE_BY_PATH:
                g_value_set_boxed (value, device->priv->info.device_file_by_path);
                break;
	case PROP_DEVICE_IS_PARTITION:
		g_value_set_boolean (value, device->priv->info.device_is_partition);
		break;
	case PROP_DEVICE_IS_PARTITION_TABLE:
		g_value_set_boolean (value, device->priv->info.device_is_partition_table);
		break;
	case PROP_DEVICE_IS_REMOVABLE:
		g_value_set_boolean (value, device->priv->info.device_is_removable);
		break;
	case PROP_DEVICE_IS_MEDIA_AVAILABLE:
		g_value_set_boolean (value, device->priv->info.device_is_media_available);
		break;
	case PROP_DEVICE_IS_DRIVE:
		g_value_set_boolean (value, device->priv->info.device_is_drive);
		break;
	case PROP_DEVICE_SIZE:
		g_value_set_uint64 (value, device->priv->info.device_size);
		break;
	case PROP_DEVICE_BLOCK_SIZE:
		g_value_set_uint64 (value, device->priv->info.device_block_size);
		break;
	case PROP_DEVICE_IS_MOUNTED:
		g_value_set_boolean (value, device->priv->info.device_is_mounted);
		break;
	case PROP_DEVICE_MOUNT_PATH:
		g_value_set_string (value, device->priv->info.device_mount_path);
		break;

        case PROP_ID_USAGE:
                g_value_set_string (value, device->priv->info.id_usage);
                break;
        case PROP_ID_TYPE:
                g_value_set_string (value, device->priv->info.id_type);
                break;
        case PROP_ID_VERSION:
                g_value_set_string (value, device->priv->info.id_version);
                break;
        case PROP_ID_UUID:
                g_value_set_string (value, device->priv->info.id_uuid);
                break;
        case PROP_ID_LABEL:
                g_value_set_string (value, device->priv->info.id_label);
                break;

	case PROP_PARTITION_SLAVE:
                if (device->priv->info.partition_slave != NULL)
                        g_value_set_boxed (value, device->priv->info.partition_slave);
                else
                        g_value_set_boxed (value, "/");
		break;
	case PROP_PARTITION_SCHEME:
		g_value_set_string (value, device->priv->info.partition_scheme);
		break;
	case PROP_PARTITION_TYPE:
		g_value_set_string (value, device->priv->info.partition_type);
		break;
	case PROP_PARTITION_LABEL:
		g_value_set_string (value, device->priv->info.partition_label);
		break;
	case PROP_PARTITION_UUID:
		g_value_set_string (value, device->priv->info.partition_uuid);
		break;
	case PROP_PARTITION_FLAGS:
		g_value_set_boxed (value, device->priv->info.partition_flags);
		break;
	case PROP_PARTITION_NUMBER:
		g_value_set_int (value, device->priv->info.partition_number);
		break;
	case PROP_PARTITION_OFFSET:
		g_value_set_uint64 (value, device->priv->info.partition_offset);
		break;
	case PROP_PARTITION_SIZE:
		g_value_set_uint64 (value, device->priv->info.partition_size);
		break;

	case PROP_PARTITION_TABLE_SCHEME:
		g_value_set_string (value, device->priv->info.partition_table_scheme);
		break;
	case PROP_PARTITION_TABLE_COUNT:
		g_value_set_int (value, device->priv->info.partition_table_count);
		break;
	case PROP_PARTITION_TABLE_MAX_NUMBER:
		g_value_set_int (value, device->priv->info.partition_table_max_number);
		break;
	case PROP_PARTITION_TABLE_OFFSETS:
		g_value_set_boxed (value, device->priv->info.partition_table_offsets);
		break;
	case PROP_PARTITION_TABLE_SIZES:
		g_value_set_boxed (value, device->priv->info.partition_table_sizes);
		break;

	case PROP_DRIVE_VENDOR:
		g_value_set_string (value, device->priv->info.drive_vendor);
		break;
	case PROP_DRIVE_MODEL:
		g_value_set_string (value, device->priv->info.drive_model);
		break;
	case PROP_DRIVE_REVISION:
		g_value_set_string (value, device->priv->info.drive_revision);
		break;
	case PROP_DRIVE_SERIAL:
		g_value_set_string (value, device->priv->info.drive_serial);
		break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
devkit_disks_device_class_init (DevkitDisksDeviceClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = devkit_disks_device_constructor;
        object_class->finalize = devkit_disks_device_finalize;
        object_class->get_property = get_property;

        g_type_class_add_private (klass, sizeof (DevkitDisksDevicePrivate));

        signals[CHANGED_SIGNAL] =
                g_signal_new ("changed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        dbus_g_object_type_install_info (DEVKIT_TYPE_DISKS_DEVICE, &dbus_glib_devkit_disks_device_object_info);

        dbus_g_error_domain_register (DEVKIT_DISKS_DEVICE_ERROR,
                                      NULL,
                                      DEVKIT_DISKS_DEVICE_TYPE_ERROR);

        g_object_class_install_property (
                object_class,
                PROP_NATIVE_PATH,
                g_param_spec_string ("native-path", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_FILE,
                g_param_spec_string ("device-file", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_FILE_BY_ID,
                g_param_spec_boxed ("device-file-by-id", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
                                    G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_FILE_BY_PATH,
                g_param_spec_boxed ("device-file-by-path", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
                                    G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_PARTITION,
                g_param_spec_boolean ("device-is-partition", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_PARTITION_TABLE,
                g_param_spec_boolean ("device-is-partition-table", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_REMOVABLE,
                g_param_spec_boolean ("device-is-removable", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_MEDIA_AVAILABLE,
                g_param_spec_boolean ("device-is-media-available", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_DRIVE,
                g_param_spec_boolean ("device-is-drive", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_SIZE,
                g_param_spec_uint64 ("device-size", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_BLOCK_SIZE,
                g_param_spec_uint64 ("device-block-size", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_MOUNTED,
                g_param_spec_boolean ("device-is-mounted", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_MOUNT_PATH,
                g_param_spec_string ("device-mount-path", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_ID_USAGE,
                g_param_spec_string ("id-usage", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_ID_TYPE,
                g_param_spec_string ("id-type", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_ID_VERSION,
                g_param_spec_string ("id-version", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_ID_UUID,
                g_param_spec_string ("id-uuid", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_ID_LABEL,
                g_param_spec_string ("id-label", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_PARTITION_SLAVE,
                g_param_spec_boxed ("partition-slave", NULL, NULL, DBUS_TYPE_G_OBJECT_PATH, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_SCHEME,
                g_param_spec_string ("partition-scheme", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_TYPE,
                g_param_spec_string ("partition-type", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_LABEL,
                g_param_spec_string ("partition-label", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_UUID,
                g_param_spec_string ("partition-uuid", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_FLAGS,
                g_param_spec_boxed ("partition-flags", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
                                    G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_NUMBER,
                g_param_spec_int ("partition-number", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_OFFSET,
                g_param_spec_uint64 ("partition-offset", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_SIZE,
                g_param_spec_uint64 ("partition-size", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_PARTITION_TABLE_SCHEME,
                g_param_spec_string ("partition-table-scheme", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_TABLE_COUNT,
                g_param_spec_int ("partition-table-count", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_TABLE_MAX_NUMBER,
                g_param_spec_int ("partition-table-max-number", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_TABLE_OFFSETS,
                g_param_spec_boxed ("partition-table-offsets", NULL, NULL,
                                    dbus_g_type_get_collection ("GArray", G_TYPE_UINT64),
                                    G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_PARTITION_TABLE_SIZES,
                g_param_spec_boxed ("partition-table-sizes", NULL, NULL,
                                    dbus_g_type_get_collection ("GArray", G_TYPE_UINT64),
                                    G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_DRIVE_VENDOR,
                g_param_spec_string ("drive-vendor", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_MODEL,
                g_param_spec_string ("drive-model", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_REVISION,
                g_param_spec_string ("drive-revision", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_SERIAL,
                g_param_spec_string ("drive-serial", NULL, NULL, NULL, G_PARAM_READABLE));
}

static void
devkit_disks_device_init (DevkitDisksDevice *device)
{
        device->priv = DEVKIT_DISKS_DEVICE_GET_PRIVATE (device);
        init_info (device);
}

static void
devkit_disks_device_finalize (GObject *object)
{
        DevkitDisksDevice *device;

        g_return_if_fail (object != NULL);
        g_return_if_fail (DEVKIT_IS_DISKS_DEVICE (object));

        device = DEVKIT_DISKS_DEVICE (object);
        g_return_if_fail (device->priv != NULL);

        g_object_unref (device->priv->daemon);
        g_free (device->priv->object_path);

        g_free (device->priv->native_path);

        free_info (device);

        G_OBJECT_CLASS (devkit_disks_device_parent_class)->finalize (object);
}

static char *
compute_object_path_from_basename (const char *native_path_basename)
{
        char *basename;
        char *object_path;
        unsigned int n;

        /* TODO: need to be more thorough with making proper object
         * names that won't make D-Bus crash. This is just to cope
         * with dm-0...
         */
        basename = g_path_get_basename (native_path_basename);
        for (n = 0; basename[n] != '\0'; n++)
                if (basename[n] == '-')
                        basename[n] = '_';
        object_path = g_build_filename ("/devices/", basename, NULL);
        g_free (basename);

        return object_path;
}

static char *
compute_object_path (const char *native_path)
{
        char *basename;
        char *object_path;

        basename = g_path_get_basename (native_path);
        object_path = compute_object_path_from_basename (basename);
        g_free (basename);
        return object_path;
}

static gboolean
register_disks_device (DevkitDisksDevice *device)
{
        DBusConnection *connection;
        GError *error = NULL;

        device->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (device->priv->system_bus_connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                goto error;
        }
        connection = dbus_g_connection_get_connection (device->priv->system_bus_connection);

        device->priv->object_path = compute_object_path (device->priv->native_path);

        dbus_g_connection_register_g_object (device->priv->system_bus_connection,
                                             device->priv->object_path,
                                             G_OBJECT (device));

        device->priv->system_bus_proxy = dbus_g_proxy_new_for_name (device->priv->system_bus_connection,
                                                                    DBUS_SERVICE_DBUS,
                                                                    DBUS_PATH_DBUS,
                                                                    DBUS_INTERFACE_DBUS);

        return TRUE;

error:
        return FALSE;
}

static int
sysfs_get_int (const char *dir, const char *attribute)
{
        int result;
        char *contents;
        char *filename;

        result = 0;
        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_get_contents (filename, &contents, NULL, NULL)) {
                result = atoi (contents);
                g_free (contents);
        }
        g_free (filename);


        return result;
}

static int
sysfs_get_uint64 (const char *dir, const char *attribute)
{
        guint64 result;
        char *contents;
        char *filename;

        result = 0;
        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_get_contents (filename, &contents, NULL, NULL)) {
                result = atoll (contents);
                g_free (contents);
        }
        g_free (filename);


        return result;
}

static gboolean
sysfs_file_exists (const char *dir, const char *attribute)
{
        gboolean result;
        char *filename;

        result = FALSE;
        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
                result = TRUE;
        }
        g_free (filename);

        return result;
}

static void
free_info (DevkitDisksDevice *device)
{
        g_free (device->priv->info.device_file);
        g_ptr_array_foreach (device->priv->info.device_file_by_id, (GFunc) g_free, NULL);
        g_ptr_array_foreach (device->priv->info.device_file_by_path, (GFunc) g_free, NULL);
        g_ptr_array_free (device->priv->info.device_file_by_id, TRUE);
        g_ptr_array_free (device->priv->info.device_file_by_path, TRUE);

        g_free (device->priv->info.id_usage);
        g_free (device->priv->info.id_type);
        g_free (device->priv->info.id_version);
        g_free (device->priv->info.id_uuid);
        g_free (device->priv->info.id_label);

        g_free (device->priv->info.partition_slave);
        g_free (device->priv->info.partition_scheme);
        g_free (device->priv->info.partition_type);
        g_free (device->priv->info.partition_label);
        g_free (device->priv->info.partition_uuid);
        g_ptr_array_foreach (device->priv->info.partition_flags, (GFunc) g_free, NULL);
        g_ptr_array_free (device->priv->info.partition_flags, TRUE);
        g_array_free (device->priv->info.partition_table_offsets, TRUE);
        g_array_free (device->priv->info.partition_table_sizes, TRUE);

        g_free (device->priv->info.partition_table_scheme);

        g_free (device->priv->info.drive_vendor);
        g_free (device->priv->info.drive_model);
        g_free (device->priv->info.drive_revision);
        g_free (device->priv->info.drive_serial);
}

static void
init_info (DevkitDisksDevice *device)
{
        memset (&(device->priv->info), 0, sizeof (device->priv->info));
        device->priv->info.device_file_by_id = g_ptr_array_new ();
        device->priv->info.device_file_by_path = g_ptr_array_new ();
        device->priv->info.partition_flags = g_ptr_array_new ();
        device->priv->info.partition_table_offsets = g_array_new (FALSE, TRUE, sizeof (guint64));
        device->priv->info.partition_table_sizes = g_array_new (FALSE, TRUE, sizeof (guint64));
}


static devkit_bool_t
update_info_add_ptr (DevKitInfo *info, const char *str, void *user_data)
{
        GPtrArray *ptr_array = user_data;
        g_ptr_array_add (ptr_array, g_strdup (str));
        return FALSE;
}

static devkit_bool_t
update_info_properties_cb (DevKitInfo *info, const char *key, void *user_data)
{
        DevkitDisksDevice *device = user_data;

        if (strcmp (key, "ID_FS_USAGE") == 0) {
                device->priv->info.id_usage   = g_strdup (devkit_info_property_get_string (info, key));
        } else if (strcmp (key, "ID_FS_TYPE") == 0) {
                device->priv->info.id_type    = g_strdup (devkit_info_property_get_string (info, key));
        } else if (strcmp (key, "ID_FS_VERSION") == 0) {
                device->priv->info.id_version = g_strdup (devkit_info_property_get_string (info, key));
        } else if (strcmp (key, "ID_FS_UUID") == 0) {
                device->priv->info.id_uuid    = g_strdup (devkit_info_property_get_string (info, key));
        } else if (strcmp (key, "ID_FS_LABEL") == 0) {
                device->priv->info.id_label   = g_strdup (devkit_info_property_get_string (info, key));

        } else if (strcmp (key, "ID_VENDOR") == 0) {
                if (device->priv->info.device_is_drive && device->priv->info.drive_vendor == NULL)
                        device->priv->info.drive_vendor = g_strdup (devkit_info_property_get_string (info, key));
        } else if (strcmp (key, "ID_MODEL") == 0) {
                if (device->priv->info.device_is_drive && device->priv->info.drive_model == NULL)
                        device->priv->info.drive_model = g_strdup (devkit_info_property_get_string (info, key));
        } else if (strcmp (key, "ID_REVISION") == 0) {
                if (device->priv->info.device_is_drive && device->priv->info.drive_revision == NULL)
                        device->priv->info.drive_revision = g_strdup (devkit_info_property_get_string (info, key));
        } else if (strcmp (key, "ID_SERIAL_SHORT") == 0) {
                if (device->priv->info.device_is_drive && device->priv->info.drive_serial == NULL)
                        device->priv->info.drive_serial = g_strdup (devkit_info_property_get_string (info, key));


        } else if (strcmp (key, "PART_SCHEME") == 0) {

                if (device->priv->info.device_is_partition) {
                        device->priv->info.partition_scheme =
                                g_strdup (devkit_info_property_get_string (info, key));
                } else {
                        device->priv->info.device_is_partition_table = TRUE;
                        device->priv->info.partition_table_scheme =
                                g_strdup (devkit_info_property_get_string (info, key));
                }
        } else if (strcmp (key, "PART_COUNT") == 0) {
                device->priv->info.partition_table_count = devkit_info_property_get_int (info, key);
        } else if (g_str_has_prefix (key, "PART_P") && g_ascii_isdigit (key[6])) {
                char *endp;
                int part_number = strtol (key + 6, &endp, 10);
                if (*endp == '_') {

                        if (!device->priv->info.device_is_partition) {
                                guint64 value;
                                unsigned int index;
                                GArray *array;

                                if (part_number > device->priv->info.partition_table_max_number)
                                        device->priv->info.partition_table_max_number = part_number;

                                array = NULL;
                                index = 0;
                                value = devkit_info_property_get_uint64 (info, key);
                                if (g_str_has_prefix (endp, "_OFFSET")) {
                                        array = device->priv->info.partition_table_offsets;
                                        index = part_number - 1;
                                } else if (g_str_has_prefix (endp, "_SIZE")) {
                                        array = device->priv->info.partition_table_sizes;
                                        index = part_number - 1;
                                }
                                if (array != NULL) {
                                        g_array_set_size (array, index + 1 > array->len ? index + 1 : array->len);
                                        g_array_index (array, guint64, index) = value;
                                }

                        } else if (device->priv->info.device_is_partition &&
                                   part_number == device->priv->info.partition_number) {

                                if (g_str_has_prefix (endp, "_LABEL")) {
                                        device->priv->info.partition_label =
                                                g_strdup (devkit_info_property_get_string (info, key));
                                } else if (g_str_has_prefix (endp, "_UUID")) {
                                        device->priv->info.partition_uuid =
                                                g_strdup (devkit_info_property_get_string (info, key));
                                } else if (g_str_has_prefix (endp, "_TYPE")) {
                                        device->priv->info.partition_type =
                                                g_strdup (devkit_info_property_get_string (info, key));
                                } else if (g_str_has_prefix (endp, "_OFFSET")) {
                                        device->priv->info.partition_offset =
                                                devkit_info_property_get_uint64 (info, key);
                                } else if (g_str_has_prefix (endp, "_SIZE")) {
                                        device->priv->info.partition_size =
                                                devkit_info_property_get_uint64 (info, key);
                                } else if (g_str_has_prefix (endp, "_FLAGS")) {
                                        devkit_info_property_strlist_foreach (info, key, update_info_add_ptr,
                                                                              device->priv->info.partition_flags);
                                }
                        }
                }

        } else if (strcmp (key, "MEDIA_AVAILABLE") == 0) {
                if (device->priv->info.device_is_removable) {
                        device->priv->info.device_is_media_available = devkit_info_property_get_bool (info, key);
                }
        }

        return FALSE;
}

static gboolean
update_info_symlinks_cb (DevKitInfo *info, const char *value, void *user_data)
{
        DevkitDisksDevice *device = user_data;

        if (g_str_has_prefix (value, "/dev/disk/by-id/") || g_str_has_prefix (value, "/dev/disk/by-uuid/")) {
                g_ptr_array_add (device->priv->info.device_file_by_id, g_strdup (value));
        } else if (g_str_has_prefix (value, "/dev/disk/by-path/")) {
                g_ptr_array_add (device->priv->info.device_file_by_path, g_strdup (value));
        }

        return FALSE;
}

static gboolean
update_info (DevkitDisksDevice *device)
{
        gboolean ret;
        DevKitInfo *info;

        ret = FALSE;

        /* free all info and prep for new info */
        free_info (device);
        init_info (device);

        /* fill in drive information */
        if (sysfs_file_exists (device->priv->native_path, "device")) {
                device->priv->info.device_is_drive = TRUE;
                /* TODO: fill in drive props */
        } else {
                device->priv->info.device_is_drive = FALSE;
        }

        device->priv->info.device_is_removable =
                (sysfs_get_int (device->priv->native_path, "removable") != 0);
        if (!device->priv->info.device_is_removable)
                device->priv->info.device_is_media_available = TRUE;
        /* TODO: FIXME; need to get the block size */
        device->priv->info.device_block_size = 512;
        device->priv->info.device_size =
                sysfs_get_uint64 (device->priv->native_path, "size") * device->priv->info.device_block_size;

        /* figure out if we're a partition and, if so, who our slave is */
        if (sysfs_file_exists (device->priv->native_path, "start")) {
                guint64 start, size;
                char *s;
                char *p;
                int n;

                /* we're partitioned by the kernel */
                device->priv->info.device_is_partition = TRUE;
                start = sysfs_get_uint64 (device->priv->native_path, "start");
                size = sysfs_get_uint64 (device->priv->native_path, "size");
                device->priv->info.partition_offset = start * device->priv->info.device_block_size;
                device->priv->info.partition_size = size * device->priv->info.device_block_size;

                s = device->priv->native_path;
                for (n = strlen (s) - 1; n >= 0 && g_ascii_isdigit (s[n]); n--)
                        ;
                device->priv->info.partition_number = atoi (s + n + 1);

                s = g_strdup (device->priv->native_path);
                for (n = strlen (s) - 1; n >= 0 && s[n] != '/'; n--)
                        s[n] = '\0';
                s[n] = '\0';
                p = g_path_get_basename (s);
                device->priv->info.partition_slave = compute_object_path_from_basename (p);
                g_free (p);
                g_free (s);

                /* since the env from the parent is imported, we'll
                 * add partition table information from enclosing
                 * device by matching on partition number
                 */
        } else {
                /* TODO: handle partitions created by kpartx / dm-linear */
        }


        info = devkit_info_new (device->priv->native_path);
        if (info == NULL) {
                goto out;
        }

        device->priv->info.device_file = g_strdup (devkit_info_get_device_file (info));
        devkit_info_device_file_symlinks_foreach (info, update_info_symlinks_cb, device);
        devkit_info_property_foreach (info, update_info_properties_cb, device);
        devkit_info_unref (info);

        ret = TRUE;

out:
        return ret;
}

DevkitDisksDevice *
devkit_disks_device_new (DevkitDisksDaemon *daemon, const char *native_path)
{
        DevkitDisksDevice *device;
        gboolean res;

        device = DEVKIT_DISKS_DEVICE (g_object_new (DEVKIT_TYPE_DISKS_DEVICE, NULL));

        device->priv->daemon = g_object_ref (daemon);
        device->priv->native_path = g_strdup (native_path);
        if (!update_info (device)) {
                g_object_unref (device);
                device = NULL;
                goto out;
        }

        res = register_disks_device (DEVKIT_DISKS_DEVICE (device));
        if (! res) {
                g_object_unref (device);
                device = NULL;
                goto out;
        }

out:
        return device;
}

static void
emit_changed (DevkitDisksDevice *device)
{
        g_print ("emitting changed on %s\n", device->priv->native_path);
        g_signal_emit_by_name (device->priv->daemon,
                               "device-changed",
                               device->priv->object_path,
                               NULL);
        g_signal_emit (device, signals[CHANGED_SIGNAL], 0);
}

void
devkit_disks_device_changed (DevkitDisksDevice *device)
{
        /* TODO: fix up update_info to return TRUE iff something has changed */
        if (update_info (device))
                emit_changed (device);
}

/*--------------------------------------------------------------------------------------------------------------*/

/**
 * devkit_disks_enumerate_native_paths:
 *
 * Enumerates all block devices on the system.
 *
 * Returns: A #GList of native paths for devices (on Linux the sysfs path)
 */
GList *
devkit_disks_enumerate_native_paths (void)
{
        GList *ret;
        GDir *dir;
        gboolean have_class_block;
        const char *name;

        ret = 0;

        /* TODO: rip out support for running without /sys/class/block */

        have_class_block = FALSE;
        if (g_file_test ("/sys/class/block", G_FILE_TEST_EXISTS))
                have_class_block = TRUE;

        dir = g_dir_open (have_class_block ? "/sys/class/block" : "/sys/block", 0, NULL);
        if (dir == NULL)
                goto out;

        while ((name = g_dir_read_name (dir)) != NULL) {
                char *s;
                char sysfs_path[PATH_MAX];

                /* skip all ram%d block devices */
                if (g_str_has_prefix (name, "ram"))
                        continue;

                s = g_build_filename (have_class_block ? "/sys/class/block" : "/sys/block", name, NULL);
                if (realpath (s, sysfs_path) == NULL) {
                        g_free (s);
                        continue;
                }
                g_free (s);

                ret = g_list_prepend (ret, g_strdup (sysfs_path));

                if (!have_class_block) {
                        GDir *part_dir;
                        const char *part_name;

                        if((part_dir = g_dir_open (sysfs_path, 0, NULL)) != NULL) {
                                while ((part_name = g_dir_read_name (part_dir)) != NULL) {
                                        if (g_str_has_prefix (part_name, name)) {
                                                char *part_sysfs_path;
                                                part_sysfs_path = g_build_filename (sysfs_path, part_name, NULL);
                                                ret = g_list_prepend (ret, part_sysfs_path);
                                        }
                                }
                                g_dir_close (part_dir);
                        }
                }
        }
        g_dir_close (dir);

        /* TODO: probing order.. might be tricky.. right now we just
         *       sort the list
         */
        ret = g_list_sort (ret, (GCompareFunc) strcmp);
out:
        return ret;
}

/*--------------------------------------------------------------------------------------------------------------*/

const char *
devkit_disks_device_local_get_object_path (DevkitDisksDevice *device)
{
        return device->priv->object_path;
}

const char *
devkit_disks_device_local_get_native_path (DevkitDisksDevice *device)
{
        return device->priv->native_path;
}

const char *
devkit_disks_device_local_get_device_file (DevkitDisksDevice *device)
{
        return device->priv->info.device_file;
}

const char *
devkit_disks_device_local_get_mount_path (DevkitDisksDevice *device)
{
        return device->priv->info.device_mount_path;
}

void
devkit_disks_device_local_set_mounted (DevkitDisksDevice *device, const char *mount_path)
{
        g_free (device->priv->info.device_mount_path);
        device->priv->info.device_mount_path = g_strdup (mount_path);
        device->priv->info.device_is_mounted = TRUE;
        emit_changed (device);
}

void
devkit_disks_device_local_set_unmounted (DevkitDisksDevice *device)
{
        char *mount_path;
        gboolean remove_dir_on_unmount;

        mount_path = g_strdup (device->priv->info.device_mount_path);

        /* make sure we clean up directories created by ourselves in /media */
        if (!mounts_file_has_device (device, NULL, &remove_dir_on_unmount)) {
                g_warning ("Cannot determine if directory should be removed on late unmount path");
                remove_dir_on_unmount = FALSE;
        }

        g_free (device->priv->info.device_mount_path);
        device->priv->info.device_mount_path = NULL;
        device->priv->info.device_is_mounted = FALSE;

        if (mount_path != NULL) {
                mounts_file_remove (device, mount_path);
                if (remove_dir_on_unmount) {
                        if (g_rmdir (mount_path) != 0) {
                                g_warning ("Error removing dir '%s' in late unmount path: %m", mount_path);
                        }
                }
        }

        emit_changed (device);

        g_free (mount_path);
}

/*--------------------------------------------------------------------------------------------------------------*/

static gboolean
throw_error (DBusGMethodInvocation *context, int error_code, const char *format, ...)
{
        GError *error;
        va_list args;
        char *message;

        va_start (args, format);
        message = g_strdup_vprintf (format, args);
        va_end (args);

        error = g_error_new (DEVKIT_DISKS_DEVICE_ERROR,
                             error_code,
                             message);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        g_free (message);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef void (*JobCompletedFunc) (DBusGMethodInvocation *context,
                                  DevkitDisksDevice *device,
                                  PolKitCaller *caller,
                                  int status,
                                  const char *stderr,
                                  gpointer user_data);

typedef struct {
        DevkitDisksDevice *device;
        PolKitCaller *pk_caller;
        DBusGMethodInvocation *context;
        JobCompletedFunc job_completed_func;
        GPid pid;
        gpointer user_data;
        GDestroyNotify user_data_destroy_func;

        int stderr_fd;
        GIOChannel *error_channel;
        guint error_channel_source_id;
        GString *error_string;
} Job;

static void
job_free (Job *job)
{
        if (job->device != NULL)
                g_object_unref (job->device);
        if (job->pk_caller != NULL)
                polkit_caller_unref (job->pk_caller);
        if (job->stderr_fd >= 0)
                close (job->stderr_fd);
        g_source_remove (job->error_channel_source_id);
        g_io_channel_unref (job->error_channel);
        g_string_free (job->error_string, TRUE);
        g_free (job);
}

static void
job_child_watch_cb (GPid pid, int status, gpointer user_data)
{
        Job *job = user_data;

        job->job_completed_func (job->context,
                                 job->device,
                                 job->pk_caller,
                                 status,
                                 job->error_string->str,
                                 job->user_data);
        job_free (job);
}

static gboolean
job_read_error (GIOChannel *channel,
                GIOCondition condition,
                gpointer user_data)
{
  char *str;
  gsize str_len;
  Job *job = user_data;

  g_io_channel_read_to_end (channel, &str, &str_len, NULL);
  g_string_append (job->error_string, str);
  g_free (str);
  return TRUE;
}

static gboolean
job_new (DBusGMethodInvocation *context,
         DevkitDisksDevice *device,
         PolKitCaller *pk_caller,
         char **argv,
         JobCompletedFunc job_completed_func,
         GError **error,
         gpointer user_data,
         GDestroyNotify user_data_destroy_func)
{
        Job *job;
        gboolean ret;

        ret = FALSE;

        job = g_new0 (Job, 1);
        job->context = context;
        job->device = DEVKIT_DISKS_DEVICE (g_object_ref (device));
        job->pk_caller = pk_caller != NULL ? polkit_caller_ref (pk_caller) : NULL;
        job->job_completed_func = job_completed_func;
        job->user_data = user_data;
        job->user_data_destroy_func = user_data_destroy_func;
        job->stderr_fd = -1;

        if (!g_spawn_async_with_pipes (NULL,
                                       argv,
                                       NULL,
                                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                       NULL,
                                       NULL,
                                       &(job->pid),
                                       NULL,
                                       NULL,
                                       &(job->stderr_fd),
                                       error)) {
                job->user_data_destroy_func (job->user_data);
                goto out;
        }

        g_child_watch_add (job->pid, job_child_watch_cb, job);

        job->error_string = g_string_new ("");
        job->error_channel = g_io_channel_unix_new (job->stderr_fd);
        job->error_channel_source_id = g_io_add_watch (job->error_channel, G_IO_IN, job_read_error, job);

        ret = TRUE;

out:
        if (!ret)
                job_free (job);
        return ret;
}

/*--------------------------------------------------------------------------------------------------------------*/
/* exported methods */

typedef struct {
        char *mount_point;
        gboolean remove_dir_on_unmount;
        gboolean is_remount;
} MountData;

static MountData *
mount_data_new (const char *mount_point, gboolean remove_dir_on_unmount, gboolean is_remount)
{
        MountData *data;
        data = g_new0 (MountData, 1);
        data->mount_point = g_strdup (mount_point);
        data->remove_dir_on_unmount = remove_dir_on_unmount;
        data->is_remount = is_remount;
        return data;
}

static void
mount_data_free (MountData *data)
{
        g_free (data->mount_point);
        g_free (data);
}

static gboolean
is_device_in_fstab (DevkitDisksDevice *device)
{
        GList *l;
        GList *mount_points;
        gboolean ret;

        ret = FALSE;

        mount_points = g_unix_mount_points_get (NULL);
        for (l = mount_points; l != NULL; l = l->next) {
                GUnixMountPoint *mount_point = l->data;
                char canonical_device_file[PATH_MAX];

                /* get the canonical path; e.g. resolve
                 *
                 * /dev/disk/by-path/pci-0000:00:1d.7-usb-0:3:1.0-scsi-0:0:0:3-part5 into /dev/sde5
                 */
                if (realpath (g_unix_mount_point_get_device_path (mount_point), canonical_device_file) == NULL)
                        continue;

                if (strcmp (device->priv->info.device_file, canonical_device_file) == 0) {
                        ret = TRUE;
                        break;
                }
        }
        g_list_foreach (mount_points, (GFunc) g_unix_mount_point_free, NULL);
        g_list_free (mount_points);

        return ret;
}

typedef struct {
        const char *mount_option;
        const char *authorization_needed;
} FSRestrictedMountOption;

typedef struct {
        const char         *fstype;
        const char * const *defaults;
        const char * const *allow;
        const char * const *allow_uid_self;
        const char * const *allow_gid_self;
        const FSRestrictedMountOption *restricted;
} FSMountOptions;

/* ---------------------- vfat -------------------- */
/* TODO: add more filesystems */

static const char *vfat_defaults[] =       {"uid=",
                                            "gid=",
                                            "shortname=lower",
                                            NULL};
static const char *vfat_allow[] =          {"utf8",
                                            "shortname=",
                                            "umask=",
                                            "dmask=",
                                            "fmask=",
                                            "codepage=",
                                            NULL};
static const FSRestrictedMountOption vfat_restricted[] = {
        {"uid=", "org.freedesktop.devicekit.disks.mount-option.vfat-uid"},
        {"gid=", "org.freedesktop.devicekit.disks.mount-option.vfat-gid"},
        {NULL, NULL},
};
static const char *vfat_allow_uid_self[] = {"uid=", NULL};
static const char *vfat_allow_gid_self[] = {"gid=", NULL};

/* ------------------------------------------------ */
/* TODO: support context= */

static const char *any_allow[] = {"exec",
                                  "noexec",
                                  "nodev",
                                  "nosuid",
                                  "atime",
                                  "noatime",
                                  "nodiratime",
                                  "remount",
                                  "ro",
                                  "rw",
                                  "sync",
                                  "dirsync",
                                  NULL};

static const FSRestrictedMountOption any_restricted[] = {
        {"suid", "org.freedesktop.devicekit.disks.mount-option.suid"},
        {"dev", "org.freedesktop.devicekit.disks.mount-option.dev"},
        {NULL, NULL},
};

static const FSMountOptions fs_mount_options[] = {
        {"vfat", vfat_defaults, vfat_allow, vfat_allow_uid_self, vfat_allow_gid_self, vfat_restricted},
};

/* ------------------------------------------------ */

static int num_fs_mount_options = sizeof (fs_mount_options) / sizeof (FSMountOptions);

static const FSMountOptions *
find_mount_options_for_fs (const char *fstype)
{
        int n;
        const FSMountOptions *fsmo;

        for (n = 0; n < num_fs_mount_options; n++) {
                fsmo = fs_mount_options + n;
                if (strcmp (fsmo->fstype, fstype) == 0)
                        goto out;
        }

        fsmo = NULL;
out:
        return fsmo;
}

static gid_t
find_primary_gid (uid_t uid)
{
        struct passwd *pw;
        gid_t gid;

        gid = (gid_t) -1;

        pw = getpwuid (uid);
        if (pw == NULL) {
                g_warning ("Couldn't look up uid %d: %m", uid);
                goto out;
        }
        gid = pw->pw_gid;

out:
        return gid;
}

static gboolean
is_uid_in_gid (uid_t uid, gid_t gid)
{
        gboolean ret;
        struct passwd *pw;
        static gid_t supplementary_groups[128];
        int num_supplementary_groups = 128;
        int n;

        /* TODO: use some #define instead of harcoding some random number like 128 */

        ret = FALSE;

        pw = getpwuid (uid);
        if (pw == NULL) {
                g_warning ("Couldn't look up uid %d: %m", uid);
                goto out;
        }
        if (pw->pw_gid == gid) {
                ret = TRUE;
                goto out;
        }

        if (getgrouplist (pw->pw_name, pw->pw_gid, supplementary_groups, &num_supplementary_groups) < 0) {
                g_warning ("Couldn't find supplementary groups for uid %d: %m", uid);
                goto out;
        }

        for (n = 0; n < num_supplementary_groups; n++) {
                if (supplementary_groups[n] == gid) {
                        ret = TRUE;
                        goto out;
                }
        }

out:
        return ret;
}

static gboolean
is_mount_option_allowed (const FSMountOptions *fsmo,
                         const char *option,
                         uid_t caller_uid,
                         const char **auth_needed)
{
        int n;
        char *endp;
        uid_t uid;
        gid_t gid;
        gboolean allowed;
        const char *ep;
        gsize ep_len;

        allowed = FALSE;
        *auth_needed = NULL;

        /* first run through the allowed mount options */
        if (fsmo != NULL) {
                for (n = 0; fsmo->allow[n] != NULL; n++) {
                        ep = strstr (fsmo->allow[n], "=");
                        if (ep != NULL && ep[1] == '\0') {
                                ep_len = ep - fsmo->allow[n] + 1;
                                if (strncmp (fsmo->allow[n], option, ep_len) == 0) {
                                        allowed = TRUE;
                                        goto out;
                                }
                        } else {
                                if (strcmp (fsmo->allow[n], option) == 0) {
                                        allowed = TRUE;
                                        goto out;
                                }
                        }
                }
        }
        for (n = 0; any_allow[n] != NULL; n++) {
                ep = strstr (any_allow[n], "=");
                if (ep != NULL && ep[1] == '\0') {
                        ep_len = ep - any_allow[n] + 1;
                        if (strncmp (any_allow[n], option, ep_len) == 0) {
                                allowed = TRUE;
                                goto out;
                        }
                } else {
                        if (strcmp (any_allow[n], option) == 0) {
                                allowed = TRUE;
                                goto out;
                        }
                }
        }

        /* .. then check for mount options where the caller is allowed to pass
         * in his own uid
         */
        if (fsmo != NULL) {
                for (n = 0; fsmo->allow_uid_self[n] != NULL; n++) {
                        const char *r_mount_option = fsmo->allow_uid_self[n];
                        if (g_str_has_prefix (option, r_mount_option)) {
                                uid = strtol (option + strlen (r_mount_option), &endp, 10);
                                if (*endp != '\0')
                                        continue;
                                if (uid == caller_uid) {
                                        allowed = TRUE;
                                        goto out;
                                }
                        }
                }
        }

        /* .. ditto for gid
         */
        if (fsmo != NULL) {
                for (n = 0; fsmo->allow_gid_self[n] != NULL; n++) {
                        const char *r_mount_option = fsmo->allow_gid_self[n];
                        if (g_str_has_prefix (option, r_mount_option)) {
                                gid = strtol (option + strlen (r_mount_option), &endp, 10);
                                if (*endp != '\0')
                                        continue;
                                if (is_uid_in_gid (caller_uid, gid)) {
                                        allowed = TRUE;
                                        goto out;
                                }
                        }
                }
        }

        /* .. and finally check the mount options that requires authorization */
        if (fsmo != NULL) {
                for (n = 0; fsmo->restricted[n].mount_option != NULL; n++) {
                        const char *r_mount_option = fsmo->restricted[n].mount_option;
                        const char *r_authorization_needed = fsmo->restricted[n].authorization_needed;
                        ep = strstr (r_mount_option, "=");
                        if (ep != NULL && ep[1] == '\0') {
                                ep_len = ep - r_mount_option + 1;
                                if (strncmp (r_mount_option, option, ep_len) == 0) {
                                        allowed = TRUE;
                                        *auth_needed = r_authorization_needed;
                                        goto out;
                                }
                        } else {
                                if (strcmp (r_mount_option, option) == 0) {
                                        allowed = TRUE;
                                        *auth_needed = r_authorization_needed;
                                        goto out;
                                }
                        }
                }
        }
        for (n = 0; any_restricted[n].mount_option != NULL; n++) {
                const char *r_mount_option = any_restricted[n].mount_option;
                const char *r_authorization_needed = any_restricted[n].authorization_needed;
                ep = strstr (r_mount_option, "=");
                if (ep != NULL && ep[1] == '\0') {
                        ep_len = ep - r_mount_option + 1;
                        if (strncmp (r_mount_option, option, ep_len) == 0) {
                                allowed = TRUE;
                                *auth_needed = r_authorization_needed;
                                goto out;
                        }
                } else {
                        if (strcmp (r_mount_option, option) == 0) {
                                allowed = TRUE;
                                *auth_needed = r_authorization_needed;
                                goto out;
                        }
                }
        }


out:
        return allowed;
}

static char **
prepend_default_mount_options (const FSMountOptions *fsmo, uid_t caller_uid, char **given_options)
{
        GPtrArray *options;
        int n;
        char *s;
        gid_t gid;

        options = g_ptr_array_new ();
        if (fsmo != NULL) {
                for (n = 0; fsmo->defaults[n] != NULL; n++) {
                        const char *option = fsmo->defaults[n];

                        if (strcmp (option, "uid=") == 0) {
                                s = g_strdup_printf ("uid=%d", caller_uid);
                                g_ptr_array_add (options, s);
                        } else if (strcmp (option, "gid=") == 0) {
                                gid = find_primary_gid (caller_uid);
                                if (gid != (gid_t) -1) {
                                        s = g_strdup_printf ("gid=%d", gid);
                                        g_ptr_array_add (options, s);
                                }
                        } else {
                                g_ptr_array_add (options, g_strdup (option));
                        }
                }
        }
        for (n = 0; given_options[n] != NULL; n++) {
                g_ptr_array_add (options, g_strdup (given_options[n]));
        }

        g_ptr_array_add (options, NULL);

        return (char **) g_ptr_array_free (options, FALSE);
}

static void
mount_completed_cb (DBusGMethodInvocation *context,
                    DevkitDisksDevice *device,
                    PolKitCaller *pk_caller,
                    int status,
                    const char *stderr,
                    gpointer user_data)
{
        MountData *data = (MountData *) user_data;
        uid_t uid;

        uid = 0;
        if (pk_caller != NULL)
                polkit_caller_get_uid (pk_caller, &uid);

        if (WEXITSTATUS (status) == 0) {
                if (!data->is_remount) {
                        devkit_disks_device_local_set_mounted (device, data->mount_point);
                        mounts_file_add (device, uid, data->remove_dir_on_unmount);
                }
                dbus_g_method_return (context, data->mount_point);
        } else {
                if (!data->is_remount) {
                        if (g_rmdir (data->mount_point) != 0) {
                                g_warning ("Error removing dir in late mount error path: %m");
                        }
                }
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                             "Error mounting: mount exited with exit code %d: %s", WEXITSTATUS (status), stderr);
        }
}


gboolean
devkit_disks_device_mount (DevkitDisksDevice     *device,
                           const char            *filesystem_type,
                           char                 **given_options,
                           DBusGMethodInvocation *context)
{
        int n;
        GString *s;
        char *argv[10];
        char *mount_point;
        char *fstype;
        char *mount_options;
        GError *error;
        PolKitCaller *pk_caller;
        uid_t caller_uid;
        gboolean remove_dir_on_unmount;
        const FSMountOptions *fsmo;
        char **options;
        gboolean is_remount;

        fstype = NULL;
        options = NULL;
        mount_options = NULL;
        mount_point = NULL;
        is_remount = FALSE;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        polkit_caller_get_uid (pk_caller, &caller_uid);

        if (device->priv->info.id_usage == NULL ||
            strcmp (device->priv->info.id_usage, "filesystem") != 0) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTABLE,
                             "Not a mountable file system");
                goto out;
        }

        /* Check if the device is referenced in /etc/fstab; if it is we refuse
         * to mount the device to avoid violating system policy.
         */
        if (is_device_in_fstab (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_FSTAB_ENTRY,
                             "Refusing to mount devices referenced in /etc/fstab");
                goto out;
        }

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  "org.freedesktop.devicekit.disks.mount",
                                                  context)) {
                goto out;
        }

        /* set the fstype */
        fstype = NULL;
        if (strlen (filesystem_type) == 0) {
                if (device->priv->info.id_type != NULL && strlen (device->priv->info.id_type)) {
                        fstype = g_strdup (device->priv->info.id_type);
                } else {
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTABLE, "No file system type");
                        goto out;
                }
        } else {
                fstype = g_strdup (filesystem_type);
        }

        fsmo = find_mount_options_for_fs (fstype);

        /* always prepend some reasonable default mount options; these should be
         * chosen here so the user can override them later on.
         */
        options = prepend_default_mount_options (fsmo, caller_uid, given_options);

        /* validate mount options and check for authorizations */
        s = g_string_new ("uhelper=devkit,nodev,nosuid");
        for (n = 0; options[n] != NULL; n++) {
                const char *option = options[n];
                const char *auth_needed;

                /* avoid attacks like passing "shortname=lower,uid=0" as a single mount option */
                if (strstr (option, ",") != NULL) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_MOUNT_OPTION_NOT_ALLOWED,
                                     "Malformed mount option: ", option);
                        g_string_free (s, TRUE);
                        goto out;
                }

                /* first check if the mount option is allowed */
                if (!is_mount_option_allowed (fsmo, option, caller_uid, &auth_needed)) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_MOUNT_OPTION_NOT_ALLOWED,
                                     "Mount option %s is not allowed", option);
                        g_string_free (s, TRUE);
                        goto out;
                }

                /* may still be allowed but also may require an authorization */
                if (auth_needed != NULL) {
                        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                                  pk_caller,
                                                                  auth_needed,
                                                                  context)) {
                                g_string_free (s, TRUE);
                                goto out;
                        }
                }

                if (strcmp (option, "remount") == 0)
                        is_remount = TRUE;

                g_string_append_c (s, ',');
                g_string_append (s, option);
        }
        mount_options = g_string_free (s, FALSE);

        if (device->priv->info.device_is_mounted) {
                if (!is_remount) {
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_ALREADY_MOUNTED,
                                     "Device is already mounted");
                        goto out;
                }
        }

        /* handle some constraints required by remount */
        if (is_remount) {
                if (!device->priv->info.device_is_mounted ||
                    device->priv->info.device_mount_path == NULL) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_CANNOT_REMOUNT,
                                     "Can't remount a device that is not mounted");
                        goto out;
                }

                if (strlen (filesystem_type) > 0) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_CANNOT_REMOUNT,
                                     "Can't remount a device with a different file system type");
                        goto out;
                }
        }

        if (!is_remount) {
                /* Determine the mount point to use.
                 *
                 * TODO: use characteristics of the drive such as the name, connection etc.
                 *       to get better names (/media/disk is kinda lame).
                 */
                if (device->priv->info.id_label != NULL) {
                        mount_point = g_build_filename ("/media", device->priv->info.id_label, NULL);
                } else if (device->priv->info.id_uuid != NULL) {
                        mount_point = g_build_filename ("/media", device->priv->info.id_uuid, NULL);
                } else {
                        mount_point = g_strup ("/media/disk");
                }

try_another_mount_point:
                /* ... then uniqify the mount point and mkdir it */
                if (g_file_test (mount_point, G_FILE_TEST_EXISTS)) {
                        char *s = mount_point;
                        /* TODO: append numbers instead of _, __ and so on */
                        mount_point = g_strdup_printf ("%s_", mount_point);
                        g_free (s);
                        goto try_another_mount_point;
                }

                remove_dir_on_unmount = TRUE;

                if (g_mkdir (mount_point, 0700) != 0) {
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_GENERAL, "Error creating moint point: %m");
                        goto out;
                }

                n = 0;
                argv[n++] = "mount";
                argv[n++] = "-t";
                argv[n++] = fstype;
                argv[n++] = "-o";
                argv[n++] = mount_options;
                argv[n++] = device->priv->info.device_file;
                argv[n++] = mount_point;
                argv[n++] = NULL;
        } else {
                /* we recycle the mount point on remount */
                mount_point = g_strdup (device->priv->info.device_mount_path);
                n = 0;
                argv[n++] = "mount";
                argv[n++] = "-o";
                argv[n++] = mount_options;
                argv[n++] = mount_point;
                argv[n++] = NULL;
        }

        error = NULL;
        if (!job_new (context,
                      device,
                      pk_caller,
                      argv,
                      mount_completed_cb,
                      &error,
                      mount_data_new (mount_point, remove_dir_on_unmount, is_remount),
                      (GDestroyNotify) mount_data_free)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_GENERAL, "Error mounting: %s", error->message);
                g_error_free (error);
                if (!is_remount) {
                        if (g_rmdir (mount_point) != 0) {
                                g_warning ("Error removing dir in early mount error path: %m");
                        }
                }
                goto out;
        }

out:
        g_free (fstype);
        g_free (mount_options);
        g_free (mount_point);
        g_strfreev (options);
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
unmount_completed_cb (DBusGMethodInvocation *context,
                      DevkitDisksDevice *device,
                      PolKitCaller *pk_caller,
                      int status,
                      const char *stderr,
                      gpointer user_data)
{
        char *mount_path = user_data;

        if (WEXITSTATUS (status) == 0) {
                devkit_disks_device_local_set_unmounted (device);
                mounts_file_remove (device, mount_path);
                dbus_g_method_return (context);
        } else {
                if (strstr (stderr, "device is busy") != NULL) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_FILESYSTEM_BUSY,
                                     "Cannot unmount because file system on device is busy");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error unmounting: umount exited with exit code %d: %s",
                                     WEXITSTATUS (status),
                                     stderr);
                }
        }
}

gboolean
devkit_disks_device_unmount (DevkitDisksDevice     *device,
                             char                 **options,
                             DBusGMethodInvocation *context)
{
        int n;
        char *argv[16];
        GError *error;
        PolKitCaller *pk_caller;
        uid_t uid;
        uid_t uid_of_mount;
        gboolean force_unmount;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        uid = 0;
        if (pk_caller != NULL)
                polkit_caller_get_uid (pk_caller, &uid);

        if (!device->priv->info.device_is_mounted ||
            device->priv->info.device_mount_path == NULL) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTED,
                             "Device is not mounted");
                goto out;
        }

        if (!mounts_file_has_device (device, &uid_of_mount, NULL)) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTED_BY_DK,
                             "Device is not mounted by DeviceKit-disks");
                goto out;
        }

        if (uid_of_mount != uid) {
                if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                          pk_caller,
                                                          "org.freedesktop.devicekit.disks.unmount-others",
                                                          context))
                        goto out;
        }

        force_unmount = FALSE;
        for (n = 0; options[n] != NULL; n++) {
                char *option = options[n];
                if (strcmp ("force", option) == 0) {
                        force_unmount = TRUE;
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_UNMOUNT_OPTION_NOT_ALLOWED,
                                     "Device is not mounted by DeviceKit-disks");
                }
        }

        n = 0;
        argv[n++] = "umount";
        if (force_unmount) {
                /* on Linux we currently only have lazy unmount to emulate this */
                argv[n++] = "-l";
        }
        argv[n++] = device->priv->info.device_mount_path;
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      device,
                      pk_caller,
                      argv,
                      unmount_completed_cb,
                      &error,
                      g_strdup (device->priv->info.device_mount_path),
                      g_free)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_GENERAL, "Error unmounting: %s", error->message);
                g_error_free (error);
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

