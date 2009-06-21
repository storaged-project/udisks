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

#define _GNU_SOURCE 1

/* ---------------------------------------------------------------------------------------------------- */
/* We might want these things to be configurable; for now they are hardcoded */

/* update ATA SMART every 30 minutes */
#define ATA_SMART_REFRESH_INTERVAL_SECONDS (30*60)

/* clean up old ATA SMART entries every 24 hours (and on startup) */
#define ATA_SMART_CLEANUP_INTERVAL_SECONDS (24*60*60)

/* delete entries older than five days */
#define ATA_SMART_KEEP_ENTRIES_SECONDS (5*24*60*60)

/* the poll frequency for IO activity when clients wants to spin down drives */
#define SPINDOWN_POLL_FREQ_SECONDS 5

/* ---------------------------------------------------------------------------------------------------- */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <net/if_arp.h>
#include <fcntl.h>
#include <signal.h>


#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gudev/gudev.h>

#include "devkit-disks-daemon.h"
#include "devkit-disks-device.h"
#include "devkit-disks-device-private.h"
#include "devkit-disks-mount-file.h"
#include "devkit-disks-mount.h"
#include "devkit-disks-mount-monitor.h"
#include "devkit-disks-poller.h"
#include "devkit-disks-inhibitor.h"
#include "devkit-disks-ata-smart-db.h"

#include "devkit-disks-daemon-glue.h"
#include "devkit-disks-marshal.h"


/*--------------------------------------------------------------------------------------------------------------*/

enum
{
        PROP_0,
        PROP_DAEMON_VERSION,
        PROP_DAEMON_IS_INHIBITED,
        PROP_SUPPORTS_LUKS_DEVICES,
        PROP_KNOWN_FILESYSTEMS,
};

enum
{
        DEVICE_ADDED_SIGNAL,
        DEVICE_REMOVED_SIGNAL,
        DEVICE_CHANGED_SIGNAL,
        DEVICE_JOB_CHANGED_SIGNAL,
        LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

struct DevkitDisksDaemonPrivate
{
        DBusGConnection         *system_bus_connection;
        DBusGProxy              *system_bus_proxy;

        PolkitAuthority         *authority;

        GUdevClient             *gudev_client;

	GIOChannel              *mdstat_channel;

        GHashTable              *map_dev_t_to_device;
        GHashTable              *map_device_file_to_device;
        GHashTable              *map_native_path_to_device;
        GHashTable              *map_object_path_to_device;

        DevkitDisksMountMonitor *mount_monitor;

        DevkitDisksAtaSmartDb   *ata_smart_db;

        guint                    ata_smart_refresh_timer_id;
        guint                    ata_smart_cleanup_timer_id;

        GList *polling_inhibitors;

        GList *inhibitors;

        guint spindown_timeout_id;
        GList *spindown_inhibitors;
};

static void     devkit_disks_daemon_class_init  (DevkitDisksDaemonClass *klass);
static void     devkit_disks_daemon_init        (DevkitDisksDaemon      *seat);
static void     devkit_disks_daemon_finalize    (GObject     *object);

static void     daemon_polling_inhibitor_disconnected_cb (DevkitDisksInhibitor *inhibitor,
                                                          DevkitDisksDaemon    *daemon);

static void     daemon_inhibitor_disconnected_cb (DevkitDisksInhibitor *inhibitor,
                                                  DevkitDisksDaemon    *daemon);

G_DEFINE_TYPE (DevkitDisksDaemon, devkit_disks_daemon, G_TYPE_OBJECT)

#define DEVKIT_DISKS_DAEMON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_DISKS_TYPE_DAEMON, DevkitDisksDaemonPrivate))

/*--------------------------------------------------------------------------------------------------------------*/

GQuark
devkit_disks_error_quark (void)
{
        static GQuark ret = 0;

        if (ret == 0) {
                ret = g_quark_from_static_string ("devkit_disks_error");
        }

        return ret;
}


#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
devkit_disks_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0)
        {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (DEVKIT_DISKS_ERROR_FAILED, "Failed"),
                        ENUM_ENTRY (DEVKIT_DISKS_ERROR_PERMISSION_DENIED, "PermissionDenied"),
                        ENUM_ENTRY (DEVKIT_DISKS_ERROR_INHIBITED, "Inhibited"),
                        ENUM_ENTRY (DEVKIT_DISKS_ERROR_BUSY, "Busy"),
                        ENUM_ENTRY (DEVKIT_DISKS_ERROR_CANCELLED, "Cancelled"),
                        ENUM_ENTRY (DEVKIT_DISKS_ERROR_INVALID_OPTION, "InvalidOption"),
                        ENUM_ENTRY (DEVKIT_DISKS_ERROR_NOT_SUPPORTED, "NotSupported"),
                        ENUM_ENTRY (DEVKIT_DISKS_ERROR_ATA_SMART_WOULD_WAKEUP, "AtaSmartWouldWakeup"),
                        { 0, 0, 0 }
                };
                g_assert (DEVKIT_DISKS_NUM_ERRORS == G_N_ELEMENTS (values) - 1);
                etype = g_enum_register_static ("DevkitDisksError", values);
        }
        return etype;
}


static GObject *
devkit_disks_daemon_constructor (GType                  type,
                                 guint                  n_construct_properties,
                                 GObjectConstructParam *construct_properties)
{
        DevkitDisksDaemon      *daemon;
        DevkitDisksDaemonClass *klass;

        klass = DEVKIT_DISKS_DAEMON_CLASS (g_type_class_peek (DEVKIT_DISKS_TYPE_DAEMON));

        daemon = DEVKIT_DISKS_DAEMON (
                G_OBJECT_CLASS (devkit_disks_daemon_parent_class)->constructor (type,
                                                                                n_construct_properties,
                                                                                construct_properties));
        return G_OBJECT (daemon);
}

/*--------------------------------------------------------------------------------------------------------------*/

#define KNOWN_FILESYSTEMS_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray",   \
                                                               G_TYPE_STRING, \
                                                               G_TYPE_STRING, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_UINT, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_INVALID))

static const DevkitDisksFilesystem known_file_systems[] = {
        {
                "vfat",         /* id */
                "FAT",          /* name */
                FALSE,          /* supports_unix_owners */
                TRUE,           /* can_mount */
                TRUE,           /* can_create */
                254,            /* max_label_len */
                TRUE,           /* supports_label_rename */
                FALSE,          /* supports_online_label_rename*/
                TRUE,           /* supports_fsck */
                FALSE,          /* supports_online_fsck */
                FALSE,          /* supports_resize_enlarge */
                FALSE,          /* supports_online_resize_enlarge */
                FALSE,          /* supports_resize_shrink */
                FALSE,          /* supports_online_resize_shrink */
        },
        {
                "ext2",         /* id */
                "Linux Ext2",   /* name */
                TRUE,           /* supports_unix_owners */
                TRUE,           /* can_mount */
                TRUE,           /* can_create */
                16,             /* max_label_len */
                TRUE,           /* supports_label_rename */
                TRUE,           /* supports_online_label_rename*/
                TRUE,           /* supports_fsck */
                FALSE,          /* supports_online_fsck */
                TRUE,           /* supports_resize_enlarge */
                TRUE,           /* supports_online_resize_enlarge */
                TRUE,           /* supports_resize_shrink */
                TRUE,           /* supports_online_resize_shrink */
        },
        {
                "ext3",         /* id */
                "Linux Ext3",   /* name */
                TRUE,           /* supports_unix_owners */
                TRUE,           /* can_mount */
                TRUE,           /* can_create */
                16,             /* max_label_len */
                TRUE,           /* supports_label_rename */
                TRUE,           /* supports_online_label_rename*/
                TRUE,           /* supports_fsck */
                FALSE,          /* supports_online_fsck */
                TRUE,           /* supports_resize_enlarge */
                TRUE,           /* supports_online_resize_enlarge */
                TRUE,           /* supports_resize_shrink */
                TRUE,           /* supports_online_resize_shrink */
        },
        {
                "ext4",         /* id */
                "Linux Ext4",   /* name */
                TRUE,           /* supports_unix_owners */
                TRUE,           /* can_mount */
                TRUE,           /* can_create */
                16,             /* max_label_len */
                TRUE,           /* supports_label_rename */
                TRUE,           /* supports_online_label_rename*/
                TRUE,           /* supports_fsck */
                FALSE,          /* supports_online_fsck */
                TRUE,           /* supports_resize_enlarge */
                TRUE,           /* supports_online_resize_enlarge */
                TRUE,           /* supports_resize_shrink */
                TRUE,           /* supports_online_resize_shrink */
        },
        {
                "xfs",          /* id */
                "XFS",          /* name */
                TRUE,           /* supports_unix_owners */
                TRUE,           /* can_mount */
                TRUE,           /* can_create */
                12,             /* max_label_len */
                TRUE,           /* supports_label_rename */
                FALSE,          /* supports_online_label_rename*/
                TRUE,           /* supports_fsck */
                FALSE,          /* supports_online_fsck */
                FALSE,          /* supports_resize_enlarge */
                TRUE,           /* supports_online_resize_enlarge */
                FALSE,          /* supports_resize_shrink */
                FALSE,          /* supports_online_resize_shrink */
        },
        {
                "ntfs",         /* id */
                "NTFS",         /* name */
                FALSE,          /* supports_unix_owners */
                TRUE,           /* can_mount */
                TRUE,           /* can_create */
                128,            /* max_label_len */
                TRUE,           /* supports_label_rename */
                FALSE,          /* supports_online_label_rename*/
                FALSE,          /* supports_fsck (TODO: hmm.. ntfsck doesn't support -a yet?) */
                FALSE,          /* supports_online_fsck */
                TRUE,           /* supports_resize_enlarge */
                FALSE,          /* supports_online_resize_enlarge */
                TRUE,           /* supports_resize_shrink */
                FALSE,          /* supports_online_resize_shrink */
        },
        {
                "swap",         /* id */
                "Swap Space",   /* name */
                FALSE,          /* supports_unix_owners */
                FALSE,          /* can_mount */
                TRUE,           /* can_create */
                0,              /* max_label_len (TODO: not actually true for new style swap areas) */
                FALSE,          /* supports_label_rename */
                FALSE,          /* supports_online_label_rename*/
                FALSE,          /* supports_fsck */
                FALSE,          /* supports_online_fsck */
                FALSE,          /* supports_resize_enlarge */
                FALSE,          /* supports_online_resize_enlarge */
                FALSE,          /* supports_resize_shrink */
                FALSE,          /* supports_online_resize_shrink */
        },
};

