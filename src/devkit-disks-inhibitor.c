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

#include <string.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "devkit-disks-inhibitor.h"

struct DevkitDisksInhibitorPrivate
{
        gchar *unique_dbus_name;
        gchar *cookie;
};

enum
{
        DISCONNECTED_SIGNAL,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (DevkitDisksInhibitor, devkit_disks_inhibitor, G_TYPE_OBJECT)

static GList *inhibitors = NULL;

static void
devkit_disks_inhibitor_finalize (GObject *object)
{
        DevkitDisksInhibitor *inhibitor;

        inhibitor = DEVKIT_DISKS_INHIBITOR (object);

        inhibitors = g_list_remove (inhibitors, inhibitor);

        g_free (inhibitor->priv->unique_dbus_name);
        g_free (inhibitor->priv->cookie);

        G_OBJECT_CLASS (devkit_disks_inhibitor_parent_class)->finalize (object);
}

static void
devkit_disks_inhibitor_init (DevkitDisksInhibitor *inhibitor)
{
  inhibitor->priv = G_TYPE_INSTANCE_GET_PRIVATE (inhibitor, DEVKIT_TYPE_DISKS_INHIBITOR, DevkitDisksInhibitorPrivate);
}

static void
devkit_disks_inhibitor_class_init (DevkitDisksInhibitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = devkit_disks_inhibitor_finalize;

        g_type_class_add_private (klass, sizeof (DevkitDisksInhibitorPrivate));

        signals[DISCONNECTED_SIGNAL] =
                g_signal_new ("disconnected",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

DevkitDisksInhibitor *
devkit_disks_inhibitor_new (DBusGMethodInvocation *context)
{
        DevkitDisksInhibitor *inhibitor;
        static gint inhibitor_count = 0;

        inhibitor = DEVKIT_DISKS_INHIBITOR (g_object_new (DEVKIT_TYPE_DISKS_INHIBITOR, NULL));

        inhibitor->priv->unique_dbus_name = g_strdup (dbus_g_method_get_sender (context));

        /* TODO: maybe use a real random number (if it turns out we need this to be cryptographically secure etc.) */
        inhibitor->priv->cookie = g_strdup_printf ("devkit_disks_inhibitor_%d", inhibitor_count++);

        inhibitors = g_list_prepend (inhibitors, inhibitor);

        return inhibitor;
}

const gchar *
devkit_disks_inhibitor_get_unique_dbus_name (DevkitDisksInhibitor *inhibitor)
{
        return inhibitor->priv->unique_dbus_name;
}

const gchar *
devkit_disks_inhibitor_get_cookie (DevkitDisksInhibitor *inhibitor)
{
        return inhibitor->priv->cookie;
}


void devkit_disks_inhibitor_name_owner_changed (DBusMessage *message);

void
devkit_disks_inhibitor_name_owner_changed (DBusMessage *message)
{

        if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
		char *name;
		char *new_owner;
		char *old_owner;

		if (!dbus_message_get_args (message, NULL,
					    DBUS_TYPE_STRING, &name,
					    DBUS_TYPE_STRING, &old_owner,
					    DBUS_TYPE_STRING, &new_owner,
					    DBUS_TYPE_INVALID)) {

                        g_warning ("The NameOwnerChanged signal has the wrong signature.");
			goto out;
		}

                if (strlen (new_owner) == 0) {
                        GList *l;

                        for (l = inhibitors; l != NULL; l = l->next) {
                                DevkitDisksInhibitor *inhibitor = DEVKIT_DISKS_INHIBITOR (l->data);

                                g_debug (" looking at %s", inhibitor->priv->unique_dbus_name);
                                if (g_strcmp0 (name, inhibitor->priv->unique_dbus_name) == 0) {
                                        g_signal_emit_by_name (inhibitor, "disconnected");
                                }
                        }
                }
        }

 out:
        ;
}

