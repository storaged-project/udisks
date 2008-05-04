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
#include <pwd.h>
#include <grp.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <gio/gunixmounts.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <devkit-gobject.h>
#include <polkit-dbus/polkit-dbus.h>
#include <sqlite3.h>

#include "devkit-disks-device.h"
#include "devkit-disks-device-private.h"
#include "devkit-disks-marshal.h"
#include "mounts-file.h"

/*--------------------------------------------------------------------------------------------------------------*/
#include "devkit-disks-device-glue.h"

static void     devkit_disks_device_class_init  (DevkitDisksDeviceClass *klass);
static void     devkit_disks_device_init        (DevkitDisksDevice      *seat);
static void     devkit_disks_device_finalize    (GObject     *object);

static void     init_info                  (DevkitDisksDevice *device);
static void     free_info                  (DevkitDisksDevice *device);
static gboolean update_info                (DevkitDisksDevice *device);

/* Returns the cleartext device. If device==NULL, unlocking failed and an error has
 * been reported back to the caller
 */
typedef void (*UnlockEncryptionHookFunc) (DBusGMethodInvocation *context,
                                          DevkitDisksDevice *device,
                                          gpointer user_data);

static gboolean devkit_disks_device_encrypted_unlock_internal (DevkitDisksDevice        *device,
                                                               const char               *secret,
                                                               char                    **options,
                                                               UnlockEncryptionHookFunc  hook_func,
                                                               gpointer                  hook_user_data,
                                                               DBusGMethodInvocation    *context);

/* if filesystem_create_succeeded==FALSE, mkfs failed and an error has been reported back to the caller */
typedef void (*FilesystemCreateHookFunc) (DBusGMethodInvocation *context,
                                          DevkitDisksDevice *device,
                                          gboolean filesystem_create_succeeded,
                                          gpointer user_data);

static gboolean
devkit_disks_device_filesystem_create_internal (DevkitDisksDevice       *device,
                                                const char              *fstype,
                                                char                   **options,
                                                FilesystemCreateHookFunc hook_func,
                                                gpointer                 hook_user_data,
                                                DBusGMethodInvocation *context);

typedef void (*ForceRemovalCompleteFunc)     (DevkitDisksDevice        *device,
                                              gboolean                  success,
                                              gpointer                  user_data);

static void force_removal                    (DevkitDisksDevice        *device,
                                              ForceRemovalCompleteFunc  callback,
                                              gpointer                  user_data);

static void force_unmount                    (DevkitDisksDevice        *device,
                                              ForceRemovalCompleteFunc  callback,
                                              gpointer                  user_data);

static void force_crypto_teardown            (DevkitDisksDevice        *device,
                                              DevkitDisksDevice        *cleartext_device,
                                              ForceRemovalCompleteFunc  callback,
                                              gpointer                  user_data);

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
        PROP_DEVICE_IS_READ_ONLY,
        PROP_DEVICE_IS_DRIVE,
        PROP_DEVICE_IS_CRYPTO_CLEARTEXT,
        PROP_DEVICE_IS_LINUX_MD_COMPONENT,
        PROP_DEVICE_IS_LINUX_MD,
        PROP_DEVICE_SIZE,
        PROP_DEVICE_BLOCK_SIZE,
        PROP_DEVICE_IS_MOUNTED,
        PROP_DEVICE_IS_BUSY,
        PROP_DEVICE_MOUNT_PATH,

        PROP_JOB_IN_PROGRESS,
        PROP_JOB_ID,
        PROP_JOB_IS_CANCELLABLE,
        PROP_JOB_NUM_TASKS,
        PROP_JOB_CUR_TASK,
        PROP_JOB_CUR_TASK_ID,
        PROP_JOB_CUR_TASK_PERCENTAGE,

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

        PROP_CRYPTO_CLEARTEXT_SLAVE,

        PROP_DRIVE_VENDOR,
        PROP_DRIVE_MODEL,
        PROP_DRIVE_REVISION,
        PROP_DRIVE_SERIAL,
        PROP_DRIVE_CONNECTION_INTERFACE,
        PROP_DRIVE_CONNECTION_SPEED,
        PROP_DRIVE_MEDIA_COMPATIBILITY,
        PROP_DRIVE_MEDIA,

        PROP_DRIVE_SMART_IS_CAPABLE,
        PROP_DRIVE_SMART_IS_ENABLED,
        PROP_DRIVE_SMART_TIME_COLLECTED,
        PROP_DRIVE_SMART_IS_FAILING,
        PROP_DRIVE_SMART_TEMPERATURE,
        PROP_DRIVE_SMART_TIME_POWERED_ON,
        PROP_DRIVE_SMART_LAST_SELF_TEST_RESULT,
        PROP_DRIVE_SMART_ATTRIBUTES,

        PROP_LINUX_MD_COMPONENT_LEVEL,
        PROP_LINUX_MD_COMPONENT_NUM_RAID_DEVICES,
        PROP_LINUX_MD_COMPONENT_UUID,
        PROP_LINUX_MD_COMPONENT_NAME,
        PROP_LINUX_MD_COMPONENT_VERSION,
        PROP_LINUX_MD_COMPONENT_UPDATE_TIME,
        PROP_LINUX_MD_COMPONENT_EVENTS,

        PROP_LINUX_MD_LEVEL,
        PROP_LINUX_MD_NUM_RAID_DEVICES,
        PROP_LINUX_MD_UUID,
        PROP_LINUX_MD_NAME,
        PROP_LINUX_MD_VERSION,
        PROP_LINUX_MD_SLAVES,
        PROP_LINUX_MD_SLAVES_STATE,
        PROP_LINUX_MD_IS_DEGRADED,
        PROP_LINUX_MD_SYNC_ACTION,
        PROP_LINUX_MD_SYNC_PERCENTAGE,
        PROP_LINUX_MD_SYNC_SPEED,
};

enum
{
        CHANGED_SIGNAL,
        JOB_CHANGED_SIGNAL,
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

