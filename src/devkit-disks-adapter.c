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
#include "devkit-disks-adapter.h"
#include "devkit-disks-adapter-private.h"
#include "devkit-disks-marshal.h"

/*--------------------------------------------------------------------------------------------------------------*/
#include "devkit-disks-adapter-glue.h"

static void     devkit_disks_adapter_class_init  (DevkitDisksAdapterClass *klass);
static void     devkit_disks_adapter_init        (DevkitDisksAdapter      *seat);
static void     devkit_disks_adapter_finalize    (GObject     *object);

static gboolean update_info                (DevkitDisksAdapter *adapter);

static void     drain_pending_changes (DevkitDisksAdapter *adapter, gboolean force_update);

enum
{
        PROP_0,
        PROP_NATIVE_PATH,

        PROP_VENDOR,
        PROP_MODEL,
        PROP_DRIVER,
        PROP_NUM_PORTS,
        PROP_FABRIC,
};

enum
{
        CHANGED_SIGNAL,
        LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (DevkitDisksAdapter, devkit_disks_adapter, G_TYPE_OBJECT)

#define DEVKIT_DISKS_ADAPTER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_DISKS_TYPE_ADAPTER, DevkitDisksAdapterPrivate))

static void
get_property (GObject         *object,
              guint            prop_id,
              GValue          *value,
              GParamSpec      *pspec)
{
        DevkitDisksAdapter *adapter = DEVKIT_DISKS_ADAPTER (object);

        switch (prop_id) {
        case PROP_NATIVE_PATH:
                g_value_set_string (value, adapter->priv->native_path);
                break;

        case PROP_VENDOR:
                g_value_set_string (value, adapter->priv->vendor);
                break;

        case PROP_MODEL:
                g_value_set_string (value, adapter->priv->model);
                break;

        case PROP_DRIVER:
                g_value_set_string (value, adapter->priv->driver);
                break;

        case PROP_NUM_PORTS:
                g_value_set_uint (value, adapter->priv->num_ports);
                break;

        case PROP_FABRIC:
                g_value_set_string (value, adapter->priv->fabric);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
devkit_disks_adapter_class_init (DevkitDisksAdapterClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = devkit_disks_adapter_finalize;
        object_class->get_property = get_property;

        g_type_class_add_private (klass, sizeof (DevkitDisksAdapterPrivate));

        signals[CHANGED_SIGNAL] =
                g_signal_new ("changed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        dbus_g_object_type_install_info (DEVKIT_DISKS_TYPE_ADAPTER, &dbus_glib_devkit_disks_adapter_object_info);

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
        g_object_class_install_property (
                object_class,
                PROP_NUM_PORTS,
                g_param_spec_uint ("num-ports", NULL, NULL, 0, G_MAXUINT, 0, G_PARAM_READABLE));
        g_object_class_install_property (
                object_class,
                PROP_FABRIC,
                g_param_spec_string ("fabric", NULL, NULL, NULL, G_PARAM_READABLE));
}

static void
devkit_disks_adapter_init (DevkitDisksAdapter *adapter)
{
        adapter->priv = DEVKIT_DISKS_ADAPTER_GET_PRIVATE (adapter);
}

static void
devkit_disks_adapter_finalize (GObject *object)
{
        DevkitDisksAdapter *adapter;

        g_return_if_fail (object != NULL);
        g_return_if_fail (DEVKIT_DISKS_IS_ADAPTER (object));

        adapter = DEVKIT_DISKS_ADAPTER (object);
        g_return_if_fail (adapter->priv != NULL);

        /* g_debug ("finalizing %s", adapter->priv->native_path); */

        g_object_unref (adapter->priv->d);
        g_object_unref (adapter->priv->daemon);
        g_free (adapter->priv->object_path);

        g_free (adapter->priv->native_path);

        if (adapter->priv->emit_changed_idle_id > 0)
                g_source_remove (adapter->priv->emit_changed_idle_id);

        /* free properties */
        g_free (adapter->priv->vendor);
        g_free (adapter->priv->model);
        g_free (adapter->priv->driver);

        G_OBJECT_CLASS (devkit_disks_adapter_parent_class)->finalize (object);
}

/**
 * compute_object_path:
 * @native_path: Either an absolute sysfs path or the basename
 *
 * Maps @native_path to the D-Bus object path for the adapter.
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

        s = g_string_new ("/org/freedesktop/DeviceKit/Disks/adapters/");
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
register_disks_adapter (DevkitDisksAdapter *adapter)
{
        DBusConnection *connection;
        GError *error = NULL;

        adapter->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (adapter->priv->system_bus_connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                goto error;
        }
        connection = dbus_g_connection_get_connection (adapter->priv->system_bus_connection);

        adapter->priv->object_path = compute_object_path (adapter->priv->native_path);

        /* safety first */
        if (dbus_g_connection_lookup_g_object (adapter->priv->system_bus_connection,
                                               adapter->priv->object_path) != NULL) {
                g_error ("**** HACK: Wanting to register object at path `%s' but there is already an "
                         "object there. This is an internal error in the daemon. Aborting.\n",
                         adapter->priv->object_path);
        }

        dbus_g_connection_register_g_object (adapter->priv->system_bus_connection,
                                             adapter->priv->object_path,
                                             G_OBJECT (adapter));

        return TRUE;

error:
        return FALSE;
}

void
devkit_disks_adapter_removed (DevkitDisksAdapter *adapter)
{
        adapter->priv->removed = TRUE;

        dbus_g_connection_unregister_g_object (adapter->priv->system_bus_connection,
                                               G_OBJECT (adapter));
        g_assert (dbus_g_connection_lookup_g_object (adapter->priv->system_bus_connection,
                                                     adapter->priv->object_path) == NULL);
}

DevkitDisksAdapter *
devkit_disks_adapter_new (DevkitDisksDaemon *daemon, GUdevDevice *d)
{
        DevkitDisksAdapter *adapter;
        const char *native_path;

        adapter = NULL;
        native_path = g_udev_device_get_sysfs_path (d);

        adapter = DEVKIT_DISKS_ADAPTER (g_object_new (DEVKIT_DISKS_TYPE_ADAPTER, NULL));
        adapter->priv->d = g_object_ref (d);
        adapter->priv->daemon = g_object_ref (daemon);
        adapter->priv->native_path = g_strdup (native_path);

        if (!update_info (adapter)) {
                g_object_unref (adapter);
                adapter = NULL;
                goto out;
        }

        if (!register_disks_adapter (DEVKIT_DISKS_ADAPTER (adapter))) {
                g_object_unref (adapter);
                adapter = NULL;
                goto out;
        }

out:
        return adapter;
}

static void
drain_pending_changes (DevkitDisksAdapter *adapter, gboolean force_update)
{
        gboolean emit_changed;

        emit_changed = FALSE;

        /* the update-in-idle is set up if, and only if, there are pending changes - so
         * we should emit a 'change' event only if it is set up
         */
        if (adapter->priv->emit_changed_idle_id != 0) {
                g_source_remove (adapter->priv->emit_changed_idle_id);
                adapter->priv->emit_changed_idle_id = 0;
                emit_changed = TRUE;
        }

        if ((!adapter->priv->removed) && (emit_changed || force_update)) {
                if (adapter->priv->object_path != NULL) {
                        g_print ("**** EMITTING CHANGED for %s\n", adapter->priv->native_path);
                        g_signal_emit_by_name (adapter, "changed");
                        g_signal_emit_by_name (adapter->priv->daemon, "adapter-changed", adapter->priv->object_path);
                }
        }
}

/* called by the daemon on the 'change' uevent */
gboolean
devkit_disks_adapter_changed (DevkitDisksAdapter *adapter, GUdevDevice *d, gboolean synthesized)
{
        gboolean keep_adapter;

        g_object_unref (adapter->priv->d);
        adapter->priv->d = g_object_ref (d);

        keep_adapter = update_info (adapter);

        /* this 'change' event might prompt us to remove the adapter */
        if (!keep_adapter)
                goto out;

        /* no, it's good .. keep it.. and always force a 'change' signal if the event isn't synthesized */
        drain_pending_changes (adapter, !synthesized);

out:
        return keep_adapter;
}

/* ---------------------------------------------------------------------------------------------------- */

const char *
devkit_disks_adapter_local_get_object_path (DevkitDisksAdapter *adapter)
{
        return adapter->priv->object_path;
}

const char *
devkit_disks_adapter_local_get_native_path (DevkitDisksAdapter *adapter)
{
        return adapter->priv->native_path;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * update_info:
 * @adapter: the adapter
 *
 * Update information about the adapter.
 *
 * If one or more properties changed, the changes are scheduled to be emitted. Use
 * drain_pending_changes() to force emitting the pending changes (which is useful
 * before returning the result of an operation).
 *
 * Returns: #TRUE to keep (or add) the adapter; #FALSE to ignore (or remove) the adapter
 **/
static gboolean
update_info (DevkitDisksAdapter *adapter)
{
        gboolean ret;
        guint64 device_class;
        gchar *vendor;
        gchar *model;
        const gchar *driver;

        ret = FALSE;
        vendor = NULL;
        model = NULL;

        /* Only care about Mass Storage Adapter devices */
        device_class = g_udev_device_get_sysfs_attr_as_uint64 (adapter->priv->d, "class");
        if (((device_class & 0xff0000) >> 16) != 0x01)
                goto out;

        g_print ("**** UPDATING %s\n", adapter->priv->native_path);

        vendor = g_strdup (g_udev_device_get_property (adapter->priv->d, "ID_VENDOR_FROM_DATABASE"));
        model = g_strdup (g_udev_device_get_property (adapter->priv->d, "ID_MODEL_FROM_DATABASE"));

        /* TODO: probably want subsystem vendor and model - for the adapters in my Thinkpad X61 (not T61!)
         * it looks like this
         *
         *  00:1f.1: vendor:        Intel Corporation"
         *           model:         82801HBM/HEM (ICH8M/ICH8M-E) IDE Adapter
         *           subsys_vendor: Lenovo
         *           subsys_model:  ThinkPad T61
         *
         *  00:1f.2: vendor:        Intel Corporation
         *           model:         82801HBM/HEM (ICH8M/ICH8M-E) SATA AHCI Adapter
         *           subsys_vendor: Lenovo
         *           subsys_model:  ThinkPad T61
         *
         * or maybe not...
         */

        /* TODO: we want some kind of "type" or "interconnect" for the
         * adapter - e.g. SATA/PATA/SAS/FC/iSCSI - also want version
         * (e.g. SATA1, SATA2) and speed (e.g. 150MB/s, 300MB/s)
         */

        /* TODO: want some kind of information about the number of ports - and for
         * each port the "type" of connector - e.g. PATA, SATA, eSATA, SAS,
         * SASx4 (wide lane), FC... and the role (initiator or target)
         */

        /* TODO: want to convey some kind of information about where the adapter
         * is located (express-card, pc-card, pci-slot, onboard)...
         */

        /* TODO: also, enclosure information (needs thought re SES-2 enclosure support) */

        if (vendor == NULL) {
                vendor = g_strdup_printf ("[vendor=0x%04x subsys=0x%04x]",
                                          g_udev_device_get_sysfs_attr_as_int (adapter->priv->d, "vendor"),
                                          g_udev_device_get_sysfs_attr_as_int (adapter->priv->d, "subsystem_vendor"));
        }
        if (model == NULL) {
                vendor = g_strdup_printf ("Storage Adapter [model=0x%04x subsys=0x%04x]",
                                          g_udev_device_get_sysfs_attr_as_int (adapter->priv->d, "device"),
                                          g_udev_device_get_sysfs_attr_as_int (adapter->priv->d, "subsystem_device"));
        }

        driver = g_udev_device_get_driver (adapter->priv->d);

        devkit_disks_adapter_set_vendor (adapter, vendor);
        devkit_disks_adapter_set_model (adapter, model);
        devkit_disks_adapter_set_driver (adapter, driver);

        ret = TRUE;

 out:
        g_free (vendor);
        g_free (model);
        return ret;
}

