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

#include "devkit-disks-daemon.h"
#include "devkit-disks-device.h"
#include "devkit-disks-daemon-glue.h"

/*--------------------------------------------------------------------------------------------------------------*/

enum
{
        DEVICE_ADDED_SIGNAL,
        DEVICE_REMOVED_SIGNAL,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

struct DevkitDisksDaemonPrivate
{
        DBusGConnection *system_bus_connection;
        DBusGProxy      *system_bus_proxy;
        PolKitContext   *pk_context;
        PolKitTracker   *pk_tracker;

	int              udev_socket;
	GIOChannel      *udev_channel;

        GList           *inhibitors;
        guint            killtimer_id;
        int              num_local_inhibitors;
        gboolean         no_exit;

        GList           *devices;
};

static void     devkit_disks_daemon_class_init  (DevkitDisksDaemonClass *klass);
static void     devkit_disks_daemon_init        (DevkitDisksDaemon      *seat);
static void     devkit_disks_daemon_finalize    (GObject     *object);

G_DEFINE_TYPE (DevkitDisksDaemon, devkit_disks_daemon, G_TYPE_OBJECT)

#define DEVKIT_DISKS_DAEMON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_TYPE_DISKS_DAEMON, DevkitDisksDaemonPrivate))

typedef struct
{
        char *cookie;
        char *system_bus_name;
} Inhibitor;

static void
inhibitor_free (Inhibitor *inhibitor)
{
        g_free (inhibitor->cookie);
        g_free (inhibitor->system_bus_name);
        g_free (inhibitor);
}

static void
inhibitor_list_changed (DevkitDisksDaemon *daemon)
{
        devkit_disks_daemon_reset_killtimer (daemon);
}

void
devkit_disks_daemon_inhibit_killtimer (DevkitDisksDaemon *daemon)
{
        daemon->priv->num_local_inhibitors++;
        devkit_disks_daemon_reset_killtimer (daemon);
}

void
devkit_disks_daemon_uninhibit_killtimer (DevkitDisksDaemon *daemon)
{
        daemon->priv->num_local_inhibitors--;
        devkit_disks_daemon_reset_killtimer (daemon);
}

static gboolean
killtimer_do_exit (gpointer user_data)
{
        g_debug ("Exiting due to inactivity");
        exit (0);
        return FALSE;
}

void
devkit_disks_daemon_reset_killtimer (DevkitDisksDaemon *daemon)
{
        /* remove existing timer */
        if (daemon->priv->killtimer_id > 0) {
                //g_debug ("Removed existing killtimer");
                g_source_remove (daemon->priv->killtimer_id);
                daemon->priv->killtimer_id = 0;
        }

        /* someone on the bus is inhibiting us */
        if (g_list_length (daemon->priv->inhibitors) > 0)
                return;

        /* some local long running method is inhibiting us */
        if (daemon->priv->num_local_inhibitors > 0)
                return;

        if (daemon->priv->no_exit)
                return;

        //g_debug ("Setting killtimer to 30 seconds");
        daemon->priv->killtimer_id = g_timeout_add (30 * 1000, killtimer_do_exit, NULL);
}


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
                                ENUM_ENTRY (DEVKIT_DISKS_DAEMON_ERROR_NOT_AUTHORIZED, "NotAuthorized"),
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

        dbus_g_object_type_install_info (DEVKIT_TYPE_DISKS_DAEMON, &dbus_glib_devkit_disks_daemon_object_info);

        dbus_g_error_domain_register (DEVKIT_DISKS_DAEMON_ERROR, NULL, DEVKIT_DISKS_DAEMON_TYPE_ERROR);

}