        if (etype == 0) {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_GENERAL, "GeneralError"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NO_SUCH_DEVICE, "NoSuchDevice"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_SUPPORTED, "NotSupported"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTABLE, "NotMountable"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_MOUNTED, "Mounted"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTED, "NotMounted"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTED_BY_DK, "NotMountedByDeviceKit"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_FSTAB_ENTRY, "FstabEntry"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_MOUNT_OPTION_NOT_ALLOWED, "MountOptionNotAllowed"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_FILESYSTEM_BUSY, "FilesystemBusy"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_CANNOT_REMOUNT, "CannotRemount"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_UNMOUNT_OPTION_NOT_ALLOWED, "UnmountOptionNotAllowed"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NO_JOB_IN_PROGRESS, "NoJobInProgress"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_JOB_ALREADY_IN_PROGRESS, "JobAlreadyInProgress"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_JOB_CANNOT_BE_CANCELLED, "JobCannotBeCancelled"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED, "JobWasCancelled"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_PARTITION, "NotPartition"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_PARTITIONED, "NotPartitioned"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_CRYPTO, "NotCrypto"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_CRYPTO_ALREADY_UNLOCKED, "CryptoAlreadyUnlocked"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_CRYPTO_NOT_UNLOCKED, "CryptoNotUnlocked"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY, "IsBusy"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_DRIVE, "NotDrive"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_SMART_CAPABLE, "NotSmartCapable"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD, "NotLinuxMd"),
                        ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD_COMPONENT, "NotLinuxMdComponent"),
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
	case PROP_DEVICE_IS_READ_ONLY:
		g_value_set_boolean (value, device->priv->info.device_is_read_only);
		break;
	case PROP_DEVICE_IS_DRIVE:
		g_value_set_boolean (value, device->priv->info.device_is_drive);
		break;
	case PROP_DEVICE_IS_CRYPTO_CLEARTEXT:
		g_value_set_boolean (value, device->priv->info.device_is_crypto_cleartext);
		break;
	case PROP_DEVICE_IS_LINUX_MD_COMPONENT:
		g_value_set_boolean (value, device->priv->info.device_is_linux_md_component);
		break;
	case PROP_DEVICE_IS_LINUX_MD:
		g_value_set_boolean (value, device->priv->info.device_is_linux_md);
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
	case PROP_DEVICE_IS_BUSY:
                /* this property is special; it's value is computed on demand */
		g_value_set_boolean (value, devkit_disks_device_local_is_busy (device));
		break;
	case PROP_DEVICE_MOUNT_PATH:
		g_value_set_string (value, device->priv->info.device_mount_path);
		break;

	case PROP_JOB_IN_PROGRESS:
		g_value_set_boolean (value, device->priv->job_in_progress);
		break;
	case PROP_JOB_ID:
		g_value_set_string (value, device->priv->job_id);
		break;
	case PROP_JOB_IS_CANCELLABLE:
		g_value_set_boolean (value, device->priv->job_is_cancellable);
		break;
	case PROP_JOB_NUM_TASKS:
		g_value_set_int (value, device->priv->job_num_tasks);
		break;
	case PROP_JOB_CUR_TASK:
		g_value_set_int (value, device->priv->job_cur_task);
		break;
	case PROP_JOB_CUR_TASK_ID:
		g_value_set_string (value, device->priv->job_cur_task_id);
		break;
	case PROP_JOB_CUR_TASK_PERCENTAGE:
		g_value_set_double (value, device->priv->job_cur_task_percentage);
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

	case PROP_CRYPTO_CLEARTEXT_SLAVE:
                if (device->priv->info.crypto_cleartext_slave != NULL)
                        g_value_set_boxed (value, device->priv->info.crypto_cleartext_slave);
                else
                        g_value_set_boxed (value, "/");
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
	case PROP_DRIVE_CONNECTION_INTERFACE:
		g_value_set_string (value, device->priv->info.drive_connection_interface);
		break;
	case PROP_DRIVE_CONNECTION_SPEED:
		g_value_set_uint64 (value, device->priv->info.drive_connection_speed);
		break;
	case PROP_DRIVE_MEDIA_COMPATIBILITY:
		g_value_set_boxed (value, device->priv->info.drive_media_compatibility);
		break;
	case PROP_DRIVE_MEDIA:
		g_value_set_string (value, device->priv->info.drive_media);
		break;

	case PROP_DRIVE_SMART_IS_CAPABLE:
		g_value_set_boolean (value, device->priv->drive_smart_is_capable);
		break;
	case PROP_DRIVE_SMART_IS_ENABLED:
		g_value_set_boolean (value, device->priv->drive_smart_is_enabled);
		break;
	case PROP_DRIVE_SMART_TIME_COLLECTED:
		g_value_set_uint64 (value, device->priv->drive_smart_time_collected);
		break;
	case PROP_DRIVE_SMART_IS_FAILING:
		g_value_set_boolean (value, device->priv->drive_smart_is_failing);
		break;
	case PROP_DRIVE_SMART_TEMPERATURE:
		g_value_set_double (value, device->priv->drive_smart_temperature);
		break;
	case PROP_DRIVE_SMART_TIME_POWERED_ON:
		g_value_set_uint64 (value, device->priv->drive_smart_time_powered_on);
		break;
	case PROP_DRIVE_SMART_LAST_SELF_TEST_RESULT:
		g_value_set_string (value, device->priv->drive_smart_last_self_test_result);
		break;
	case PROP_DRIVE_SMART_ATTRIBUTES:
		g_value_set_boxed (value, device->priv->drive_smart_attributes);
		break;

	case PROP_LINUX_MD_COMPONENT_LEVEL:
		g_value_set_string (value, device->priv->info.linux_md_component_level);
		break;
	case PROP_LINUX_MD_COMPONENT_NUM_RAID_DEVICES:
		g_value_set_int (value, device->priv->info.linux_md_component_num_raid_devices);
		break;
	case PROP_LINUX_MD_COMPONENT_UUID:
		g_value_set_string (value, device->priv->info.linux_md_component_uuid);
		break;
	case PROP_LINUX_MD_COMPONENT_NAME:
		g_value_set_string (value, device->priv->info.linux_md_component_name);
		break;
	case PROP_LINUX_MD_COMPONENT_VERSION:
		g_value_set_string (value, device->priv->info.linux_md_component_version);
		break;
	case PROP_LINUX_MD_COMPONENT_UPDATE_TIME:
		g_value_set_uint64 (value, device->priv->info.linux_md_component_update_time);
		break;
	case PROP_LINUX_MD_COMPONENT_EVENTS:
		g_value_set_uint64 (value, device->priv->info.linux_md_component_events);
		break;

	case PROP_LINUX_MD_LEVEL:
		g_value_set_string (value, device->priv->info.linux_md_level);
		break;
	case PROP_LINUX_MD_NUM_RAID_DEVICES:
		g_value_set_int (value, device->priv->info.linux_md_num_raid_devices);
		break;
	case PROP_LINUX_MD_VERSION:
		g_value_set_string (value, device->priv->info.linux_md_version);
		break;
	case PROP_LINUX_MD_SLAVES:
                g_value_set_boxed (value, device->priv->info.linux_md_slaves);
                break;
	case PROP_LINUX_MD_SLAVES_STATE:
                g_value_set_boxed (value, device->priv->info.linux_md_slaves_state);
                break;
	case PROP_LINUX_MD_IS_DEGRADED:
                g_value_set_boolean (value, device->priv->info.linux_md_is_degraded);
                break;
	case PROP_LINUX_MD_SYNC_ACTION:
                g_value_set_string (value, device->priv->info.linux_md_sync_action);
                break;
	case PROP_LINUX_MD_SYNC_PERCENTAGE:
                g_value_set_double (value, device->priv->info.linux_md_sync_percentage);
                break;
	case PROP_LINUX_MD_SYNC_SPEED:
                g_value_set_uint64 (value, device->priv->info.linux_md_sync_speed);
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

        signals[JOB_CHANGED_SIGNAL] =
                g_signal_new ("job-changed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              devkit_disks_marshal_VOID__BOOLEAN_STRING_BOOLEAN_INT_INT_STRING_DOUBLE,
                              G_TYPE_NONE,
                              7,
                              G_TYPE_BOOLEAN,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN,
                              G_TYPE_INT,
                              G_TYPE_INT,
                              G_TYPE_STRING,
                              G_TYPE_DOUBLE);

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
                PROP_DEVICE_IS_READ_ONLY,
                g_param_spec_boolean ("device-is-read-only", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_DRIVE,
                g_param_spec_boolean ("device-is-drive", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_CRYPTO_CLEARTEXT,
                g_param_spec_boolean ("device-is-crypto-cleartext", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_LINUX_MD_COMPONENT,
                g_param_spec_boolean ("device-is-linux-md-component", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_IS_LINUX_MD,
                g_param_spec_boolean ("device-is-linux-md", NULL, NULL, FALSE, G_PARAM_READABLE));
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
                PROP_DEVICE_IS_BUSY,
                g_param_spec_boolean ("device-is-busy", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DEVICE_MOUNT_PATH,
                g_param_spec_string ("device-mount-path", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_JOB_IN_PROGRESS,
                g_param_spec_boolean ("job-in-progress", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_JOB_ID,
                g_param_spec_string ("job-id", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_JOB_IS_CANCELLABLE,
                g_param_spec_boolean ("job-is-cancellable", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_JOB_NUM_TASKS,
                g_param_spec_int ("job-num-tasks", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_JOB_CUR_TASK,
                g_param_spec_int ("job-cur-task", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_JOB_CUR_TASK_ID,
                g_param_spec_string ("job-cur-task-id", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_JOB_CUR_TASK_PERCENTAGE,
                g_param_spec_double ("job-cur-task-percentage", NULL, NULL, -1, 100, -1, G_PARAM_READABLE));

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
                PROP_CRYPTO_CLEARTEXT_SLAVE,
                g_param_spec_boxed ("crypto-cleartext-slave", NULL, NULL, DBUS_TYPE_G_OBJECT_PATH, G_PARAM_READABLE));

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
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_CONNECTION_INTERFACE,
                g_param_spec_string ("drive-connection-interface", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_CONNECTION_SPEED,
                g_param_spec_uint64 ("drive-connection-speed", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_MEDIA_COMPATIBILITY,
                g_param_spec_boxed ("drive-media-compatibility", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
                                    G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_MEDIA,
                g_param_spec_string ("drive-media", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_DRIVE_SMART_IS_CAPABLE,
                g_param_spec_boolean ("drive-smart-is-capable", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_SMART_IS_ENABLED,
                g_param_spec_boolean ("drive-smart-is-enabled", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_SMART_TIME_COLLECTED,
                g_param_spec_uint64 ("drive-smart-time-collected", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_SMART_IS_FAILING,
                g_param_spec_boolean ("drive-smart-is-failing", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_SMART_TEMPERATURE,
                g_param_spec_double ("drive-smart-temperature", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_SMART_TIME_POWERED_ON,
                g_param_spec_uint64 ("drive-smart-time-powered-on", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_SMART_LAST_SELF_TEST_RESULT,
                g_param_spec_string ("drive-smart-last-self-test-result", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVE_SMART_ATTRIBUTES,
                g_param_spec_boxed ("drive-smart-attributes", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", SMART_DATA_STRUCT_TYPE),
                                    G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_COMPONENT_LEVEL,
                g_param_spec_string ("linux-md-component-level", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_COMPONENT_NUM_RAID_DEVICES,
                g_param_spec_int ("linux-md-component-num-raid-devices", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_COMPONENT_UUID,
                g_param_spec_string ("linux-md-component-uuid", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_COMPONENT_NAME,
                g_param_spec_string ("linux-md-component-name", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_COMPONENT_VERSION,
                g_param_spec_string ("linux-md-component-version", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_COMPONENT_UPDATE_TIME,
                g_param_spec_uint64 ("linux-md-component-update-time", NULL, NULL, 0, G_MAXUINT64,0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_COMPONENT_EVENTS,
                g_param_spec_uint64 ("linux-md-component-events", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_LEVEL,
                g_param_spec_string ("linux-md-level", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_NUM_RAID_DEVICES,
                g_param_spec_int ("linux-md-num-raid-devices", NULL, NULL, 0, G_MAXINT, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_UUID,
                g_param_spec_string ("linux-md-uuid", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_NAME,
                g_param_spec_string ("linux-md-name", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_VERSION,
                g_param_spec_string ("linux-md-version", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_SLAVES,
                g_param_spec_boxed ("linux-md-slaves", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
                                    G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_SLAVES_STATE,
                g_param_spec_boxed ("linux-md-slaves-state", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING),
                                    G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_IS_DEGRADED,
                g_param_spec_boolean ("linux-md-is-degraded", NULL, NULL, FALSE, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_SYNC_ACTION,
                g_param_spec_string ("linux-md-sync-action", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_SYNC_PERCENTAGE,
                g_param_spec_double ("linux-md-sync-percentage", NULL, NULL, 0.0, 100.0, 0.0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_LINUX_MD_SYNC_SPEED,
                g_param_spec_uint64 ("linux-md-sync-speed", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));
}

static void
devkit_disks_device_init (DevkitDisksDevice *device)
{
        device->priv = DEVKIT_DISKS_DEVICE_GET_PRIVATE (device);
        init_info (device);
        device->priv->drive_smart_attributes = g_ptr_array_new ();
}

static void
devkit_disks_device_finalize (GObject *object)
{
        DevkitDisksDevice *device;

        g_return_if_fail (object != NULL);
        g_return_if_fail (DEVKIT_IS_DISKS_DEVICE (object));

        device = DEVKIT_DISKS_DEVICE (object);
        g_return_if_fail (device->priv != NULL);

        g_object_unref (device->priv->d);
        g_object_unref (device->priv->daemon);
        g_free (device->priv->object_path);

        g_free (device->priv->native_path);

        free_info (device);

        g_free (device->priv->drive_smart_last_self_test_result);
        g_ptr_array_foreach (device->priv->drive_smart_attributes, (GFunc) g_value_array_free, NULL);
        g_ptr_array_free (device->priv->drive_smart_attributes, TRUE);

        if (device->priv->linux_md_poll_timeout_id > 0)
                g_source_remove (device->priv->linux_md_poll_timeout_id);

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

static double
sysfs_get_double (const char *dir, const char *attribute)
{
        double result;
        char *contents;
        char *filename;

        result = 0.0;
        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_get_contents (filename, &contents, NULL, NULL)) {
                result = atof (contents);
                g_free (contents);
        }
        g_free (filename);


        return result;
}

static gboolean
sysfs_file_contains (const char *dir, const char *attribute, const char *string)
{
        gboolean result;
        char *filename;
        char *s;

        result = FALSE;

        filename = g_build_filename (dir, attribute, NULL);
        if (g_file_get_contents (filename, &s, NULL, NULL)) {
                result = (strstr(s, string) != NULL);
                g_free (s);
        }
        g_free (filename);

        return result;
}

static char *
sysfs_get_string (const char *dir, const char *attribute)
{
        char *result;
        char *filename;

        result = NULL;
        filename = g_build_filename (dir, attribute, NULL);
        if (!g_file_get_contents (filename, &result, NULL, NULL)) {
                result = g_strdup ("");
        }
        g_free (filename);

        return result;
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

static guint64
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
devkit_device_emit_changed_to_kernel (DevkitDisksDevice *device)
{
        FILE *f;
        char *filename;

        filename = g_build_filename (device->priv->native_path, "uevent", NULL);
        f = fopen (filename, "w");
        if (f == NULL) {
                g_warning ("error opening %s for writing: %m", filename);
        } else {
                /* TODO: change 'add' to 'change' when new udev rules are released */
                if (fputs ("add", f) == EOF) {
                        g_warning ("error writing 'add' to %s: %m", filename);
                }
                fclose (f);
        }
        g_free (filename);
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

        g_free (device->priv->info.crypto_cleartext_slave);

        g_free (device->priv->info.drive_vendor);
        g_free (device->priv->info.drive_model);
        g_free (device->priv->info.drive_revision);
        g_free (device->priv->info.drive_serial);
        g_free (device->priv->info.drive_connection_interface);
        g_ptr_array_foreach (device->priv->info.drive_media_compatibility, (GFunc) g_free, NULL);
        g_ptr_array_free (device->priv->info.drive_media_compatibility, TRUE);
        g_free (device->priv->info.drive_media);

        g_free (device->priv->info.linux_md_component_level);
        g_free (device->priv->info.linux_md_component_uuid);
        g_free (device->priv->info.linux_md_component_name);
        g_free (device->priv->info.linux_md_component_version);

        g_free (device->priv->info.linux_md_level);
        g_free (device->priv->info.linux_md_version);
        g_ptr_array_foreach (device->priv->info.linux_md_slaves, (GFunc) g_free, NULL);
        g_ptr_array_free (device->priv->info.linux_md_slaves, TRUE);
        g_ptr_array_foreach (device->priv->info.linux_md_slaves_state, (GFunc) g_free, NULL);
        g_ptr_array_free (device->priv->info.linux_md_slaves_state, TRUE);

        g_free (device->priv->info.dm_name);
        g_ptr_array_foreach (device->priv->info.slaves_objpath, (GFunc) g_free, NULL);
        g_ptr_array_free (device->priv->info.slaves_objpath, TRUE);
        g_ptr_array_foreach (device->priv->info.holders_objpath, (GFunc) g_free, NULL);
        g_ptr_array_free (device->priv->info.holders_objpath, TRUE);
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
        device->priv->info.drive_media_compatibility = g_ptr_array_new ();
        device->priv->info.linux_md_slaves = g_ptr_array_new ();
        device->priv->info.linux_md_slaves_state = g_ptr_array_new ();
        device->priv->info.slaves_objpath = g_ptr_array_new ();
        device->priv->info.holders_objpath = g_ptr_array_new ();
}


static char *
_dupv8 (const char *s)
{
        const char *end_valid;

        if (!g_utf8_validate (s,
                             -1,
                             &end_valid)) {
                g_warning ("The string '%s' is not valid UTF-8. Invalid characters begins at '%s'", s, end_valid);
                return g_strndup (s, end_valid - s);
        } else {
                return g_strdup (s);
        }
}

static char *
sysfs_resolve_link (const char *sysfs_path, const char *name)
{
        char *full_path;
        char link_path[PATH_MAX];
        char resolved_path[PATH_MAX];
        ssize_t num;
        gboolean found_it;

        found_it = FALSE;

        full_path = g_build_filename (sysfs_path, name, NULL);

        //g_warning ("name='%s'", name);
        //g_warning ("full_path='%s'", full_path);
        num = readlink (full_path, link_path, sizeof (link_path) - 1);
        if (num != -1) {
                char *absolute_path;

                link_path[num] = '\0';

                //g_warning ("link_path='%s'", link_path);
                absolute_path = g_build_filename (sysfs_path, link_path, NULL);
                //g_warning ("absolute_path='%s'", absolute_path);
                if (realpath (absolute_path, resolved_path) != NULL) {
                        //g_warning ("resolved_path='%s'", resolved_path);
                        found_it = TRUE;
                }
                g_free (absolute_path);
        }
        g_free (full_path);

        if (found_it)
                return g_strdup (resolved_path);
        else
                return NULL;
}

static gboolean
update_info_properties_cb (DevkitDevice *d, const char *key, const char *value, void *user_data)
{
        int n;
        gboolean ignore_device;
        DevkitDisksDevice *device = user_data;

        ignore_device = FALSE;

        if (strcmp (key, "ID_FS_USAGE") == 0) {
                device->priv->info.id_usage   = g_strdup (value);
        } else if (strcmp (key, "ID_FS_TYPE") == 0) {
                device->priv->info.id_type    = g_strdup (value);
        } else if (strcmp (key, "ID_FS_VERSION") == 0) {
                device->priv->info.id_version = g_strdup (value);
                if (device->priv->info.device_is_linux_md_component) {
                        device->priv->info.linux_md_component_version = g_strdup (value);
                }
        } else if (strcmp (key, "ID_FS_UUID") == 0) {
                device->priv->info.id_uuid    = g_strdup (value);
                if (device->priv->info.device_is_linux_md_component) {
                        device->priv->info.linux_md_component_uuid = g_strdup (value);
                }
        } else if (strcmp (key, "ID_FS_LABEL") == 0) {
                device->priv->info.id_label   = g_strdup (value);
                if (device->priv->info.device_is_linux_md_component) {
                        device->priv->info.linux_md_component_name = g_strdup (value);
                }

        } else if (strcmp (key, "ID_VENDOR") == 0) {
                if (device->priv->info.device_is_drive && device->priv->info.drive_vendor == NULL)
                        device->priv->info.drive_vendor = g_strdup (value);
        } else if (strcmp (key, "ID_MODEL") == 0) {
                if (device->priv->info.device_is_drive && device->priv->info.drive_model == NULL)
                        device->priv->info.drive_model = g_strdup (value);
        } else if (strcmp (key, "ID_REVISION") == 0) {
                if (device->priv->info.device_is_drive && device->priv->info.drive_revision == NULL)
                        device->priv->info.drive_revision = g_strdup (value);
        } else if (strcmp (key, "ID_SERIAL_SHORT") == 0) {
                if (device->priv->info.device_is_drive && device->priv->info.drive_serial == NULL)
                        device->priv->info.drive_serial = g_strdup (value);

        } else if (strcmp (key, "PART_SCHEME") == 0) {

                if (device->priv->info.device_is_partition) {
                        device->priv->info.partition_scheme =
                                g_strdup (value);
                } else {
                        device->priv->info.device_is_partition_table = TRUE;
                        device->priv->info.partition_table_scheme =
                                g_strdup (value);
                }
        } else if (strcmp (key, "PART_COUNT") == 0) {
                device->priv->info.partition_table_count = devkit_device_get_property_as_int (d, key);
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
                                value = devkit_device_get_property_as_uint64 (d, key);
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
                                                g_strdup (value);
                                } else if (g_str_has_prefix (endp, "_UUID")) {
                                        device->priv->info.partition_uuid =
                                                g_strdup (value);
                                } else if (g_str_has_prefix (endp, "_TYPE")) {
                                        device->priv->info.partition_type =
                                                g_strdup (value);
                                } else if (g_str_has_prefix (endp, "_OFFSET")) {
                                        device->priv->info.partition_offset =
                                                devkit_device_get_property_as_uint64 (d, key);
                                } else if (g_str_has_prefix (endp, "_SIZE")) {
                                        device->priv->info.partition_size =
                                                devkit_device_get_property_as_uint64 (d, key);
                                } else if (g_str_has_prefix (endp, "_FLAGS")) {
                                        char **tokens = devkit_device_dup_property_as_strv (d, key);
                                        for (n = 0; tokens[n] != NULL; n++)
                                                g_ptr_array_add (device->priv->info.partition_flags, tokens[n]);
                                        g_free (tokens);
                                }
                        }
                }

        } else if (strcmp (key, "MD_DEVICES") == 0) {
                device->priv->info.linux_md_component_num_raid_devices = devkit_device_get_property_as_int (d, key);

        } else if (strcmp (key, "MD_LEVEL") == 0) {
                device->priv->info.linux_md_component_level = g_strdup (value);

        } else if (strcmp (key, "MD_UPDATE_TIME") == 0) {
                device->priv->info.linux_md_component_update_time = devkit_device_get_property_as_uint64 (d, key);

        } else if (strcmp (key, "MD_EVENTS") == 0) {
                device->priv->info.linux_md_component_events = devkit_device_get_property_as_uint64 (d, key);

        } else if (strcmp (key, "DM_NAME") == 0) {
                if (g_str_has_prefix (value, "temporary-cryptsetup-")) {
                        /* ignore temporary devices created by /sbin/cryptsetup */
                        ignore_device = TRUE;
                        goto out;
                } else {
                        /* TODO: export this at some point */
                        device->priv->info.dm_name = g_strdup (value);
                }

        } else if (strcmp (key, "DM_TARGET_TYPES") == 0) {
                if (strcmp (value, "crypt") == 0) {
                        /* we're a dm-crypt target and can, by design, then only have one slave */
                        if (device->priv->info.slaves_objpath->len == 1) {
                                /* avoid claiming we are a drive since we want to be related
                                 * to the cryptotext device
                                 */
                                device->priv->info.device_is_drive = FALSE;

                                device->priv->info.device_is_crypto_cleartext = TRUE;
                                device->priv->info.crypto_cleartext_slave =
                                        g_strdup (g_ptr_array_index (device->priv->info.slaves_objpath, 0));
                        }
                }

        } else if (strcmp (key, "COMPAT_MEDIA_TYPE") == 0) {
                char **tokens = devkit_device_dup_property_as_strv (d, key);
                for (n = 0; tokens[n] != NULL; n++)
                        g_ptr_array_add (device->priv->info.drive_media_compatibility, tokens[n]);
                g_free (tokens);
        } else if (strcmp (key, "MEDIA_TYPE") == 0) {
                device->priv->info.drive_media = g_strdup (value);


        } else if (strcmp (key, "MEDIA_AVAILABLE") == 0) {
                if (device->priv->info.device_is_removable) {
                        device->priv->info.device_is_media_available = devkit_device_get_property_as_boolean (d, key);
                }
        }

out:
        return ignore_device;
}

static void
update_slaves (DevkitDisksDevice *device)
{
        unsigned int n;

        /* Problem: The kernel doesn't send out a 'change' event when holders/ change. This
         *          means that we'll have stale data in holder_objpath. However, since having
         *          a slave is something one has for his lifetime, we can manually update
         *          the holders/ on the slaves when the holder is added/removed.
         *
         *          E.g. when a holder (e.g. dm-0) appears, we call update_holders() on every
         *          device referenced in the slaves/ directory (e.g. sdb1). Similar, when a
         *          holder (e.g. dm-0) disappears we'll do the same on the devices in
         *          slaves_objpath (the sysfs entry is long gone already so can't look in
         *          the slaves/ directory) e.g. for sdb1.
         *
         *          Of course the kernel should just generate 'change' events for e.g. sdb1.
         */

        for (n = 0; n < device->priv->info.slaves_objpath->len; n++) {
                const char *slave_objpath = device->priv->info.slaves_objpath->pdata[n];
                DevkitDisksDevice *slave;

                slave = devkit_disks_daemon_local_find_by_object_path (device->priv->daemon, slave_objpath);
                if (slave != NULL) {
                        update_info (slave);
                }
        }
}

static void
update_drive_properties (DevkitDisksDevice *device)
{
        char *s;
        char *p;
        char *model;
        char *vendor;
        char *subsystem;
        char *connection_interface;
        guint64 connection_speed;

        connection_interface = NULL;
        connection_speed = 0;

        /* walk up the device tree to figure out the subsystem */
        s = g_strdup (device->priv->native_path);
        do {
                p = sysfs_resolve_link (s, "subsystem");
                if (p != NULL) {
                        subsystem = g_path_get_basename (p);
                        g_free (p);

                        if (strcmp (subsystem, "scsi") == 0) {
                                g_free (connection_interface);
                                connection_interface = g_strdup ("scsi");
                                connection_speed = 0;

                                /* continue walking up the chain; we just use scsi as a fallback */

                                /* grab the names from SCSI since the names from udev currently
                                 *  - replaces whitespace with _
                                 *  - is missing for e.g. Firewire
                                 */
                                vendor = sysfs_get_string (s, "vendor");
                                if (vendor != NULL) {
                                        g_free (device->priv->info.drive_vendor);
                                        g_strstrip (vendor);
                                        device->priv->info.drive_vendor = _dupv8 (vendor);
                                        g_free (vendor);
                                }

                                model = sysfs_get_string (s, "model");
                                if (model != NULL) {
                                        g_free (device->priv->info.drive_model);
                                        g_strstrip (model);
                                        device->priv->info.drive_model = _dupv8 (model);
                                        g_free (model);
                                }

                                /* TODO: need to improve this code; we probably need the kernel to export more
                                 *       information before we can properly get the type and speed.
                                 */

                                if (device->priv->info.drive_vendor != NULL &&
                                    strcmp (device->priv->info.drive_vendor, "ATA") == 0) {
                                        g_free (connection_interface);
                                        connection_interface = g_strdup ("ata");
                                        break;
                                }

                        } else if (strcmp (subsystem, "usb") == 0) {
                                double usb_speed;

                                /* both the interface and the device will be 'usb'. However only
                                 * the device will have the 'speed' property.
                                 */
                                usb_speed = sysfs_get_double (s, "speed");
                                if (usb_speed > 0) {
                                        g_free (connection_interface);
                                        connection_interface = g_strdup ("usb");
                                        connection_speed = usb_speed * (1000 * 1000);
                                        break;
                                }
                        } else if (strcmp (subsystem, "firewire") == 0) {

                                /* TODO: krh has promised a speed file in sysfs; theoretically, the speed can
                                 *       be anything from 100, 200, 400, 800 and 3200. Till then we just hardcode
                                 *       a resonable default of 400 Mbit/s.
                                 */

                                g_free (connection_interface);
                                connection_interface = g_strdup ("firewire");
                                connection_speed = 400 * (1000 * 1000);
                                break;
                        }


                        g_free (subsystem);
                }

                /* advance up the chain */
                p = g_strrstr (s, "/");
                if (p == NULL)
                        break;
                *p = '\0';

                /* but stop at the root */
                if (strcmp (s, "/sys/devices") == 0)
                        break;

        } while (TRUE);

        if (connection_interface != NULL) {
                device->priv->info.drive_connection_interface = connection_interface;
                device->priv->info.drive_connection_speed = connection_speed;
        }

        g_free (s);
}

static void
linux_md_emit_changed_on_components (DevkitDisksDevice *device)
{
        unsigned int n;

        if (!device->priv->info.device_is_linux_md) {
                g_warning ("not a linux-md device");
                goto out;
        }

        for (n = 0; n < device->priv->info.linux_md_slaves->len; n++) {
                DevkitDisksDevice *d;
                d = devkit_disks_daemon_local_find_by_object_path (
                        device->priv->daemon,
                        device->priv->info.linux_md_slaves->pdata[n]);
                if (d != NULL)
                        devkit_device_emit_changed_to_kernel (d);
        }
out:
        ;
}

static gboolean
poll_syncing_md_device (gpointer user_data)
{
        DevkitDisksDevice *device = DEVKIT_DISKS_DEVICE (user_data);

        g_warning ("polling md device");

        device->priv->linux_md_poll_timeout_id = 0;
        devkit_disks_daemon_local_synthesize_changed (device->priv->daemon, device->priv->d);
        return FALSE;
}

static gboolean
strv_has_str (char **strv, char *str)
{
        int n;

        g_return_val_if_fail (strv != NULL && str != NULL, FALSE);

        for (n = 0; strv[n] != NULL; n++) {
                if (strcmp (strv[n], str) == 0)
                        return TRUE;
        }
        return FALSE;
}

/**
 * update_info:
 * @device: the device
 *
 * Update information about the device.
 *
 * Returns: #TRUE to keep (or add) the device; #FALSE to ignore (or remove) the device
 **/
static gboolean
update_info (DevkitDisksDevice *device)
{
        guint64 start, size;
        char *s;
        char *p;
        int n;
        gboolean ret;
        int fd;
        int block_size;
        char *path;
        GDir *dir;
        const char *name;
        GList *l;
        const char *fstype;

        ret = FALSE;

        /* md is special; we don't get "remove" events from the kernel when an array is
         * stopped; so catch it very early before erasing our existing slave variable (we
         * need this to set them slaves free)
         */

        /* TODO: may need to accept more states than 'clear' */
        if (sysfs_file_contains (device->priv->native_path, "md/array_state", "clear")) {
                /* set the slaves free and remove ourselves */
                update_slaves (device);

                /* see if this is a going away event */
                if (device->priv->info.device_is_linux_md) {
                        /* Since we're going away it means that the superblocks on the components have
                         * been updated. We want to reread this new data.
                         */
                        linux_md_emit_changed_on_components (device);
                }
		g_warning ("Linux MD array %s is not 'clear'; removing", device->priv->native_path);
                goto out;
        }

        /* free all info and prep for new info */
        free_info (device);
        init_info (device);

        /* drive identification */
        if (sysfs_file_exists (device->priv->native_path, "range")) {
                device->priv->info.device_is_drive = TRUE;
        } else {
                device->priv->info.device_is_drive = FALSE;
        }

        device->priv->info.device_file = g_strdup (devkit_device_get_device_file (device->priv->d));
        if (device->priv->info.device_file == NULL) {
		g_warning ("No device file for %s", device->priv->native_path);
                goto out;
        }

        const char * const * symlinks;
        symlinks = devkit_device_get_device_file_symlinks (device->priv->d);
        for (n = 0; symlinks[n] != NULL; n++) {
                if (g_str_has_prefix (symlinks[n], "/dev/disk/by-id/") ||
                    g_str_has_prefix (symlinks[n], "/dev/disk/by-uuid/")) {
                        g_ptr_array_add (device->priv->info.device_file_by_id, g_strdup (symlinks[n]));
                } else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-path/")) {
                        g_ptr_array_add (device->priv->info.device_file_by_path, g_strdup (symlinks[n]));
                }
        }

        /* TODO: hmm.. it would be really nice if sysfs could export this. There's a
         *       queue/hw_sector_size in sysfs but that's not available for e.g. Linux md devices
         */
        errno = 0;
        fd = open (device->priv->info.device_file, O_RDONLY);
        if (fd < 0 && errno != ENOMEDIUM) {
		g_warning ("Cannot open %s read only", device->priv->info.device_file);
                goto out;
        }
        if (errno == ENOMEDIUM) {
                block_size = 0;
        } else {
                if (ioctl (fd, BLKSSZGET, &block_size) != 0) {
                        g_warning ("Cannot determine block size for %s", device->priv->info.device_file);
                        goto out;
                }
                close (fd);

                /* So we have media, find out if it's read-only.
                 *
                 * (e.g. write-protect on SD cards, optical drives etc.)
                 */
                errno = 0;
                fd = open (device->priv->info.device_file, O_WRONLY);
                if (fd < 0) {
                        if (errno == EROFS) {
                                device->priv->info.device_is_read_only = TRUE;
                        } else {
                                g_warning ("Cannot determine if %s is read only: %m", device->priv->info.device_file);
                                goto out;
                        }
                } else {
                        close (fd);
                }

        }
        device->priv->info.device_block_size = block_size;


        device->priv->info.device_is_removable =
                (sysfs_get_int (device->priv->native_path, "removable") != 0);
        if (!device->priv->info.device_is_removable)
                device->priv->info.device_is_media_available = TRUE;
        /* Weird. Does the kernel use 512 byte sectors for "size" and "start"? */
        device->priv->info.device_size =
                sysfs_get_uint64 (device->priv->native_path, "size") * ((guint64) 512); /* device->priv->info.device_block_size; */

        /* figure out if we're a partition and, if so, who our slave is */
        if (sysfs_file_exists (device->priv->native_path, "start")) {

                /* we're partitioned by the kernel */
                device->priv->info.device_is_partition = TRUE;
                start = sysfs_get_uint64 (device->priv->native_path, "start");
                size = sysfs_get_uint64 (device->priv->native_path, "size");
                device->priv->info.partition_offset = start * 512; /* device->priv->info.device_block_size; */
                device->priv->info.partition_size = size * 512; /* device->priv->info.device_block_size; */

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

        /* Maintain (non-exported) properties holders and slaves for the holders resp. slaves
         * directories in sysfs. The entries in these arrays are object paths (that may not
         * exist; we just compute the name).
         */
        path = g_build_filename (device->priv->native_path, "slaves", NULL);
        if((dir = g_dir_open (path, 0, NULL)) != NULL) {
                while ((name = g_dir_read_name (dir)) != NULL) {
                        s = compute_object_path_from_basename (name);
                        g_ptr_array_add (device->priv->info.slaves_objpath, s);
                }
                g_dir_close (dir);
        }
        g_free (path);

        path = g_build_filename (device->priv->native_path, "holders", NULL);
        if((dir = g_dir_open (path, 0, NULL)) != NULL) {
                while ((name = g_dir_read_name (dir)) != NULL) {
                        s = compute_object_path_from_basename (name);
                        g_ptr_array_add (device->priv->info.holders_objpath, s);
                }
                g_dir_close (dir);
        }
        g_free (path);

        /* ------------------------------------- */
        /* Now set all properties from udev data */
        /* ------------------------------------- */

        /* set this first since e.g. ID_FS_LABEL et. al. needs to be redirected/copied */
        fstype = devkit_device_get_property (device->priv->d, "ID_FS_TYPE");
        if (fstype != NULL && strcmp (fstype, "linux_raid_member") == 0) {
                device->priv->info.device_is_linux_md_component = TRUE;
        }

        if (devkit_device_properties_foreach (device->priv->d, update_info_properties_cb, device)) {
		g_warning ("Iteration of properties was short circuited for %s", device->priv->native_path);
                goto out;
        }

        update_slaves (device);

        /* Linux MD detection */
        if (sysfs_file_exists (device->priv->native_path, "md")) {
                char *raid_level;

                device->priv->info.device_is_linux_md = TRUE;

                raid_level = g_strstrip (sysfs_get_string (device->priv->native_path, "md/level"));
                if (raid_level == NULL) {
                        g_warning ("Didn't find md/level for %s. Ignoring.", device->priv->native_path);
                        goto out;
                }
                device->priv->info.linux_md_level = raid_level;
                device->priv->info.linux_md_num_raid_devices =
                        sysfs_get_int (device->priv->native_path, "md/raid_disks");
                device->priv->info.linux_md_version = g_strstrip (sysfs_get_string (device->priv->native_path,
                                                                                            "md/metadata_version"));
                /* Verify that all slaves are there */
                for (n = 0; n < (int) device->priv->info.slaves_objpath->len; n++) {
                        DevkitDisksDevice *slave;
                        char *component_state;
                        char *component_slot_str;
                        char **states;

                        slave = devkit_disks_daemon_local_find_by_object_path (
                                device->priv->daemon,
                                device->priv->info.slaves_objpath->pdata[n]);

                        if (slave == NULL) {
                                g_warning ("Unreferenced slave %s for array %s. Ignoring.",
                                           (const char *) device->priv->info.slaves_objpath->pdata[n],
                                           device->priv->info.device_file);
                                goto out;
                        }

                        g_ptr_array_add (device->priv->info.linux_md_slaves,
                                         g_strdup (device->priv->info.slaves_objpath->pdata[n]));

                        /* compute state of slave */
                        s = g_path_get_basename (slave->priv->native_path);
                        p = g_strdup_printf ("md/dev-%s/state", s);
                        component_state = g_strstrip (sysfs_get_string (device->priv->native_path, p));
                        g_free (p);
                        p = g_strdup_printf ("md/dev-%s/slot", s);
                        component_slot_str = g_strstrip (sysfs_get_string (device->priv->native_path, p));
                        g_free (p);
                        g_free (s);

                        /* state can be separated by commas (see Documentation/md.txt) */
                        states = g_strsplit (component_state, ",", 0);
                        if (strv_has_str (states, "in_sync")) {
                                g_ptr_array_add (device->priv->info.linux_md_slaves_state,
                                                 g_strdup ("in_sync"));
                        } else if (strv_has_str (states, "spare")) {
                                if (component_slot_str != NULL && strcmp (component_slot_str, "none") == 0) {
                                        g_ptr_array_add (device->priv->info.linux_md_slaves_state,
                                                         g_strdup ("spare"));
                                } else {
                                        g_ptr_array_add (device->priv->info.linux_md_slaves_state,
                                                         g_strdup ("sync_in_progress"));
                                }
                        } else {
                                g_ptr_array_add (device->priv->info.linux_md_slaves_state,
                                                 g_strdup ("unknown"));
                                g_warning ("cannot parse '%s' with slot '%s'",
                                           component_state, component_slot_str);
                        }

                        g_strfreev (states);
                        g_free (component_state);
                        g_free (component_slot_str);
                }

                device->priv->info.drive_vendor = g_strdup ("Linux");
                device->priv->info.drive_model = g_strdup_printf ("Software RAID %s",
                                                                  device->priv->info.linux_md_level);
                device->priv->info.drive_revision = g_strdup (device->priv->info.linux_md_version);
                device->priv->info.drive_connection_interface = g_strdup ("virtual");

                /* RAID-0 can never resync or run degraded */
                if (strcmp (device->priv->info.linux_md_level, "raid0") == 0 ||
                    strcmp (device->priv->info.linux_md_level, "linear") == 0) {
                        device->priv->info.linux_md_sync_action = g_strdup ("idle");
                } else {
                        if (!sysfs_file_contains (device->priv->native_path, "md/degraded", "0")) {
                                device->priv->info.linux_md_is_degraded = TRUE;
                        }

                        device->priv->info.linux_md_sync_action = g_strstrip (
                                sysfs_get_string (device->priv->native_path, "md/sync_action"));

                        /* if not idle; update percentage and speed */
                        if (device->priv->info.linux_md_sync_action != NULL &&
                            strcmp (device->priv->info.linux_md_sync_action, "idle") != 0) {
                                char *s;
                                guint64 done;
                                guint64 remaining;

                                s = g_strstrip (sysfs_get_string (device->priv->native_path, "md/sync_completed"));
                                if (sscanf (s, "%" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT "", &done, &remaining) == 2) {
                                        device->priv->info.linux_md_sync_percentage =
                                                100.0 * ((double) done) / ((double) remaining);
                                } else {
                                        g_warning ("cannot parse md/sync_completed: '%s'", s);
                                }
                                g_free (s);

                                device->priv->info.linux_md_sync_speed =
                                        1000L * sysfs_get_uint64 (device->priv->native_path, "md/sync_speed");

                                /* Since the kernel doesn't emit uevents while the job is pending, set up
                                 * a timeout for every 2.5 secs to synthesize the change event so we can
                                 * refresh all properties.
                                 */
                                if (device->priv->linux_md_poll_timeout_id == 0) {
                                        device->priv->linux_md_poll_timeout_id =
                                                g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                                                                            2,
                                                                            poll_syncing_md_device,
                                                                            g_object_ref (device),
                                                                            g_object_unref);
                                }
                        }
                }

        }

        /* Update whether device is mounted */
        l = g_list_prepend (NULL, device);
        devkit_disks_daemon_local_update_mount_state (device->priv->daemon, l, FALSE);
        g_list_free (l);

        if (device->priv->info.device_is_drive)
                update_drive_properties (device);

        /* Update whether device is S.M.A.R.T. capable
         *
         * TODO: need to check that it's hard disk and not e.g. an optical drive
         *
         * TODO: need to honor a quirk for certain USB drives being smart capable, cf.
         *
         *         Thanks to contributor Matthieu Castet, smartctl has
         *         a new option '-d usbcypress'. So you can address
         *         USB devices with cypress chips. The chipset
         *         contains an ATACB proprietary pass through for ATA
         *         commands passed through SCSI commands. Get current
         *         version from CVS.
         *
         *       from http://smartmontools.sourceforge.net/
         */
        if (device->priv->info.drive_connection_interface != NULL) {
                if (g_str_has_prefix (device->priv->info.drive_connection_interface, "ata") ||
                    g_str_has_prefix (device->priv->info.drive_connection_interface, "scsi")) {
                        device->priv->drive_smart_is_capable = TRUE;
                }
        }

        ret = TRUE;

out:
        return ret;
}

gboolean
devkit_disks_device_local_is_busy (DevkitDisksDevice *device)
{
        gboolean ret;

        ret = TRUE;

        /* busy if a job is pending */
        if (device->priv->job != NULL)
                goto out;

        /* or if we're mounted */
        if (device->priv->info.device_is_mounted)
                goto out;

        /* or if another block device is using/holding us (e.g. if holders/ is non-empty in sysfs) */
        if (device->priv->info.holders_objpath->len > 0)
                goto out;

        ret = FALSE;

out:
        return ret;
}

void
devkit_disks_device_removed (DevkitDisksDevice *device)
{
        update_slaves (device);

        /* If the device is busy, we possibly need to clean up if the
         * device itself is busy. This includes
         *
         *  - force unmounting the device and/or all it's partitions
         *  - tearing down a crypto mapping if it's a cleartext device
         *    backed by a crypted device
         *  - removing the device from a RAID array in case of Linux MD.
         *
         * but see force_removal() for details.
         *
         * This is the normally the path where the enclosing device is
         * removed. Compare with devkit_disks_device_changed() for the
         * other path.
         */
        force_removal (device, NULL, NULL);
}

DevkitDisksDevice *
devkit_disks_device_new (DevkitDisksDaemon *daemon, DevkitDevice *d)
{
        DevkitDisksDevice *device;
        const char *native_path;

        device = NULL;
        native_path = devkit_device_get_native_path (d);

        /* ignore ram and loop devices */
        if (g_str_has_prefix (native_path, "/sys/devices/virtual/block/ram") ||
            g_str_has_prefix (native_path, "/sys/devices/virtual/block/loop"))
                goto out;

        device = DEVKIT_DISKS_DEVICE (g_object_new (DEVKIT_TYPE_DISKS_DEVICE, NULL));
        device->priv->d = g_object_ref (d);
        device->priv->daemon = g_object_ref (daemon);
        device->priv->native_path = g_strdup (native_path);
        if (!update_info (device)) {
                g_object_unref (device);
                device = NULL;
                goto out;
        }

        if (! register_disks_device (DEVKIT_DISKS_DEVICE (device))) {
                g_object_unref (device);
                device = NULL;
                goto out;
        }

        if (device->priv->info.device_is_linux_md) {
                /* Since we've just been added it means that the superblocks on the components
                 * have been updated. We want to reread this new data.
                 */
                linux_md_emit_changed_on_components (device);
        }

out:
        return device;
}

static void
emit_job_changed (DevkitDisksDevice *device)
{
        g_print ("emitting job-changed on %s\n", device->priv->native_path);
        g_signal_emit_by_name (device->priv->daemon,
                               "device-job-changed",
                               device->priv->object_path,
                               device->priv->job_in_progress,
                               device->priv->job_id,
                               device->priv->job_is_cancellable,
                               device->priv->job_num_tasks,
                               device->priv->job_cur_task,
                               device->priv->job_cur_task_id,
                               device->priv->job_cur_task_percentage,
                               NULL);
        g_signal_emit (device, signals[JOB_CHANGED_SIGNAL], 0,
                       device->priv->job_in_progress,
                       device->priv->job_id,
                       device->priv->job_is_cancellable,
                       device->priv->job_num_tasks,
                       device->priv->job_cur_task,
                       device->priv->job_cur_task_id,
                       device->priv->job_cur_task_percentage);
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

gboolean
devkit_disks_device_changed (DevkitDisksDevice *device, DevkitDevice *d, gboolean synthesized)
{
        gboolean keep_device;

        g_object_unref (device->priv->d);
        device->priv->d = g_object_ref (d);

        keep_device = update_info (device);

        /* if we're a linux md device.. then a change event might mean some metadata on the
         * components changed. So trigger a change on each slave
         */
        if (!synthesized && device->priv->info.device_is_linux_md) {
                linux_md_emit_changed_on_components (device);
        }

        /* this 'change' event might prompt us to remove the device */
        if (!keep_device)
                goto out;

        /* no, it's good .. keep it */
        emit_changed (device);

        /* Check if media was removed. If so, we possibly need to clean up
         * if the device itself is busy. This includes
         *
         *  - force unmounting the device
         *  - tearing down a crypto mapping if it's a cleartext device
         *    backed by a crypted device
         *  - removing the device from a RAID array in case of Linux MD.
         *
         * but see force_removal() for details.
         *
         * This is the normally the path where the media is removed but the enclosing
         * device is still present. Compare with devkit_disks_device_removed() for
         * the other path.
         */
        if (!device->priv->info.device_is_media_available) {
                GList *l;
                GList *devices;

                force_removal (device, NULL, NULL);

                /* check all partitions */
                devices = devkit_disks_daemon_local_get_all_devices (device->priv->daemon);
                for (l = devices; l != NULL; l = l->next) {
                        DevkitDisksDevice *d = DEVKIT_DISKS_DEVICE (l->data);

                        if (d->priv->info.device_is_partition &&
                            d->priv->info.partition_slave != NULL &&
                            strcmp (d->priv->info.partition_slave, device->priv->object_path) == 0) {

                                force_removal (d, NULL, NULL);
                        }
                }
        }
out:
        return keep_device;
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
devkit_disks_device_local_set_mounted (DevkitDisksDevice *device,
                                       const char        *mount_path,
                                       gboolean           emit_changed_signal)
{
        g_free (device->priv->info.device_mount_path);
        device->priv->info.device_mount_path = g_strdup (mount_path);
        device->priv->info.device_is_mounted = TRUE;
        if (emit_changed_signal)
                emit_changed (device);
}

void
devkit_disks_device_local_set_unmounted (DevkitDisksDevice *device,
                                         gboolean           emit_changed_signal)
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

        if (emit_changed_signal)
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

        if (context == NULL)
                return TRUE;

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
                                  gboolean was_cancelled,
                                  int status,
                                  const char *stderr,
                                  const char *stdout,
                                  gpointer user_data);

struct Job {
        char *job_id;

        DevkitDisksDevice *device;
        PolKitCaller *pk_caller;
        DBusGMethodInvocation *context;
        JobCompletedFunc job_completed_func;
        GPid pid;
        gpointer user_data;
        GDestroyNotify user_data_destroy_func;
        gboolean was_cancelled;

        int stderr_fd;
        GIOChannel *error_channel;
        guint error_channel_source_id;
        GString *error_string;

        int stdout_fd;
        GIOChannel *out_channel;
        guint out_channel_source_id;
        GString *stdout_string;
        int stdout_string_cursor;

        char *stdin_str;
        char *stdin_cursor;
        int stdin_fd;
        GIOChannel *in_channel;
        guint in_channel_source_id;
};

static void
job_free (Job *job)
{
        if (job->user_data_destroy_func != NULL)
                job->user_data_destroy_func (job->user_data);
        if (job->device != NULL)
                g_object_unref (job->device);
        if (job->pk_caller != NULL)
                polkit_caller_unref (job->pk_caller);
        if (job->stderr_fd >= 0)
                close (job->stderr_fd);
        if (job->stdout_fd >= 0)
                close (job->stdout_fd);
        if (job->stdin_fd >= 0) {
                close (job->stdin_fd);
                g_source_remove (job->in_channel_source_id);
                g_io_channel_unref (job->in_channel);
        }
        g_source_remove (job->error_channel_source_id);
        g_source_remove (job->out_channel_source_id);
        g_io_channel_unref (job->error_channel);
        g_io_channel_unref (job->out_channel);
        g_string_free (job->error_string, TRUE);
        /* scrub stdin (may contain secrets) */
        if (job->stdin_str != NULL) {
                memset (job->stdin_str, '\0', strlen (job->stdin_str));
        }
        g_string_free (job->stdout_string, TRUE);
        g_free (job->stdin_str);
        g_free (job->job_id);
        g_free (job);
}

static void
job_child_watch_cb (GPid pid, int status, gpointer user_data)
{
        char *buf;
        gsize buf_size;
        Job *job = user_data;

        if (g_io_channel_read_to_end (job->error_channel, &buf, &buf_size, NULL) == G_IO_STATUS_NORMAL) {
                g_string_append_len (job->error_string, buf, buf_size);
                g_free (buf);
        }
        if (g_io_channel_read_to_end (job->out_channel, &buf, &buf_size, NULL) == G_IO_STATUS_NORMAL) {
                g_string_append_len (job->stdout_string, buf, buf_size);
                g_free (buf);
        }

        g_print ("helper(pid %5d): completed with exit code %d\n", job->pid, WEXITSTATUS (status));

        if (job->device != NULL && job->job_id != NULL) {
                job->device->priv->job_in_progress = FALSE;
                g_free (job->device->priv->job_id);
                job->device->priv->job_id = NULL;
                job->device->priv->job_is_cancellable = FALSE;
                job->device->priv->job_num_tasks = 0;
                job->device->priv->job_cur_task = 0;
                g_free (job->device->priv->job_cur_task_id);
                job->device->priv->job_cur_task_id = NULL;
                job->device->priv->job_cur_task_percentage = -1.0;

                job->device->priv->job = NULL;
        }

        job->job_completed_func (job->context,
                                 job->device,
                                 job->pk_caller,
                                 job->was_cancelled,
                                 status,
                                 job->error_string->str,
                                 job->stdout_string->str,
                                 job->user_data);

        if (job->device != NULL && job->job_id != NULL) {
                emit_job_changed (job->device);
        }
        job_free (job);
}

static void
job_cancel (DevkitDisksDevice *device)
{
        g_return_if_fail (device->priv->job != NULL);

        device->priv->job->was_cancelled = TRUE;

        /* TODO: maybe wait and user a bigger hammer? (SIGKILL) */
        kill (device->priv->job->pid, SIGTERM);
}

static gboolean
job_read_error (GIOChannel *channel,
                GIOCondition condition,
                gpointer user_data)
{
        char buf[1024];
        gsize bytes_read;
        Job *job = user_data;

        g_io_channel_read_chars (channel, buf, sizeof buf, &bytes_read, NULL);
        g_string_append_len (job->error_string, buf, bytes_read);
        return TRUE;
}

static gboolean
job_write_in (GIOChannel *channel,
              GIOCondition condition,
              gpointer user_data)
{
        Job *job = user_data;
        gsize bytes_written;

        if (job->stdin_cursor == NULL || job->stdin_cursor[0] == '\0') {
                /* nothing left to write; remove ourselves */
                return FALSE;
        }

        g_io_channel_write_chars (channel, job->stdin_cursor, strlen (job->stdin_cursor),
                                  &bytes_written, NULL);
        g_io_channel_flush (channel, NULL);
        job->stdin_cursor += bytes_written;
        return TRUE;
}

static gboolean
job_read_out (GIOChannel *channel,
              GIOCondition condition,
              gpointer user_data)
{
        char *s;
        char *line;
        char buf[1024];
        gsize bytes_read;
        Job *job = user_data;

        g_io_channel_read_chars (channel, buf, sizeof buf, &bytes_read, NULL);
        g_string_append_len (job->stdout_string, buf, bytes_read);

        do {
                gsize line_len;

                s = strstr (job->stdout_string->str + job->stdout_string_cursor, "\n");
                if (s == NULL)
                        break;

                line_len = s - (job->stdout_string->str + job->stdout_string_cursor);
                line = g_strndup (job->stdout_string->str + job->stdout_string_cursor, line_len);
                job->stdout_string_cursor += line_len + 1;

                //g_print ("helper(pid %5d): '%s'\n", job->pid, line);

                if (strlen (line) < 256) {
                        int cur_task;
                        int num_tasks;
                        double cur_task_percentage;;
                        char cur_task_id[256];

                        if (sscanf (line, "progress: %d %d %lg %s",
                                    &cur_task,
                                    &num_tasks,
                                    &cur_task_percentage,
                                    (char *) &cur_task_id) == 4) {
                                if (job->device != NULL && job->job_id != NULL) {
                                        job->device->priv->job_num_tasks = num_tasks;
                                        job->device->priv->job_cur_task = cur_task;
                                        g_free (job->device->priv->job_cur_task_id);
                                        job->device->priv->job_cur_task_id = g_strdup (cur_task_id);
                                        job->device->priv->job_cur_task_percentage = cur_task_percentage;
                                        emit_job_changed (job->device);
                                }
                        }
                }

                g_free (line);
        } while (TRUE);

        return TRUE;
}

static gboolean
job_new (DBusGMethodInvocation *context,
         const char            *job_id,
         gboolean               is_cancellable,
         DevkitDisksDevice     *device,
         PolKitCaller          *pk_caller,
         char                 **argv,
         const char            *stdin_str,
         JobCompletedFunc       job_completed_func,
         gpointer               user_data,
         GDestroyNotify         user_data_destroy_func)
{
        Job *job;
        gboolean ret;
        GError *error;

        ret = FALSE;
        job = NULL;

        if (device != NULL) {
                if (device->priv->job != NULL) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_ALREADY_IN_PROGRESS,
                                     "There is already a job running");
                        goto out;
                }
        }

        job = g_new0 (Job, 1);
        job->context = context;
        job->device = device != NULL ? DEVKIT_DISKS_DEVICE (g_object_ref (device)) : NULL;
        job->pk_caller = pk_caller != NULL ? polkit_caller_ref (pk_caller) : NULL;
        job->job_completed_func = job_completed_func;
        job->user_data = user_data;
        job->user_data_destroy_func = user_data_destroy_func;
        job->stderr_fd = -1;
        job->stdout_fd = -1;
        job->stdin_fd = -1;
        job->stdin_str = g_strdup (stdin_str);
        job->stdin_cursor = job->stdin_str;
        job->stdout_string = g_string_sized_new (1024);
        job->job_id = g_strdup (job_id);

        if (device != NULL && job_id != NULL) {
                g_free (job->device->priv->job_id);
                job->device->priv->job_id = g_strdup (job_id);
        }

        error = NULL;
        if (!g_spawn_async_with_pipes (NULL,
                                       argv,
                                       NULL,
                                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                       NULL,
                                       NULL,
                                       &(job->pid),
                                       stdin_str != NULL ? &(job->stdin_fd) : NULL,
                                       &(job->stdout_fd),
                                       &(job->stderr_fd),
                                       &error)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_GENERAL, "Error starting job: %s", error->message);
                g_error_free (error);
                goto out;
        }

        g_child_watch_add (job->pid, job_child_watch_cb, job);

        job->error_string = g_string_new ("");
        job->error_channel = g_io_channel_unix_new (job->stderr_fd);
        error = NULL;
        if (g_io_channel_set_flags (job->error_channel, G_IO_FLAG_NONBLOCK, &error) != G_IO_STATUS_NORMAL) {
                g_warning ("Cannon set stderr fd for child to be non blocking: %s", error->message);
                g_error_free (error);
        }
        job->error_channel_source_id = g_io_add_watch (job->error_channel, G_IO_IN, job_read_error, job);

        job->out_channel = g_io_channel_unix_new (job->stdout_fd);
        error = NULL;
        if (g_io_channel_set_flags (job->out_channel, G_IO_FLAG_NONBLOCK, &error) != G_IO_STATUS_NORMAL) {
                g_warning ("Cannon set stdout fd for child to be non blocking: %s", error->message);
                g_error_free (error);
        }
        job->out_channel_source_id = g_io_add_watch (job->out_channel, G_IO_IN, job_read_out, job);

        if (job->stdin_fd >= 0) {
                job->in_channel = g_io_channel_unix_new (job->stdin_fd);
                if (g_io_channel_set_flags (job->in_channel, G_IO_FLAG_NONBLOCK, &error) != G_IO_STATUS_NORMAL) {
                        g_warning ("Cannon set stdin fd for child to be non blocking: %s", error->message);
                        g_error_free (error);
                }
                job->in_channel_source_id = g_io_add_watch (job->in_channel, G_IO_OUT, job_write_in, job);
        }

        ret = TRUE;

        if (device != NULL && job_id != NULL) {
                device->priv->job_in_progress = TRUE;
                device->priv->job_is_cancellable = is_cancellable;
                device->priv->job_num_tasks = 0;
                device->priv->job_cur_task = 0;
                g_free (device->priv->job_cur_task_id);
                device->priv->job_cur_task_id = NULL;
                device->priv->job_cur_task_percentage = -1.0;

                device->priv->job = job;

                emit_job_changed (device);
        }

        if (device != NULL) {
                g_print ("helper(pid %5d): launched job %s on %s\n", job->pid, argv[0], device->priv->info.device_file);
        } else {
                g_print ("helper(pid %5d): launched job %s on daemon\n", job->pid, argv[0]);
        }

out:
        if (!ret && job != NULL)
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
filesystem_mount_data_new (const char *mount_point, gboolean remove_dir_on_unmount, gboolean is_remount)
{
        MountData *data;
        data = g_new0 (MountData, 1);
        data->mount_point = g_strdup (mount_point);
        data->remove_dir_on_unmount = remove_dir_on_unmount;
        data->is_remount = is_remount;
        return data;
}

static void
filesystem_mount_data_free (MountData *data)
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
filesystem_mount_completed_cb (DBusGMethodInvocation *context,
                               DevkitDisksDevice *device,
                               PolKitCaller *pk_caller,
                               gboolean job_was_cancelled,
                               int status,
                               const char *stderr,
                               const char *stdout,
                               gpointer user_data)
{
        MountData *data = (MountData *) user_data;
        uid_t uid;

        uid = 0;
        if (pk_caller != NULL)
                polkit_caller_get_uid (pk_caller, &uid);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                if (!data->is_remount) {
                        devkit_disks_device_local_set_mounted (device, data->mount_point, TRUE);
                        mounts_file_add (device, uid, data->remove_dir_on_unmount);
                }
                dbus_g_method_return (context, data->mount_point);
        } else {
                if (!data->is_remount) {
                        if (g_rmdir (data->mount_point) != 0) {
                                g_warning ("Error removing dir in late mount error path: %m");
                        }
                }

                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error mounting: mount exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
        }
}


gboolean
devkit_disks_device_filesystem_mount (DevkitDisksDevice     *device,
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

        /* always prepend some reasonable default mount options; these are
         * chosen here; the user can override them if he wants to
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
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_MOUNTED,
                                     "Device is already mounted");
                        goto out;
                }
        }

        if (devkit_disks_device_local_is_busy (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Device is busy");
                goto out;
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
                      "FilesystemMount",
                      FALSE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      filesystem_mount_completed_cb,
                      filesystem_mount_data_new (mount_point, remove_dir_on_unmount, is_remount),
                      (GDestroyNotify) filesystem_mount_data_free)) {
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
filesystem_unmount_completed_cb (DBusGMethodInvocation *context,
                                 DevkitDisksDevice *device,
                                 PolKitCaller *pk_caller,
                                 gboolean job_was_cancelled,
                                 int status,
                                 const char *stderr,
                                 const char *stdout,
                                 gpointer user_data)
{
        char *mount_path = user_data;

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                devkit_disks_device_local_set_unmounted (device, TRUE);
                mounts_file_remove (device, mount_path);
                dbus_g_method_return (context);
        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
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
}

gboolean
devkit_disks_device_filesystem_unmount (DevkitDisksDevice     *device,
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
                                     "Unknown option %s", option);
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
                      "FilesystemUnmount",
                      FALSE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      filesystem_unmount_completed_cb,
                      g_strdup (device->priv->info.device_mount_path),
                      g_free)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
erase_completed_cb (DBusGMethodInvocation *context,
                    DevkitDisksDevice *device,
                    PolKitCaller *pk_caller,
                    gboolean job_was_cancelled,
                    int status,
                    const char *stderr,
                    const char *stdout,
                    gpointer user_data)
{
        /* either way, poke the kernel so we can reread the data */
        devkit_device_emit_changed_to_kernel (device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                dbus_g_method_return (context);
        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error erasing: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status),
                                     stderr);
                }
        }
}

gboolean
devkit_disks_device_erase (DevkitDisksDevice     *device,
                           char                 **options,
                           DBusGMethodInvocation *context)
{
        int n, m;
        char *argv[128];
        GError *error;
        PolKitCaller *pk_caller;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (devkit_disks_device_local_is_busy (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Device is busy");
                goto out;
        }

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit authorization */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context))
                goto out;

        n = 0;
        argv[n++] = PACKAGE_LIBEXEC_DIR "/devkit-disks-helper-erase";
        argv[n++] = device->priv->info.device_file;
        for (m = 0; options[m] != NULL; m++) {
                if (n >= (int) sizeof (argv) - 1) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Too many options");
                        goto out;
                }
                /* the helper will validate each option */
                argv[n++] = (char *) options[m];
        }
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "Erase",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      erase_completed_cb,
                      NULL,
                      NULL)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
partition_delete_completed_cb (DBusGMethodInvocation *context,
                               DevkitDisksDevice *device,
                               PolKitCaller *pk_caller,
                               gboolean job_was_cancelled,
                               int status,
                               const char *stderr,
                               const char *stdout,
                               gpointer user_data)
{
        DevkitDisksDevice *enclosing_device = DEVKIT_DISKS_DEVICE (user_data);

        /* either way, poke the kernel about the enclosing disk so we can reread the partitioning table */
        devkit_device_emit_changed_to_kernel (enclosing_device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                dbus_g_method_return (context);
        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error erasing: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status),
                                     stderr);
                }
        }
}

gboolean
devkit_disks_device_partition_delete (DevkitDisksDevice     *device,
                                      char                 **options,
                                      DBusGMethodInvocation *context)
{
        int n;
        int m;
        char *argv[16];
        GError *error;
        char *offset_as_string;
        char *size_as_string;
        char *part_number_as_string;
        PolKitCaller *pk_caller;
        DevkitDisksDevice *enclosing_device;

        offset_as_string = NULL;
        size_as_string = NULL;
        part_number_as_string = NULL;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (devkit_disks_device_local_is_busy (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Device is busy");
                goto out;
        }

        if (!device->priv->info.device_is_partition) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_NOT_PARTITION,
                             "Device is not a partition");
                goto out;
        }

        enclosing_device = devkit_disks_daemon_local_find_by_object_path (device->priv->daemon,
                                                                          device->priv->info.partition_slave);
        if (enclosing_device == NULL) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                             "Cannot find enclosing device");
                goto out;
        }

        if (devkit_disks_device_local_is_busy (enclosing_device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Enclosing device is busy");
                goto out;
        }

#if 0
        /* see rant in devkit_disks_device_partition_create() */
        if (devkit_disks_device_local_partitions_are_busy (enclosing_device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "A sibling partition is busy (TODO: addpart/delpart/partx to the rescue!)");
                goto out;
        }
#endif

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit authorization */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context))
                goto out;

        offset_as_string = g_strdup_printf ("%" G_GINT64_FORMAT "", device->priv->info.partition_offset);
        size_as_string = g_strdup_printf ("%" G_GINT64_FORMAT "", device->priv->info.partition_size);
        part_number_as_string = g_strdup_printf ("%d", device->priv->info.partition_number);

        n = 0;
        argv[n++] = PACKAGE_LIBEXEC_DIR "/devkit-disks-helper-delete-partition";
        argv[n++] = enclosing_device->priv->info.device_file;
        argv[n++] = device->priv->info.device_file;
        argv[n++] = offset_as_string;
        argv[n++] = size_as_string;
        argv[n++] = part_number_as_string;
        for (m = 0; options[m] != NULL; m++) {
                if (n >= (int) sizeof (argv) - 1) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Too many options");
                        goto out;
                }
                /* the helper will validate each option */
                argv[n++] = (char *) options[m];
        }
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "PartitionDelete",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      partition_delete_completed_cb,
                      g_object_ref (enclosing_device),
                      g_object_unref)) {
                goto out;
        }

out:
        g_free (offset_as_string);
        g_free (size_as_string);
        g_free (part_number_as_string);
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct {
        FilesystemCreateHookFunc hook_func;
        gpointer                 hook_user_data;
} MkfsData;

static void
mkfs_data_unref (MkfsData *data)
{
        g_free (data);
}

static void
filesystem_create_completed_cb (DBusGMethodInvocation *context,
                                DevkitDisksDevice *device,
                                PolKitCaller *pk_caller,
                                gboolean job_was_cancelled,
                                int status,
                                const char *stderr,
                                const char *stdout,
                                gpointer user_data)
{
        MkfsData *data = user_data;

        /* either way, poke the kernel so we can reread the data */
        devkit_device_emit_changed_to_kernel (device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                if (data->hook_func != NULL)
                        data->hook_func (context, device, TRUE, data->hook_user_data);
                else
                        dbus_g_method_return (context);
        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error creating file system: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status),
                                     stderr);
                }

                if (data->hook_func != NULL)
                        data->hook_func (context, device, FALSE, data->hook_user_data);
        }
}

typedef struct {
        int refcount;

        DBusGMethodInvocation *context;
        DevkitDisksDevice *device;

        char *passphrase;

        char **options;
        char *fstype;

        FilesystemCreateHookFunc mkfs_hook_func;
        gpointer                 mkfs_hook_user_data;

        guint device_changed_signal_handler_id;
        guint device_changed_timeout_id;
} MkfsEncryptedData;

static MkfsEncryptedData *
mkfse_data_ref (MkfsEncryptedData *data)
{
        data->refcount++;
        return data;
}

static void
mkfse_data_unref (MkfsEncryptedData *data)
{
        data->refcount--;
        if (data->refcount == 0) {
                if (data->passphrase != NULL) {
                        memset (data->passphrase, '\0', strlen (data->passphrase));
                        g_free (data->passphrase);
                }
                if (data->device != NULL)
                        g_object_unref (data->device);
                g_strfreev (data->options);
                g_free (data->fstype);
                g_free (data);
        }
}

static void
filesystem_create_wait_for_cleartext_device_hook (DBusGMethodInvocation *context,
                                                  DevkitDisksDevice *device,
                                                  gpointer user_data)
{
        MkfsEncryptedData *data = user_data;

        if (device == NULL) {
                /* Dang, unlocking failed. The unlock method have already thrown an exception for us. */
        } else {
                /* We're unlocked.. awesome.. Now we can _finally_ create the file system.
                 * What a ride. We're returning to exactly to where we came from. Back to
                 * the source. Only the device is different.
                 */

                devkit_disks_device_filesystem_create_internal (device,
                                                                data->fstype,
                                                                data->options,
                                                                data->mkfs_hook_func,
                                                                data->mkfs_hook_user_data,
                                                                data->context);
                mkfse_data_unref (data);
        }
}

static void
filesystem_create_wait_for_encrypted_device_changed_cb (DevkitDisksDaemon *daemon,
                                                        const char *object_path,
                                                        gpointer user_data)
{
        MkfsEncryptedData *data = user_data;
        DevkitDisksDevice *device;

        /* check if we're now a LUKS crypto device */
        device = devkit_disks_daemon_local_find_by_object_path (daemon, object_path);
        if (device == data->device &&
            (device->priv->info.id_usage != NULL && strcmp (device->priv->info.id_usage, "crypto") == 0) &&
            (device->priv->info.id_type != NULL && strcmp (device->priv->info.id_type, "crypto_LUKS") == 0)) {

                /* yay! we are now set up the corresponding cleartext device */

                devkit_disks_device_encrypted_unlock_internal (data->device,
                                                               data->passphrase,
                                                               NULL,
                                                               filesystem_create_wait_for_cleartext_device_hook,
                                                               data,
                                                               data->context);

                g_signal_handler_disconnect (daemon, data->device_changed_signal_handler_id);
                g_source_remove (data->device_changed_timeout_id);
        }
}

static gboolean
filesystem_create_wait_for_encrypted_device_not_seen_cb (gpointer user_data)
{
        MkfsEncryptedData *data = user_data;

        throw_error (data->context,
                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                     "Error creating encrypted file system: timeout (10s) waiting for encrypted device to show up");

        g_signal_handler_disconnect (data->device->priv->daemon, data->device_changed_signal_handler_id);
        mkfse_data_unref (data);

        return FALSE;
}



static void
filesystem_create_create_encrypted_device_completed_cb (DBusGMethodInvocation *context,
                                                        DevkitDisksDevice *device,
                                                        PolKitCaller *pk_caller,
                                                        gboolean job_was_cancelled,
                                                        int status,
                                                        const char *stderr,
                                                        const char *stdout,
                                                        gpointer user_data)
{
        MkfsEncryptedData *data = user_data;

        /* either way, poke the kernel so we can reread the data (new uuid etc.) */
        devkit_device_emit_changed_to_kernel (device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                /* OK! So we've got ourselves an encrypted device. Let's set it up so we can create a file
                 * system. Sit and wait for the change event to appear so we can setup with the right UUID.
                 */

                data->device_changed_signal_handler_id = g_signal_connect_after (
                        device->priv->daemon,
                        "device-changed",
                        (GCallback) filesystem_create_wait_for_encrypted_device_changed_cb,
                        mkfse_data_ref (data));

                /* set up timeout for error reporting if waiting failed
                 *
                 * (the signal handler and the timeout handler share the ref to data
                 * as one will cancel the other)
                 */
                data->device_changed_timeout_id = g_timeout_add (
                        10 * 1000,
                        filesystem_create_wait_for_encrypted_device_not_seen_cb,
                        data);


        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error creating file system: cryptsetup exited with exit code %d: %s",
                                     WEXITSTATUS (status),
                                     stderr);
                }
        }
}

static gboolean
devkit_disks_device_filesystem_create_internal (DevkitDisksDevice       *device,
                                                const char              *fstype,
                                                char                   **options,
                                                FilesystemCreateHookFunc hook_func,
                                                gpointer                 hook_user_data,
                                                DBusGMethodInvocation *context)
{
        int n, m;
        char *argv[128];
        GError *error;
        PolKitCaller *pk_caller;
        char *s;
        char *options_for_stdin;
        char *passphrase_stdin;
        MkfsData *mkfs_data;

        options_for_stdin = NULL;
        passphrase_stdin = NULL;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (devkit_disks_device_local_is_busy (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Device is busy");
                goto out;
        }

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit authorization */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context))
                goto out;

        if (strlen (fstype) == 0) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                             "fstype not specified");
                goto out;
        }

        /* search for encrypt=<passphrase> and do a detour if that's specified */
        for (n = 0; options[n] != NULL; n++) {
                if (g_str_has_prefix (options[n], "encrypt=")) {
                        MkfsEncryptedData *mkfse_data;

                        /* So this is a request to create an encrypted device to put the
                         * file system on; save all options for mkfs (except encrypt=) for
                         * later invocation once we have a cleartext device.
                         */

                        mkfse_data = g_new0 (MkfsEncryptedData, 1);
                        mkfse_data->refcount = 1;
                        mkfse_data->context = context;
                        mkfse_data->device = g_object_ref (device);
                        mkfse_data->passphrase = g_strdup (options[n] + sizeof ("encrypt=") - 1);
                        mkfse_data->mkfs_hook_func = hook_func;
                        mkfse_data->mkfs_hook_user_data = hook_user_data;
                        mkfse_data->fstype = g_strdup (fstype);
                        mkfse_data->options = g_strdupv (options);
                        g_free (mkfse_data->options[n]);
                        for (m = n; mkfse_data->options[m] != NULL; m++) {
                                mkfse_data->options[m] = mkfse_data->options[m + 1];
                        }

                        passphrase_stdin = g_strdup_printf ("%s\n", mkfse_data->passphrase);

                        n = 0;
                        argv[n++] = "cryptsetup";
                        argv[n++] = "-q";
                        argv[n++] = "luksFormat";
                        argv[n++] = device->priv->info.device_file;
                        argv[n++] = NULL;

                        error = NULL;
                        if (!job_new (context,
                                      "CreateEncryptedDevice",
                                      TRUE,
                                      device,
                                      pk_caller,
                                      argv,
                                      passphrase_stdin,
                                      filesystem_create_create_encrypted_device_completed_cb,
                                      mkfse_data,
                                      (GDestroyNotify) mkfse_data_unref)) {
                                goto out;
                        }

                        goto out;
                }
        }

        mkfs_data = g_new (MkfsData, 1);
        mkfs_data->hook_func = hook_func;
        mkfs_data->hook_user_data = hook_user_data;

        /* pass options on stdin as it may contain secrets */
        s = g_strjoinv ("\n", options);
        options_for_stdin = g_strconcat (s, "\n\n", NULL);
        g_free (s);

        n = 0;
        argv[n++] = PACKAGE_LIBEXEC_DIR "/devkit-disks-helper-mkfs";
        argv[n++] = (char *) fstype;
        argv[n++] = device->priv->info.device_file;
        argv[n++] = device->priv->info.device_is_partition_table ? "1" : "0";
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "FilesystemCreate",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      options_for_stdin,
                      filesystem_create_completed_cb,
                      mkfs_data,
                      (GDestroyNotify) mkfs_data_unref)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        g_free (options_for_stdin);
        if (passphrase_stdin != NULL) {
                memset (passphrase_stdin, '\0', strlen (passphrase_stdin));
                g_free (passphrase_stdin);
        }
        return TRUE;
}

gboolean
devkit_disks_device_filesystem_create (DevkitDisksDevice     *device,
                                       const char            *fstype,
                                       char                 **options,
                                       DBusGMethodInvocation *context)
{
        return devkit_disks_device_filesystem_create_internal (device, fstype, options, NULL, NULL, context);
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
devkit_disks_device_job_cancel (DevkitDisksDevice     *device,
                                DBusGMethodInvocation *context)
{
        if (!device->priv->job_in_progress) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_NO_JOB_IN_PROGRESS,
                             "There is no job to cancel");
                goto out;
        }

        if (!device->priv->job_is_cancellable) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_JOB_CANNOT_BE_CANCELLED,
                             "Job cannot be cancelled");
                goto out;
        }

        /* TODO: check authorization */

        job_cancel (device);

        /* TODO: wait returning once the job is actually cancelled? */
        dbus_g_method_return (context);

out:
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct {
        int refcount;

        guint device_added_signal_handler_id;
        guint device_added_timeout_id;

        DBusGMethodInvocation *context;
        DevkitDisksDevice *device;
        guint64 offset;
        guint64 size;

        guint64 created_offset;
        guint64 created_size;

        char *fstype;
        char **fsoptions;

} CreatePartitionData;

static CreatePartitionData *
partition_create_data_new (DBusGMethodInvocation *context,
                           DevkitDisksDevice *device,
                           guint64 offset,
                           guint64 size,
                           const char *fstype,
                           char **fsoptions)
{
        CreatePartitionData *data;

        data = g_new0 (CreatePartitionData, 1);
        data->refcount = 1;

        data->context = context;
        data->device = g_object_ref (device);
        data->offset = offset;
        data->size = size;
        data->fstype = g_strdup (fstype);
        data->fsoptions = g_strdupv (fsoptions);

        return data;
}

static CreatePartitionData *
partition_create_data_ref (CreatePartitionData *data)
{
        data->refcount++;
        return data;
}

static void
partition_create_data_unref (CreatePartitionData *data)
{
        data->refcount--;
        if (data->refcount == 0) {
                g_object_unref (data->device);
                g_free (data->fstype);
                g_strfreev (data->fsoptions);
                g_free (data);
        }
}

static void
partition_create_filesystem_create_hook (DBusGMethodInvocation *context,
                                         DevkitDisksDevice *device,
                                         gboolean filesystem_create_succeeded,
                                         gpointer user_data)
{
        if (!filesystem_create_succeeded) {
                /* dang.. FilesystemCreate already reported an error */
        } else {
                /* it worked.. */
                dbus_g_method_return (context, device->priv->object_path);
        }
}

static void
partition_create_device_added_cb (DevkitDisksDaemon *daemon,
                                  const char *object_path,
                                  gpointer user_data)
{
        CreatePartitionData *data = user_data;
        DevkitDisksDevice *device;

        /* check the device added is the partition we've created */
        device = devkit_disks_daemon_local_find_by_object_path (daemon, object_path);
        if (device != NULL &&
            device->priv->info.device_is_partition &&
            strcmp (device->priv->info.partition_slave, data->device->priv->object_path) == 0 &&
            data->created_offset == device->priv->info.partition_offset &&
            data->created_size == device->priv->info.partition_size) {

                /* yay! it is.. now create the file system if requested */
                if (strlen (data->fstype) > 0) {
                        devkit_disks_device_filesystem_create_internal (device,
                                                                        data->fstype,
                                                                        data->fsoptions,
                                                                        partition_create_filesystem_create_hook,
                                                                        NULL,
                                                                        data->context);
                } else {
                        dbus_g_method_return (data->context, device->priv->object_path);
                }

                g_signal_handler_disconnect (daemon, data->device_added_signal_handler_id);
                g_source_remove (data->device_added_timeout_id);
                partition_create_data_unref (data);
        }
}

static gboolean
partition_create_device_not_seen_cb (gpointer user_data)
{
        CreatePartitionData *data = user_data;

        throw_error (data->context,
                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                     "Error creating partition: timeout (10s) waiting for partition to show up");

        g_signal_handler_disconnect (data->device->priv->daemon, data->device_added_signal_handler_id);
        partition_create_data_unref (data);

        return FALSE;
}

static void
partition_create_completed_cb (DBusGMethodInvocation *context,
                               DevkitDisksDevice *device,
                               PolKitCaller *pk_caller,
                               gboolean job_was_cancelled,
                               int status,
                               const char *stderr,
                               const char *stdout,
                               gpointer user_data)
{
        CreatePartitionData *data = user_data;

        /* either way, poke the kernel so we can reread the data */
        devkit_device_emit_changed_to_kernel (device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                int n;
                int m;
                guint64 offset;
                guint64 size;
                char **tokens;

                /* Find the
                 *
                 *   job-create-partition-offset:
                 *   job-create-partition-size:
                 *
                 * lines and parse the new start and end. We need this
                 * for waiting on the created partition (since the requested
                 * start and size passed may not be honored due to disk/cylinder/sector
                 * alignment reasons).
                 */
                offset = 0;
                size = 0;
                m = 0;
                tokens = g_strsplit (stderr, "\n", 0);
                for (n = 0; tokens[n] != NULL; n++) {
                        char *line = tokens[n];
                        char *endp;

                        if (m == 2)
                                break;

                        if (g_str_has_prefix (line, "job-create-partition-offset: ")) {
                                offset = strtoll (line + sizeof ("job-create-partition-offset: ") - 1, &endp, 10);
                                if (*endp == '\0')
                                        m++;
                        } else if (g_str_has_prefix (line, "job-create-partition-size: ")) {
                                size = strtoll (line + sizeof ("job-create-partition-size: ") - 1, &endp, 10);
                                if (*endp == '\0')
                                        m++;
                        }
                }
                g_strfreev (tokens);

                if (m != 2) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error creating partition: internal error, expected to find new "
                                     "start and end but m=%d", m);
                } else {
                        data->created_offset = offset;
                        data->created_size = size;

                        /* sit around and wait for the new partition to appear */
                        data->device_added_signal_handler_id = g_signal_connect_after (
                                device->priv->daemon,
                                "device-added",
                                (GCallback) partition_create_device_added_cb,
                                partition_create_data_ref (data));

                        /* set up timeout for error reporting if waiting failed
                         *
                         * (the signal handler and the timeout handler share the ref to data
                         * as one will cancel the other)
                         */
                        data->device_added_timeout_id = g_timeout_add (10 * 1000,
                                                                       partition_create_device_not_seen_cb,
                                                                       data);
                }
        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error creating partition: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status),
                                     stderr);
                }
        }
}

