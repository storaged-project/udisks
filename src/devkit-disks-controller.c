/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 David Zeuthen <david@fubar.dk>
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

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <gio/gunixmounts.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gudev/gudev.h>
#include <atasmart.h>

#include "devkit-disks-daemon.h"
#include "devkit-disks-controller.h"
#include "devkit-disks-controller-private.h"
#include "devkit-disks-marshal.h"

/*--------------------------------------------------------------------------------------------------------------*/
#include "devkit-disks-controller-glue.h"

static void     devkit_disks_controller_class_init  (DevkitDisksControllerClass *klass);
static void     devkit_disks_controller_init        (DevkitDisksController      *seat);
static void     devkit_disks_controller_finalize    (GObject     *object);

static gboolean update_info                (DevkitDisksController *controller);

static void     drain_pending_changes (DevkitDisksController *controller, gboolean force_update);

enum
{
        PROP_0,
        PROP_NATIVE_PATH,

        PROP_VENDOR,
        PROP_MODEL,
        PROP_DRIVER,
};

enum
{
        CHANGED_SIGNAL,
        LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (DevkitDisksController, devkit_disks_controller, G_TYPE_OBJECT)

#define DEVKIT_DISKS_CONTROLLER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_DISKS_TYPE_CONTROLLER, DevkitDisksControllerPrivate))

static void
get_property (GObject         *object,
              guint            prop_id,
              GValue          *value,
              GParamSpec      *pspec)
{
        DevkitDisksController *controller = DEVKIT_DISKS_CONTROLLER (object);

        switch (prop_id) {
        case PROP_NATIVE_PATH:
                g_value_set_string (value, controller->priv->native_path);
                break;

        case PROP_VENDOR:
                g_value_set_string (value, controller->priv->vendor);
                break;

        case PROP_MODEL:
                g_value_set_string (value, controller->priv->model);
                break;

        case PROP_DRIVER:
                g_value_set_string (value, controller->priv->driver);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
devkit_disks_controller_class_init (DevkitDisksControllerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = devkit_disks_controller_finalize;
        object_class->get_property = get_property;

        g_type_class_add_private (klass, sizeof (DevkitDisksControllerPrivate));

        signals[CHANGED_SIGNAL] =
                g_signal_new ("changed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        dbus_g_object_type_install_info (DEVKIT_DISKS_TYPE_CONTROLLER, &dbus_glib_devkit_disks_controller_object_info);

        g_object_class_install_property (
                object_class,
                PROP_NATIVE_PATH,
                g_param_spec_string ("native-path", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_VENDOR,
                g_param_spec_string ("vendor", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_MODEL,
                g_param_spec_string ("model", NULL, NULL, NULL, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_DRIVER,
                g_param_spec_string ("driver", NULL, NULL, NULL, G_PARAM_READABLE));
}

static void
devkit_disks_controller_init (DevkitDisksController *controller)
{
        controller->priv = DEVKIT_DISKS_CONTROLLER_GET_PRIVATE (controller);
}

static void
devkit_disks_controller_finalize (GObject *object)
{
        DevkitDisksController *controller;

        g_return_if_fail (object != NULL);
        g_return_if_fail (DEVKIT_DISKS_IS_CONTROLLER (object));

        controller = DEVKIT_DISKS_CONTROLLER (object);
        g_return_if_fail (controller->priv != NULL);

        /* g_debug ("finalizing %s", controller->priv->native_path); */

        g_object_unref (controller->priv->d);
        g_object_unref (controller->priv->daemon);
        g_free (controller->priv->object_path);

        g_free (controller->priv->native_path);

        if (controller->priv->emit_changed_idle_id > 0)
                g_source_remove (controller->priv->emit_changed_idle_id);

        /* free properties */
        g_free (controller->priv->vendor);
        g_free (controller->priv->model);
        g_free (controller->priv->driver);

        G_OBJECT_CLASS (devkit_disks_controller_parent_class)->finalize (object);
}

/**
 * compute_object_path:
 * @native_path: Either an absolute sysfs path or the basename
 *
 * Maps @native_path to the D-Bus object path for the controller.
 *
 * Returns: A valid D-Bus object path. Free with g_free().
 */
static char *
compute_object_path (const char *native_path)
{
        const gchar *basename;
        GString *s;
        guint n;

        basename = strrchr (native_path, '/');
        if (basename != NULL) {
                basename++;
        } else {
                basename = native_path;
        }

        s = g_string_new ("/org/freedesktop/DeviceKit/Disks/controllers/");
        for (n = 0; basename[n] != '\0'; n++) {
                gint c = basename[n];

                /* D-Bus spec sez:
                 *
                 * Each element must only contain the ASCII characters "[A-Z][a-z][0-9]_"
                 */
                if ((c >= 'A' && c <= 'Z') ||
                    (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9')) {
                        g_string_append_c (s, c);
                } else {
                        /* Escape bytes not in [A-Z][a-z][0-9] as _<hex-with-two-digits> */
                        g_string_append_printf (s, "_%02x", c);
                }
        }

        return g_string_free (s, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
register_disks_controller (DevkitDisksController *controller)
{
        DBusConnection *connection;
        GError *error = NULL;

        controller->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (controller->priv->system_bus_connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                goto error;
        }
        connection = dbus_g_connection_get_connection (controller->priv->system_bus_connection);

        controller->priv->object_path = compute_object_path (controller->priv->native_path);

        /* safety first */
        if (dbus_g_connection_lookup_g_object (controller->priv->system_bus_connection,
                                               controller->priv->object_path) != NULL) {
                g_error ("**** HACK: Wanting to register object at path `%s' but there is already an "
                         "object there. This is an internal error in the daemon. Aborting.\n",
                         controller->priv->object_path);
        }

        dbus_g_connection_register_g_object (controller->priv->system_bus_connection,
                                             controller->priv->object_path,
                                             G_OBJECT (controller));

        return TRUE;

error:
        return FALSE;
}

void
devkit_disks_controller_removed (DevkitDisksController *controller)
{
        controller->priv->removed = TRUE;

        dbus_g_connection_unregister_g_object (controller->priv->system_bus_connection,
                                               G_OBJECT (controller));
        g_assert (dbus_g_connection_lookup_g_object (controller->priv->system_bus_connection,
                                                     controller->priv->object_path) == NULL);
}

DevkitDisksController *
devkit_disks_controller_new (DevkitDisksDaemon *daemon, GUdevDevice *d)
{
        DevkitDisksController *controller;
        const char *native_path;

        controller = NULL;
        native_path = g_udev_device_get_sysfs_path (d);

        controller = DEVKIT_DISKS_CONTROLLER (g_object_new (DEVKIT_DISKS_TYPE_CONTROLLER, NULL));
        controller->priv->d = g_object_ref (d);
        controller->priv->daemon = g_object_ref (daemon);
        controller->priv->native_path = g_strdup (native_path);

        if (!update_info (controller)) {
                g_object_unref (controller);
                controller = NULL;
                goto out;
        }

        if (!register_disks_controller (DEVKIT_DISKS_CONTROLLER (controller))) {
                g_object_unref (controller);
                controller = NULL;
                goto out;
        }

out:
        return controller;
}

static void
drain_pending_changes (DevkitDisksController *controller, gboolean force_update)
{
        gboolean emit_changed;

        emit_changed = FALSE;

        /* the update-in-idle is set up if, and only if, there are pending changes - so
         * we should emit a 'change' event only if it is set up
         */
        if (controller->priv->emit_changed_idle_id != 0) {
                g_source_remove (controller->priv->emit_changed_idle_id);
                controller->priv->emit_changed_idle_id = 0;
                emit_changed = TRUE;
        }

        if ((!controller->priv->removed) && (emit_changed || force_update)) {
                if (controller->priv->object_path != NULL) {
                        g_print ("**** EMITTING CHANGED for %s\n", controller->priv->native_path);
                        g_signal_emit_by_name (controller, "changed");
                        g_signal_emit_by_name (controller->priv->daemon, "controller-changed", controller->priv->object_path);
                }
        }
}

/* called by the daemon on the 'change' uevent */
gboolean
devkit_disks_controller_changed (DevkitDisksController *controller, GUdevDevice *d, gboolean synthesized)
{
        gboolean keep_controller;

        g_object_unref (controller->priv->d);
        controller->priv->d = g_object_ref (d);

        keep_controller = update_info (controller);

        /* this 'change' event might prompt us to remove the controller */
        if (!keep_controller)
                goto out;

        /* no, it's good .. keep it.. and always force a 'change' signal if the event isn't synthesized */
        drain_pending_changes (controller, !synthesized);

out:
        return keep_controller;
}

/* ---------------------------------------------------------------------------------------------------- */

const char *
devkit_disks_controller_local_get_object_path (DevkitDisksController *controller)
{
        return controller->priv->object_path;
}

const char *
devkit_disks_controller_local_get_native_path (DevkitDisksController *controller)
{
        return controller->priv->native_path;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * update_info:
 * @controller: the controller
 *
 * Update information about the controller.
 *
 * If one or more properties changed, the changes are scheduled to be emitted. Use
 * drain_pending_changes() to force emitting the pending changes (which is useful
 * before returning the result of an operation).
 *
 * Returns: #TRUE to keep (or add) the controller; #FALSE to ignore (or remove) the controller
 **/
static gboolean
update_info (DevkitDisksController *controller)
{
        gboolean ret;
        guint64 device_class;
        gchar *vendor;
        gchar *model;
        const gchar *driver;

        ret = FALSE;
        vendor = NULL;
        model = NULL;

        /* Only care about Mass Storage Controller devices */
        device_class = g_udev_device_get_sysfs_attr_as_uint64 (controller->priv->d, "class");
        if (((device_class & 0xff0000) >> 16) != 0x01)
                goto out;

        g_print ("**** UPDATING %s\n", controller->priv->native_path);

        vendor = g_strdup (g_udev_device_get_property (controller->priv->d, "ID_VENDOR_FROM_DATABASE"));
        model = g_strdup (g_udev_device_get_property (controller->priv->d, "ID_MODEL_FROM_DATABASE"));

        /* TODO: probably want subsystem vendor and model - for the controllers in my Thinkpad X61 (not T61!)
         * it looks like this
         *
         *  00:1f.1: vendor:        Intel Corporation"
         *           model:         82801HBM/HEM (ICH8M/ICH8M-E) IDE Controller
         *           subsys_vendor: Lenovo
         *           subsys_model:  ThinkPad T61
         *
         *  00:1f.2: vendor:        Intel Corporation
         *           model:         82801HBM/HEM (ICH8M/ICH8M-E) SATA AHCI Controller
         *           subsys_vendor: Lenovo
         *           subsys_model:  ThinkPad T61
         *
         * or maybe not...
         */

        /* TODO: we want some kind of "type" or "interconnect" for the
         * controller - e.g. SATA/PATA/SAS/FC/iSCSI - also want version
         * (e.g. SATA1, SATA2) and speed (e.g. 150MB/s, 300MB/s)
         */

        /* TODO: want some kind of information about the number of ports - and for
         * each port the "type" of connector - e.g. PATA, SATA, eSATA, SAS,
         * SASx4 (wide lane), FC... and the role (initiator or target)
         */

        /* TODO: want to convey some kind of information about where the controller
         * is located (express-card, pc-card, pci-slot, onboard)...
         */

        /* TODO: also, enclosure information (needs thought re SES-2 enclosure support) */

        if (vendor == NULL) {
                vendor = g_strdup_printf ("[vendor=0x%04x subsys=0x%04x]",
                                          g_udev_device_get_sysfs_attr_as_int (controller->priv->d, "vendor"),
                                          g_udev_device_get_sysfs_attr_as_int (controller->priv->d, "subsystem_vendor"));
        }
        if (model == NULL) {
                vendor = g_strdup_printf ("Storage Controller [model=0x%04x subsys=0x%04x]",
                                          g_udev_device_get_sysfs_attr_as_int (controller->priv->d, "device"),
                                          g_udev_device_get_sysfs_attr_as_int (controller->priv->d, "subsystem_device"));
        }

        driver = g_udev_device_get_driver (controller->priv->d);

        devkit_disks_controller_set_vendor (controller, vendor);
        devkit_disks_controller_set_model (controller, model);
        devkit_disks_controller_set_driver (controller, driver);

        ret = TRUE;

 out:
        g_free (vendor);
        g_free (model);
        return ret;
}