static const int num_known_file_systems = sizeof (known_file_systems) / sizeof (DevkitDisksFilesystem);

DevkitDisksAtaSmartDb *
devkit_disks_daemon_local_get_ata_smart_db (DevkitDisksDaemon *daemon)
{
        return daemon->priv->ata_smart_db;
}

const DevkitDisksFilesystem *
devkit_disks_daemon_local_get_fs_details (DevkitDisksDaemon  *daemon,
                                          const gchar        *filesystem_id)
{
        gint n;
        const DevkitDisksFilesystem *ret;

        ret = NULL;

        for (n = 0; n < num_known_file_systems; n++) {
                if (strcmp (known_file_systems[n].id, filesystem_id) == 0) {
                        ret = &known_file_systems[n];
                        break;
                }
        }

        return ret;
}

static GPtrArray *
get_known_filesystems (DevkitDisksDaemon *daemon)
{
        int n;
        GPtrArray *ret;

        ret = g_ptr_array_new ();
        for (n = 0; n < num_known_file_systems; n++) {
                GValue elem = {0};
                const DevkitDisksFilesystem *fs = known_file_systems + n;

                g_value_init (&elem, KNOWN_FILESYSTEMS_STRUCT_TYPE);
                g_value_take_boxed (&elem, dbus_g_type_specialized_construct (KNOWN_FILESYSTEMS_STRUCT_TYPE));
                dbus_g_type_struct_set (&elem,
                                        0, fs->id,
                                        1, fs->name,
                                        2, fs->supports_unix_owners,
                                        3, fs->can_mount,
                                        4, fs->can_create,
                                        5, fs->max_label_len,
                                        6, fs->supports_label_rename,
                                        7, fs->supports_online_label_rename,
                                        8, fs->supports_fsck,
                                        9, fs->supports_online_fsck,
                                        10, fs->supports_resize_enlarge,
                                        11, fs->supports_online_resize_enlarge,
                                        12, fs->supports_resize_shrink,
                                        13, fs->supports_online_resize_shrink,
                                        G_MAXUINT);
                g_ptr_array_add (ret, g_value_get_boxed (&elem));
        }

        return ret;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
get_property (GObject         *object,
              guint            prop_id,
              GValue          *value,
              GParamSpec      *pspec)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (object);
        GPtrArray *filesystems;

        switch (prop_id) {
        case PROP_DAEMON_VERSION:
                g_value_set_string (value, VERSION);
                break;

        case PROP_DAEMON_IS_INHIBITED:
                g_value_set_boolean (value, (daemon->priv->inhibitors != NULL));
                break;

        case PROP_SUPPORTS_LUKS_DEVICES:
                /* TODO: probably Linux only */
                g_value_set_boolean (value, TRUE);
                break;

        case PROP_KNOWN_FILESYSTEMS:
                filesystems = get_known_filesystems (daemon);
                g_value_set_boxed (value, filesystems);
                g_ptr_array_foreach (filesystems, (GFunc) g_value_array_free, NULL);
                g_ptr_array_free (filesystems, TRUE);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
devkit_disks_daemon_class_init (DevkitDisksDaemonClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = devkit_disks_daemon_constructor;
        object_class->finalize = devkit_disks_daemon_finalize;
        object_class->get_property = get_property;

        g_type_class_add_private (klass, sizeof (DevkitDisksDaemonPrivate));

        signals[DEVICE_ADDED_SIGNAL] =
                g_signal_new ("device-added",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

        signals[DEVICE_REMOVED_SIGNAL] =
                g_signal_new ("device-removed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

        signals[DEVICE_CHANGED_SIGNAL] =
                g_signal_new ("device-changed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

        signals[DEVICE_JOB_CHANGED_SIGNAL] =
                g_signal_new ("device-job-changed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              devkit_disks_marshal_VOID__BOXED_BOOLEAN_STRING_UINT_BOOLEAN_DOUBLE,
                              G_TYPE_NONE,
                              6,
                              DBUS_TYPE_G_OBJECT_PATH,
                              G_TYPE_BOOLEAN,
                              G_TYPE_STRING,
                              G_TYPE_UINT,
                              G_TYPE_BOOLEAN,
                              G_TYPE_DOUBLE);


        dbus_g_object_type_install_info (DEVKIT_DISKS_TYPE_DAEMON, &dbus_glib_devkit_disks_daemon_object_info);

        dbus_g_error_domain_register (DEVKIT_DISKS_ERROR,
                                      "org.freedesktop.DeviceKit.Disks.Error",
                                      DEVKIT_DISKS_TYPE_ERROR);

        g_object_class_install_property (
                object_class,
                PROP_DAEMON_VERSION,
                g_param_spec_string ("daemon-version", NULL, NULL, NULL, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_DAEMON_IS_INHIBITED,
                g_param_spec_boolean ("daemon-is-inhibited", NULL, NULL, FALSE, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_SUPPORTS_LUKS_DEVICES,
                g_param_spec_boolean ("supports-luks-devices", NULL, NULL, FALSE, G_PARAM_READABLE));

        g_object_class_install_property (
                object_class,
                PROP_KNOWN_FILESYSTEMS,
                g_param_spec_boxed ("known-filesystems", NULL, NULL,
                                    dbus_g_type_get_collection ("GPtrArray", KNOWN_FILESYSTEMS_STRUCT_TYPE),
                                    G_PARAM_READABLE));
}

static void
devkit_disks_daemon_init (DevkitDisksDaemon *daemon)
{
        daemon->priv = DEVKIT_DISKS_DAEMON_GET_PRIVATE (daemon);
        daemon->priv->map_dev_t_to_device = g_hash_table_new_full (g_direct_hash,
                                                                   g_direct_equal,
                                                                   NULL,
                                                                   g_object_unref);
        daemon->priv->map_device_file_to_device = g_hash_table_new_full (g_str_hash,
                                                                         g_str_equal,
                                                                         g_free,
                                                                         g_object_unref);
        daemon->priv->map_native_path_to_device = g_hash_table_new_full (g_str_hash,
                                                                         g_str_equal,
                                                                         g_free,
                                                                         g_object_unref);
        daemon->priv->map_object_path_to_device = g_hash_table_new_full (g_str_hash,
                                                                         g_str_equal,
                                                                         g_free,
                                                                         g_object_unref);
}

static void
devkit_disks_daemon_finalize (GObject *object)
{
        DevkitDisksDaemon *daemon;
        GList *l;

        g_return_if_fail (object != NULL);
        g_return_if_fail (DEVKIT_DISKS_IS_DAEMON (object));

        daemon = DEVKIT_DISKS_DAEMON (object);

        g_return_if_fail (daemon->priv != NULL);

        if (daemon->priv->authority != NULL)
                g_object_unref (daemon->priv->authority);

        if (daemon->priv->system_bus_proxy != NULL)
                g_object_unref (daemon->priv->system_bus_proxy);

        if (daemon->priv->system_bus_connection != NULL)
                dbus_g_connection_unref (daemon->priv->system_bus_connection);

        if (daemon->priv->mdstat_channel != NULL)
                g_io_channel_unref (daemon->priv->mdstat_channel);

        if (daemon->priv->map_dev_t_to_device != NULL) {
                g_hash_table_unref (daemon->priv->map_dev_t_to_device);
        }
        if (daemon->priv->map_device_file_to_device != NULL) {
                g_hash_table_unref (daemon->priv->map_device_file_to_device);
        }
        if (daemon->priv->map_native_path_to_device != NULL) {
                g_hash_table_unref (daemon->priv->map_native_path_to_device);
        }
        if (daemon->priv->map_object_path_to_device != NULL) {
                g_hash_table_unref (daemon->priv->map_object_path_to_device);
        }


        if (daemon->priv->mount_monitor != NULL) {
                g_object_unref (daemon->priv->mount_monitor);
        }

        if (daemon->priv->ata_smart_db != NULL) {
                g_object_unref (daemon->priv->ata_smart_db);
        }

        if (daemon->priv->gudev_client != NULL) {
                g_object_unref (daemon->priv->gudev_client);
        }

        if (daemon->priv->ata_smart_cleanup_timer_id > 0) {
                g_source_remove (daemon->priv->ata_smart_cleanup_timer_id);
        }

        if (daemon->priv->ata_smart_refresh_timer_id > 0) {
                g_source_remove (daemon->priv->ata_smart_refresh_timer_id);
        }

        if (daemon->priv->spindown_timeout_id > 0) {
                g_source_remove (daemon->priv->spindown_timeout_id);
        }

        for (l = daemon->priv->polling_inhibitors; l != NULL; l = l->next) {
                DevkitDisksInhibitor *inhibitor = DEVKIT_DISKS_INHIBITOR (l->data);
                g_signal_handlers_disconnect_by_func (inhibitor, daemon_polling_inhibitor_disconnected_cb, daemon);
                g_object_unref (inhibitor);
        }
        g_list_free (daemon->priv->polling_inhibitors);

        for (l = daemon->priv->inhibitors; l != NULL; l = l->next) {
                DevkitDisksInhibitor *inhibitor = DEVKIT_DISKS_INHIBITOR (l->data);
                g_signal_handlers_disconnect_by_func (inhibitor, daemon_inhibitor_disconnected_cb, daemon);
                g_object_unref (inhibitor);
        }
        g_list_free (daemon->priv->inhibitors);

        G_OBJECT_CLASS (devkit_disks_daemon_parent_class)->finalize (object);
}


void devkit_disks_inhibitor_name_owner_changed (DBusMessage *message);

static DBusHandlerResult
_filter (DBusConnection *connection, DBusMessage *message, void *user_data)
{
        //DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);
        const char *interface;

        interface = dbus_message_get_interface (message);

        if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
                /* for now, pass NameOwnerChanged to DevkitDisksInhibitor */
                devkit_disks_inhibitor_name_owner_changed (message);
        }

        /* other filters might want to process this message too */
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void device_add (DevkitDisksDaemon *daemon, GUdevDevice *d, gboolean emit_event);
static void device_remove (DevkitDisksDaemon *daemon, GUdevDevice *d);

static void
device_changed (DevkitDisksDaemon *daemon, GUdevDevice *d, gboolean synthesized)
{
        DevkitDisksDevice *device;
        const char *native_path;

        native_path = g_udev_device_get_sysfs_path (d);
        device = g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
        if (device != NULL) {
                g_print ("**** CHANGING %s\n", native_path);
                if (!devkit_disks_device_changed (device, d, synthesized)) {
                        g_print ("**** CHANGE TRIGGERED REMOVE %s\n", native_path);
                        device_remove (daemon, d);
                } else {
                        g_print ("**** CHANGED %s\n", native_path);
                        devkit_disks_daemon_local_update_poller (daemon);
                        devkit_disks_daemon_local_update_spindown (daemon);
                }
        } else {
                g_print ("**** TREATING CHANGE AS ADD %s\n", native_path);
                device_add (daemon, d, TRUE);
        }
}

void
devkit_disks_daemon_local_synthesize_changed (DevkitDisksDaemon *daemon,
                                              DevkitDisksDevice *device)
{
        g_object_ref (device->priv->d);
        device_changed (daemon, device->priv->d, TRUE);
        g_object_unref (device->priv->d);
}

void
devkit_disks_daemon_local_synthesize_changed_on_all_devices (DevkitDisksDaemon *daemon)
{
        GHashTableIter hash_iter;
        DevkitDisksDevice *device;

        g_hash_table_iter_init (&hash_iter, daemon->priv->map_object_path_to_device);
        while (g_hash_table_iter_next (&hash_iter, NULL, (gpointer) &device)) {
                devkit_disks_daemon_local_synthesize_changed (daemon, device);
        }
}

static void
device_add (DevkitDisksDaemon *daemon, GUdevDevice *d, gboolean emit_event)
{
        DevkitDisksDevice *device;
        const char *native_path;

        native_path = g_udev_device_get_sysfs_path (d);
        device = g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
        if (device != NULL) {
                /* we already have the device; treat as change event */
                g_print ("**** TREATING ADD AS CHANGE %s\n", native_path);
                device_changed (daemon, d, FALSE);
        } else {
                g_print ("**** ADDING %s\n", native_path);
                device = devkit_disks_device_new (daemon, d);

                if (device != NULL) {
                        g_hash_table_insert (daemon->priv->map_dev_t_to_device,
                                             GINT_TO_POINTER (devkit_disks_device_local_get_dev (device)),
                                             g_object_ref (device));
                        g_hash_table_insert (daemon->priv->map_device_file_to_device,
                                             g_strdup (devkit_disks_device_local_get_device_file (device)),
                                             g_object_ref (device));
                        g_hash_table_insert (daemon->priv->map_native_path_to_device,
                                             g_strdup (native_path),
                                             g_object_ref (device));
                        g_hash_table_insert (daemon->priv->map_object_path_to_device,
                                             g_strdup (devkit_disks_device_local_get_object_path (device)),
                                             g_object_ref (device));
                        g_print ("**** ADDED %s\n", native_path);
                        if (emit_event) {
                                const char *object_path;
                                object_path = devkit_disks_device_local_get_object_path (device);
                                g_print ("**** EMITTING ADDED for %s\n", device->priv->native_path);
                                g_signal_emit (daemon, signals[DEVICE_ADDED_SIGNAL], 0, object_path);
                        }
                        devkit_disks_daemon_local_update_poller (daemon);
                        devkit_disks_daemon_local_update_spindown (daemon);
                } else {
                        g_print ("**** IGNORING ADD %s\n", native_path);
                }
        }
}

static void
device_remove (DevkitDisksDaemon *daemon, GUdevDevice *d)
{
        DevkitDisksDevice *device;
        const char *native_path;

        native_path = g_udev_device_get_sysfs_path (d);
        device = g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
        if (device == NULL) {
                g_print ("**** IGNORING REMOVE %s\n", native_path);
        } else {
                g_print ("**** REMOVING %s\n", native_path);

                g_warn_if_fail (g_strcmp0 (native_path, device->priv->native_path) == 0);

                g_warn_if_fail (g_hash_table_remove (daemon->priv->map_native_path_to_device,
                                                     device->priv->native_path));
                g_warn_if_fail (g_hash_table_remove (daemon->priv->map_device_file_to_device,
                                                     device->priv->device_file));
                g_warn_if_fail (g_hash_table_remove (daemon->priv->map_object_path_to_device,
                                                     device->priv->object_path));
                g_warn_if_fail (g_hash_table_remove (daemon->priv->map_dev_t_to_device,
                                                     GINT_TO_POINTER (device->priv->dev)));

                devkit_disks_device_removed (device);

                g_print ("**** EMITTING REMOVED for %s\n", device->priv->native_path);
                g_signal_emit (daemon, signals[DEVICE_REMOVED_SIGNAL], 0,
                               devkit_disks_device_local_get_object_path (device));

                g_object_unref (device);

                devkit_disks_daemon_local_update_poller (daemon);
                devkit_disks_daemon_local_update_spindown (daemon);
        }
}

static void
on_uevent (GUdevClient  *client,
           const char   *action,
           GUdevDevice *device,
           gpointer      user_data)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);

        if (strcmp (action, "add") == 0) {
                device_add (daemon, device, TRUE);
        } else if (strcmp (action, "remove") == 0) {
                device_remove (daemon, device);
        } else if (strcmp (action, "change") == 0) {
                device_changed (daemon, device, FALSE);
        } else {
                g_print ("*** NOTE: unhandled action '%s' on %s\n", action, g_udev_device_get_sysfs_path (device));
        }
}

DevkitDisksDevice *
devkit_disks_daemon_local_find_by_dev (DevkitDisksDaemon *daemon, dev_t dev)
{
        return g_hash_table_lookup (daemon->priv->map_dev_t_to_device, GINT_TO_POINTER (dev));
}

DevkitDisksDevice *
devkit_disks_daemon_local_find_by_device_file (DevkitDisksDaemon *daemon, const char *device_file)
{
        return g_hash_table_lookup (daemon->priv->map_device_file_to_device, device_file);
}

DevkitDisksDevice *
devkit_disks_daemon_local_find_by_native_path (DevkitDisksDaemon *daemon, const char *native_path)
{
        return g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
}

DevkitDisksDevice *
devkit_disks_daemon_local_find_by_object_path (DevkitDisksDaemon *daemon,
                                               const char *object_path)
{
        return g_hash_table_lookup (daemon->priv->map_object_path_to_device, object_path);
}

GList *
devkit_disks_daemon_local_get_all_devices (DevkitDisksDaemon *daemon)
{
        return g_hash_table_get_values (daemon->priv->map_native_path_to_device);
}

static void
mount_removed (DevkitDisksMountMonitor *monitor,
               DevkitDisksMount        *mount,
               gpointer                 user_data)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);
        DevkitDisksDevice *device;

        device = g_hash_table_lookup (daemon->priv->map_dev_t_to_device,
                                      GINT_TO_POINTER (devkit_disks_mount_get_dev (mount)));
        if (device != NULL) {
                g_print ("**** UNMOUNTED %s\n", device->priv->native_path);
                devkit_disks_daemon_local_synthesize_changed (daemon, device);
        }
}

static void
mount_added (DevkitDisksMountMonitor *monitor,
             DevkitDisksMount        *mount,
             gpointer                 user_data)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);
        DevkitDisksDevice *device;

        device = g_hash_table_lookup (daemon->priv->map_dev_t_to_device,
                                      GINT_TO_POINTER (devkit_disks_mount_get_dev (mount)));
        if (device != NULL) {
                g_print ("**** MOUNTED %s\n", device->priv->native_path);
                devkit_disks_daemon_local_synthesize_changed (daemon, device);
        }
}

static gboolean
mdstat_changed_event (GIOChannel *channel, GIOCondition cond, gpointer user_data)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);
        GHashTableIter iter;
        char *str;
        gsize len;
        DevkitDisksDevice *device;
        char *native_path;
        GPtrArray *a;
        int n;

	if (cond & ~G_IO_PRI)
                goto out;

        if (g_io_channel_seek (channel, 0, G_SEEK_SET) != G_IO_ERROR_NONE) {
                g_warning ("Cannot seek in /proc/mdstat");
                goto out;
        }

        g_io_channel_read_to_end (channel, &str, &len, NULL);

        /* synthesize this as a change event on _all_ md devices; need to be careful; the change
         * event might remove the device and thus change the hash table (e.g. invalidate our iterator)
         */
        a = g_ptr_array_new ();
        g_hash_table_iter_init (&iter, daemon->priv->map_native_path_to_device);
        while (g_hash_table_iter_next (&iter, (gpointer *) &native_path, (gpointer *) &device)) {
                if (device->priv->device_is_linux_md) {
                        g_ptr_array_add (a, g_object_ref (device->priv->d));
                }
        }

        for (n = 0; n < (int) a->len; n++) {
                GUdevDevice *d = a->pdata[n];
                g_debug ("using change on /proc/mdstat to trigger change event on %s", native_path);
                device_changed (daemon, d, FALSE);
                g_object_unref (d);
        }

        g_ptr_array_free (a, TRUE);

