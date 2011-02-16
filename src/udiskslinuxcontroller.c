/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#include <stdio.h>
#include <string.h>

#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxcontroller.h"

/**
 * SECTION:udiskslinuxcontroller
 * @title: UDisksLinuxController
 * @short_description: Linux Disk Controllers (ATA, SCSI, etc.)
 *
 * Object corresponding to a controller on Linux.
 */

typedef struct _UDisksLinuxControllerClass   UDisksLinuxControllerClass;

/**
 * UDisksLinuxController:
 *
 * The #UDisksLinuxController structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxController
{
  GDBusObject parent_instance;

  UDisksDaemon *daemon;

  GUdevDevice *device;

  /* interfaces */
  UDisksController *iface_controller;
};

struct _UDisksLinuxControllerClass
{
  GDBusObjectClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

static gboolean udisks_linux_controller_check_device (GUdevDevice *device);

G_DEFINE_TYPE (UDisksLinuxController, udisks_linux_controller, G_TYPE_DBUS_OBJECT);

static void
udisks_linux_controller_finalize (GObject *object)
{
  UDisksLinuxController *controller = UDISKS_LINUX_CONTROLLER (object);

  /* note: we don't hold a ref to controller->daemon or controller->mount_monitor */

  g_object_unref (controller->device);

  if (controller->iface_controller != NULL)
    g_object_unref (controller->iface_controller);

  if (G_OBJECT_CLASS (udisks_linux_controller_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_controller_parent_class)->finalize (object);
}

static void
udisks_linux_controller_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  UDisksLinuxController *controller = UDISKS_LINUX_CONTROLLER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_controller_get_daemon (controller));
      break;

    case PROP_DEVICE:
      g_value_set_object (value, udisks_linux_controller_get_device (controller));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_controller_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  UDisksLinuxController *controller = UDISKS_LINUX_CONTROLLER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (controller->daemon == NULL);
      /* we don't take a reference to the daemon */
      controller->daemon = g_value_get_object (value);
      break;

    case PROP_DEVICE:
      g_assert (controller->device == NULL);
      controller->device = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
udisks_linux_controller_init (UDisksLinuxController *controller)
{
}

static GObjectConstructParam *
find_construct_property (guint                  n_construct_properties,
                         GObjectConstructParam *construct_properties,
                         const gchar           *name)
{
  guint n;
  for (n = 0; n < n_construct_properties; n++)
    if (g_strcmp0 (g_param_spec_get_name (construct_properties[n].pspec), name) == 0)
      return &construct_properties[n];
  return NULL;
}

/* unless given, compute object path from sysfs path */
static GObject *
udisks_linux_controller_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
  GObjectConstructParam *device_cp;
  GUdevDevice *device;

  device_cp = find_construct_property (n_construct_properties, construct_properties, "device");
  g_assert (device_cp != NULL);

  device = G_UDEV_DEVICE (g_value_get_object (device_cp->value));
  g_assert (device != NULL);

  if (!udisks_linux_controller_check_device (device))
    {
      return NULL;
    }
  else
    {
      return G_OBJECT_CLASS (udisks_linux_controller_parent_class)->constructor (type,
                                                                            n_construct_properties,
                                                                            construct_properties);
    }
}

static void
strip_and_replace_with_uscore (gchar *s)
{
  guint n;

  if (s == NULL)
    goto out;

  g_strstrip (s);

  for (n = 0; s != NULL && s[n] != '\0'; n++)
    {
      if (s[n] == ' ' || s[n] == '-')
        s[n] = '_';
    }

 out:
  ;
}

