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
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "devkit-disks-device.h"

/*--------------------------------------------------------------------------------------------------------------*/
#include "devkit-disks-device-glue.h"

struct DevkitDisksDevicePrivate
{
        DBusGConnection *system_bus_connection;
        DBusGProxy      *system_bus_proxy;
        DevkitDisksDaemon *daemon;
        char *device_path;
        char *object_path;
};

static void     devkit_disks_device_class_init  (DevkitDisksDeviceClass *klass);
static void     devkit_disks_device_init        (DevkitDisksDevice      *seat);
static void     devkit_disks_device_finalize    (GObject     *object);

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
                static const GEnumValue values[] =
                        {
                                ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_GENERAL, "GeneralError"),
                                ENUM_ENTRY (DEVKIT_DISKS_DEVICE_ERROR_NOT_AUTHORIZED, "NotAuthorized"),
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
devkit_disks_device_class_init (DevkitDisksDeviceClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = devkit_disks_device_constructor;
        object_class->finalize = devkit_disks_device_finalize;

        g_type_class_add_private (klass, sizeof (DevkitDisksDevicePrivate));

        dbus_g_object_type_install_info (DEVKIT_TYPE_DISKS_DEVICE, &dbus_glib_devkit_disks_device_object_info);

        dbus_g_error_domain_register (DEVKIT_DISKS_DEVICE_ERROR, NULL, DEVKIT_DISKS_DEVICE_TYPE_ERROR);

}

static void
devkit_disks_device_init (DevkitDisksDevice *device)
{
        device->priv = DEVKIT_DISKS_DEVICE_GET_PRIVATE (device);

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
        g_free (device->priv->device_path);
        g_free (device->priv->object_path);

        G_OBJECT_CLASS (devkit_disks_device_parent_class)->finalize (object);
}

static gboolean
register_disks_device (DevkitDisksDevice *device)
{
        DBusConnection *connection;
        char *basename;
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

        basename = g_path_get_basename (device->priv->device_path);
        device->priv->object_path = g_build_filename ("/devices/", basename, NULL);
        g_free (basename);

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


DevkitDisksDevice *
devkit_disks_device_new (DevkitDisksDaemon *daemon, const char *device_path)
{
        DevkitDisksDevice *device;
        gboolean res;

        device = DEVKIT_DISKS_DEVICE (g_object_new (DEVKIT_TYPE_DISKS_DEVICE, NULL));
        device->priv->daemon = g_object_ref (daemon);
        device->priv->device_path = g_strdup (device_path);

        res = register_disks_device (DEVKIT_DISKS_DEVICE (device));
        if (! res) {
                g_object_unref (device);
                return NULL;
        }

        return device;
}

/*--------------------------------------------------------------------------------------------------------------*/

/**
 * devkit_disks_enumerate_devices:
 *
 * Enumerates all block devices on the system.
 *
 * Returns: A #GList of device paths (on Linux the sysfs path)
 */
GList *
devkit_disks_enumerate_devices (void)
{
        GList *ret;
        GDir *dir;
        const char *name;

        ret = 0;

        dir = g_dir_open ("/sys/class/block", 0, NULL);
        if (dir == NULL)
                goto out;

        while ((name = g_dir_read_name (dir)) != NULL) {
                char *s;
                char sysfs_path[PATH_MAX];

                s = g_build_filename ("/sys/class/block", name, NULL);
                if (realpath (s, sysfs_path) == NULL) {
                        g_free (s);
                        continue;
                }
                g_free (s);

                ret = g_list_prepend (ret, g_strdup (sysfs_path));
        }
        g_dir_close (dir);

out:
        return ret;
}

/*--------------------------------------------------------------------------------------------------------------*/

const char *
devkit_disks_device_get_object_path (DevkitDisksDevice *device)
{
        return device->priv->object_path;
}

const char *
devkit_disks_device_get_device_path (DevkitDisksDevice *device)
{
        return device->priv->device_path;
}

/*--------------------------------------------------------------------------------------------------------------*/
/* exported methods */

gboolean
devkit_disks_device_get_device_file (DevkitDisksDevice     *device,
                                     DBusGMethodInvocation *context)
{
        /* TODO */
        dbus_g_method_return (context, "/dev/disk/something");
        return TRUE;
}