gboolean
devkit_disks_device_partition_create (DevkitDisksDevice     *device,
                                      guint64                offset,
                                      guint64                size,
                                      const char            *type,
                                      const char            *label,
                                      char                 **flags,
                                      char                 **options,
                                      const char            *fstype,
                                      char                 **fsoptions,
                                      DBusGMethodInvocation *context)
{
        int n;
        int m;
        char *argv[128];
        GError *error;
        PolKitCaller *pk_caller;
        char *offset_as_string;
        char *size_as_string;
        char *max_number_as_string;
        char *flags_as_string;

        offset_as_string = NULL;
        size_as_string = NULL;
        max_number_as_string = NULL;
        flags_as_string = NULL;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (!device->priv->info.device_is_partition_table) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_NOT_PARTITIONED,
                             "Device is not partitioned");
                goto out;
        }

        if (devkit_disks_device_local_is_busy (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Device is busy");
                goto out;
        }

        /* partutil.c / libparted will check there are no partitions in the requested slice */

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit authorization */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context))
                goto out;

        if (strlen (type) == 0) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                             "type not specified");
                goto out;
        }

        offset_as_string = g_strdup_printf ("%" G_GINT64_FORMAT "", offset);
        size_as_string = g_strdup_printf ("%" G_GINT64_FORMAT "", size);
        max_number_as_string = g_strdup_printf ("%d", device->priv->info.partition_table_max_number);
        /* TODO: check that neither of the flags include ',' */
        flags_as_string = g_strjoinv (",", flags);

        n = 0;
        argv[n++] = PACKAGE_LIBEXEC_DIR "/devkit-disks-helper-create-partition";
        argv[n++] = device->priv->info.device_file;;
        argv[n++] = offset_as_string;
        argv[n++] = size_as_string;
        argv[n++] = max_number_as_string;
        argv[n++] = (char *) type;
        argv[n++] = (char *) label;
        argv[n++] = (char *) flags_as_string;
        for (m = 0; options[m] != NULL; m++) {
                if (n >= (int) sizeof (argv) - 1) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Too many options");
                        goto out;
                }
                /* the helper will validate each option */
                argv[n++] = (char *) options[m];
        }
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "PartitionCreate",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      partition_create_completed_cb,
                      partition_create_data_new (context, device, offset, size, fstype, fsoptions),
                      (GDestroyNotify) partition_create_data_unref)) {
                goto out;
        }