static void
udisks_linux_controller_constructed (GObject *object)
{
  UDisksLinuxController *controller = UDISKS_LINUX_CONTROLLER (object);
  gchar *vendor;
  gchar *model;
  gchar *serial;
  GString *str;

  /* initial coldplug */
  udisks_linux_controller_uevent (controller, "add", NULL);

  /* compute the object path */
  vendor = g_strdup (udisks_controller_get_vendor (controller->iface_controller));
  model = g_strdup (udisks_controller_get_model (controller->iface_controller));
  serial = g_strdup (udisks_controller_get_serial (controller->iface_controller));
  strip_and_replace_with_uscore (vendor);
  strip_and_replace_with_uscore (model);
  strip_and_replace_with_uscore (serial);
  str = g_string_new ("/org/freedesktop/UDisks2/controllers/");
  if (vendor == NULL && model == NULL && serial == NULL)
    {
      g_string_append (str, "controller");
    }
  else
    {
      /* TODO: use slot information */

      /* <VENDOR>_<MODEL>_<SERIAL> */
      if (vendor != NULL && strlen (vendor) > 0)
        {
          udisks_safe_append_to_object_path (str, vendor);
        }
      if (model != NULL && strlen (model) > 0)
        {
          if (str->str[str->len - 1] != '/')
            g_string_append_c (str, '_');
          udisks_safe_append_to_object_path (str, model);
        }
      if (serial != NULL && strlen (serial) > 0)
        {
          if (str->str[str->len - 1] != '/')
            g_string_append_c (str, '_');
          udisks_safe_append_to_object_path (str, serial);
        }
    }
  g_free (vendor);
  g_free (model);
  g_free (serial);
  g_dbus_object_set_object_path (G_DBUS_OBJECT (controller), str->str);
  g_string_free (str, TRUE);

  if (G_OBJECT_CLASS (udisks_linux_controller_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_controller_parent_class)->constructed (object);
}

static void
udisks_linux_controller_class_init (UDisksLinuxControllerClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructor  = udisks_linux_controller_constructor;
  gobject_class->finalize     = udisks_linux_controller_finalize;
  gobject_class->constructed  = udisks_linux_controller_constructed;
  gobject_class->set_property = udisks_linux_controller_set_property;
  gobject_class->get_property = udisks_linux_controller_get_property;

  /**
   * UDisksLinuxController:daemon:
   *
   * The #UDisksDaemon the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the object is for",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksLinuxController:device:
   *
   * The #GUdevDevice for the object. Connect to the #GObject::notify
   * signal to get notified whenever this is updated.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DEVICE,
                                   g_param_spec_object ("device",
                                                        "Device",
                                                        "The device for the object",
                                                        G_UDEV_TYPE_DEVICE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

}

/**
 * udisks_linux_controller_new:
 * @daemon: A #UDisksDaemon.
 * @device: The #GUdevDevice for the sysfs controller device.
 *
 * Create a new controller object.
 *
 * Returns: A #UDisksLinuxController object or %NULL if @device does not represent a controller. Free with g_object_unref().
 */
UDisksLinuxController *
udisks_linux_controller_new (UDisksDaemon  *daemon,
                        GUdevDevice   *device)
{
  GObject *object;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), NULL);

  object = g_object_new (UDISKS_TYPE_LINUX_CONTROLLER,
                         "daemon", daemon,
                         "device", device,
                         NULL);

  if (object != NULL)
    return UDISKS_LINUX_CONTROLLER (object);
  else
    return NULL;
}

/**
 * udisks_linux_controller_get_daemon:
 * @controller: A #UDisksLinuxController.
 *
 * Gets the daemon used by @controller.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @controller.
 */
UDisksDaemon *
udisks_linux_controller_get_daemon (UDisksLinuxController *controller)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_CONTROLLER (controller), NULL);
  return controller->daemon;
}

/**
 * udisks_linux_controller_get_device:
 * @controller: A #UDisksLinuxController.
 *
 * Gets the current #GUdevDevice for @controller. Connect to
 * #GObject::notify to track changes to the #UDisksLinuxController:device
 * property.
 *
 * Returns: A #GUdevDevice. Free with g_object_unref().
 */
GUdevDevice *
udisks_linux_controller_get_device (UDisksLinuxController *controller)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_CONTROLLER (controller), NULL);
  return g_object_ref (controller->device);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef gboolean (*HasInterfaceFunc)    (UDisksLinuxController     *controller);
typedef void     (*UpdateInterfaceFunc) (UDisksLinuxController     *controller,
                                         const gchar    *uevent_action,
                                         GDBusInterface *interface);