out:
	return TRUE;
}

static gboolean
cleanup_ata_smart_data (DevkitDisksDaemon *daemon)
{
        time_t now;
        time_t cut_off_point;

        now = time (NULL);
        cut_off_point = now - ATA_SMART_KEEP_ENTRIES_SECONDS;

        g_print ("**** Deleting all ATA SMART data older than %d seconds\n", ATA_SMART_KEEP_ENTRIES_SECONDS);

        devkit_disks_ata_smart_db_delete_entries (daemon->priv->ata_smart_db,
                                                  cut_off_point);

        /* cleanup in another N seconds */
        daemon->priv->ata_smart_cleanup_timer_id = g_timeout_add_seconds (ATA_SMART_CLEANUP_INTERVAL_SECONDS,
                                                                          (GSourceFunc) cleanup_ata_smart_data,
                                                                          daemon);

        return FALSE;
}

static gboolean
refresh_ata_smart_data (DevkitDisksDaemon *daemon)
{
        DevkitDisksDevice *device;
        const char *native_path;
        GHashTableIter iter;

        g_hash_table_iter_init (&iter, daemon->priv->map_native_path_to_device);
        while (g_hash_table_iter_next (&iter, (gpointer *) &native_path, (gpointer *) &device)) {
                if (device->priv->drive_ata_smart_is_available) {
                        char *options[] = {"nowakeup", NULL};

                        g_print ("**** Refreshing ATA SMART data for %s\n", native_path);

                        devkit_disks_device_drive_ata_smart_refresh_data (device, options, NULL);
                }
        }


        /* update in another N seconds */
        daemon->priv->ata_smart_refresh_timer_id = g_timeout_add_seconds (ATA_SMART_REFRESH_INTERVAL_SECONDS,
                                                                          (GSourceFunc) refresh_ata_smart_data,
                                                                          daemon);

        return FALSE;
}