out:
        g_free (offset_as_string);
        g_free (size_as_string);
        g_free (max_number_as_string);
        g_free (flags_as_string);
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct {
        DBusGMethodInvocation *context;
        DevkitDisksDevice *device;
        DevkitDisksDevice *enclosing_device;

        char *type;
        char *label;
        char **flags;
} ModifyPartitionData;

static ModifyPartitionData *
partition_modify_data_new (DBusGMethodInvocation *context,
                           DevkitDisksDevice *device,
                           DevkitDisksDevice *enclosing_device,
                           const char *type,
                           const char *label,
                           char **flags)
{
        ModifyPartitionData *data;

        data = g_new0 (ModifyPartitionData, 1);

        data->context = context;
        data->device = g_object_ref (device);
        data->enclosing_device = g_object_ref (enclosing_device);
        data->type = g_strdup (type);
        data->label = g_strdup (label);
        data->flags = g_strdupv (flags);

        return data;
}

static void
partition_modify_data_unref (ModifyPartitionData *data)
{
        g_object_unref (data->device);
        g_object_unref (data->enclosing_device);
        g_free (data->type);
        g_free (data->label);
        g_free (data->flags);
        g_free (data);
}

static void
partition_modify_completed_cb (DBusGMethodInvocation *context,
                               DevkitDisksDevice *device,
                               PolKitCaller *pk_caller,
                               gboolean job_was_cancelled,
                               int status,
                               const char *stderr,
                               const char *stdout,
                               gpointer user_data)
{
        ModifyPartitionData *data = user_data;

        /* either way, poke the kernel so we can reread the data */
        devkit_device_emit_changed_to_kernel (data->enclosing_device);
        devkit_device_emit_changed_to_kernel (data->device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                int n;

                /* update local copy, don't wait for the kernel */

                g_free (device->priv->info.partition_type);
                device->priv->info.partition_type = g_strdup (data->type);

                g_free (device->priv->info.partition_label);
                device->priv->info.partition_label = g_strdup (data->label);

                g_ptr_array_foreach (device->priv->info.partition_flags, (GFunc) g_free, NULL);
                g_ptr_array_free (device->priv->info.partition_flags, TRUE);
                device->priv->info.partition_flags = g_ptr_array_new ();
                for (n = 0; data->flags[n] != NULL; n++) {
                        g_ptr_array_add (device->priv->info.partition_flags, g_strdup (data->flags[n]));
                }

                emit_changed (device);

                dbus_g_method_return (context);

        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error modifying partition: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status),
                                     stderr);
                }
        }
}