static void
devkit_disks_daemon_init (DevkitDisksDaemon *daemon)
{
        daemon->priv = DEVKIT_DISKS_DAEMON_GET_PRIVATE (daemon);
        daemon->priv->udev_socket = -1;
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

        if (daemon->priv->inhibitors != NULL) {
                g_list_foreach (daemon->priv->inhibitors, (GFunc) inhibitor_free, NULL);
                g_list_free (daemon->priv->inhibitors);
        }

        if (daemon->priv->killtimer_id > 0) {
                g_source_remove (daemon->priv->killtimer_id);
        }

        if (daemon->priv->udev_socket != -1)
                close (daemon->priv->udev_socket);

        if (daemon->priv->udev_channel != NULL)
                g_io_channel_unref (daemon->priv->udev_channel);

        if (daemon->priv->devices != NULL) {
                g_list_foreach (daemon->priv->devices, (GFunc) g_object_unref, NULL);
                g_list_free (daemon->priv->devices);
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
                GList *l;
                const char *system_bus_name;
                const char *old_service_name;
                const char *new_service_name;

                /* pass NameOwnerChanged signals from the bus to PolKitTracker */
                polkit_tracker_dbus_func (daemon->priv->pk_tracker, message);

                /* find the name */
		if (dbus_message_get_args (message, NULL,
                                           DBUS_TYPE_STRING, &system_bus_name,
                                           DBUS_TYPE_STRING, &old_service_name,
                                           DBUS_TYPE_STRING, &new_service_name,
                                           DBUS_TYPE_INVALID)) {

                        if (strlen (new_service_name) == 0) {
                                /* he exited.. remove from inhibitor list */
                                for (l = daemon->priv->inhibitors; l != NULL; l = l->next) {
                                        Inhibitor *inhibitor = l->data;
                                        if (strcmp (system_bus_name, inhibitor->system_bus_name) == 0) {
                                                daemon->priv->inhibitors = g_list_remove (daemon->priv->inhibitors,
                                                                                          inhibitor);
                                                inhibitor_list_changed (daemon);
                                                //g_debug ("removed inhibitor %s %s (disconnected from the bus)",
                                                //inhibitor->cookie, inhibitor->system_bus_name);
                                                break;
                                        }
                                }
                        }
                }
        }

        if (interface != NULL && g_str_has_prefix (interface, "org.freedesktop.ConsoleKit")) {
                /* pass ConsoleKit signals to PolKitTracker */
                polkit_tracker_dbus_func (daemon->priv->pk_tracker, message);
        }

        /* other filters might want to process this message too */
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
device_add (DevkitDisksDaemon *daemon, const char *native_path, gboolean emit_event)
{
        DevkitDisksDevice *device;
        device = devkit_disks_device_new (daemon, native_path);
        daemon->priv->devices = g_list_prepend (daemon->priv->devices, device);
        if (emit_event) {
                g_signal_emit (daemon, signals[DEVICE_ADDED_SIGNAL], 0,
                               devkit_disks_device_local_get_object_path (device));
        }
}

static void
device_remove (DevkitDisksDaemon *daemon, const char *native_path)
{
        GList *l;
        for (l = daemon->priv->devices; l != NULL; l = l->next) {
                DevkitDisksDevice *device = l->data;
                if (strcmp (native_path, devkit_disks_device_local_get_native_path (device)) == 0) {
                        daemon->priv->devices = g_list_remove (daemon->priv->devices, device);
                        g_signal_emit (daemon, signals[DEVICE_REMOVED_SIGNAL], 0,
                                       devkit_disks_device_local_get_object_path (device));
                        g_object_unref (device);
                        break;
                }
        }
}

static void
device_changed (DevkitDisksDaemon *daemon, const char *native_path)
{
        /* TODO */
}

static gboolean
receive_udev_data (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
        DevkitDisksDaemon *daemon = user_data;
	int fd;
	int retval;
	struct msghdr smsg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	struct ucred *cred;
	char cred_msg[CMSG_SPACE(sizeof(struct ucred))];
	char buf[4096];
	size_t bufpos = 0;
	const char *action;
	const char *devpath;
	const char *subsystem;

	memset(buf, 0x00, sizeof (buf));
	iov.iov_base = &buf;
	iov.iov_len = sizeof (buf);
	memset (&smsg, 0x00, sizeof (struct msghdr));
	smsg.msg_iov = &iov;
	smsg.msg_iovlen = 1;
	smsg.msg_control = cred_msg;
	smsg.msg_controllen = sizeof (cred_msg);

	fd = g_io_channel_unix_get_fd (source);

	retval = recvmsg (fd, &smsg, 0);
	if (retval <  0) {
		if (errno != EINTR)
			g_warning ("Unable to receive message: %m");
		goto out;
	}
	cmsg = CMSG_FIRSTHDR (&smsg);
	cred = (struct ucred *) CMSG_DATA (cmsg);

	if (cmsg == NULL || cmsg->cmsg_type != SCM_CREDENTIALS) {
		g_warning ("No sender credentials received, message ignored");
		goto out;
	}

	if (cred->uid != 0) {
		g_warning ("Sender uid=%d, message ignored", cred->uid);
		goto out;
	}

	if (!strstr(buf, "@/")) {
		g_warning ("invalid message format");
		goto out;
	}

        action = NULL;
        devpath = NULL;
        subsystem = NULL;
	while (bufpos < sizeof (buf)) {
		size_t keylen;
		char *key;

		key = &buf[bufpos];
		keylen = strlen(key);
		if (keylen == 0)
			break;
		bufpos += keylen + 1;

		if (strncmp (key, "ACTION=", 7) == 0) {
			action = key + 7;
		} else if (strncmp (key, "DEVPATH=", 8) == 0) {
                        devpath = key + 8;
		} else if (strncmp(key, "SUBSYSTEM=", 10) == 0) {
                        subsystem = key + 10;
                }
                /* TODO: collect other values */
	}

	if (action != NULL && devpath != NULL && subsystem != NULL) {
                if (strcmp (subsystem, "block") == 0) {
                        char *native_path;
                        native_path = g_build_filename ("/sys", devpath, NULL);
                        if (strcmp (action, "add") == 0) {
                                device_add (daemon, native_path, TRUE);
                        } else if (strcmp (action, "remove") == 0) {
                                device_remove (daemon, native_path);
                        } else if (strcmp (action, "changed") == 0) {
                                device_changed (daemon, native_path);
                        }
                        g_free (native_path);
                }

	} else {
                g_warning ("malformed message");
        }

out:
	return TRUE;
}


static gboolean
register_disks_daemon (DevkitDisksDaemon *daemon)
{
	struct sockaddr_un saddr;
	socklen_t addrlen;
	const int on = 1;
        DBusConnection *connection;
        DBusError dbus_error;
        GError *error = NULL;

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

	/* setup socket for listening from messages from udev */

	memset (&saddr, 0x00, sizeof(saddr));
	saddr.sun_family = AF_LOCAL;
	/* use abstract namespace for socket path */
	strcpy (&saddr.sun_path[1], "/org/freedesktop/devicekit/disks/udev_event");
	addrlen = offsetof (struct sockaddr_un, sun_path) + strlen(saddr.sun_path+1) + 1;
	daemon->priv->udev_socket = socket (AF_LOCAL, SOCK_DGRAM, 0);
	if (daemon->priv->udev_socket == -1) {
		g_warning ("Couldn't open udev event socket: %m");
                goto error;
	}

	if (bind (daemon->priv->udev_socket, (struct sockaddr *) &saddr, addrlen) < 0) {
		g_warning ("Error binding to udev event socket: %m");
                goto error;
	}
	/* enable receiving of the sender credentials */
	setsockopt (daemon->priv->udev_socket, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));
	daemon->priv->udev_channel = g_io_channel_unix_new (daemon->priv->udev_socket);
	g_io_add_watch (daemon->priv->udev_channel, G_IO_IN, receive_udev_data, daemon);
	g_io_channel_unref (daemon->priv->udev_channel);

        devkit_disks_daemon_reset_killtimer (daemon);

        return TRUE;

error:
        return FALSE;
}