static void
update_iface (UDisksLinuxController           *controller,
              const gchar          *uevent_action,
              HasInterfaceFunc      has_func,
              UpdateInterfaceFunc   update_func,
              GType                 stub_type,
              gpointer              _interface_pointer)
{
  gboolean has;
  gboolean add;
  GDBusInterface **interface_pointer = _interface_pointer;

  g_return_if_fail (controller != NULL);
  g_return_if_fail (has_func != NULL);
  g_return_if_fail (update_func != NULL);
  g_return_if_fail (g_type_is_a (stub_type, G_TYPE_OBJECT));
  g_return_if_fail (g_type_is_a (stub_type, G_TYPE_DBUS_INTERFACE));
  g_return_if_fail (interface_pointer != NULL);
  g_return_if_fail (*interface_pointer == NULL || G_IS_DBUS_INTERFACE (*interface_pointer));

  add = FALSE;
  has = has_func (controller);
  if (*interface_pointer == NULL)
    {
      if (has)
        {
          *interface_pointer = g_object_new (stub_type, NULL);
          add = TRUE;
        }
    }
  else
    {
      if (!has)
        {
          g_dbus_object_remove_interface (G_DBUS_OBJECT (controller), G_DBUS_INTERFACE (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      update_func (controller, uevent_action, G_DBUS_INTERFACE (*interface_pointer));
      if (add)
        g_dbus_object_add_interface (G_DBUS_OBJECT (controller), G_DBUS_INTERFACE (*interface_pointer));
    }
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.Controller */

static gboolean
controller_check (UDisksLinuxController *controller)
{
  return TRUE;
}

static void
controller_update (UDisksLinuxController  *controller,
                   const gchar            *uevent_action,
                   GDBusInterface         *_iface)
{
  UDisksController *iface = UDISKS_CONTROLLER (_iface);
  gchar *vendor;
  gchar *model;
  gchar *address;

  vendor = g_strdup (g_udev_device_get_property (controller->device, "ID_VENDOR_FROM_DATABASE"));
  if (vendor == NULL)
    {
      vendor = g_strdup_printf ("[vendor=0x%04x subsys=0x%04x]",
                                g_udev_device_get_sysfs_attr_as_int (controller->device, "vendor"),
                                g_udev_device_get_sysfs_attr_as_int (controller->device, "subsystem_vendor"));
    }

  model = g_strdup (g_udev_device_get_property (controller->device, "ID_MODEL_FROM_DATABASE"));
  if (model == NULL)
    {
      vendor = g_strdup_printf ("[model=0x%04x subsys=0x%04x]",
                                g_udev_device_get_sysfs_attr_as_int (controller->device, "device"),
                                g_udev_device_get_sysfs_attr_as_int (controller->device, "subsystem_device"));
    }

  udisks_controller_set_vendor (iface, vendor);
  udisks_controller_set_model (iface, model);

  address = g_strdup (g_udev_device_get_property (controller->device, "PCI_SLOT_NAME"));
  if (address != NULL)
    {
      gchar *s;

      g_strstrip (address);
      udisks_controller_set_address (iface, address);

      s = g_strrstr (address, ".");
      if (s != NULL)
        {
          GDir *dir;
          gchar *slot_name;

          *s = '\0';

          /* Now look in /sys/bus/pci/slots/SLOTNAME/address - annoyingly, there
           * are no symlinks... grr..
           */
          slot_name = NULL;
          dir = g_dir_open ("/sys/bus/pci/slots", 0, NULL);
          if (dir != NULL)
            {
              const gchar *name;
              while ((name = g_dir_read_name (dir)) != NULL && slot_name == NULL)
                {
                  gchar *address_file;
                  gchar *address_for_slot;
                  address_file = g_strdup_printf ("/sys/bus/pci/slots/%s/address", name);
                  if (g_file_get_contents (address_file, &address_for_slot, NULL, NULL))
                    {
                      g_strstrip (address_for_slot);
                      if (g_strcmp0 (address, address_for_slot) == 0)
                        {
                          slot_name = g_strdup (name);
                        }
                      g_free (address_for_slot);
                    }
                  g_free (address_file);
                }
              g_dir_close (dir);
            }

          udisks_controller_set_physical_slot (iface, slot_name);
        }
    }

  g_free (vendor);
  g_free (model);
  g_free (address);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_controller_uevent:
 * @controller: A #UDisksLinuxController.
 * @action: Uevent action or %NULL
 * @device: A new #GUdevDevice device object or %NULL if the device hasn't changed.
 *
 * Updates all information on interfaces on @controller.
 */
void
udisks_linux_controller_uevent (UDisksLinuxController *controller,
                           const gchar      *action,
                           GUdevDevice      *device)
{
  g_return_if_fail (UDISKS_IS_LINUX_CONTROLLER (controller));
  g_return_if_fail (device == NULL || G_UDEV_IS_DEVICE (device));

  if (device != NULL)
    {
      g_object_unref (controller->device);
      controller->device = g_object_ref (device);
      g_object_notify (G_OBJECT (controller), "device");
    }

  update_iface (controller, action, controller_check, controller_update,
                UDISKS_TYPE_CONTROLLER_STUB, &controller->iface_controller);
}

/* ---------------------------------------------------------------------------------------------------- */

/* <internal>
 * udisks_linux_controller_check_device:
 * @device: A #GUdevDevice.
 *
 * Checks if we should even construct a #UDisksLinuxController for @device.
 *
 * Returns: %TRUE if we should construct an object, %FALSE otherwise.
 */
static gboolean
udisks_linux_controller_check_device (GUdevDevice *device)
{
  gboolean ret;
  GDir *dir;
  guint num_scsi_host_objects;

  ret = FALSE;

  num_scsi_host_objects = 0;
  dir = g_dir_open (g_udev_device_get_sysfs_path (device), 0, NULL);
  if (dir != NULL)
    {
      const gchar *name;
      while ((name = g_dir_read_name (dir)) != NULL)
        {
          gint number;
          if (sscanf (name, "host%d", &number) != 1)
            continue;

          num_scsi_host_objects++;
        }
      g_dir_close (dir);
    }

  /* For now, don't bother if no driver is bound */
  if (num_scsi_host_objects == 0)
    goto out;

  ret = TRUE;

 out:
  return ret;
}