gboolean
devkit_disks_device_partition_modify (DevkitDisksDevice     *device,
                                      const char            *type,
                                      const char            *label,
                                      char                 **flags,
                                      DBusGMethodInvocation *context)
{
        int n;
        char *argv[128];
        GError *error;
        PolKitCaller *pk_caller;
        char *offset_as_string;
        char *size_as_string;
        char *flags_as_string;
        DevkitDisksDevice *enclosing_device;

        offset_as_string = NULL;
        size_as_string = NULL;
        flags_as_string = NULL;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (!device->priv->info.device_is_partition) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_NOT_PARTITION,
                             "Device is not a partition");
                goto out;
        }

        enclosing_device = devkit_disks_daemon_local_find_by_object_path (device->priv->daemon,
                                                                          device->priv->info.partition_slave);
        if (enclosing_device == NULL) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                             "Cannot find enclosing device");
                goto out;
        }

        if (devkit_disks_device_local_is_busy (enclosing_device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Enclosing device is busy");
                goto out;
        }

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit authorization */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context))
                goto out;

        if (strlen (type) == 0) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                             "type not specified");
                goto out;
        }

        offset_as_string = g_strdup_printf ("%" G_GINT64_FORMAT "", device->priv->info.partition_offset);
        size_as_string = g_strdup_printf ("%" G_GINT64_FORMAT "", device->priv->info.partition_size);
        /* TODO: check that neither of the flags include ',' */
        flags_as_string = g_strjoinv (",", flags);

        n = 0;
        argv[n++] = PACKAGE_LIBEXEC_DIR "/devkit-disks-helper-modify-partition";
        argv[n++] = enclosing_device->priv->info.device_file;
        argv[n++] = offset_as_string;
        argv[n++] = size_as_string;
        argv[n++] = (char *) type;
        argv[n++] = (char *) label;
        argv[n++] = (char *) flags_as_string;
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "PartitionModify",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      partition_modify_completed_cb,
                      partition_modify_data_new (context, device, enclosing_device, type, label, flags),
                      (GDestroyNotify) partition_modify_data_unref)) {
                goto out;
        }

out:
        g_free (offset_as_string);
        g_free (size_as_string);
        g_free (flags_as_string);
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
partition_table_create_completed_cb (DBusGMethodInvocation *context,
                                     DevkitDisksDevice *device,
                                     PolKitCaller *pk_caller,
                                     gboolean job_was_cancelled,
                                     int status,
                                     const char *stderr,
                                     const char *stdout,
                                     gpointer user_data)
{
        /* either way, poke the kernel so we can reread the data */
        devkit_device_emit_changed_to_kernel (device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                dbus_g_method_return (context);
        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error creating partition table: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status),
                                     stderr);
                }
        }
}

/* note: this only checks whether the actual partitions are busy;
 * caller will need to check the main device itself too
 */
gboolean
devkit_disks_device_local_partitions_are_busy (DevkitDisksDevice *device)
{
        gboolean ret;
        GList *l;
        GList *devices;

        ret = FALSE;

        devices = devkit_disks_daemon_local_get_all_devices (device->priv->daemon);
        for (l = devices; l != NULL; l = l->next) {
                DevkitDisksDevice *d = DEVKIT_DISKS_DEVICE (l->data);

                if (d->priv->info.device_is_partition &&
                    d->priv->info.partition_slave != NULL &&
                    strcmp (d->priv->info.partition_slave, device->priv->object_path) == 0) {

                        if (devkit_disks_device_local_is_busy (d)) {
                                ret = TRUE;
                                break;
                        }
                }
        }

        return ret;
}

