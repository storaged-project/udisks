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
#include <gio/gunixmounts.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <devkit-gobject.h>

#include "devkit-disks-daemon.h"
#include "devkit-disks-device.h"
#include "devkit-disks-device-private.h"
#include "mounts-file.h"

#include "devkit-disks-daemon-glue.h"
#include "devkit-disks-marshal.h"


/*--------------------------------------------------------------------------------------------------------------*/

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
        DBusGConnection   *system_bus_connection;
        DBusGProxy        *system_bus_proxy;
        PolKitContext     *pk_context;
        PolKitTracker     *pk_tracker;

        DevkitClient      *devkit_client;

	GIOChannel        *mdstat_channel;

        GHashTable        *map_native_path_to_device;
        GHashTable        *map_object_path_to_device;

        GUnixMountMonitor *mount_monitor;

        guint              smart_refresh_timer_id;

        DevkitDisksLogger *logger;
};

static void     devkit_disks_daemon_class_init  (DevkitDisksDaemonClass *klass);
static void     devkit_disks_daemon_init        (DevkitDisksDaemon      *seat);
static void     devkit_disks_daemon_finalize    (GObject     *object);

G_DEFINE_TYPE (DevkitDisksDaemon, devkit_disks_daemon, G_TYPE_OBJECT)

#define DEVKIT_DISKS_DAEMON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_TYPE_DISKS_DAEMON, DevkitDisksDaemonPrivate))

/*--------------------------------------------------------------------------------------------------------------*/

void
devkit_disks_daemon_local_update_mount_state (DevkitDisksDaemon *daemon, GList *devices, gboolean emit_changed)
{
        GList *l;
        GList *mounts;
        GList *devices_copy;
        GList *j;
        GList *jj;

        devices_copy = g_list_copy (devices);

        /* TODO: cache the mounts list to avoid rereading every time */
        mounts = g_unix_mounts_get (NULL);

        for (l = mounts; l != NULL; l = l->next) {
                GUnixMountEntry *mount_entry = l->data;
                const char *device_file;
                const char *mount_path;

                /* TODO: maybe use realpath() on the device_path */
                device_file = g_unix_mount_get_device_path (mount_entry);
                mount_path = g_unix_mount_get_mount_path (mount_entry);

                for (j = devices_copy; j != NULL; j = jj) {
                        DevkitDisksDevice *device = j->data;
                        const char *device_device_file;
                        const char *device_mount_path;

                        jj = j->next;

                        device_device_file = devkit_disks_device_local_get_device_file (device);
                        device_mount_path = devkit_disks_device_local_get_mount_path (device);

                        if (strcmp (device_device_file, device_file) == 0) {
                                /* is mounted */
                                if (device_mount_path != NULL && strcmp (device_mount_path, mount_path) == 0) {
                                        /* same mount path; no changes */
                                } else {
                                        /* device was just mounted... or the mount path changed */
                                        devkit_disks_device_local_set_mounted (device, mount_path, emit_changed);
                                }

                                /* we're mounted so remove from list of devices (see below) */
                                devices_copy = g_list_delete_link (devices_copy, j);

                                /* no other devices can be mounted at this path so break
                                 * out and process the next mount entry
                                 */
                                break;
                        }


                        if (device_mount_path == NULL) {
                        }
                }
        }
        g_list_foreach (mounts, (GFunc) g_unix_mount_free, NULL);
        g_list_free (mounts);

        /* Since we've removed mounted devices from the devices_copy
         * list the remaining devices in the list are not mounted.
         * Update their state to say so.
         */
        for (j = devices_copy; j != NULL; j = j->next) {
                DevkitDisksDevice *device = j->data;
                const char *device_mount_path;

                device_mount_path = devkit_disks_device_local_get_mount_path (device);
                if (device_mount_path != NULL) {
                        devkit_disks_device_local_set_unmounted (device, emit_changed);
                }
        }

        g_list_free (devices_copy);
}

/*--------------------------------------------------------------------------------------------------------------*/

GQuark
devkit_disks_daemon_error_quark (void)
{
        static GQuark ret = 0;

        if (ret == 0) {
                ret = g_quark_from_static_string ("devkit_disks_daemon_error");
        }

        return ret;
}


#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
devkit_disks_daemon_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0)
        {
                static const GEnumValue values[] =
                        {
                                ENUM_ENTRY (DEVKIT_DISKS_DAEMON_ERROR_GENERAL, "GeneralError"),
                                ENUM_ENTRY (DEVKIT_DISKS_DAEMON_ERROR_NOT_SUPPORTED, "NotSupported"),
                                ENUM_ENTRY (DEVKIT_DISKS_DAEMON_ERROR_NO_SUCH_DEVICE, "NoSuchDevice"),
                                { 0, 0, 0 }
                        };
                g_assert (DEVKIT_DISKS_DAEMON_NUM_ERRORS == G_N_ELEMENTS (values) - 1);
                etype = g_enum_register_static ("DevkitDisksDaemonError", values);
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

        klass = DEVKIT_DISKS_DAEMON_CLASS (g_type_class_peek (DEVKIT_TYPE_DISKS_DAEMON));

        daemon = DEVKIT_DISKS_DAEMON (
                G_OBJECT_CLASS (devkit_disks_daemon_parent_class)->constructor (type,
                                                                                n_construct_properties,
                                                                                construct_properties));
        return G_OBJECT (daemon);
}