static gboolean
register_disks_daemon (DevkitDisksDaemon *daemon)
{
        DBusConnection *connection;
        DBusError dbus_error;
        GError *error = NULL;
        const char *subsystems[] = {"block", NULL};

        daemon->priv->authority = polkit_authority_get ();

        error = NULL;
        daemon->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (daemon->priv->system_bus_connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                goto error;
        }
        connection = dbus_g_connection_get_connection (daemon->priv->system_bus_connection);

        dbus_g_connection_register_g_object (daemon->priv->system_bus_connection, "/org/freedesktop/DeviceKit/Disks",
                                             G_OBJECT (daemon));

        daemon->priv->system_bus_proxy = dbus_g_proxy_new_for_name (daemon->priv->system_bus_connection,
                                                                    DBUS_SERVICE_DBUS,
                                                                    DBUS_PATH_DBUS,
                                                                    DBUS_INTERFACE_DBUS);
        dbus_error_init (&dbus_error);

        /* need to listen to NameOwnerChanged */
	dbus_bus_add_match (connection,
			    "type='signal'"
			    ",interface='"DBUS_INTERFACE_DBUS"'"
			    ",sender='"DBUS_SERVICE_DBUS"'"
			    ",member='NameOwnerChanged'",
			    &dbus_error);

        if (dbus_error_is_set (&dbus_error)) {
                g_warning ("Cannot add match rule: %s: %s", dbus_error.name, dbus_error.message);
                dbus_error_free (&dbus_error);
                goto error;
        }

        if (!dbus_connection_add_filter (connection,
                                         _filter,
                                         daemon,
                                         NULL)) {
                g_warning ("Cannot add D-Bus filter: %s: %s", dbus_error.name, dbus_error.message);
                goto error;
        }

        /* listen to /proc/mdstat for md changes
         *
	 * Linux 2.6.19 and onwards throws a POLLPRI event for every change
         *
         * TODO: Some people might have md as a module so if it's not there
         *       we need to set up a watch for it to appear when loaded and
         *       then poll it. Sigh.
	 */
	daemon->priv->mdstat_channel = g_io_channel_new_file ("/proc/mdstat", "r", &error);
	if (daemon->priv->mdstat_channel != NULL) {
		g_io_add_watch (daemon->priv->mdstat_channel, G_IO_PRI, mdstat_changed_event, daemon);
	} else {
                g_warning ("No /proc/mdstat file: %s", error->message);
                g_error_free (error);
                error = NULL;
	}

        /* connect to udev */
        daemon->priv->gudev_client = g_udev_client_new (subsystems);
        g_signal_connect (daemon->priv->gudev_client,
                          "uevent",
                          G_CALLBACK (on_uevent),
                          daemon);

        daemon->priv->mount_monitor = devkit_disks_mount_monitor_new ();
        g_signal_connect (daemon->priv->mount_monitor, "mount-added", (GCallback) mount_added, daemon);
        g_signal_connect (daemon->priv->mount_monitor, "mount-removed", (GCallback) mount_removed, daemon);

        daemon->priv->ata_smart_db = devkit_disks_ata_smart_db_new ();

        return TRUE;
error:
        return FALSE;
}