gboolean
devkit_disks_device_partition_table_create (DevkitDisksDevice     *device,
                                            const char            *scheme,
                                            char                 **options,
                                            DBusGMethodInvocation *context)
{
        int n;
        int m;
        char *argv[128];
        GError *error;
        PolKitCaller *pk_caller;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (devkit_disks_device_local_is_busy (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Device is busy");
                goto out;
        }

        if (devkit_disks_device_local_partitions_are_busy (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "A partition on the device is busy");
                goto out;
        }

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit authorization */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context))
                goto out;

        if (strlen (scheme) == 0) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                             "type not specified");
                goto out;
        }

        n = 0;
        argv[n++] = PACKAGE_LIBEXEC_DIR "/devkit-disks-helper-create-partition-table";
        argv[n++] = device->priv->info.device_file;
        argv[n++] = (char *) scheme;
        for (m = 0; options[m] != NULL; m++) {
                if (n >= (int) sizeof (argv) - 1) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Too many options");
                        goto out;
                }
                /* the helper will validate each option */
                argv[n++] = (char *) options[m];
        }
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "PartitionTableCreate",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      partition_table_create_completed_cb,
                      NULL,
                      NULL)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static DevkitDisksDevice *
find_cleartext_device (DevkitDisksDevice *device)
{
        GList *devices;
        GList *l;
        DevkitDisksDevice *ret;

        ret = NULL;

        /* check that there isn't a cleartext device already  */
        devices = devkit_disks_daemon_local_get_all_devices (device->priv->daemon);
        for (l = devices; l != NULL; l = l->next) {
                DevkitDisksDevice *d = DEVKIT_DISKS_DEVICE (l->data);
                if (d->priv->info.device_is_crypto_cleartext &&
                    d->priv->info.crypto_cleartext_slave != NULL &&
                    strcmp (d->priv->info.crypto_cleartext_slave, device->priv->object_path) == 0) {
                        ret = d;
                        goto out;
                }
        }

out:
        return ret;
}

typedef struct {
        int refcount;

        guint device_added_signal_handler_id;
        guint device_added_timeout_id;

        DBusGMethodInvocation *context;
        DevkitDisksDevice *device;

        UnlockEncryptionHookFunc    hook_func;
        gpointer                    hook_user_data;
} UnlockEncryptionData;

static UnlockEncryptionData *
unlock_encryption_data_new (DBusGMethodInvocation      *context,
                            DevkitDisksDevice          *device,
                            UnlockEncryptionHookFunc    hook_func,
                            gpointer                    hook_user_data)
{
        UnlockEncryptionData *data;

        data = g_new0 (UnlockEncryptionData, 1);
        data->refcount = 1;

        data->context = context;
        data->device = g_object_ref (device);
        data->hook_func = hook_func;
        data->hook_user_data = hook_user_data;
        return data;
}

static UnlockEncryptionData *
unlock_encryption_data_ref (UnlockEncryptionData *data)
{
        data->refcount++;
        return data;
}

static void
unlock_encryption_data_unref (UnlockEncryptionData *data)
{
        data->refcount--;
        if (data->refcount == 0) {
                g_object_unref (data->device);
                g_free (data);
        }
}


static void
encrypted_unlock_device_added_cb (DevkitDisksDaemon *daemon,
                                  const char *object_path,
                                  gpointer user_data)
{
        UnlockEncryptionData *data = user_data;
        DevkitDisksDevice *device;

        /* check the device is a cleartext partition for us */
        device = devkit_disks_daemon_local_find_by_object_path (daemon, object_path);

        if (device != NULL &&
            device->priv->info.device_is_crypto_cleartext &&
            strcmp (device->priv->info.crypto_cleartext_slave, data->device->priv->object_path) == 0) {

                if (data->hook_func != NULL) {
                        data->hook_func (data->context, device, data->hook_user_data);
                } else {
                        /* yay! it is.. return value to the user */
                        dbus_g_method_return (data->context, object_path);
                }

                g_signal_handler_disconnect (daemon, data->device_added_signal_handler_id);
                g_source_remove (data->device_added_timeout_id);
                unlock_encryption_data_unref (data);
        }
}

static gboolean
encrypted_unlock_device_not_seen_cb (gpointer user_data)
{
        UnlockEncryptionData *data = user_data;

        throw_error (data->context,
                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                     "Error unlocking device: timeout (10s) waiting for cleartext device to show up");

        if (data->hook_func != NULL) {
                data->hook_func (data->context, NULL, data->hook_user_data);
        }

        g_signal_handler_disconnect (data->device->priv->daemon, data->device_added_signal_handler_id);
        unlock_encryption_data_unref (data);
        return FALSE;
}

static void
encrypted_unlock_completed_cb (DBusGMethodInvocation *context,
                               DevkitDisksDevice *device,
                               PolKitCaller *pk_caller,
                               gboolean job_was_cancelled,
                               int status,
                               const char *stderr,
                               const char *stdout,
                               gpointer user_data)
{
        UnlockEncryptionData *data = user_data;
        DevkitDisksDevice *cleartext_device;

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                cleartext_device = find_cleartext_device (device);
                if (cleartext_device != NULL) {
                        if (data->hook_func != NULL) {
                                data->hook_func (data->context, cleartext_device, data->hook_user_data);
                        } else {
                                dbus_g_method_return (data->context, cleartext_device->priv->object_path);
                        }
                } else {
                        /* sit around wait for the cleartext device to appear */
                        data->device_added_signal_handler_id = g_signal_connect_after (
                                device->priv->daemon,
                                "device-added",
                                (GCallback) encrypted_unlock_device_added_cb,
                                unlock_encryption_data_ref (data));

                        /* set up timeout for error reporting if waiting failed
                         *
                         * (the signal handler and the timeout handler share the ref to data
                         * as one will cancel the other)
                         */
                        data->device_added_timeout_id = g_timeout_add (10 * 1000,
                                                                       encrypted_unlock_device_not_seen_cb,
                                                                       data);
                }
        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error unlocking device: cryptsetup exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
                if (data->hook_func != NULL) {
                        data->hook_func (data->context, NULL, data->hook_user_data);
                }
        }
}

static gboolean
devkit_disks_device_encrypted_unlock_internal (DevkitDisksDevice        *device,
                                               const char               *secret,
                                               char                    **options,
                                               UnlockEncryptionHookFunc  hook_func,
                                               gpointer                  hook_user_data,
                                               DBusGMethodInvocation    *context)
{
        int n;
        char *argv[10];
        char *luks_name;
        GError *error;
        PolKitCaller *pk_caller;
        char *secret_as_stdin;

        luks_name = NULL;
        secret_as_stdin = NULL;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (devkit_disks_device_local_is_busy (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Device is busy");
                goto out;
        }

        if (device->priv->info.id_usage == NULL ||
            strcmp (device->priv->info.id_usage, "crypto") != 0) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_CRYPTO,
                             "Not a crypto device");
                goto out;
        }

        if (find_cleartext_device (device) != NULL) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_CRYPTO_ALREADY_UNLOCKED,
                             "Cleartext device is already unlocked");
                goto out;
        }

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.mount",
                                                  context)) {
                goto out;
        }

        /* TODO: use same naming scheme as hal */
        luks_name = g_strdup_printf ("devkit-disks-luks-uuid-%s", device->priv->info.id_uuid);
        secret_as_stdin = g_strdup_printf ("%s\n", secret);

        n = 0;
        argv[n++] = "cryptsetup";
        argv[n++] = "luksOpen";
        argv[n++] = device->priv->info.device_file;
        argv[n++] = luks_name;
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "EncryptedUnlock",
                      FALSE,
                      device,
                      pk_caller,
                      argv,
                      secret_as_stdin,
                      encrypted_unlock_completed_cb,
                      unlock_encryption_data_new (context, device, hook_func, hook_user_data),
                      (GDestroyNotify) unlock_encryption_data_unref)) {
                    goto out;
        }

out:
        /* scrub the secret */
        if (secret_as_stdin != NULL) {
                memset (secret_as_stdin, '\0', strlen (secret_as_stdin));
        }
        g_free (secret_as_stdin);
        g_free (luks_name);
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

gboolean
devkit_disks_device_encrypted_unlock (DevkitDisksDevice     *device,
                                      const char            *secret,
                                      char                 **options,
                                      DBusGMethodInvocation *context)
{
        return devkit_disks_device_encrypted_unlock_internal (device, secret, options, NULL, NULL, context);
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
encrypted_lock_completed_cb (DBusGMethodInvocation *context,
                             DevkitDisksDevice *device,
                             PolKitCaller *pk_caller,
                             gboolean job_was_cancelled,
                             int status,
                             const char *stderr,
                             const char *stdout,
                             gpointer user_data)
{
        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                dbus_g_method_return (context);
        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error locking device: cryptsetup exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
        }
}

gboolean
devkit_disks_device_encrypted_lock (DevkitDisksDevice     *device,
                                    char                 **options,
                                    DBusGMethodInvocation *context)
{
        int n;
        char *argv[10];
        GError *error;
        PolKitCaller *pk_caller;
        DevkitDisksDevice *cleartext_device;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (device->priv->info.id_usage == NULL ||
            strcmp (device->priv->info.id_usage, "crypto") != 0) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_CRYPTO,
                             "Not a crypto device");
                goto out;
        }

        cleartext_device = find_cleartext_device (device);
        if (cleartext_device == NULL) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_CRYPTO_NOT_UNLOCKED,
                             "Cleartext device is not unlocked");
                goto out;
        }

        if (cleartext_device->priv->info.dm_name == NULL || strlen (cleartext_device->priv->info.dm_name) == 0) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                             "Cannot determine device-mapper name");
                goto out;
        }

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.mount",
                                                  context)) {
                goto out;
        }

        n = 0;
        argv[n++] = "cryptsetup";
        argv[n++] = "luksClose";
        argv[n++] = cleartext_device->priv->info.dm_name;
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "EncryptedLock",
                      FALSE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      encrypted_lock_completed_cb,
                      NULL,
                      NULL)) {
                    goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
encrypted_change_passphrase_completed_cb (DBusGMethodInvocation *context,
                                          DevkitDisksDevice *device,
                                          PolKitCaller *pk_caller,
                                          gboolean job_was_cancelled,
                                          int status,
                                          const char *stderr,
                                          const char *stdout,
                                          gpointer user_data)
{
        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                dbus_g_method_return (context);
        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error changing secret on device: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
        }
}

gboolean
devkit_disks_device_encrypted_change_passphrase (DevkitDisksDevice     *device,
                                                 const char            *old_secret,
                                                 const char            *new_secret,
                                                 DBusGMethodInvocation *context)
{
        int n;
        char *argv[10];
        GError *error;
        PolKitCaller *pk_caller;
        char *secrets_as_stdin;

        secrets_as_stdin = NULL;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        /* No need to check for busy; we can actually do this while the device is unlocked as
         * only LUKS metadata is modified.
         */

        if (device->priv->info.id_usage == NULL ||
            strcmp (device->priv->info.id_usage, "crypto") != 0) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_CRYPTO,
                             "Not a crypto device");
                goto out;
        }

        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.mount",
                                                  context)) {
                goto out;
        }

        secrets_as_stdin = g_strdup_printf ("%s\n%s\n", old_secret, new_secret);

        n = 0;
        argv[n++] = "devkit-disks-helper-change-luks-password";
        argv[n++] = device->priv->info.device_file;
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "EncryptedChangePassphrase",
                      FALSE,
                      device,
                      pk_caller,
                      argv,
                      secrets_as_stdin,
                      encrypted_change_passphrase_completed_cb,
                      NULL,
                      NULL)) {
                goto out;
        }

out:
        /* scrub the secrets */
        if (secrets_as_stdin != NULL) {
                memset (secrets_as_stdin, '\0', strlen (secrets_as_stdin));
        }
        g_free (secrets_as_stdin);
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
filesystem_set_label_completed_cb (DBusGMethodInvocation *context,
                                      DevkitDisksDevice *device,
                                      PolKitCaller *pk_caller,
                                      gboolean job_was_cancelled,
                                      int status,
                                      const char *stderr,
                                      const char *stdout,
                                      gpointer user_data)
{
        char *new_label = user_data;

        /* either way, poke the kernel so we can reread the data */
        devkit_device_emit_changed_to_kernel (device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                /* update local copy, don't wait for the kernel */
                g_free (device->priv->info.id_label);
                device->priv->info.id_label = g_strdup (new_label);

                emit_changed (device);

                dbus_g_method_return (context);

        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error changing fslabel: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
        }
}

gboolean
devkit_disks_device_filesystem_set_label (DevkitDisksDevice     *device,
                                             const char            *new_label,
                                             DBusGMethodInvocation *context)
{
        int n;
        char *argv[10];
        GError *error;
        PolKitCaller *pk_caller;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        /* TODO: some file systems can do this while mounted; we need something similar
         *       to GduCreatableFilesystem cf. gdu-util.h
         */

        if (devkit_disks_device_local_is_busy (device)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Device is busy");
                goto out;
        }

        if (device->priv->info.id_usage == NULL ||
            strcmp (device->priv->info.id_usage, "filesystem") != 0) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_MOUNTABLE,
                             "Not a mountable file system");
                goto out;
        }


        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context)) {
                goto out;
        }

        n = 0;
        argv[n++] = "devkit-disks-helper-change-filesystem-label";
        argv[n++] = device->priv->info.device_file;
        argv[n++] = device->priv->info.id_type;
        argv[n++] = (char *) new_label;
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "FilesystemSetLabel",
                      FALSE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      filesystem_set_label_completed_cb,
                      g_strdup (new_label),
                      g_free)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct {
        gboolean simulation;
} DriveRefreshSmartDataData;

static DriveRefreshSmartDataData *
drive_smart_refresh_data_data_new (gboolean simulation)
{
        DriveRefreshSmartDataData *data;
        data = g_new0 (DriveRefreshSmartDataData, 1);
        data->simulation = simulation;
        return data;
}

static void
drive_smart_refresh_data_unref (DriveRefreshSmartDataData *data)
{
        g_free (data);
}

/* may be called with context==NULL */
static void
drive_smart_refresh_data_completed_cb (DBusGMethodInvocation *context,
                                       DevkitDisksDevice *device,
                                       PolKitCaller *pk_caller,
                                       gboolean job_was_cancelled,
                                       int status,
                                       const char *stderr,
                                       const char *stdout,
                                       gpointer user_data)
{
        DriveRefreshSmartDataData *data = user_data;
        int rc;
        gboolean passed;
        int n;
        char **lines;
        gboolean in_attributes;
        int power_on_hours;
        int temperature;
        const char *last_self_test_result;
        GTimeVal now;
        gboolean attributes_has_upd;

        if (job_was_cancelled || stdout == NULL) {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error retrieving S.M.A.R.T. data: no output",
                                     WEXITSTATUS (status), stderr);
                }
                goto out;
        }

        rc = WEXITSTATUS (status);

        if ((rc & (0x01|0x02|0x04)) != 0) {
                /* update our setting if we "thought" (cf. update_info()) that this device
                 * was S.M.A.R.T. capable
                 */
                if (device->priv->drive_smart_is_capable) {
                        device->priv->drive_smart_is_capable = FALSE;
                        emit_changed (device);
                }
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_NOT_SMART_CAPABLE,
                             "Device is not S.M.A.R.T. capable");
                goto out;
        }

        /* TODO: is_enabled */
        device->priv->drive_smart_is_enabled = TRUE;

        g_get_current_time (&now);
        device->priv->drive_smart_time_collected = now.tv_sec;

        g_ptr_array_foreach (device->priv->drive_smart_attributes, (GFunc) g_value_array_free, NULL);
        g_ptr_array_free (device->priv->drive_smart_attributes, TRUE);
        device->priv->drive_smart_attributes = g_ptr_array_new ();

        passed = TRUE;
        power_on_hours = 0;
        temperature = 0;
        last_self_test_result = "";

        if ((rc & 0x08) != 0) {
                passed = FALSE;
                device->priv->drive_smart_is_failing = TRUE;
        }

        lines = g_strsplit (stdout, "\n", 0);

        in_attributes = FALSE;
        attributes_has_upd = FALSE;
        for (n = 0; lines[n] != NULL; n++) {
                const char *line = (const char *) lines[n];
                int id;
                char name[256];
                unsigned int flags;
                int value;
                int worst;
                int threshold;
                char type[256];
                char updated[256];
                char when_failed[256];
                char raw_string_value[256];
                int raw_value;
                int self_test_execution_status;
                gboolean parsed_attr;

                /* We're looking at parsing this block of the output
                 *
                 * ID# ATTRIBUTE_NAME          FLAG     VALUE WORST THRESH TYPE      UPDATED  WHEN_FAILED RAW_VALUE
                 *   1 Raw_Read_Error_Rate     0x000f   200   200   051    Pre-fail  Always       -       1284
                 *   3 Spin_Up_Time            0x0003   225   215   021    Pre-fail  Always       -       5725
                 *   4 Start_Stop_Count        0x0032   100   100   000    Old_age   Always       -       204
                 *   5 Reallocated_Sector_Ct   0x0033   199   199   140    Pre-fail  Always       -       2
                 *   7 Seek_Error_Rate         0x000f   127   127   051    Pre-fail  Always       -       65877
                 *   9 Power_On_Hours          0x0032   096   096   000    Old_age   Always       -       3429
                 *  10 Spin_Retry_Count        0x0013   100   100   051    Pre-fail  Always       -       0
                 *  11 Calibration_Retry_Count 0x0012   100   100   051    Old_age   Always       -       0
                 *  12 Power_Cycle_Count       0x0032   100   100   000    Old_age   Always       -       153
                 * 190 Temperature_Celsius     0x0022   058   032   045    Old_age   Always   In_the_past 42
                 * 194 Temperature_Celsius     0x0022   253   253   000    Old_age   Always       -       43
                 * 196 Reallocated_Event_Count 0x0032   198   198   000    Old_age   Always       -       2
                 * 197 Current_Pending_Sector  0x0012   191   191   000    Old_age   Always       -       762
                 * 198 Offline_Uncorrectable   0x0010   200   200   000    Old_age   Offline      -       21
                 * 199 UDMA_CRC_Error_Count    0x003e   200   200   000    Old_age   Always       -       40
                 * 200 Multi_Zone_Error_Rate   0x0009   170   170   051    Pre-fail  Offline      -       1542
                 *
                 */

                if (data->simulation) {
                        if (strstr (line, "self-assessment test result") != NULL) {
                                if (strstr (line, "PASSED") != NULL)
                                        passed = TRUE;
                                else
                                        passed = FALSE;
                        }
                }

                if (g_str_has_prefix (line, "ID# ATTRIBUTE_NAME ")) {
                        in_attributes = TRUE;
                        if (strstr (line, "UPDATED") != NULL)
                                attributes_has_upd = TRUE;
                        continue;
                }

                if (strlen (line) >= 256) {
                        g_warning ("Ignoring line '%s' (too long)", line);
                        continue;
                }

                if (!in_attributes) {
                        if (sscanf (line, "Self-test execution status: ( %d)",
                                    &self_test_execution_status) == 1) {
                                g_warning ("self_test_execution_status: %d", self_test_execution_status);

                                switch (self_test_execution_status >> 4) {
                                case 0:
                                        last_self_test_result = "completed_ok";
                                        break;
                                case 1:
                                        last_self_test_result = "not_completed_aborted";
                                        break;
                                case 2:
                                        last_self_test_result = "not_completed_aborted_reset";
                                        break;
                                case 3:
                                        last_self_test_result = "not_completed_unknown_reason";
                                        break;
                                case 4:
                                        last_self_test_result = "completed_failed_unknown_reason";
                                        break;
                                case 5:
                                        last_self_test_result = "completed_failed_electrical";
                                        break;
                                case 6:
                                        last_self_test_result = "completed_failed_servo";
                                        break;
                                case 7:
                                        last_self_test_result = "completed_failed_read";
                                        break;
                                case 8:
                                        last_self_test_result = "completed_failed_damage";
                                        break;
                                default:
                                        last_self_test_result = "unknown";
                                        break;
                                }
                                g_free (device->priv->drive_smart_last_self_test_result);
                                device->priv->drive_smart_last_self_test_result = g_strdup (last_self_test_result);

                        }
                        continue;
                }

                if (strlen (line) == 0) {
                        break;
                }

                parsed_attr = FALSE;
                memset (raw_string_value, '\0', sizeof raw_string_value);
                if (attributes_has_upd) {
                        parsed_attr = sscanf (line, "%d %s 0x%x %d %d %d %s %s %s %255c",
                                              &id, name, &flags, &value, &worst, &threshold,
                                              type, updated, when_failed, raw_string_value) == 10;
                } else {
                        parsed_attr = sscanf (line, "%d %s 0x%x %d %d %d %s %s %255c",
                                              &id, name, &flags, &value, &worst, &threshold,
                                              type, when_failed, raw_string_value) == 9;
                }

                if (parsed_attr) {
                        /*
                        g_printerr ("                id=%d\n", id);
                        g_printerr ("              name='%s'\n", name);
                        g_printerr ("             flags=0x%x\n", flags);
                        g_printerr ("             value=%d\n", value);
                        g_printerr ("             worst=%d\n", worst);
                        g_printerr ("         threshold=%d\n", threshold);
                        g_printerr ("              type='%s'\n", type);
                        g_printerr ("           updated='%s'\n", updated);
                        g_printerr ("       when_failed='%s'\n", when_failed);
                        g_printerr ("  raw_string_value='%s'\n", raw_string_value);
                        */

                        if (sscanf (raw_string_value, "%d", &raw_value) == 1) {
                                if (id == 9) {
                                        power_on_hours = raw_value;
                                        device->priv->drive_smart_time_powered_on = raw_value * 3600;
                                } else if (id == 194) {
                                        temperature = raw_value;
                                        device->priv->drive_smart_temperature = raw_value;
                                }
                        }

                        if (id == 197) {
                        }

                        GValue elem = {0};
                        g_value_init (&elem, SMART_DATA_STRUCT_TYPE);
                        g_value_take_boxed (&elem, dbus_g_type_specialized_construct (SMART_DATA_STRUCT_TYPE));
                        dbus_g_type_struct_set (&elem,
                                                0, id,
                                                1, name,
                                                2, flags,
                                                3, value,
                                                4, worst,
                                                5, threshold,
                                                6, raw_string_value,
                                                G_MAXUINT);
                        g_ptr_array_add (device->priv->drive_smart_attributes, g_value_get_boxed (&elem));

                }

        }
        g_strfreev (lines);

        device->priv->drive_smart_is_failing = !passed;

        /* emit change event since we've updated the smart data */
        emit_changed (device);

        devkit_disks_logger_record_smart_values (devkit_disks_daemon_local_get_logger (device->priv->daemon),
                                                 device);

        if (context != NULL)
                dbus_g_method_return (context, passed, power_on_hours, temperature, last_self_test_result);