static void
devkit_disks_daemon_class_init (DevkitDisksDaemonClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = devkit_disks_daemon_constructor;
        object_class->finalize = devkit_disks_daemon_finalize;

        g_type_class_add_private (klass, sizeof (DevkitDisksDaemonPrivate));

        signals[DEVICE_ADDED_SIGNAL] =
                g_signal_new ("device-added",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE, 1, G_TYPE_STRING);

        signals[DEVICE_REMOVED_SIGNAL] =
                g_signal_new ("device-removed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE, 1, G_TYPE_STRING);

        signals[DEVICE_CHANGED_SIGNAL] =
                g_signal_new ("device-changed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE, 1, G_TYPE_STRING);

        signals[DEVICE_JOB_CHANGED_SIGNAL] =
                g_signal_new ("device-job-changed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              devkit_disks_marshal_VOID__STRING_BOOLEAN_STRING_BOOLEAN_INT_INT_STRING_DOUBLE,
                              G_TYPE_NONE,
                              8,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN,
                              G_TYPE_STRING,
                              G_TYPE_BOOLEAN,
                              G_TYPE_INT,
                              G_TYPE_INT,
                              G_TYPE_STRING,
                              G_TYPE_DOUBLE);


        dbus_g_object_type_install_info (DEVKIT_TYPE_DISKS_DAEMON, &dbus_glib_devkit_disks_daemon_object_info);

        dbus_g_error_domain_register (DEVKIT_DISKS_DAEMON_ERROR,
                                      NULL,
                                      DEVKIT_DISKS_DAEMON_TYPE_ERROR);
}

static void
devkit_disks_daemon_init (DevkitDisksDaemon *daemon)
{
        daemon->priv = DEVKIT_DISKS_DAEMON_GET_PRIVATE (daemon);
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

        g_return_if_fail (object != NULL);
        g_return_if_fail (DEVKIT_IS_DISKS_DAEMON (object));

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

        if (daemon->priv->smart_refresh_timer_id > 0) {
                g_source_remove (daemon->priv->smart_refresh_timer_id);
        }

        if (daemon->priv->logger != NULL) {
                g_object_unref (daemon->priv->logger);
        }

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

static DBusHandlerResult
_filter (DBusConnection *connection, DBusMessage *message, void *user_data)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);
        const char *interface;

        interface = dbus_message_get_interface (message);

        if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
                /* pass NameOwnerChanged signals from the bus to PolKitTracker */
                polkit_tracker_dbus_func (daemon->priv->pk_tracker, message);
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
                g_print ("removed %s\n", (char *) key);
                return TRUE;
        }
        return FALSE;
}

static void
device_went_away (gpointer user_data, GObject *where_the_object_was)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);

        g_hash_table_foreach_remove (daemon->priv->map_native_path_to_device,
                                     device_went_away_remove_cb,
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
                if (!devkit_disks_device_changed (device, d, synthesized)) {
                        g_print ("changed triggered remove on %s\n", native_path);
                        device_remove (daemon, d);
                } else {
                        g_print ("changed %s\n", native_path);
                }
        } else {
                g_print ("treating change event as add on %s\n", native_path);
                device_add (daemon, d, TRUE);
        }
}

