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

#include <string.h>

#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxdrive.h"

/**
 * SECTION:udiskslinuxdrive
 * @title: UDisksLinuxDrive
 * @short_description: Linux drives (ATA, SCSI, etc.)
 *
 * Object corresponding to a drive on Linux.
 */

typedef struct _UDisksLinuxDriveClass   UDisksLinuxDriveClass;

/**
 * UDisksLinuxDrive:
 *
 * The #UDisksLinuxDrive structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxDrive
{
  GDBusObject parent_instance;

  UDisksDaemon *daemon;

  /* list of GUdevDevice objects for scsi_device objects */
  GList *devices;

  /* interfaces */
  UDisksDrive *iface_drive;
};

struct _UDisksLinuxDriveClass
{
  GDBusObjectClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

G_DEFINE_TYPE (UDisksLinuxDrive, udisks_linux_drive, G_TYPE_DBUS_OBJECT);

static void
udisks_linux_drive_finalize (GObject *object)
{
  UDisksLinuxDrive *drive = UDISKS_LINUX_DRIVE (object);

  /* note: we don't hold a ref to drive->daemon or drive->mount_monitor */

  g_list_foreach (drive->devices, (GFunc) g_object_unref, NULL);
  g_list_free (drive->devices);

  if (drive->iface_drive != NULL)
    g_object_unref (drive->iface_drive);

  if (G_OBJECT_CLASS (udisks_linux_drive_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_drive_parent_class)->finalize (object);
}

static void
udisks_linux_drive_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  UDisksLinuxDrive *drive = UDISKS_LINUX_DRIVE (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_drive_get_daemon (drive));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_drive_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  UDisksLinuxDrive *drive = UDISKS_LINUX_DRIVE (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (drive->daemon == NULL);
      /* we don't take a reference to the daemon */
      drive->daemon = g_value_get_object (value);
      break;

    case PROP_DEVICE:
      g_assert (drive->devices == NULL);
      drive->devices = g_list_prepend (NULL, g_value_dup_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
udisks_linux_drive_init (UDisksLinuxDrive *drive)
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
udisks_linux_drive_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
  GObjectConstructParam *device_cp;
  GUdevDevice *device;

  device_cp = find_construct_property (n_construct_properties, construct_properties, "device");
  g_assert (device_cp != NULL);

  device = G_UDEV_DEVICE (g_value_get_object (device_cp->value));
  g_assert (device != NULL);

  if (!udisks_linux_drive_should_include_device (device, NULL))
    {
      return NULL;
    }
  else
    {
      return G_OBJECT_CLASS (udisks_linux_drive_parent_class)->constructor (type,
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
udisks_linux_drive_constructed (GObject *object)
{
  UDisksLinuxDrive *drive = UDISKS_LINUX_DRIVE (object);
  gchar *vendor;
  gchar *model;
  gchar *serial;
  GString *str;

  /* initial coldplug */
  udisks_linux_drive_uevent (drive, "add", drive->devices->data);

  /* compute the object path */
  vendor = g_strdup (udisks_drive_get_vendor (drive->iface_drive));
  model = g_strdup (udisks_drive_get_model (drive->iface_drive));
  serial = g_strdup (udisks_drive_get_serial (drive->iface_drive));
  strip_and_replace_with_uscore (vendor);
  strip_and_replace_with_uscore (model);
  strip_and_replace_with_uscore (serial);
  str = g_string_new ("/org/freedesktop/UDisks2/drives/");
  if (vendor == NULL && model == NULL && serial == NULL)
    {
      g_string_append (str, "drive");
    }
  else
    {
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
  g_dbus_object_set_object_path (G_DBUS_OBJECT (drive), str->str);
  g_string_free (str, TRUE);

  if (G_OBJECT_CLASS (udisks_linux_drive_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_drive_parent_class)->constructed (object);
}

static void
udisks_linux_drive_class_init (UDisksLinuxDriveClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructor  = udisks_linux_drive_constructor;
  gobject_class->finalize     = udisks_linux_drive_finalize;
  gobject_class->constructed  = udisks_linux_drive_constructed;
  gobject_class->set_property = udisks_linux_drive_set_property;
  gobject_class->get_property = udisks_linux_drive_get_property;

  /**
   * UDisksLinuxDrive:daemon:
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
   * UDisksLinuxDrive:device:
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
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

}

/**
 * udisks_linux_drive_new:
 * @daemon: A #UDisksDaemon.
 * @device: The #GUdevDevice for the sysfs drive device.
 *
 * Create a new drive object.
 *
 * Returns: A #UDisksLinuxDrive object or %NULL if @device does not represent a drive. Free with g_object_unref().
 */
UDisksLinuxDrive *
udisks_linux_drive_new (UDisksDaemon  *daemon,
                        GUdevDevice   *device)
{
  GObject *object;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), NULL);

  object = g_object_new (UDISKS_TYPE_LINUX_DRIVE,
                         "daemon", daemon,
                         "device", device,
                         NULL);

  if (object != NULL)
    return UDISKS_LINUX_DRIVE (object);
  else
    return NULL;
}

/**
 * udisks_linux_drive_get_daemon:
 * @drive: A #UDisksLinuxDrive.
 *
 * Gets the daemon used by @drive.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @drive.
 */
UDisksDaemon *
udisks_linux_drive_get_daemon (UDisksLinuxDrive *drive)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE (drive), NULL);
  return drive->daemon;
}

/**
 * udisks_linux_drive_get_devices:
 * @drive: A #UDisksLinuxDrive.
 *
 * Gets the current #GUdevDevice objects associated with @drive.
 *
 * Returns: A list of #GUdevDevice objects. Free each element with
 * g_object_unref(), then free the list with g_list_free().
 */
GList *
udisks_linux_drive_get_devices (UDisksLinuxDrive *drive)
{
  GList *ret;
  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE (drive), NULL);
  ret = g_list_copy (drive->devices);
  g_list_foreach (ret, (GFunc) g_object_ref, NULL);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef gboolean (*HasInterfaceFunc)    (UDisksLinuxDrive     *drive);
typedef void     (*UpdateInterfaceFunc) (UDisksLinuxDrive     *drive,
                                         const gchar    *uevent_action,
                                         GDBusInterface *interface);

static void
update_iface (UDisksLinuxDrive           *drive,
              const gchar          *uevent_action,
              HasInterfaceFunc      has_func,
              UpdateInterfaceFunc   update_func,
              GType                 stub_type,
              gpointer              _interface_pointer)
{
  gboolean has;
  gboolean add;
  GDBusInterface **interface_pointer = _interface_pointer;

  g_return_if_fail (drive != NULL);
  g_return_if_fail (has_func != NULL);
  g_return_if_fail (update_func != NULL);
  g_return_if_fail (g_type_is_a (stub_type, G_TYPE_OBJECT));
  g_return_if_fail (g_type_is_a (stub_type, G_TYPE_DBUS_INTERFACE));
  g_return_if_fail (interface_pointer != NULL);
  g_return_if_fail (*interface_pointer == NULL || G_IS_DBUS_INTERFACE (*interface_pointer));

  add = FALSE;
  has = has_func (drive);
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
          g_dbus_object_remove_interface (G_DBUS_OBJECT (drive), G_DBUS_INTERFACE (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      update_func (drive, uevent_action, G_DBUS_INTERFACE (*interface_pointer));
      if (add)
        g_dbus_object_add_interface (G_DBUS_OBJECT (drive), G_DBUS_INTERFACE (*interface_pointer));
    }
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.Drive */

static gboolean
drive_check (UDisksLinuxDrive *drive)
{
  return TRUE;
}

static void
drive_update (UDisksLinuxDrive      *drive,
              const gchar           *uevent_action,
              GDBusInterface        *_iface)
{
  UDisksDrive *iface = UDISKS_DRIVE (_iface);
  GUdevDevice *device;

  if (drive->devices == NULL)
    goto out;

  device = G_UDEV_DEVICE (drive->devices->data);

  /* this is the _almost_ the same for both ATA and SCSI devices (cf. udev's ata_id and scsi_id)
   * but we special case since there are subtle differences...
   */
  if (g_udev_device_get_property_as_boolean (device, "ID_ATA"))
    {
      const gchar *model;
      const gchar *serial;

      model = g_udev_device_get_property (device, "ID_MODEL_ENC");
      if (model != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (model);
          g_strstrip (s);
          udisks_drive_set_model (iface, s);
          g_free (s);
        }

      udisks_drive_set_vendor (iface, g_udev_device_get_property (device, ""));
      udisks_drive_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      serial = g_udev_device_get_property (device, "ID_SERIAL_SHORT");
      if (serial == NULL)
        serial = g_udev_device_get_property (device, "ID_SERIAL");
      udisks_drive_set_serial (iface, serial);
      udisks_drive_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));
    }
  else if (g_udev_device_get_property_as_boolean (device, "ID_SCSI"))
    {
      const gchar *vendor;
      const gchar *model;

      vendor = g_udev_device_get_property (device, "ID_VENDOR_ENC");
      if (vendor != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (vendor);
          g_strstrip (s);
          udisks_drive_set_vendor (iface, s);
          g_free (s);
        }

      model = g_udev_device_get_property (device, "ID_MODEL_ENC");
      if (model != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (model);
          g_strstrip (s);
          udisks_drive_set_model (iface, s);
          g_free (s);
        }

      udisks_drive_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      udisks_drive_set_serial (iface, g_udev_device_get_property (device, "ID_SCSI_SERIAL"));
      udisks_drive_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));
    }
  else
    {
      /* generic fallback... */
      udisks_drive_set_vendor (iface, g_udev_device_get_property (device, "ID_VENDOR"));
      udisks_drive_set_model (iface, g_udev_device_get_property (device, "ID_MODEL"));
      udisks_drive_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      udisks_drive_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL_SHORT"));
      udisks_drive_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));
    }

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static GList *
find_link_for_sysfs_path (UDisksLinuxDrive *drive,
                    const gchar *sysfs_path)
{
  GList *l;
  GList *ret;
  ret = NULL;
  for (l = drive->devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      if (g_strcmp0 (g_udev_device_get_sysfs_path (device), sysfs_path) == 0)
        {
          ret = l;
          goto out;
        }
    }
 out:
  return ret;
}

/**
 * udisks_linux_drive_uevent:
 * @drive: A #UDisksLinuxDrive.
 * @action: Uevent action or %NULL
 * @device: A new #GUdevDevice device object or %NULL if the device hasn't changed.
 *
 * Updates all information on interfaces on @drive.
 */
void
udisks_linux_drive_uevent (UDisksLinuxDrive *drive,
                           const gchar      *action,
                           GUdevDevice      *device)
{
  GList *link;

  g_return_if_fail (UDISKS_IS_LINUX_DRIVE (drive));
  g_return_if_fail (G_UDEV_IS_DEVICE (device));

  link = find_link_for_sysfs_path (drive, g_udev_device_get_sysfs_path (device));
  if (g_strcmp0 (action, "remove") == 0)
    {
      if (link != NULL)
        {
          g_object_unref (G_UDEV_DEVICE (link->data));
          drive->devices = g_list_delete_link (drive->devices, link);
        }
      else
        {
          udisks_daemon_log (drive->daemon,
                             UDISKS_LOG_LEVEL_WARNING,
                             "Drive doesn't have device with sysfs path %s on remove event",
                             g_udev_device_get_sysfs_path (device));
        }
    }
  else
    {
      if (link != NULL)
        {
          g_object_unref (G_UDEV_DEVICE (link->data));
          link->data = g_object_ref (device);
        }
      else
        {
          drive->devices = g_list_append (drive->devices, g_object_ref (device));
        }
    }

  update_iface (drive, action, drive_check, drive_update,
                UDISKS_TYPE_DRIVE_STUB, &drive->iface_drive);
}

/* ---------------------------------------------------------------------------------------------------- */

/* <internal>
 * udisks_linux_drive_should_include_device:
 * @device: A #GUdevDevice.
 * @out_vpd: Return location for unique ID or %NULL.
 *
 * Checks if we should even construct a #UDisksLinuxDrive for @device.
 *
 * Returns: %TRUE if we should construct an object, %FALSE otherwise.
 */
gboolean
udisks_linux_drive_should_include_device (GUdevDevice  *device,
                                          gchar       **out_vpd)
{
  gboolean ret;
  gint type;
  const gchar *serial;
  const gchar *wwn;
  const gchar *vpd;

  ret = FALSE;

  /* The 'scsi' subsystem encompasses several objects with varying
   * DEVTYPE including
   *
   *  - scsi_device
   *  - scsi_target
   *  - scsi_host
   *
   * and we are only interested in the first.
   */
  if (g_strcmp0 (g_udev_device_get_devtype (device), "scsi_device") != 0)
    goto out;

  /* In fact, we are only interested in SCSI devices with peripheral type
   * 0x00 (Direct-access block device) and 0x05 (CD/DVD device). If we
   * didn't do this check we'd end up adding Enclosure Services Devices
   * and RAID controllers here.
   *
   * See SPC-4, section 6.4.2: Standard INQUIRY data for where
   * the various peripheral types are defined.
   */
  type = g_udev_device_get_sysfs_attr_as_int (device, "type");
  if (!(type == 0x00 || type == 0x05))
    goto out;

  /* prefer WWN to serial */
  serial = g_udev_device_get_property (device, "ID_SERIAL");
  wwn = g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION");
  if (wwn != NULL && strlen (wwn) > 0)
    {
      vpd = wwn;
    }
  else if (serial != NULL && strlen (serial) > 0)
    {
      vpd = serial;
    }
  else
    {
      vpd = NULL;
    }

  if (out_vpd != NULL)
    *out_vpd = g_strdup (vpd);
  ret = TRUE;

 out:
  return ret;
}