out:
        ;
}

/* may be called with context==NULL */
gboolean
devkit_disks_device_drive_smart_refresh_data (DevkitDisksDevice     *device,
                                              char                 **options,
                                              DBusGMethodInvocation *context)
{
        int n;
        char *argv[10];
        GError *error;
        PolKitCaller *pk_caller;
        const char *simulpath;
        gboolean nowakeup;

        pk_caller = NULL;
        if (context != NULL) {
                if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon,
                                                                                  context)) == NULL)
                        goto out;
        }

        if (!device->priv->info.device_is_drive) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_DRIVE,
                             "Device is not a drive");
                goto out;
        }

        if (!device->priv->drive_smart_is_capable) {
                throw_error (context,
                             DEVKIT_DISKS_DEVICE_ERROR_NOT_SMART_CAPABLE,
                             "Device is not S.M.A.R.T. capable");
                goto out;
        }

#if 0
        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context)) {
                goto out;
        }
#endif

        simulpath = NULL;
        nowakeup = FALSE;
        for (n = 0; options[n] != NULL; n++) {
                if (g_str_has_prefix (options[n], "simulate=")) {
                        if (context != NULL) {
                                uid_t uid;
                                if (!polkit_caller_get_uid (pk_caller, &uid) || uid != 0) {
                                        throw_error (context,
                                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                                     "Only uid 0 may use the simulate= option");
                                        goto out;
                                }
                        }
                        simulpath = (const char *) options[n] + 9;
                } else if (strcmp (options[n], "nowakeup") == 0) {
                        nowakeup = TRUE;
                }
        }

        if (simulpath != NULL) {
                n = 0;
                argv[n++] = "cat";
                argv[n++] = (char *) simulpath;
                argv[n++] = NULL;
        } else {
                n = 0;
                /* TODO: honor option 'nowakeup' */
                argv[n++] = "smartctl";
                argv[n++] = "--all";
                argv[n++] = device->priv->info.device_file;
                argv[n++] = NULL;
        }

        error = NULL;

        if (!job_new (context,
                      NULL,     /* don't run this as a job */
                      FALSE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      drive_smart_refresh_data_completed_cb,
                      drive_smart_refresh_data_data_new (simulpath != NULL),
                      (GDestroyNotify) drive_smart_refresh_data_unref)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
drive_smart_initiate_selftest_completed_cb (DBusGMethodInvocation *context,
                                            DevkitDisksDevice *device,
                                            PolKitCaller *pk_caller,
                                            gboolean job_was_cancelled,
                                            int status,
                                            const char *stderr,
                                            const char *stdout,
                                            gpointer user_data)
{
        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                dbus_g_method_return (context);

        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error running self test: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
        }
}

gboolean
devkit_disks_device_drive_smart_initiate_selftest (DevkitDisksDevice     *device,
                                                   const char            *test,
                                                   gboolean               captive,
                                                   DBusGMethodInvocation *context)
{
        int n;
        char *argv[10];
        GError *error;
        PolKitCaller *pk_caller;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (!device->priv->info.device_is_drive) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_DRIVE,
                             "Device is not a drive");
                goto out;
        }

        if (captive) {
                if (devkit_disks_device_local_is_busy (device)) {
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                                     "Device is busy");
                        goto out;
                }

                if (devkit_disks_device_local_partitions_are_busy (device)) {
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                                     "A partition on the device is busy");
                        goto out;
                }
        }

#if 0
        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context)) {
                goto out;
        }
#endif

        n = 0;
        argv[n++] = PACKAGE_LIBEXEC_DIR "/devkit-disks-helper-smart-selftest";
        argv[n++] = device->priv->info.device_file;
        argv[n++] = (char *) test;
        argv[n++] = captive ? "1" : "0";
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "DriveSmartInitiateSelftest",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      drive_smart_initiate_selftest_completed_cb,
                      NULL,
                      NULL)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
linux_md_stop_completed_cb (DBusGMethodInvocation *context,
                                  DevkitDisksDevice *device,
                                  PolKitCaller *pk_caller,
                                  gboolean job_was_cancelled,
                                  int status,
                                  const char *stderr,
                                  const char *stdout,
                                  gpointer user_data)
{
        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                /* the kernel side of md currently doesn't emit a 'changed' event so
                 * generate one such that the md device can disappear from our
                 * database
                 */
                devkit_device_emit_changed_to_kernel (device);

                dbus_g_method_return (context);

        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error stopping array: mdadm exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
        }
}

gboolean
devkit_disks_device_linux_md_stop (DevkitDisksDevice     *device,
                                         char                 **options,
                                         DBusGMethodInvocation *context)
{
        int n;
        char *argv[10];
        GError *error;
        PolKitCaller *pk_caller;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (!device->priv->info.device_is_linux_md) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD,
                             "Device is not a Linux md drive");
                goto out;
        }

#if 0
        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context)) {
                goto out;
        }
#endif

        n = 0;
        argv[n++] = "mdadm";
        argv[n++] = "--stop";
        argv[n++] = device->priv->info.device_file;
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "LinuxMdStop",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      linux_md_stop_completed_cb,
                      NULL,
                      NULL)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
linux_md_add_component_completed_cb (DBusGMethodInvocation *context,
                                              DevkitDisksDevice *device,
                                              PolKitCaller *pk_caller,
                                              gboolean job_was_cancelled,
                                              int status,
                                              const char *stderr,
                                              const char *stdout,
                                              gpointer user_data)
{
        DevkitDisksDevice *slave = DEVKIT_DISKS_DEVICE (user_data);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                /* the slave got new metadata on it; reread that */
                devkit_device_emit_changed_to_kernel (slave);

                /* the kernel side of md currently doesn't emit a 'changed' event so
                 * generate one since state may have changed (e.g. rebuild started etc.)
                 */
                devkit_device_emit_changed_to_kernel (device);

                dbus_g_method_return (context);

        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error stopping array: mdadm exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
        }
}

gboolean
devkit_disks_device_linux_md_add_component (DevkitDisksDevice     *device,
                                                     char                  *component,
                                                     char                 **options,
                                                     DBusGMethodInvocation *context)
{
        int n;
        char *argv[10];
        GError *error;
        PolKitCaller *pk_caller;
        DevkitDisksDevice *slave;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (!device->priv->info.device_is_linux_md) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD,
                             "Device is not a Linux md drive");
                goto out;
        }

        slave = devkit_disks_daemon_local_find_by_object_path (device->priv->daemon, component);
        if (slave == NULL) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NO_SUCH_DEVICE,
                             "Component doesn't exist");
                goto out;
        }

        /* it's fine if the given device isn't a Linux md component _yet_; think
         * hot adding a new disk if an old one failed
         */

        if (devkit_disks_device_local_is_busy (slave)) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                             "Component to add is busy");
                goto out;
        }

        /* TODO: check component size is OK */

#if 0
        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context)) {
                goto out;
        }
#endif

        n = 0;
        argv[n++] = "mdadm";
        argv[n++] = "--manage";
        argv[n++] = device->priv->info.device_file;
        argv[n++] = "--add";
        argv[n++] = slave->priv->info.device_file;
        argv[n++] = "--force";
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "LinuxMdAddComponent",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      linux_md_add_component_completed_cb,
                      g_object_ref (slave),
                      g_object_unref)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/


typedef struct {
        int refcount;

        DBusGMethodInvocation *context;
        DevkitDisksDevice *slave;
        char **options;

        guint device_changed_signal_handler_id;
        guint device_changed_timeout_id;

} RemoveComponentData;

static RemoveComponentData *
remove_component_data_new (DBusGMethodInvocation      *context,
                           DevkitDisksDevice          *slave,
                           char                      **options)
{
        RemoveComponentData *data;

        data = g_new0 (RemoveComponentData, 1);
        data->refcount = 1;

        data->context = context;
        data->slave = g_object_ref (slave);
        data->options = g_strdupv (options);
        return data;
}

static RemoveComponentData *
remove_component_data_ref (RemoveComponentData *data)
{
        data->refcount++;
        return data;
}

static void
remove_component_data_unref (RemoveComponentData *data)
{
        data->refcount--;
        if (data->refcount == 0) {
                g_object_unref (data->slave);
                g_free (data->options);
                g_free (data);
        }
}


static void
linux_md_remove_component_device_changed_cb (DevkitDisksDaemon *daemon,
                                                        const char *object_path,
                                                        gpointer user_data)
{
        RemoveComponentData *data = user_data;
        DevkitDisksDevice *device;

        /* check if we're now a LUKS crypto device */
        device = devkit_disks_daemon_local_find_by_object_path (daemon, object_path);
        if (device == data->slave && !devkit_disks_device_local_is_busy (data->slave)) {

                /* yay! now scrub it! */
                devkit_disks_device_erase (data->slave, data->options, data->context);

                /* TODO: leaking data? */

                g_signal_handler_disconnect (daemon, data->device_changed_signal_handler_id);
                g_source_remove (data->device_changed_timeout_id);
        }
}

static gboolean
linux_md_remove_component_device_not_seen_cb (gpointer user_data)
{
        RemoveComponentData *data = user_data;

        throw_error (data->context,
                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                     "Error removing component: timeout (10s) waiting for slave to stop being busy");

        g_signal_handler_disconnect (data->slave->priv->daemon, data->device_changed_signal_handler_id);
        remove_component_data_unref (data);

        return FALSE;
}


static void
linux_md_remove_component_completed_cb (DBusGMethodInvocation *context,
                                                   DevkitDisksDevice *device,
                                                   PolKitCaller *pk_caller,
                                                   gboolean job_was_cancelled,
                                                   int status,
                                                   const char *stderr,
                                                   const char *stdout,
                                                   gpointer user_data)
{
        RemoveComponentData *data = user_data;

        /* the slave got new metadata on it; reread that */
        devkit_device_emit_changed_to_kernel (data->slave);

        /* the kernel side of md currently doesn't emit a 'changed' event so
         * generate one since state may have changed (e.g. rebuild started etc.)
         */
        devkit_device_emit_changed_to_kernel (device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                /* wait for the slave to be busy, then start erasing it */

                data->device_changed_signal_handler_id = g_signal_connect_after (
                        device->priv->daemon,
                        "device-changed",
                        (GCallback) linux_md_remove_component_device_changed_cb,
                        remove_component_data_ref (data));

                /* set up timeout for error reporting if waiting failed
                 *
                 * (the signal handler and the timeout handler share the ref to data
                 * as one will cancel the other)
                 */
                data->device_changed_timeout_id = g_timeout_add (
                        10 * 1000,
                        linux_md_remove_component_device_not_seen_cb,
                        data);

        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error stopping array: helper exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
        }
}

gboolean
devkit_disks_device_linux_md_remove_component (DevkitDisksDevice     *device,
                                                          char                  *component,
                                                          char                 **options,
                                                          DBusGMethodInvocation *context)
{
        int n, m;
        char *argv[128];
        GError *error;
        PolKitCaller *pk_caller;
        DevkitDisksDevice *slave;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon, context)) == NULL)
                goto out;

        if (!device->priv->info.device_is_linux_md) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD,
                             "Device is not a Linux md drive");
                goto out;
        }

        slave = devkit_disks_daemon_local_find_by_object_path (device->priv->daemon, component);
        if (slave == NULL) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NO_SUCH_DEVICE,
                             "Component doesn't exist");
                goto out;
        }

        /* check that it really is a component of the md device */
        for (n = 0; n < (int) device->priv->info.linux_md_slaves->len; n++) {
                if (strcmp (component, device->priv->info.linux_md_slaves->pdata[n]) == 0)
                        break;
        }
        if (n == (int) device->priv->info.linux_md_slaves->len) {
                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NO_SUCH_DEVICE,
                             "Component isn't part of the running array");
                goto out;
        }

#if 0
        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context)) {
                goto out;
        }
#endif

        n = 0;
        argv[n++] = PACKAGE_LIBEXEC_DIR "/devkit-disks-helper-linux-md-remove-component";
        argv[n++] = device->priv->info.device_file;
        argv[n++] = slave->priv->info.device_file;
        for (m = 0; options[m] != NULL; m++) {
                if (n >= (int) sizeof (argv) - 1) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Too many options");
                        goto out;
                }
                /* the helper will validate each option */
                argv[n++] = (char *) options[m];
        }
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "LinuxMdRemoveComponent",
                      TRUE,
                      device,
                      pk_caller,
                      argv,
                      NULL,
                      linux_md_remove_component_completed_cb,
                      remove_component_data_new (context, slave, options),
                      (GDestroyNotify) remove_component_data_unref)) {
                goto out;
        }

out:
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct {
        int refcount;

        guint device_added_signal_handler_id;
        guint device_added_timeout_id;

        DBusGMethodInvocation *context;

        DevkitDisksDaemon *daemon;
        char *uuid;

} LinuxMdStartData;

static LinuxMdStartData *
linux_md_start_data_new (DBusGMethodInvocation *context,
                         DevkitDisksDaemon     *daemon,
                         const char            *uuid)
{
        LinuxMdStartData *data;

        data = g_new0 (LinuxMdStartData, 1);
        data->refcount = 1;

        data->context = context;
        data->daemon = g_object_ref (daemon);
        data->uuid = g_strdup (uuid);
        return data;
}

static LinuxMdStartData *
linux_md_start_data_ref (LinuxMdStartData *data)
{
        data->refcount++;
        return data;
}

static void
linux_md_start_data_unref (LinuxMdStartData *data)
{
        data->refcount--;
        if (data->refcount == 0) {
                g_object_unref (data->daemon);
                g_free (data->uuid);
                g_free (data);
        }
}

static void
linux_md_start_device_added_cb (DevkitDisksDaemon *daemon,
                                const char *object_path,
                                gpointer user_data)
{
        LinuxMdStartData *data = user_data;
        DevkitDisksDevice *device;

        /* check the device is the one we're looking for */
        device = devkit_disks_daemon_local_find_by_object_path (daemon, object_path);

        if (device != NULL &&
            device->priv->info.device_is_linux_md) {

                /* TODO: actually check this properly by looking at slaves vs. components */

                /* yay! it is.. return value to the user */
                dbus_g_method_return (data->context, object_path);

                g_signal_handler_disconnect (daemon, data->device_added_signal_handler_id);
                g_source_remove (data->device_added_timeout_id);
                linux_md_start_data_unref (data);
        }
}

static gboolean
linux_md_start_device_not_seen_cb (gpointer user_data)
{
        LinuxMdStartData *data = user_data;

        throw_error (data->context,
                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                     "Error assembling array: timeout (10s) waiting for array to show up");

        g_signal_handler_disconnect (data->daemon, data->device_added_signal_handler_id);
        linux_md_start_data_unref (data);
        return FALSE;
}

/* NOTE: This is job completion callback from a method on the daemon, not the device. */

static void
linux_md_start_completed_cb (DBusGMethodInvocation *context,
                             DevkitDisksDevice *device,
                             PolKitCaller *pk_caller,
                             gboolean job_was_cancelled,
                             int status,
                             const char *stderr,
                             const char *stdout,
                             gpointer user_data)
{
        LinuxMdStartData *data = user_data;

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {
                GList *l;
                GList *devices;
                char *objpath;

                /* see if the component appeared already */

                objpath = NULL;

                devices = devkit_disks_daemon_local_get_all_devices (data->daemon);
                for (l = devices; l != NULL; l = l->next) {
                        DevkitDisksDevice *device = DEVKIT_DISKS_DEVICE (l->data);

                        if (device->priv->info.device_is_linux_md) {

                                /* TODO: check properly */

                                /* yup, return to caller */
                                objpath = device->priv->object_path;
                                break;
                        }
                }

                if (objpath != NULL) {
                        dbus_g_method_return (context, objpath);
                } else {
                        /* sit around and wait for the md array to appear */

                        /* sit around wait for the cleartext device to appear */
                        data->device_added_signal_handler_id = g_signal_connect_after (
                                data->daemon,
                                "device-added",
                                (GCallback) linux_md_start_device_added_cb,
                                linux_md_start_data_ref (data));

                        /* set up timeout for error reporting if waiting failed
                         *
                         * (the signal handler and the timeout handler share the ref to data
                         * as one will cancel the other)
                         */
                        data->device_added_timeout_id = g_timeout_add (10 * 1000,
                                                                       linux_md_start_device_not_seen_cb,
                                                                       data);
                }


        } else {
                if (job_was_cancelled) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_JOB_WAS_CANCELLED,
                                     "Job was cancelled");
                } else {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Error assembling array: mdadm exited with exit code %d: %s",
                                     WEXITSTATUS (status), stderr);
                }
        }
}

/* NOTE: This is a method on the daemon, not the device. */

