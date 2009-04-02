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
#include <devkit-gobject/devkit-gobject.h>

#include "devkit-disks-daemon.h"
#include "devkit-disks-device.h"
#include "devkit-disks-device-private.h"
#include "devkit-disks-mount-file.h"
#include "devkit-disks-mount.h"
#include "devkit-disks-mount-monitor.h"
#include "devkit-disks-poller.h"
#include "devkit-disks-inhibitor.h"

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
        PolKitContext           *pk_context;
        PolKitTracker           *pk_tracker;

        DevkitClient            *devkit_client;

	GIOChannel              *mdstat_channel;

        GHashTable              *map_dev_t_to_device;
        GHashTable              *map_device_file_to_device;
        GHashTable              *map_native_path_to_device;
        GHashTable              *map_object_path_to_device;

        DevkitDisksMountMonitor *mount_monitor;

        guint                    ata_smart_refresh_timer_id;

        GList *polling_inhibitors;

        GList *inhibitors;
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
                                                                   NULL);
        daemon->priv->map_device_file_to_device = g_hash_table_new_full (g_str_hash,
                                                                         g_str_equal,
                                                                         g_free,
                                                                         NULL);
        daemon->priv->map_native_path_to_device = g_hash_table_new_full (g_str_hash,
                                                                         g_str_equal,
                                                                         g_free,
                                                                         NULL);
        daemon->priv->map_object_path_to_device = g_hash_table_new_full (g_str_hash,
                                                                         g_str_equal,
                                                                         g_free,
                                                                         NULL);
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

        if (daemon->priv->pk_context != NULL)
                polkit_context_unref (daemon->priv->pk_context);

        if (daemon->priv->pk_tracker != NULL)
                polkit_tracker_unref (daemon->priv->pk_tracker);

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

        if (daemon->priv->devkit_client != NULL) {
                g_object_unref (daemon->priv->devkit_client);
        }

        if (daemon->priv->ata_smart_refresh_timer_id > 0) {
                g_source_remove (daemon->priv->ata_smart_refresh_timer_id);
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

static gboolean
pk_io_watch_have_data (GIOChannel *channel, GIOCondition condition, gpointer user_data)
{
        int fd;
        PolKitContext *pk_context = user_data;
        fd = g_io_channel_unix_get_fd (channel);
        polkit_context_io_func (pk_context, fd);
        return TRUE;
}

static int
pk_io_add_watch (PolKitContext *pk_context, int fd)
{
        guint id = 0;
        GIOChannel *channel;
        channel = g_io_channel_unix_new (fd);
        if (channel == NULL)
                goto out;
        id = g_io_add_watch (channel, G_IO_IN, pk_io_watch_have_data, pk_context);
        if (id == 0) {
                g_io_channel_unref (channel);
                goto out;
        }
        g_io_channel_unref (channel);
out:
        return id;
}

static void
pk_io_remove_watch (PolKitContext *pk_context, int watch_id)
{
        g_source_remove (watch_id);
}

void devkit_disks_inhibitor_name_owner_changed (DBusMessage *message);

static DBusHandlerResult
_filter (DBusConnection *connection, DBusMessage *message, void *user_data)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);
        const char *interface;

        interface = dbus_message_get_interface (message);

        if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
                /* pass NameOwnerChanged signals from the bus to PolKitTracker */
                polkit_tracker_dbus_func (daemon->priv->pk_tracker, message);

                /* for now, pass NameOwnerChanged to DevkitDisksInhibitor */
                devkit_disks_inhibitor_name_owner_changed (message);
        }

        if (interface != NULL && g_str_has_prefix (interface, "org.freedesktop.ConsoleKit")) {
                /* pass ConsoleKit signals to PolKitTracker */
                polkit_tracker_dbus_func (daemon->priv->pk_tracker, message);
        }

        /* other filters might want to process this message too */
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
device_went_away_remove_quiet_cb (gpointer key, gpointer value, gpointer user_data)
{
        if (value == user_data) {
                return TRUE;
        }
        return FALSE;
}

