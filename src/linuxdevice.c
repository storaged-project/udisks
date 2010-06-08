/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
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

#include "config.h"

#include <string.h>
#include <gudev/gudev.h>

#include "linuxdevice.h"

struct _LinuxDevicePrivate
{
  GUdevDevice *udev_device;
  gboolean visible;
  gchar *object_path;
};

enum
{
  PROP_0,
  PROP_UDEV_DEVICE,
  PROP_VISIBLE,
  PROP_OBJECT_PATH,
};

/* ---------------------------------------------------------------------------------------------------- */

static void device_iface_init (DeviceIface *iface);

/* ---------------------------------------------------------------------------------------------------- */

G_DEFINE_TYPE_WITH_CODE (LinuxDevice, linux_device, TYPE_DEVICE_STUB,
                         G_IMPLEMENT_INTERFACE (TYPE_DEVICE, device_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
linux_device_get_property (GObject      *object,
                           guint         prop_id,
                           GValue       *value,
                           GParamSpec   *pspec)
{
  LinuxDevice *device = LINUX_DEVICE (object);

  switch (prop_id)
    {
    case PROP_UDEV_DEVICE:
      g_value_set_object (value, linux_device_get_udev_device (device));
      break;

    case PROP_VISIBLE:
      g_value_set_boolean (value, linux_device_get_visible (device));
      break;

    case PROP_OBJECT_PATH:
      g_value_set_string (value, linux_device_get_object_path (device));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
linux_device_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  LinuxDevice *device = LINUX_DEVICE (object);

  switch (prop_id)
    {
    case PROP_UDEV_DEVICE:
      linux_device_set_udev_device (device, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
linux_device_finalize (GObject *object)
{
  LinuxDevice *device = LINUX_DEVICE (object);

  if (device->priv->udev_device != NULL)
    g_object_unref (device->priv->udev_device);
  g_free (device->priv->object_path);

  if (G_OBJECT_CLASS (linux_device_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (linux_device_parent_class)->finalize (object);
}

static void
linux_device_init (LinuxDevice *device)
{
  device->priv = G_TYPE_INSTANCE_GET_PRIVATE (device, TYPE_LINUX_DEVICE, LinuxDevicePrivate);
}

static void
linux_device_constructed (GObject *object)
{
  LinuxDevice *device = LINUX_DEVICE (object);

  linux_device_update (device);

  if (G_OBJECT_CLASS (linux_device_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (linux_device_parent_class)->constructed (object);
}

static void
linux_device_class_init (LinuxDeviceClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = linux_device_get_property;
  gobject_class->set_property = linux_device_set_property;
  gobject_class->constructed  = linux_device_constructed;
  gobject_class->finalize     = linux_device_finalize;

  g_type_class_add_private (klass, sizeof (LinuxDevicePrivate));

  g_object_class_install_property (gobject_class,
                                   PROP_UDEV_DEVICE,
                                   g_param_spec_object ("udev-device",
                                                        "UDev Device",
                                                        "The underlying GUdevDevice",
                                                        G_UDEV_TYPE_DEVICE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  g_object_class_install_property (gobject_class,
                                   PROP_VISIBLE,
                                   g_param_spec_boolean ("visible",
                                                         "Visible",
                                                         "Whether the device should be exported on D-Bus",
                                                         TRUE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_BLURB |
                                                         G_PARAM_STATIC_NICK));

  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_PATH,
                                   g_param_spec_string ("object-path",
                                                        "Object Path",
                                                        "The D-Bus object path to use for exporting the device",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));
}

/* ---------------------------------------------------------------------------------------------------- */

LinuxDevice *
linux_device_new (GUdevDevice *udev_device)
{
  return LINUX_DEVICE (g_object_new (TYPE_LINUX_DEVICE,
                                     "udev-device", udev_device,
                                     NULL));
}

GUdevDevice *
linux_device_get_udev_device (LinuxDevice *device)
{
  g_return_val_if_fail (IS_LINUX_DEVICE (device), NULL);
  return device->priv->udev_device;
}

void
linux_device_set_udev_device (LinuxDevice *device,
                              GUdevDevice *udev_device)
{
  g_return_if_fail (IS_LINUX_DEVICE (device));
  g_return_if_fail (G_UDEV_IS_DEVICE (udev_device));

  if (device->priv->udev_device != NULL)
    g_object_unref (device->priv->udev_device);
  device->priv->udev_device = g_object_ref (udev_device);
}

gboolean
linux_device_get_visible (LinuxDevice *device)
{
  g_return_val_if_fail (IS_LINUX_DEVICE (device), FALSE);
  return device->priv->visible;
}

const gchar *
linux_device_get_object_path (LinuxDevice *device)
{
  g_return_val_if_fail (IS_LINUX_DEVICE (device), NULL);
  return device->priv->object_path;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
device_iface_init (DeviceIface *iface)
{
  /* TODO */
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
util_compute_object_path (const gchar *path)
{
  const gchar *basename;
  GString *s;
  guint n;

  g_return_val_if_fail (path != NULL, NULL);

  basename = strrchr (path, '/');
  if (basename != NULL)
    basename++;
  else
    basename = path;

  s = g_string_new ("/org/freedesktop/UDisks/devices/");
  for (n = 0; basename[n] != '\0'; n++)
    {
      gint c = basename[n];

      /* D-Bus spec sez:
       *
       * Each element must only contain the ASCII characters "[A-Z][a-z][0-9]_"
       */
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
          g_string_append_c (s, c);
        }
      else
        {
          /* Escape bytes not in [A-Z][a-z][0-9] as _<hex-with-two-digits> */
          g_string_append_printf (s, "_%02x", c);
        }
    }

  return g_string_free (s, FALSE);
}

void
linux_device_update (LinuxDevice *device)
{
  g_return_if_fail (IS_LINUX_DEVICE (device));

  device_set_native_path (DEVICE (device), (gchar *) g_udev_device_get_sysfs_path (device->priv->udev_device));

  /* TODO */
  device->priv->visible = TRUE;
  device->priv->object_path = util_compute_object_path (device_get_native_path (DEVICE (device)));

  device_set_device_detection_time (DEVICE (device), device_get_device_detection_time (DEVICE (device)) + 1);
  device_set_device_media_detection_time (DEVICE (device), device_get_device_media_detection_time (DEVICE (device)) + 2);
}

/* ---------------------------------------------------------------------------------------------------- */