DevkitDisksDaemon *
devkit_disks_daemon_new (void)
{
        DevkitDisksDaemon *daemon;
        GList *devices;
        GList *l;
        DevkitDisksDevice *device;
        GHashTableIter device_iter;

        daemon = DEVKIT_DISKS_DAEMON (g_object_new (DEVKIT_DISKS_TYPE_DAEMON, NULL));

        if (!register_disks_daemon (DEVKIT_DISKS_DAEMON (daemon))) {
                g_object_unref (daemon);
                goto error;
        }

        devices = g_udev_client_query_by_subsystem (daemon->priv->gudev_client, "block");
        for (l = devices; l != NULL; l = l->next) {
                GUdevDevice *device = l->data;
                device_add (daemon, device, FALSE);
        }
        g_list_foreach (devices, (GFunc) g_object_unref, NULL);
        g_list_free (devices);

        /* now refresh data for all devices just added to get slave/holder relationships
         * properly initialized
         */
        g_hash_table_iter_init (&device_iter, daemon->priv->map_object_path_to_device);
        while (g_hash_table_iter_next (&device_iter, NULL, (gpointer) &device)) {
                devkit_disks_daemon_local_synthesize_changed (daemon, device);
        }

        /* clean stale directories in /media as well as stale
         * entries in /var/lib/DeviceKit-disks/mtab
         */
        l = g_hash_table_get_values (daemon->priv->map_native_path_to_device);
        devkit_disks_mount_file_clean_stale (l);
        g_list_free (l);

        /* clean up old ATA SMART data from the database */
        cleanup_ata_smart_data (daemon);

        /* set up timer for refreshing ATA SMART data - we don't want to refresh immediately because
         * when adding a device we also do this...
         */
        daemon->priv->ata_smart_refresh_timer_id = g_timeout_add_seconds (ATA_SMART_REFRESH_INTERVAL_SECONDS,
                                                                          (GSourceFunc) refresh_ata_smart_data,
                                                                          daemon);

        return daemon;

 error:
        return NULL;
}