gboolean
devkit_disks_daemon_linux_md_start (DevkitDisksDaemon     *daemon,
                                    GPtrArray             *components,
                                    char                 **options,
                                    DBusGMethodInvocation *context)
{
        int n;
        int m;
        char *argv[128];
        GError *error;
        PolKitCaller *pk_caller;
        char *uuid;
        char *md_device_file;

        uuid = NULL;
        md_device_file = NULL;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (daemon, context)) == NULL)
                goto out;

        /* check that all given components exist, that they are indeed linux-md-components and
         * that their uuid agrees
         */
        for (n = 0; n < (int) components->len; n++) {
                DevkitDisksDevice *slave;
                const char *component_objpath = components->pdata[n];

                slave = devkit_disks_daemon_local_find_by_object_path (daemon, component_objpath);
                if (slave == NULL) {
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD_COMPONENT,
                                     "Component %s doesn't exist", component_objpath);
                        goto out;
                }

                if (!slave->priv->info.device_is_linux_md_component) {
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD_COMPONENT,
                                     "%s is not a linux-md component", component_objpath);
                        goto out;
                }

                if (n == 0) {
                        uuid = g_strdup (slave->priv->info.linux_md_component_uuid);
                        if (uuid == NULL) {
                                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD_COMPONENT,
                                             "no uuid for one of the components");
                        }
                } else {
                        const char *this_uuid;
                        this_uuid = slave->priv->info.linux_md_component_uuid;

                        if (this_uuid == NULL || strcmp (uuid, this_uuid) != 0) {
                                throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD_COMPONENT,
                                             "uuid mismatch between given components");
                                goto out;
                        }
                }

                if (devkit_disks_device_local_is_busy (slave)) {
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_IS_BUSY,
                                     "component %d is busy", n);
                        goto out;
                }
        }

        /* find an unused md minor... Man, I wish mdadm could do this itself; this is slightly racy */
        for (n = 0; TRUE; n++) {
                char *native_path;
                char *array_state;

                /* TODO: move to /sys/class/block instead */
                native_path = g_strdup_printf ("/sys/block/md%d", n);
                if (!sysfs_file_exists (native_path, "md/array_state")) {
                        /* Apparently this slot is free since there is no such file. So let's peruse it. */
                        g_free (native_path);
                        break;
                } else {
                        array_state = sysfs_get_string (native_path, "md/array_state");
                        g_strstrip (array_state);
                        if (strcmp (array_state, "clear") == 0) {
                                /* It's clear! Let's use it! */
                                g_free (array_state);
                                g_free (native_path);
                                break;
                        }
                        g_free (array_state);
                }
                g_free (native_path);
        }

        md_device_file = g_strdup_printf ("/dev/md%d", n);

#if 0
        if (!devkit_disks_damon_local_check_auth (device->priv->daemon,
                                                  pk_caller,
                                                  /* TODO: revisit auth */
                                                  "org.freedesktop.devicekit.disks.erase",
                                                  context)) {
                goto out;
        }
#endif

        n = 0;
        argv[n++] = "mdadm";
        argv[n++] = "--assemble";
        argv[n++] = md_device_file;
        argv[n++] = "--run";
        for (m = 0; m < (int) components->len; m++) {
                DevkitDisksDevice *slave;
                const char *component_objpath = components->pdata[m];

                slave = devkit_disks_daemon_local_find_by_object_path (daemon, component_objpath);
                if (slave == NULL) {
                        throw_error (context, DEVKIT_DISKS_DEVICE_ERROR_NOT_LINUX_MD_COMPONENT,
                                     "Component %s doesn't exist", component_objpath);
                        goto out;
                }

                if (n >= (int) sizeof (argv) - 1) {
                        throw_error (context,
                                     DEVKIT_DISKS_DEVICE_ERROR_GENERAL,
                                     "Too many components");
                        goto out;
                }

                argv[n++] = (char *) slave->priv->info.device_file;
        }
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (context,
                      "LinuxMdStart",
                      TRUE,
                      NULL,
                      pk_caller,
                      argv,
                      NULL,
                      linux_md_start_completed_cb,
                      linux_md_start_data_new (context, daemon, uuid),
                      (GDestroyNotify) linux_md_start_data_unref)) {
                goto out;
        }

out:
        g_free (uuid);
        g_free (md_device_file);
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);

        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct {
        char                     *mount_path;
        ForceRemovalCompleteFunc  fr_callback;
        gpointer                  fr_user_data;
} ForceUnmountData;

static ForceUnmountData *
force_unmount_data_new (char                     *mount_path,
                        ForceRemovalCompleteFunc  fr_callback,
                        gpointer                  fr_user_data)
{
        ForceUnmountData *data;

        data = g_new0 (ForceUnmountData, 1);
        data->mount_path = g_strdup (mount_path);
        data->fr_callback = fr_callback;
        data->fr_user_data = fr_user_data;

        return data;
}

static void
force_unmount_data_unref (ForceUnmountData *data)
{
        g_free (data->mount_path);
        g_free (data);
}

static void
force_unmount_completed_cb (DBusGMethodInvocation *context,
                            DevkitDisksDevice *device,
                            PolKitCaller *pk_caller,
                            gboolean job_was_cancelled,
                            int status,
                            const char *stderr,
                            const char *stdout,
                            gpointer user_data)
{
        ForceUnmountData *data = user_data;
        char *touch_str;

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                g_warning ("Successfully force unmounted device %s", device->priv->info.device_file);
                devkit_disks_device_local_set_unmounted (device, TRUE);
                mounts_file_remove (device, data->mount_path);

                /* TODO: when we add polling, this can probably be removed. I have no idea why hal's
                 *       poller don't cause the kernel to revalidate the (missing) media
                 */
                touch_str = g_strdup_printf ("touch %s", device->priv->info.device_file);
                g_spawn_command_line_sync (touch_str, NULL, NULL, NULL, NULL);
                g_free (touch_str);

                if (data->fr_callback != NULL)
                        data->fr_callback (device, TRUE, data->fr_user_data);
        } else {
                g_warning ("force unmount failed: %s", stderr);
                if (data->fr_callback != NULL)
                        data->fr_callback (device, FALSE, data->fr_user_data);
        }
}

static void
force_unmount (DevkitDisksDevice        *device,
               ForceRemovalCompleteFunc  callback,
               gpointer                  user_data)
{
        int n;
        char *argv[16];
        GError *error;

        n = 0;
        argv[n++] = "umount";
        /* on Linux, we only have lazy unmount for now */
        argv[n++] = "-l";
        argv[n++] = device->priv->info.device_mount_path;
        argv[n++] = NULL;

        error = NULL;
        if (!job_new (NULL,
                      "ForceUnmount",
                      FALSE,
                      device,
                      NULL,
                      argv,
                      NULL,
                      force_unmount_completed_cb,
                      force_unmount_data_new (device->priv->info.device_mount_path, callback, user_data),
                      (GDestroyNotify) force_unmount_data_unref)) {
                g_warning ("Couldn't spawn unmount for force unmounting: %s", error->message);
                g_error_free (error);
                if (callback != NULL)
                        callback (device, FALSE, user_data);
        }
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct {
        DevkitDisksDevice        *device;
        char                     *dm_name;
        ForceRemovalCompleteFunc  fr_callback;
        gpointer                  fr_user_data;
} ForceCryptoTeardownData;

static void
force_crypto_teardown_completed_cb (DBusGMethodInvocation *context,
                                    DevkitDisksDevice *device,
                                    PolKitCaller *pk_caller,
                                    gboolean job_was_cancelled,
                                    int status,
                                    const char *stderr,
                                    const char *stdout,
                                    gpointer user_data)
{
        ForceCryptoTeardownData *data = user_data;
        char *touch_str;

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                g_warning ("Successfully teared down crypto device %s", device->priv->info.device_file);

                /* TODO: when we add polling, this can probably be removed. I have no idea why hal's
                 *       poller don't cause the kernel to revalidate the (missing) media
                 */
                touch_str = g_strdup_printf ("touch %s", device->priv->info.device_file);
                g_spawn_command_line_sync (touch_str, NULL, NULL, NULL, NULL);
                g_free (touch_str);

                if (data->fr_callback != NULL)
                        data->fr_callback (device, TRUE, data->fr_user_data);
        } else {
                g_warning ("force crypto teardown failed: %s", stderr);
                if (data->fr_callback != NULL)
                        data->fr_callback (device, FALSE, data->fr_user_data);
        }
}

static ForceCryptoTeardownData *
force_crypto_teardown_data_new (DevkitDisksDevice        *device,
                                const char               *dm_name,
                                ForceRemovalCompleteFunc  fr_callback,
                                gpointer                  fr_user_data)
{
        ForceCryptoTeardownData *data;

        data = g_new0 (ForceCryptoTeardownData, 1);
        data->device = g_object_ref (device);
        data->dm_name = g_strdup (dm_name);
        data->fr_callback = fr_callback;
        data->fr_user_data = fr_user_data;
        return data;
}

static void
force_crypto_teardown_data_unref (ForceCryptoTeardownData *data)
{
        if (data->device != NULL)
                g_object_unref (data->device);
        g_free (data->dm_name);
        g_free (data);
}

static void
force_crypto_teardown_cleartext_done (DevkitDisksDevice *device,
                                      gboolean success,
                                      gpointer user_data)
{
        int n;
        char *argv[16];
        GError *error;
        ForceCryptoTeardownData *data = user_data;

        if (!success) {
                if (data->fr_callback != NULL)
                        data->fr_callback (data->device, FALSE, data->fr_user_data);

                force_crypto_teardown_data_unref (data);
                goto out;
        }

        /* ok, clear text device is out of the way; now tear it down */

        n = 0;
        argv[n++] = "cryptsetup";
        argv[n++] = "luksClose";
        argv[n++] = data->dm_name;
        argv[n++] = NULL;

        g_warning ("doing cryptsetup luksClose %s", data->dm_name);

        error = NULL;
        if (!job_new (NULL,
                      "ForceCryptoTeardown",
                      FALSE,
                      data->device,
                      NULL,
                      argv,
                      NULL,
                      force_crypto_teardown_completed_cb,
                      data,
                      (GDestroyNotify) force_crypto_teardown_data_unref)) {

                g_warning ("Couldn't spawn cryptsetup for force teardown: %s", error->message);
                g_error_free (error);
                if (data->fr_callback != NULL)
                        data->fr_callback (data->device, FALSE, data->fr_user_data);

                force_crypto_teardown_data_unref (data);
        }
out:
        ;
}

static void
force_crypto_teardown (DevkitDisksDevice        *device,
                       DevkitDisksDevice        *cleartext_device,
                       ForceRemovalCompleteFunc  callback,
                       gpointer                  user_data)
{
        /* first we gotta force remove the clear text device */
        force_removal (cleartext_device,
                       force_crypto_teardown_cleartext_done,
                       force_crypto_teardown_data_new (device,
                                                       cleartext_device->priv->info.dm_name,
                                                       callback,
                                                       user_data));
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct {
        DevkitDisksDevice        *component_device;
        ForceRemovalCompleteFunc  fr_callback;
        gpointer                  fr_user_data;
} ForceLinuxMdRemovalData;

static ForceLinuxMdRemovalData *
force_linux_md_removal_data_new (DevkitDisksDevice        *component_device,
                                 ForceRemovalCompleteFunc  fr_callback,
                                 gpointer                  fr_user_data)
{
        ForceLinuxMdRemovalData *data;

        data = g_new0 (ForceLinuxMdRemovalData, 1);
        data->component_device = g_object_ref (component_device);
        data->fr_callback = fr_callback;
        data->fr_user_data = fr_user_data;

        return data;
}

static void
force_linux_md_removal_data_unref (ForceLinuxMdRemovalData *data)
{
        g_object_unref (data->component_device);
        g_free (data);
}

static void
force_linux_md_removal_completed_cb (DBusGMethodInvocation *context,
                                     DevkitDisksDevice *device,
                                     PolKitCaller *pk_caller,
                                     gboolean job_was_cancelled,
                                     int status,
                                     const char *stderr,
                                     const char *stdout,
                                     gpointer user_data)
{
        ForceLinuxMdRemovalData *data = user_data;
        char *touch_str;

        /* either way, the kernel won't send change events so we simply poke the kernel to do that */
        devkit_device_emit_changed_to_kernel (device);

        if (WEXITSTATUS (status) == 0 && !job_was_cancelled) {

                g_warning ("Successfully force removed linux md component %s from array %s",
                           data->component_device->priv->info.device_file,
                           device->priv->info.device_file);

                /* TODO: when we add polling, this can probably be removed. I have no idea why hal's
                 *       poller don't cause the kernel to revalidate the (missing) media
                 */
                touch_str = g_strdup_printf ("touch %s", data->component_device->priv->info.device_file);
                g_spawn_command_line_sync (touch_str, NULL, NULL, NULL, NULL);
                g_free (touch_str);

                if (data->fr_callback != NULL)
                        data->fr_callback (data->component_device, TRUE, data->fr_user_data);
        } else {
                g_warning ("force linux_md_removal failed: %s", stderr);
                if (data->fr_callback != NULL)
                        data->fr_callback (data->component_device, FALSE, data->fr_user_data);
        }
}

static void
force_linux_md_removal (DevkitDisksDevice        *device,
                        DevkitDisksDevice        *linux_md_array_device,
                        ForceRemovalCompleteFunc  callback,
                        gpointer                  user_data)
{
        int n;
        char *argv[16];
        GError *error;
        char *s;
        char *state_path;
        int fd;

        /* We arrive here if we know that a backing device for an
         * array is irrevocably gone. There are two options from
         * here.
         *
         * - if it's raid1, raid4, raid5 or raid6 or raid10 we can
         *   sustain one or more drive failures. So check if we can
         *   go on.
         *
         * - if it's raid0 or linear or we can't sustain more drive
         *   failures we simply stop the array.
         *
         * Why do anything at all? Why deal with it? Because we want
         * to notify the user that a) his array is now running degraded;
         * or b) his array is no longer functioning. Letting him wait
         * until he tries accessing the data? Bad form.
         *
         * Unfortunately there's no way to stop a busy array.. but
         * at least we can manually fail a device in order to degrade
         * the array. The life-cycle management in drivers/md/md.c
         * simply just needs to be fixed.
         */

        /* Even though the kernel has removed the component block device, it simply won't
         * realize that fact until it tries to access it (which may take hours).. so
         * manually mark the device as faulty. We can't use mdadm's --fail option
         * because the device file is no more.
         *
         * TODO: I think this is a bug in the kernel; or maybe it's policy but that
         *       makes zero sense as the block device is gone; e.g. even the symlinks
         *       in holders/ of the array device points to a directory in sysfs that
         *       is gone. Need to investigate.
         */
        s = g_path_get_basename (device->priv->native_path);
        state_path = g_strdup_printf ("%s/md/dev-%s/state",
                                      linux_md_array_device->priv->native_path,
                                      s);
        g_warning ("state_path=%s", state_path);
        fd = open (state_path, O_WRONLY);
        if (fd < 0) {
                g_warning ("cannot open %s for writing", state_path);
        } else {
                char *buf = "faulty";
                if (write (fd, buf, 6) != 6) {
                        g_warning ("cannot write 'faulty' to %s", state_path);
                }
                close (fd);
        }
        g_free (s);
        g_free (state_path);

        /* ok, now that we're marked the component as faulty use mdadm to remove all failed devices */
        n = 0;
        argv[n++] = "mdadm";
        argv[n++] = "--manage";
        argv[n++] = linux_md_array_device->priv->info.device_file;
        argv[n++] = "--remove";
        argv[n++] = "failed";
        argv[n++] = NULL;

        g_warning ("doing mdadm --manage %s --remove failed", linux_md_array_device->priv->info.device_file);

        error = NULL;
        if (!job_new (NULL,
                      "ForceLinuxMdRemoval",
                      FALSE,
                      linux_md_array_device,
                      NULL,
                      argv,
                      NULL,
                      force_linux_md_removal_completed_cb,
                      force_linux_md_removal_data_new (device, callback, user_data),
                      (GDestroyNotify) force_linux_md_removal_data_unref)) {

                g_warning ("Couldn't spawn mdadm for force removal: %s", error->message);
                g_error_free (error);
                if (callback != NULL)
                        callback (device, FALSE, user_data);
        }
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
force_removal (DevkitDisksDevice        *device,
               ForceRemovalCompleteFunc  callback,
               gpointer                  user_data)
{
        g_warning ("in force removal for %s", device->priv->info.device_file);

        /* Device is going bye bye. If this device is
         *
         *  - Mounted by us, then forcibly unmount it.
         *
         *  - If it's a crypto device, check if there's cleartext
         *    companion. If so, tear it down if it was setup by us.
         *
         *  - A Linux MD component that is part of a running array,
         *    we need to fail it on the array and the remove it
         *    from the array
         *
         */
        if (device->priv->info.device_is_mounted && device->priv->info.device_mount_path != NULL) {
                gboolean remove_dir_on_unmount;

                if (mounts_file_has_device (device, NULL, &remove_dir_on_unmount)) {
                        g_warning ("Force unmounting device %s", device->priv->info.device_file);
                        force_unmount (device, callback, user_data);
                        goto pending;
                }
        }

        if (device->priv->info.id_usage != NULL && strcmp (device->priv->info.id_usage, "crypto") == 0) {
                GList *devices;
                GList *l;

                /* look for cleartext device  */
                devices = devkit_disks_daemon_local_get_all_devices (device->priv->daemon);
                for (l = devices; l != NULL; l = l->next) {
                        DevkitDisksDevice *d = DEVKIT_DISKS_DEVICE (l->data);
                        if (d->priv->info.device_is_crypto_cleartext &&
                            d->priv->info.crypto_cleartext_slave != NULL &&
                            strcmp (d->priv->info.crypto_cleartext_slave, device->priv->object_path) == 0) {

                                /* Check whether it is set up by us */
                                if (d->priv->info.dm_name != NULL &&
                                    g_str_has_prefix (d->priv->info.dm_name, "devkit-disks-luks-uuid-")) {

                                        g_warning ("Force crypto teardown device %s (cleartext %s)",
                                                   device->priv->info.device_file,
                                                   d->priv->info.device_file);

                                        /* Gotcha */
                                        force_crypto_teardown (device, d, callback, user_data);
                                        goto pending;
                                }
                        }
                }
        }

        if (device->priv->info.device_is_linux_md_component) {
                GList *devices;
                GList *l;

                /* look for the array */
                devices = devkit_disks_daemon_local_get_all_devices (device->priv->daemon);
                for (l = devices; l != NULL; l = l->next) {
                        DevkitDisksDevice *d = DEVKIT_DISKS_DEVICE (l->data);

                        if (d->priv->info.device_is_linux_md) {
                                /* TODO: check properly */

                                unsigned int n;

                                /* At least it's the same uuid.. but it may not be part of the running
                                 * array. Need to check linux_md_slaves to be sure.
                                 */

                                /* TODO: check if we (DeviceKit-disks) set up this array */

                                for (n = 0; n < d->priv->info.linux_md_slaves->len; n++) {
                                        if (strcmp (d->priv->info.linux_md_slaves->pdata[n],
                                                    device->priv->object_path) == 0) {

                                                g_warning ("Force linux md component teardown of %s from array %s",
                                                           device->priv->info.device_file,
                                                           d->priv->info.device_file);

                                                /* You're going to remove-from-array-city, buddy! */
                                                force_linux_md_removal (device, d, callback, user_data);
                                                goto pending;
                                        }
                                }
                        }
                }
        }

        /* nothing to force remove */
        if (callback != NULL)
                callback (device, TRUE, user_data);

pending:
        ;
}


/*--------------------------------------------------------------------------------------------------------------*/