void
devkit_disks_daemon_local_synthesize_changed (DevkitDisksDaemon       *daemon,
                                              DevkitDevice            *d)
{
        g_object_ref (d);
        device_changed (daemon, d, TRUE);
        g_object_unref (d);
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
                g_print ("treating add event as change event on %s\n", native_path);
                device_changed (daemon, d, FALSE);
        } else {
                device = devkit_disks_device_new (daemon, d);

                if (device != NULL) {
                        /* only take a weak ref; the device will stay on the bus until
                         * it's unreffed. So if we ref it, it'll never go away. Stupid
                         * dbus-glib, no cookie for you.
                         */
                        g_object_weak_ref (G_OBJECT (device), device_went_away, daemon);
                        g_hash_table_insert (daemon->priv->map_native_path_to_device,
                                             g_strdup (native_path),
                                             device);
                        g_hash_table_insert (daemon->priv->map_object_path_to_device,
                                             g_strdup (devkit_disks_device_local_get_object_path (device)),
                                             device);
                        g_print ("added %s\n", native_path);
                        if (emit_event) {
                                g_signal_emit (daemon, signals[DEVICE_ADDED_SIGNAL], 0,
                                               devkit_disks_device_local_get_object_path (device));
                        }
                } else {
                        g_print ("ignoring add event on %s\n", native_path);
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
                g_print ("ignoring remove event on %s\n", native_path);
        } else {
                devkit_disks_device_removed (device);
                g_signal_emit (daemon, signals[DEVICE_REMOVED_SIGNAL], 0,
                               devkit_disks_device_local_get_object_path (device));
                g_object_unref (device);
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
mounts_changed (GUnixMountMonitor *monitor, gpointer user_data)
{
        DevkitDisksDaemon *daemon = DEVKIT_DISKS_DAEMON (user_data);
        GList *devices;

        devices = g_hash_table_get_values (daemon->priv->map_native_path_to_device);
        devkit_disks_daemon_local_update_mount_state (daemon, devices, TRUE);
        g_list_free (devices);
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
                if (device->priv->info.device_is_linux_md) {
                        g_ptr_array_add (a, g_object_ref (device->priv->d));
                }
        }

        for (n = 0; n < (int) a->len; n++) {
                DevkitDevice *d = a->pdata[n];
                g_warning ("using change on /proc/mdstat to trigger change event on %s", native_path);
                device_changed (daemon, d, FALSE);
                g_object_unref (d);
        }

        g_ptr_array_free (a, TRUE);

out:
	return TRUE;
}

static gboolean
refresh_smart_data (DevkitDisksDaemon *daemon)
{
        DevkitDisksDevice *device;
        const char *native_path;
        GHashTableIter iter;

        g_hash_table_iter_init (&iter, daemon->priv->map_native_path_to_device);
        while (g_hash_table_iter_next (&iter, (gpointer *) &native_path, (gpointer *) &device)) {
                if (device->priv->drive_smart_is_capable) {
                        char *options[] = {NULL};

                        g_warning ("automatically refreshing SMART data for %s", native_path);
                        devkit_disks_device_drive_smart_refresh_data (device, options, NULL);
                }
        }

        /* update in another 30 minutes */
        daemon->priv->smart_refresh_timer_id = g_timeout_add_seconds (30 * 60,
                                                                      (GSourceFunc) refresh_smart_data,
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

        /* monitor mounts */
        daemon->priv->mount_monitor = g_unix_mount_monitor_new ();
        // See http://bugzilla.gnome.org/show_bug.cgi?id=521946
        //g_unix_mount_monitor_set_rate_limit (daemon->priv->mount_monitor, 50);
        g_signal_connect (daemon->priv->mount_monitor, "mounts-changed", (GCallback) mounts_changed, daemon);

        daemon->priv->logger = devkit_disks_logger_new ();

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
        const char *subsystems[] = {"block", NULL};

        daemon = DEVKIT_DISKS_DAEMON (g_object_new (DEVKIT_TYPE_DISKS_DAEMON, NULL));

        if (!register_disks_daemon (DEVKIT_DISKS_DAEMON (daemon))) {
                g_object_unref (daemon);
                return NULL;
        }


        devices = devkit_client_enumerate_by_subsystem (daemon->priv->devkit_client,
                                                         subsystems,
                                                         &error);
        if (error != NULL) {
                g_warning ("Cannot enumerate devices: %s", error->message);
                g_error_free (error);
                g_object_unref (daemon);
                return NULL;
        }
        for (l = devices; l != NULL; l = l->next) {
                DevkitDevice *device = l->data;
                device_add (daemon, device, FALSE);
        }
        g_list_foreach (devices, (GFunc) g_object_unref, NULL);
        g_list_free (devices);

        /* clean stale directories in /media as well as stale
         * entries in /var/lib/DeviceKit-disks/mtab
         */
        l = g_hash_table_get_values (daemon->priv->map_native_path_to_device);
        mounts_file_clean_stale (l);
        g_list_free (l);

        /* initial refresh of SMART data */
        refresh_smart_data (daemon);

        return daemon;
}

DevkitDisksLogger *
devkit_disks_daemon_local_get_logger (DevkitDisksDaemon *daemon)
{
        return daemon->priv->logger;
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
                error = g_error_new (DEVKIT_DISKS_DAEMON_ERROR,
                                     DEVKIT_DISKS_DAEMON_ERROR_GENERAL,
                                     "Error getting information about caller: %s: %s",
                                     dbus_error.name, dbus_error.message);
                dbus_error_free (&dbus_error);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return NULL;
        }

        return pk_caller;
}

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
                error = NULL;
                dbus_set_g_error (&error, &d_error);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                dbus_error_free (&d_error);
        }
        polkit_action_unref (pk_action);
        return ret;
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

        error = g_error_new (DEVKIT_DISKS_DAEMON_ERROR,
                             error_code,
                             message);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        g_free (message);
        return TRUE;
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
        GHashTableIter iter;
        DevkitDisksDevice *device;

        object_path = NULL;
        g_hash_table_iter_init (&iter, daemon->priv->map_native_path_to_device);
        while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &device)) {
                if (strcmp (devkit_disks_device_local_get_device_file (device), device_file) == 0) {
                        object_path = devkit_disks_device_local_get_object_path (device);
                        goto out;
                }
        }

out:
        if (object_path != NULL) {
                dbus_g_method_return (context, object_path);
        } else {
                throw_error (context,
                             DEVKIT_DISKS_DAEMON_ERROR_NO_SUCH_DEVICE,
                             "No such device");
        }
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/