DevkitDisksMountMonitor *
devkit_disks_daemon_local_get_mount_monitor (DevkitDisksDaemon *daemon)
{
        return daemon->priv->mount_monitor;
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

        error = g_error_new (DEVKIT_DISKS_ERROR,
                             error_code,
                             "%s", message);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        g_free (message);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
devkit_disks_daemon_local_get_uid (DevkitDisksDaemon     *daemon,
                                   uid_t                 *out_uid,
                                   DBusGMethodInvocation *context)
{
        gchar *sender;
        DBusError dbus_error;
        DBusConnection *connection;

        /* context can be NULL for things called by the daemon itself e.g. ATA SMART refresh */
        if (context == NULL) {
                *out_uid = 0;
                goto out;
        }

        /* TODO: right now this is synchronous and slow; when we switch to a better D-Bus
         *       binding a'la EggDBus there will be a utility class (with caching) where we
         *       can get this from
         */

        sender = dbus_g_method_get_sender (context);
        connection = dbus_g_connection_get_connection (daemon->priv->system_bus_connection);
        dbus_error_init (&dbus_error);
        *out_uid = dbus_bus_get_unix_user (connection,
                                           sender,
                                           &dbus_error);
        if (dbus_error_is_set (&dbus_error)) {
                *out_uid = 0;
                g_warning ("Cannot get uid for sender %s: %s: %s",
                           sender,
                           dbus_error.name,
                           dbus_error.message);
                dbus_error_free (&dbus_error);
        }
        g_free (sender);

 out:
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

void
devkit_disks_daemon_local_update_poller (DevkitDisksDaemon *daemon)
{
        GHashTableIter hash_iter;
        DevkitDisksDevice *device;
        GList *devices_to_poll;

        devices_to_poll = NULL;

        g_hash_table_iter_init (&hash_iter, daemon->priv->map_object_path_to_device);
        while (g_hash_table_iter_next (&hash_iter, NULL, (gpointer) &device)) {
                if (device->priv->device_is_media_change_detected &&
                    device->priv->device_is_media_change_detection_polling)
                        devices_to_poll = g_list_prepend (devices_to_poll, device);
        }

        devkit_disks_poller_set_devices (devices_to_poll);

        g_list_free (devices_to_poll);
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct
{
        gchar *action_id;
        DevkitDisksCheckAuthCallback check_auth_callback;
        DBusGMethodInvocation *context;
        DevkitDisksDaemon *daemon;
        DevkitDisksDevice *device;

        GCancellable *cancellable;
        guint num_user_data;
        gpointer *user_data_elements;
        GDestroyNotify *user_data_notifiers;

        DevkitDisksInhibitor *caller;
} CheckAuthData;

/* invoked when device is removed during authorization check */
static void
lca_device_went_away (gpointer user_data, GObject *where_the_object_was)
{
        CheckAuthData *data = user_data;

        g_object_weak_unref (G_OBJECT (data->device), lca_device_went_away, data);
        data->device = NULL;

        /* this will trigger lca_check_authorization_callback() */
        g_cancellable_cancel (data->cancellable);
}

/* invoked when caller disconnects during authorization check */
static void
lca_caller_disconnected_cb (DevkitDisksInhibitor *inhibitor,
                            gpointer              user_data)
{
        CheckAuthData *data = user_data;

        /* this will trigger lca_check_authorization_callback() */
        g_cancellable_cancel (data->cancellable);
}

static void
check_auth_data_free (CheckAuthData *data)
{
        guint n;

        g_free (data->action_id);
        g_object_unref (data->daemon);
        if (data->device != NULL)
                g_object_weak_unref (G_OBJECT (data->device), lca_device_went_away, data);
        g_object_unref (data->cancellable);
        for (n = 0; n < data->num_user_data; n++) {
                if (data->user_data_notifiers[n] != NULL)
                        data->user_data_notifiers[n] (data->user_data_elements[n]);
        }
        g_free (data->user_data_elements);
        g_free (data->user_data_notifiers);
        if (data->caller != NULL)
                g_object_unref (data->caller);
        g_free (data);
}

static void
lca_check_authorization_callback (PolkitAuthority *authority,
                                  GAsyncResult    *res,
                                  gpointer         user_data)
{
        CheckAuthData *data = user_data;
        PolkitAuthorizationResult *result;
        GError *error;
        gboolean is_authorized;

        is_authorized = FALSE;

        error = NULL;
        result = polkit_authority_check_authorization_finish (authority,
                                                              res,
                                                              &error);
        if (error != NULL) {
                throw_error (data->context,
                             DEVKIT_DISKS_ERROR_PERMISSION_DENIED,
                             "Not Authorized: %s", error->message);
                g_error_free (error);
        } else {
                if (polkit_authorization_result_get_is_authorized (result)) {
                        is_authorized = TRUE;
                } else if (polkit_authorization_result_get_is_challenge (result)) {
                        throw_error (data->context,
                                     DEVKIT_DISKS_ERROR_PERMISSION_DENIED,
                                     "Authentication is required");
                } else {
                        throw_error (data->context,
                                     DEVKIT_DISKS_ERROR_PERMISSION_DENIED,
                                     "Not Authorized");
                }
                g_object_unref (result);
        }

        if (is_authorized) {
                data->check_auth_callback (data->daemon,
                                           data->device,
                                           data->context,
                                           data->action_id,
                                           data->num_user_data,
                                           data->user_data_elements);
        }

        check_auth_data_free (data);
}

/* num_user_data param is followed by @num_user_data (gpointer, GDestroyNotify) pairs.. */
void
devkit_disks_daemon_local_check_auth (DevkitDisksDaemon            *daemon,
                                      DevkitDisksDevice            *device,
                                      const gchar                  *action_id,
                                      const gchar                  *operation,
                                      DevkitDisksCheckAuthCallback  check_auth_callback,
                                      DBusGMethodInvocation        *context,
                                      guint                         num_user_data,
                                      ...)
{
        CheckAuthData *data;
        va_list va_args;
        guint n;

        data = g_new0 (CheckAuthData, 1);
        data->action_id = g_strdup (action_id);
        data->check_auth_callback = check_auth_callback;
        data->context = context;
        data->daemon = g_object_ref (daemon);
        data->device = device;
        if (device != NULL)
                g_object_weak_ref (G_OBJECT (device), lca_device_went_away, data);

        data->cancellable = g_cancellable_new ();
        data->num_user_data = num_user_data;
        data->user_data_elements = g_new0 (gpointer, num_user_data);
        data->user_data_notifiers = g_new0 (GDestroyNotify, num_user_data);

        va_start (va_args, num_user_data);
        for (n = 0; n < num_user_data; n++) {
                data->user_data_elements[n] = va_arg (va_args, gpointer);
                data->user_data_notifiers[n] = va_arg (va_args, GDestroyNotify);
        }
        va_end (va_args);

        if (action_id != NULL) {
                PolkitSubject *subject;
                PolkitDetails *details;
                gchar partition_number_buf[32];

                /* Set details - see devkit-disks-polkit-action-lookup.c for where
                 * these key/value pairs are used
                 */
                details = polkit_details_new ();
                if (operation != NULL) {
                        polkit_details_insert (details,
                                               "operation",
                                               (gpointer) operation);
                }
                if (device != NULL) {
                        DevkitDisksDevice *drive;

                        polkit_details_insert (details,
                                               "unix-device",
                                               device->priv->device_file);
                        if (device->priv->device_file_by_id->len > 0)
                                polkit_details_insert (details,
                                                       "unix-device-by-id",
                                                       device->priv->device_file_by_id->pdata[0]);
                        if (device->priv->device_file_by_path->len > 0)
                                polkit_details_insert (details,
                                                       "unix-device-by-path",
                                                       device->priv->device_file_by_path->pdata[0]);

                        if (device->priv->device_is_drive) {
                                drive = device;
                        } else if (device->priv->device_is_partition) {
                                polkit_details_insert (details, "is-partition", "1");
                                g_snprintf (partition_number_buf,
                                            sizeof partition_number_buf,
                                            "%d",
                                            device->priv->partition_number);
                                polkit_details_insert (details, "partition-number", partition_number_buf);
                                drive = devkit_disks_daemon_local_find_by_object_path (device->priv->daemon,
                                                                                       device->priv->partition_slave);
                        } else {
                                drive = NULL;
                        }

                        if (drive != NULL) {
                                polkit_details_insert (details,
                                                       "drive-unix-device",
                                                       drive->priv->device_file);
                                if (drive->priv->device_file_by_id->len > 0)
                                        polkit_details_insert (details,
                                                               "drive-unix-device-by-id",
                                                               drive->priv->device_file_by_id->pdata[0]);
                                if (drive->priv->device_file_by_path->len > 0)
                                        polkit_details_insert (details,
                                                               "drive-unix-device-by-path",
                                                               drive->priv->device_file_by_path->pdata[0]);
                                if (drive->priv->drive_vendor != NULL)
                                        polkit_details_insert (details,
                                                               "drive-vendor",
                                                               drive->priv->drive_vendor);
                                if (drive->priv->drive_model != NULL)
                                        polkit_details_insert (details,
                                                               "drive-model",
                                                               drive->priv->drive_model);
                                if (drive->priv->drive_revision != NULL)
                                        polkit_details_insert (details,
                                                               "drive-revision",
                                                               drive->priv->drive_revision);
                                if (drive->priv->drive_serial != NULL)
                                        polkit_details_insert (details,
                                                               "drive-serial",
                                                               drive->priv->drive_serial);
                                if (drive->priv->drive_connection_interface != NULL)
                                        polkit_details_insert (details,
                                                               "drive-connection-interface",
                                                               drive->priv->drive_connection_interface);
                        }
                }

                subject = polkit_system_bus_name_new (dbus_g_method_get_sender (context));

                data->caller = devkit_disks_inhibitor_new (context);
                g_signal_connect (data->caller,
                                  "disconnected",
                                  G_CALLBACK (lca_caller_disconnected_cb),
                                  data);

                polkit_authority_check_authorization (daemon->priv->authority,
                                                      subject,
                                                      action_id,
                                                      details,
                                                      POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                                      data->cancellable,
                                                      (GAsyncReadyCallback) lca_check_authorization_callback,
                                                      data);

                g_object_unref (subject);
                g_object_unref (details);
        } else {
                data->check_auth_callback (data->daemon,
                                           data->device,
                                           data->context,
                                           data->action_id,
                                           data->num_user_data,
                                           data->user_data_elements);
                check_auth_data_free (data);
        }
}

/*--------------------------------------------------------------------------------------------------------------*/

#define SYSFS_BLOCK_STAT_MAX_SIZE 256

static void
issue_standby_child_watch_cb (GPid pid, int status, gpointer user_data)
{
        DevkitDisksDevice *device = DEVKIT_DISKS_DEVICE (user_data);

        if (WIFEXITED (status) && WEXITSTATUS (status) == 0) {
                //g_print ("**** NOTE: standby helper for %s completed successfully\n", device->priv->device_file);
        } else {
                g_warning ("standby helper for %s failed with exit code %d (if_exited=%d)\n",
                           device->priv->device_file,
                           WEXITSTATUS (status),
                           WIFEXITED (status));
        }

        g_object_unref (device);
}

static void
issue_standby_to_drive (DevkitDisksDevice *device)
{
        GError *error;
        GPid pid;
        gchar *argv[3] = {PACKAGE_LIBEXEC_DIR "/devkit-disks-helper-drive-standby",
                          NULL, /* device_file */
                          NULL};

        argv[1] = device->priv->device_file;

        error = NULL;
        if (!g_spawn_async_with_pipes (NULL,
                                       argv,
                                       NULL,
                                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                       NULL,
                                       NULL,
                                       &pid,
                                       NULL,
                                       NULL,
                                       NULL,
                                       &error)) {
                g_warning ("Error launching %s: %s", argv[0], error->message);
                g_error_free (error);
                goto out;
        }

        g_child_watch_add (pid, issue_standby_child_watch_cb, g_object_ref (device));

 out:
        ;
}

static gboolean
on_spindown_timeout (gpointer data)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (data);
        GHashTableIter hash_iter;
        DevkitDisksDevice *device;
        gchar stat_file_path[PATH_MAX];
        gchar buf[SYSFS_BLOCK_STAT_MAX_SIZE];
        int fd;
        time_t now;

        now = time (NULL);

        /* avoid allocating memory since this happens on a timer, e.g. all the FRAKKING time */

        g_hash_table_iter_init (&hash_iter, daemon->priv->map_object_path_to_device);
        while (g_hash_table_iter_next (&hash_iter, NULL, (gpointer) &device)) {

                if (!device->priv->device_is_drive ||
                    !device->priv->drive_can_spindown ||
                    device->priv->spindown_timeout == 0)
                        continue;

                g_snprintf (stat_file_path,
                            sizeof stat_file_path,
                            "%s/stat",
                            device->priv->native_path);
                fd = open (stat_file_path, O_RDONLY);
                if (fd == -1) {
                        g_warning ("Error opening %s: %m", stat_file_path);
                } else {
                        ssize_t bytes_read;
                        bytes_read = read (fd, buf, sizeof buf - 1);
                        close (fd);
                        if (bytes_read < 0) {
                                g_warning ("Error reading from %s: %m", stat_file_path);
                        } else {
                                buf[bytes_read] = '\0';
                                if (device->priv->spindown_last_stat == NULL) {
                                        /* handle initial time this is set */
                                        device->priv->spindown_last_stat = g_new0 (gchar, SYSFS_BLOCK_STAT_MAX_SIZE);
                                        memcpy (device->priv->spindown_last_stat, buf, bytes_read + 1);
                                        device->priv->spindown_last_stat_time = now;
                                        device->priv->spindown_have_issued_standby = FALSE;
                                } else {
                                        if (g_strcmp0 (buf, device->priv->spindown_last_stat) == 0) {
                                                gint64 idle_secs;

                                                idle_secs = now - device->priv->spindown_last_stat_time;
                                                /* same */
                                                if (idle_secs > device->priv->spindown_timeout &&
                                                    !device->priv->spindown_have_issued_standby) {
                                                        device->priv->spindown_have_issued_standby = TRUE;
                                                        g_print ("*** NOTE: issuing STANDBY IMMEDIATE for %s\n",
                                                                 device->priv->device_file);

                                                        /* and, now, DO IT! */
                                                        issue_standby_to_drive (device);
                                                }
                                        } else {
                                                /* differ */
                                                memcpy (device->priv->spindown_last_stat, buf, bytes_read + 1);
                                                device->priv->spindown_last_stat_time = now;
                                                device->priv->spindown_have_issued_standby = FALSE;
                                                //g_print ("*** NOTE: resetting spindown timeout on %s due "
                                                //         "to IO activity\n",
                                                //         device->priv->device_file);
                                        }
                                }
                        }
                }

        }

        /* keep timeout */
        return TRUE;
}

void
devkit_disks_daemon_local_update_spindown (DevkitDisksDaemon *daemon)
{
        GHashTableIter hash_iter;
        DevkitDisksDevice *device;
        GList *l;
        gboolean watch_for_spindown;

        watch_for_spindown = FALSE;
        g_hash_table_iter_init (&hash_iter, daemon->priv->map_object_path_to_device);
        while (g_hash_table_iter_next (&hash_iter, NULL, (gpointer) &device)) {
                gint spindown_timeout;

                if (!device->priv->device_is_drive || !device->priv->drive_can_spindown)
                        continue;

                spindown_timeout = G_MAXINT;
                if (device->priv->spindown_inhibitors == NULL && daemon->priv->spindown_inhibitors == NULL) {
                        /* no inhibitors */
                        device->priv->spindown_timeout = 0;
                        g_free (device->priv->spindown_last_stat);
                        device->priv->spindown_last_stat = NULL;
                        device->priv->spindown_last_stat_time = 0;
                        device->priv->spindown_have_issued_standby = FALSE;
                } else {
                        /* first go through all inhibitors on the device */
                        for (l = device->priv->spindown_inhibitors; l != NULL; l = l->next) {
                                DevkitDisksInhibitor *inhibitor = DEVKIT_DISKS_INHIBITOR (l->data);
                                gint spindown_timeout_inhibitor;

                                spindown_timeout_inhibitor = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (inhibitor),
                                                                                                 "spindown-timeout-seconds"));

                                if (spindown_timeout_inhibitor < spindown_timeout)
                                        spindown_timeout = spindown_timeout_inhibitor;
                        }

                        /* the all inhibitors on the daemon */
                        for (l = daemon->priv->spindown_inhibitors; l != NULL; l = l->next) {
                                DevkitDisksInhibitor *inhibitor = DEVKIT_DISKS_INHIBITOR (l->data);
                                gint spindown_timeout_inhibitor;

                                spindown_timeout_inhibitor = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (inhibitor),
                                                                                                 "spindown-timeout-seconds"));

                                if (spindown_timeout_inhibitor < spindown_timeout)
                                        spindown_timeout = spindown_timeout_inhibitor;
                        }

                        device->priv->spindown_timeout = spindown_timeout;
                        watch_for_spindown = TRUE;
                }
        }

        if (watch_for_spindown) {
                if (daemon->priv->spindown_timeout_id == 0) {
                        daemon->priv->spindown_timeout_id = g_timeout_add_seconds (SPINDOWN_POLL_FREQ_SECONDS,
                                                                                   on_spindown_timeout,
                                                                                   daemon);
                }
        } else {
                if (daemon->priv->spindown_timeout_id > 0) {
                        g_source_remove (daemon->priv->spindown_timeout_id);
                        daemon->priv->spindown_timeout_id = 0;
                }
        }
}