static gboolean
device_went_away_remove_cb (gpointer key, gpointer value, gpointer user_data)
{
        if (value == user_data) {
                g_print ("**** REMOVED %s\n", (char *) key);
                return TRUE;
        }
        return FALSE;
}

static void
device_went_away (gpointer user_data, GObject *where_the_object_was)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);

        g_hash_table_foreach_remove (daemon->priv->map_device_file_to_device,
                                     device_went_away_remove_cb,
                                     where_the_object_was);
        g_hash_table_foreach_remove (daemon->priv->map_dev_t_to_device,
                                     device_went_away_remove_quiet_cb,
                                     where_the_object_was);
        g_hash_table_foreach_remove (daemon->priv->map_native_path_to_device,
                                     device_went_away_remove_quiet_cb,
                                     where_the_object_was);
        g_hash_table_foreach_remove (daemon->priv->map_object_path_to_device,
                                     device_went_away_remove_quiet_cb,
                                     where_the_object_was);
}

static void device_add (DevkitDisksDaemon *daemon, DevkitDevice *d, gboolean emit_event);
static void device_remove (DevkitDisksDaemon *daemon, DevkitDevice *d);

static void
device_changed (DevkitDisksDaemon *daemon, DevkitDevice *d, gboolean synthesized)
{
        DevkitDisksDevice *device;
        const char *native_path;

        native_path = devkit_device_get_native_path (d);
        device = g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
        if (device != NULL) {
                g_print ("**** CHANGING %s\n", native_path);
                if (!devkit_disks_device_changed (device, d, synthesized)) {
                        g_print ("**** CHANGE TRIGGERED REMOVE %s\n", native_path);
                        device_remove (daemon, d);
                } else {
                        g_print ("**** CHANGED %s\n", native_path);
                        devkit_disks_daemon_local_update_poller (daemon);
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
device_add (DevkitDisksDaemon *daemon, DevkitDevice *d, gboolean emit_event)
{
        DevkitDisksDevice *device;
        const char *native_path;

        native_path = devkit_device_get_native_path (d);
        device = g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
        if (device != NULL) {
                /* we already have the device; treat as change event */
                g_print ("**** TREATING ADD AS CHANGE %s\n", native_path);
                device_changed (daemon, d, FALSE);
        } else {
                g_print ("**** ADDING %s\n", native_path);
                device = devkit_disks_device_new (daemon, d);

                if (device != NULL) {
                        /* only take a weak ref; the device will stay on the bus until
                         * it's unreffed. So if we ref it, it'll never go away. Stupid
                         * dbus-glib, no cookie for you.
                         */
                        g_object_weak_ref (G_OBJECT (device), device_went_away, daemon);
                        g_hash_table_insert (daemon->priv->map_dev_t_to_device,
                                             GINT_TO_POINTER (devkit_disks_device_local_get_dev (device)),
                                             device);
                        g_hash_table_insert (daemon->priv->map_device_file_to_device,
                                             g_strdup (devkit_disks_device_local_get_device_file (device)),
                                             device);
                        g_hash_table_insert (daemon->priv->map_native_path_to_device,
                                             g_strdup (native_path),
                                             device);
                        g_hash_table_insert (daemon->priv->map_object_path_to_device,
                                             g_strdup (devkit_disks_device_local_get_object_path (device)),
                                             device);
                        g_print ("**** ADDED %s\n", native_path);
                        if (emit_event) {
                                const char *object_path;
                                object_path = devkit_disks_device_local_get_object_path (device);
                                g_signal_emit (daemon, signals[DEVICE_ADDED_SIGNAL], 0, object_path);
                        }
                        devkit_disks_daemon_local_update_poller (daemon);
                } else {
                        g_print ("**** IGNORING ADD %s\n", native_path);
                }
        }
}

static void
device_remove (DevkitDisksDaemon *daemon, DevkitDevice *d)
{
        DevkitDisksDevice *device;
        const char *native_path;

        native_path = devkit_device_get_native_path (d);
        device = g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
        if (device == NULL) {
                g_print ("**** IGNORING REMOVE %s\n", native_path);
        } else {
                g_print ("**** REMOVING %s\n", native_path);
                devkit_disks_device_removed (device);
                g_signal_emit (daemon, signals[DEVICE_REMOVED_SIGNAL], 0,
                               devkit_disks_device_local_get_object_path (device));
                g_object_unref (device);
                devkit_disks_daemon_local_update_poller (daemon);
        }
}

static void
device_event_signal_handler (DevkitClient *client,
                             const char   *action,
                             DevkitDevice *device,
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
                g_warning ("unhandled action '%s' on %s", action, devkit_device_get_native_path (device));
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
                DevkitDevice *d = a->pdata[n];
                g_debug ("using change on /proc/mdstat to trigger change event on %s", native_path);
                device_changed (daemon, d, FALSE);
                g_object_unref (d);
        }

        g_ptr_array_free (a, TRUE);

out:
	return TRUE;
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

                        g_debug ("refreshing ATA SMART data for %s", native_path);

                        devkit_disks_device_drive_ata_smart_refresh_data (device, options, NULL);
                }
        }

        /* update in another 30 minutes */
        daemon->priv->ata_smart_refresh_timer_id = g_timeout_add_seconds (30 * 60,
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

        daemon->priv->pk_context = polkit_context_new ();
        polkit_context_set_io_watch_functions (daemon->priv->pk_context, pk_io_add_watch, pk_io_remove_watch);
        if (!polkit_context_init (daemon->priv->pk_context, NULL)) {
                g_critical ("cannot initialize libpolkit");
                goto error;
        }

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

        daemon->priv->pk_tracker = polkit_tracker_new ();
        polkit_tracker_set_system_bus_connection (daemon->priv->pk_tracker, connection);
        polkit_tracker_init (daemon->priv->pk_tracker);

        dbus_g_connection_register_g_object (daemon->priv->system_bus_connection, "/",
                                             G_OBJECT (daemon));

        daemon->priv->system_bus_proxy = dbus_g_proxy_new_for_name (daemon->priv->system_bus_connection,
                                                                      DBUS_SERVICE_DBUS,
                                                                      DBUS_PATH_DBUS,
                                                                      DBUS_INTERFACE_DBUS);

        /* TODO FIXME: I'm pretty sure dbus-glib blows in a way that
         * we can't say we're interested in all signals from all
         * members on all interfaces for a given service... So we do
         * this..
         */

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

        /* need to listen to ConsoleKit signals */
	dbus_bus_add_match (connection,
			    "type='signal',sender='org.freedesktop.ConsoleKit'",
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

        /* connect to the DeviceKit daemon */
        daemon->priv->devkit_client = devkit_client_new (subsystems);
        if (!devkit_client_connect (daemon->priv->devkit_client, &error)) {
		g_warning ("Couldn't open connection to DeviceKit daemon: %s", error->message);
                g_error_free (error);
                goto error;
        }
        g_signal_connect (daemon->priv->devkit_client, "device-event",
                          G_CALLBACK (device_event_signal_handler), daemon);

        daemon->priv->mount_monitor = devkit_disks_mount_monitor_new ();
        g_signal_connect (daemon->priv->mount_monitor, "mount-added", (GCallback) mount_added, daemon);
        g_signal_connect (daemon->priv->mount_monitor, "mount-removed", (GCallback) mount_removed, daemon);

        return TRUE;
error:
        return FALSE;
}


DevkitDisksDaemon *
devkit_disks_daemon_new (void)
{
        DevkitDisksDaemon *daemon;
        GError *error = NULL;
        GList *devices;
        GList *l;
        DevkitDisksDevice *device;
        GHashTableIter device_iter;
        const char *subsystems[] = {"block", NULL};

        daemon = DEVKIT_DISKS_DAEMON (g_object_new (DEVKIT_DISKS_TYPE_DAEMON, NULL));



        if (!register_disks_daemon (DEVKIT_DISKS_DAEMON (daemon))) {
                g_object_unref (daemon);
                goto error;
        }

        devices = devkit_client_enumerate_by_subsystem (daemon->priv->devkit_client,
                                                         subsystems,
                                                         &error);
        if (error != NULL) {
                g_warning ("Cannot enumerate devices: %s", error->message);
                g_error_free (error);
                g_object_unref (daemon);
                goto error;
        }
        for (l = devices; l != NULL; l = l->next) {
                DevkitDevice *device = l->data;
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

        /* set up timer for ATA smart data refresh */
        daemon->priv->ata_smart_refresh_timer_id = g_timeout_add_seconds (30 * 60,
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

PolKitCaller *
devkit_disks_damon_local_get_caller_for_context (DevkitDisksDaemon *daemon,
                                                 DBusGMethodInvocation *context)
{
        const char *sender;
        GError *error;
        DBusError dbus_error;
        PolKitCaller *pk_caller;

        sender = dbus_g_method_get_sender (context);
        dbus_error_init (&dbus_error);
        pk_caller = polkit_tracker_get_caller_from_dbus_name (daemon->priv->pk_tracker,
                                                              sender,
                                                              &dbus_error);
        if (pk_caller == NULL) {
                error = g_error_new (DEVKIT_DISKS_ERROR,
                                     DEVKIT_DISKS_ERROR_FAILED,
                                     "Error getting information about caller: %s: %s",
                                     dbus_error.name, dbus_error.message);
                dbus_error_free (&dbus_error);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return NULL;
        }

        return pk_caller;
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
devkit_disks_damon_local_check_auth (DevkitDisksDaemon     *daemon,
                                     PolKitCaller          *pk_caller,
                                     const char            *action_id,
                                     DBusGMethodInvocation *context)
{
        gboolean ret;
        GError *error;
        DBusError d_error;
        PolKitAction *pk_action;
        PolKitResult pk_result;

        ret = FALSE;

        if (daemon->priv->inhibitors != NULL) {
                uid_t uid;

                uid = (uid_t) -1;
                if (!polkit_caller_get_uid (pk_caller, &uid) || uid != 0) {
                        if (context != NULL)
                                throw_error (context,
                                             DEVKIT_DISKS_ERROR_INHIBITED,
                                             "Daemon is being inhibited");
                }
                goto out;
        }

        pk_action = polkit_action_new ();
        polkit_action_set_action_id (pk_action, action_id);
        pk_result = polkit_context_is_caller_authorized (daemon->priv->pk_context,
                                                         pk_action,
                                                         pk_caller,
                                                         TRUE,
                                                         NULL);
        if (pk_result == POLKIT_RESULT_YES) {
                ret = TRUE;
        } else {

                dbus_error_init (&d_error);
                polkit_dbus_error_generate (pk_action, pk_result, &d_error);
                if (context != NULL) {
                        error = NULL;
                        dbus_set_g_error (&error, &d_error);
                        dbus_g_method_return_error (context, error);
                        g_error_free (error);
                }
                dbus_error_free (&d_error);
        }
        polkit_action_unref (pk_action);

 out:
        return ret;
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

gboolean
devkit_disks_daemon_find_device_by_device_file (DevkitDisksDaemon     *daemon,
                                                const char            *device_file,
                                                DBusGMethodInvocation *context)
{
        const char *object_path;
        DevkitDisksDevice *device;
        gchar canonical_device_file[PATH_MAX];

        /* TODO: maybe use realpath() on the given device_file so the caller can pass in symlinks? */
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

gboolean
devkit_disks_daemon_drive_inhibit_all_polling (DevkitDisksDaemon     *daemon,
                                               char                 **options,
                                               DBusGMethodInvocation *context)
{
        DevkitDisksInhibitor *inhibitor;
        PolKitCaller *pk_caller;
        guint n;

        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (daemon, context)) == NULL)
                goto out;


        if (!devkit_disks_damon_local_check_auth (daemon,
                                                  pk_caller,
                                                  "org.freedesktop.devicekit.disks.inhibit-polling",
                                                  context))
                goto out;

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
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
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
        PolKitCaller *pk_caller;
        uid_t uid;


        if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (daemon, context)) == NULL)
                goto out;

        uid = (uid_t) -1;
        if (!polkit_caller_get_uid (pk_caller, &uid) || uid != 0) {
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
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
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