DevkitDisksDaemon *
devkit_disks_daemon_new (gboolean no_exit)
{
        gboolean res;
        DevkitDisksDaemon *daemon;
        GList *native_paths;
        GList *l;

        daemon = DEVKIT_DISKS_DAEMON (g_object_new (DEVKIT_TYPE_DISKS_DAEMON, NULL));
        daemon->priv->no_exit = no_exit;

        res = register_disks_daemon (DEVKIT_DISKS_DAEMON (daemon));
        if (! res) {
                g_object_unref (daemon);
                return NULL;
        }

        native_paths = devkit_disks_enumerate_native_paths ();
        for (l = native_paths; l != NULL; l = l->next) {
                char *native_path = l->data;
                device_add (daemon, native_path, FALSE);
                g_free (native_path);
        }
        g_list_free (native_paths);

        return daemon;
}

/*--------------------------------------------------------------------------------------------------------------*/
/* exported methods */

gboolean
devkit_disks_daemon_inhibit_shutdown (DevkitDisksDaemon     *daemon,
                                      DBusGMethodInvocation *context)
{
        GList *l;
        Inhibitor *inhibitor;
        const char *system_bus_name;

        system_bus_name = dbus_g_method_get_sender (context);

        inhibitor = g_new0 (Inhibitor, 1);
        inhibitor->system_bus_name = g_strdup (system_bus_name);
regen_cookie:
        inhibitor->cookie = g_strdup_printf ("%d", g_random_int_range (0, G_MAXINT));
        for (l = daemon->priv->inhibitors; l != NULL; l = l->next) {
                Inhibitor *i = l->data;
                if (strcmp (i->cookie, inhibitor->cookie) == 0) {
                        g_free (inhibitor->cookie);
                        goto regen_cookie;
                }
        }

        daemon->priv->inhibitors = g_list_prepend (daemon->priv->inhibitors, inhibitor);
        inhibitor_list_changed (daemon);

        //g_debug ("added inhibitor %s %s", inhibitor->cookie, inhibitor->system_bus_name);

        dbus_g_method_return (context, inhibitor->cookie);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
devkit_disks_daemon_uninhibit_shutdown (DevkitDisksDaemon     *daemon,
                                        const char            *cookie,
                                        DBusGMethodInvocation *context)
{
        GList *l;
        GError *error;
        const char *system_bus_name;

        system_bus_name = dbus_g_method_get_sender (context);

        for (l = daemon->priv->inhibitors; l != NULL; l = l->next) {
                Inhibitor *inhibitor = l->data;
                if (strcmp (cookie, inhibitor->cookie) == 0 &&
                    strcmp (system_bus_name, inhibitor->system_bus_name) == 0) {

                        daemon->priv->inhibitors = g_list_remove (daemon->priv->inhibitors, inhibitor);
                        inhibitor_list_changed (daemon);

                        //g_debug ("removed inhibitor %s %s", inhibitor->cookie, inhibitor->system_bus_name);
                        dbus_g_method_return (context);
                        goto out;
                }
        }

        error = g_error_new (DEVKIT_DISKS_DAEMON_ERROR,
                             DEVKIT_DISKS_DAEMON_ERROR_GENERAL,
                             "No such inhibitor");
        dbus_g_method_return_error (context, error);
        g_error_free (error);

out:
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
devkit_disks_daemon_enumerate_devices (DevkitDisksDaemon     *daemon,
                                       DBusGMethodInvocation *context)
{
        GList *l;
        GPtrArray *object_paths;

        object_paths = g_ptr_array_new ();
        for (l = daemon->priv->devices; l != NULL; l = l->next) {
                DevkitDisksDevice *device = l->data;
                g_ptr_array_add (object_paths, g_strdup (devkit_disks_device_local_get_object_path (device)));
        }

        dbus_g_method_return (context, object_paths);
        g_ptr_array_foreach (object_paths, (GFunc) g_free, NULL);
        g_ptr_array_free (object_paths, TRUE);
        return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/