/*--------------------------------------------------------------------------------------------------------------*/
/* exported methods */

static void
enumerate_cb (gpointer key, gpointer value, gpointer user_data)
{
        DevkitDisksDevice *device = DEVKIT_DISKS_DEVICE (value);
        GPtrArray *object_paths = user_data;
        g_ptr_array_add (object_paths, g_strdup (devkit_disks_device_local_get_object_path (device)));
}

gboolean
devkit_disks_daemon_enumerate_devices (DevkitDisksDaemon     *daemon,
                                       DBusGMethodInvocation *context)
{
        GPtrArray *object_paths;

        /* TODO: enumerate in the right order wrt. dm/md..
         *
         * see also gdu_pool_new() in src/gdu-pool.c in g-d-u
         */

        object_paths = g_ptr_array_new ();
        g_hash_table_foreach (daemon->priv->map_native_path_to_device, enumerate_cb, object_paths);
        dbus_g_method_return (context, object_paths);
        g_ptr_array_foreach (object_paths, (GFunc) g_free, NULL);
        g_ptr_array_free (object_paths, TRUE);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
enumerate_device_files_cb (gpointer key, gpointer value, gpointer user_data)
{
        DevkitDisksDevice *device = DEVKIT_DISKS_DEVICE (value);
        GPtrArray *device_files = user_data;
        guint n;

        g_ptr_array_add (device_files, g_strdup (devkit_disks_device_local_get_device_file (device)));

        for (n = 0; n < device->priv->device_file_by_id->len; n++) {
                g_ptr_array_add (device_files,
                                 g_strdup (((gchar **) device->priv->device_file_by_id->pdata)[n]));
        }

        for (n = 0; n < device->priv->device_file_by_path->len; n++) {
                g_ptr_array_add (device_files,
                                 g_strdup (((gchar **) device->priv->device_file_by_path->pdata)[n]));
        }
}

gboolean
devkit_disks_daemon_enumerate_device_files (DevkitDisksDaemon     *daemon,
                                            DBusGMethodInvocation *context)
{
        GPtrArray *device_files;

        device_files = g_ptr_array_new ();
        g_hash_table_foreach (daemon->priv->map_native_path_to_device, enumerate_device_files_cb, device_files);
        g_ptr_array_add (device_files, NULL);
        dbus_g_method_return (context, device_files->pdata);
        g_ptr_array_foreach (device_files, (GFunc) g_free, NULL);
        g_ptr_array_free (device_files, TRUE);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
devkit_disks_daemon_find_device_by_device_file (DevkitDisksDaemon     *daemon,
                                                const char            *device_file,
                                                DBusGMethodInvocation *context)
{
        const char *object_path;
        DevkitDisksDevice *device;
        gchar canonical_device_file[PATH_MAX];

        realpath (device_file, canonical_device_file);

        object_path = NULL;

        device = devkit_disks_daemon_local_find_by_device_file (daemon, canonical_device_file);
        if (device != NULL) {
                object_path = devkit_disks_device_local_get_object_path (device);
                dbus_g_method_return (context, object_path);
        } else {
                throw_error (context,
                             DEVKIT_DISKS_ERROR_FAILED,
                             "No such device");
        }

        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
devkit_disks_daemon_find_device_by_major_minor (DevkitDisksDaemon     *daemon,
                                                gint64                  major,
                                                gint64                  minor,
                                                DBusGMethodInvocation *context)
{
        const char *object_path;
        DevkitDisksDevice *device;
        dev_t dev;

        dev = makedev (major, minor);

        object_path = NULL;

        device = devkit_disks_daemon_local_find_by_dev (daemon, dev);
        if (device != NULL) {
                object_path = devkit_disks_device_local_get_object_path (device);
                dbus_g_method_return (context, object_path);
        } else {
                throw_error (context,
                             DEVKIT_DISKS_ERROR_FAILED,
                             "No such device");
        }

        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
daemon_polling_inhibitor_disconnected_cb (DevkitDisksInhibitor *inhibitor,
                                          DevkitDisksDaemon    *daemon)
{
        daemon->priv->polling_inhibitors = g_list_remove (daemon->priv->polling_inhibitors, inhibitor);
        g_signal_handlers_disconnect_by_func (inhibitor, daemon_polling_inhibitor_disconnected_cb, daemon);
        g_object_unref (inhibitor);

        devkit_disks_daemon_local_synthesize_changed_on_all_devices (daemon);
        devkit_disks_daemon_local_update_poller (daemon);
}

gboolean
devkit_disks_daemon_local_has_polling_inhibitors (DevkitDisksDaemon *daemon)
{
        return daemon->priv->polling_inhibitors != NULL;
}

static void
devkit_disks_daemon_drive_inhibit_all_polling_authorized_cb (DevkitDisksDaemon     *daemon,
                                                             DevkitDisksDevice     *device,
                                                             DBusGMethodInvocation *context,
                                                             const gchar           *action_id,
                                                             guint                  num_user_data,
                                                             gpointer              *user_data_elements)
{
        gchar **options = user_data_elements[0];
        DevkitDisksInhibitor *inhibitor;
        guint n;

        for (n = 0; options[n] != NULL; n++) {
                const char *option = options[n];
                throw_error (context,
                             DEVKIT_DISKS_ERROR_INVALID_OPTION,
                             "Unknown option %s", option);
                goto out;
        }

        inhibitor = devkit_disks_inhibitor_new (context);

        daemon->priv->polling_inhibitors = g_list_prepend (daemon->priv->polling_inhibitors, inhibitor);
        g_signal_connect (inhibitor, "disconnected", G_CALLBACK (daemon_polling_inhibitor_disconnected_cb), daemon);

        devkit_disks_daemon_local_synthesize_changed_on_all_devices (daemon);
        devkit_disks_daemon_local_update_poller (daemon);

        dbus_g_method_return (context, devkit_disks_inhibitor_get_cookie (inhibitor));

out:
        ;
}

gboolean
devkit_disks_daemon_drive_inhibit_all_polling (DevkitDisksDaemon     *daemon,
                                               char                 **options,
                                               DBusGMethodInvocation *context)
{
        devkit_disks_daemon_local_check_auth (daemon,
                                              NULL,
                                              "org.freedesktop.devicekit.disks.inhibit-polling",
                                              "InhibitAllPolling",
                                              devkit_disks_daemon_drive_inhibit_all_polling_authorized_cb,
                                              context,
                                              1,
                                              g_strdupv (options), g_strfreev);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
devkit_disks_daemon_drive_uninhibit_all_polling (DevkitDisksDaemon     *daemon,
                                                 char                  *cookie,
                                                 DBusGMethodInvocation *context)
{
        const gchar *sender;
        DevkitDisksInhibitor *inhibitor;
        GList *l;

        sender = dbus_g_method_get_sender (context);

        inhibitor = NULL;
        for (l = daemon->priv->polling_inhibitors; l != NULL; l = l->next) {
                DevkitDisksInhibitor *i = DEVKIT_DISKS_INHIBITOR (l->data);

                if (g_strcmp0 (devkit_disks_inhibitor_get_unique_dbus_name (i), sender) == 0 &&
                    g_strcmp0 (devkit_disks_inhibitor_get_cookie (i), cookie) == 0) {
                        inhibitor = i;
                        break;
                }
        }

        if (inhibitor == NULL) {
                throw_error (context,
                             DEVKIT_DISKS_ERROR_FAILED,
                             "No such inhibitor");
                goto out;
        }

        daemon->priv->polling_inhibitors = g_list_remove (daemon->priv->polling_inhibitors, inhibitor);
        g_object_unref (inhibitor);

        devkit_disks_daemon_local_synthesize_changed_on_all_devices (daemon);
        devkit_disks_daemon_local_update_poller (daemon);

        dbus_g_method_return (context);

 out:
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
daemon_inhibitor_disconnected_cb (DevkitDisksInhibitor *inhibitor,
                                  DevkitDisksDaemon    *daemon)
{
        daemon->priv->inhibitors = g_list_remove (daemon->priv->inhibitors, inhibitor);
        g_signal_handlers_disconnect_by_func (inhibitor, daemon_inhibitor_disconnected_cb, daemon);
        g_object_unref (inhibitor);
}

gboolean
devkit_disks_daemon_local_has_inhibitors (DevkitDisksDaemon *daemon)
{
        return daemon->priv->inhibitors != NULL;
}

gboolean
devkit_disks_daemon_inhibit (DevkitDisksDaemon     *daemon,
                             DBusGMethodInvocation *context)
{
        DevkitDisksInhibitor *inhibitor;
        uid_t uid;

        if (!devkit_disks_daemon_local_get_uid (daemon, &uid, context))
                goto out;

        if (uid != 0) {
                throw_error (context,
                             DEVKIT_DISKS_ERROR_FAILED,
                             "Only uid 0 is authorized to inhibit the daemon");
                goto out;
        }

        inhibitor = devkit_disks_inhibitor_new (context);

        daemon->priv->inhibitors = g_list_prepend (daemon->priv->inhibitors, inhibitor);
        g_signal_connect (inhibitor, "disconnected", G_CALLBACK (daemon_inhibitor_disconnected_cb), daemon);

        dbus_g_method_return (context, devkit_disks_inhibitor_get_cookie (inhibitor));

out:
        return TRUE;
}


/*--------------------------------------------------------------------------------------------------------------*/

gboolean
devkit_disks_daemon_uninhibit (DevkitDisksDaemon     *daemon,
                               char                  *cookie,
                               DBusGMethodInvocation *context)
{
        const gchar *sender;
        DevkitDisksInhibitor *inhibitor;
        GList *l;

        sender = dbus_g_method_get_sender (context);

        inhibitor = NULL;
        for (l = daemon->priv->inhibitors; l != NULL; l = l->next) {
                DevkitDisksInhibitor *i = DEVKIT_DISKS_INHIBITOR (l->data);

                if (g_strcmp0 (devkit_disks_inhibitor_get_unique_dbus_name (i), sender) == 0 &&
                    g_strcmp0 (devkit_disks_inhibitor_get_cookie (i), cookie) == 0) {
                        inhibitor = i;
                        break;
                }
        }

        if (inhibitor == NULL) {
                throw_error (context,
                             DEVKIT_DISKS_ERROR_FAILED,
                             "No such inhibitor");
                goto out;
        }

        daemon->priv->inhibitors = g_list_remove (daemon->priv->inhibitors, inhibitor);
        g_object_unref (inhibitor);

        dbus_g_method_return (context);

 out:
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/


static void
daemon_spindown_inhibitor_disconnected_cb (DevkitDisksInhibitor *inhibitor,
                                           DevkitDisksDaemon     *daemon)
{
        daemon->priv->spindown_inhibitors = g_list_remove (daemon->priv->spindown_inhibitors, inhibitor);
        g_signal_handlers_disconnect_by_func (inhibitor, daemon_spindown_inhibitor_disconnected_cb, daemon);
        g_object_unref (inhibitor);

        devkit_disks_daemon_local_update_spindown (daemon);
}

static void
devkit_disks_daemon_drive_set_all_spindown_timeouts_authorized_cb (DevkitDisksDaemon     *daemon,
                                                                   DevkitDisksDevice     *device,
                                                                   DBusGMethodInvocation *context,
                                                                   const gchar           *action_id,
                                                                   guint                  num_user_data,
                                                                   gpointer              *user_data_elements)
{
        gint timeout_seconds = GPOINTER_TO_INT (user_data_elements[0]);
        gchar **options = user_data_elements[1];
        DevkitDisksInhibitor *inhibitor;
        guint n;

        if (timeout_seconds < 1) {
                throw_error (context, DEVKIT_DISKS_ERROR_FAILED,
                             "Timeout seconds must be at least 1");
                goto out;
        }

        for (n = 0; options[n] != NULL; n++) {
                const char *option = options[n];
                throw_error (context,
                             DEVKIT_DISKS_ERROR_INVALID_OPTION,
                             "Unknown option %s", option);
                goto out;
        }

        inhibitor = devkit_disks_inhibitor_new (context);

        g_object_set_data (G_OBJECT (inhibitor), "spindown-timeout-seconds", GINT_TO_POINTER (timeout_seconds));

        daemon->priv->spindown_inhibitors = g_list_prepend (daemon->priv->spindown_inhibitors, inhibitor);
        g_signal_connect (inhibitor, "disconnected", G_CALLBACK (daemon_spindown_inhibitor_disconnected_cb), daemon);

        devkit_disks_daemon_local_update_spindown (daemon);

        dbus_g_method_return (context, devkit_disks_inhibitor_get_cookie (inhibitor));

out:
        ;
}

gboolean
devkit_disks_daemon_drive_set_all_spindown_timeouts (DevkitDisksDaemon     *daemon,
                                                     int                    timeout_seconds,
                                                     char                 **options,
                                                     DBusGMethodInvocation *context)
{
        if (timeout_seconds < 1) {
                throw_error (context, DEVKIT_DISKS_ERROR_FAILED,
                             "Timeout seconds must be at least 1");
                goto out;
        }

        devkit_disks_daemon_local_check_auth (daemon,
                                              NULL,
                                              "org.freedesktop.devicekit.disks.drive-set-spindown",
                                              "DriveSetAllSpindownTimeouts",
                                              devkit_disks_daemon_drive_set_all_spindown_timeouts_authorized_cb,
                                              context,
                                              2,
                                              GINT_TO_POINTER (timeout_seconds), NULL,
                                              g_strdupv (options), g_strfreev);


 out:
        return TRUE;
}

gboolean
devkit_disks_daemon_drive_unset_all_spindown_timeouts (DevkitDisksDaemon     *daemon,
                                                       char                  *cookie,
                                                       DBusGMethodInvocation *context)
{
        const gchar *sender;
        DevkitDisksInhibitor *inhibitor;
        GList *l;

        sender = dbus_g_method_get_sender (context);

        inhibitor = NULL;
        for (l = daemon->priv->spindown_inhibitors; l != NULL; l = l->next) {
                DevkitDisksInhibitor *i = DEVKIT_DISKS_INHIBITOR (l->data);

                if (g_strcmp0 (devkit_disks_inhibitor_get_unique_dbus_name (i), sender) == 0 &&
                    g_strcmp0 (devkit_disks_inhibitor_get_cookie (i), cookie) == 0) {
                        inhibitor = i;
                        break;
                }
        }

        if (inhibitor == NULL) {
                throw_error (context,
                             DEVKIT_DISKS_ERROR_FAILED,
                             "No such spindown configurator");
                goto out;
        }

        daemon->priv->spindown_inhibitors = g_list_remove (daemon->priv->spindown_inhibitors, inhibitor);
        g_object_unref (inhibitor);

        devkit_disks_daemon_local_update_spindown (daemon);

        dbus_g_method_return (context);

 out:
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/
