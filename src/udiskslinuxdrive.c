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
#include <glib/gi18n-lib.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "udiskslogging.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxdrive.h"
#include "udiskslinuxblock.h"

/**
 * SECTION:udiskslinuxdrive
 * @title: UDisksLinuxDrive
 * @short_description: Linux drives (ATA, SCSI, Software RAID, etc.)
 *
 * Object corresponding to a Drive on Linux.
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
  UDisksObjectSkeleton parent_instance;

  UDisksDaemon *daemon;

  /* list of GUdevDevice objects for block objects */
  GList *devices;

  /* interfaces */
  UDisksDrive *iface_drive;
};

struct _UDisksLinuxDriveClass
{
  UDisksObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

G_DEFINE_TYPE (UDisksLinuxDrive, udisks_linux_drive, UDISKS_TYPE_OBJECT_SKELETON);

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
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (drive), str->str);
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
 * @device: The #GUdevDevice for the sysfs block device.
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
typedef void     (*ConnectInterfaceFunc) (UDisksLinuxDrive     *drive);
typedef void     (*UpdateInterfaceFunc) (UDisksLinuxDrive     *drive,
                                         const gchar    *uevent_action,
                                         GDBusInterface *interface);

static void
update_iface (UDisksLinuxDrive           *drive,
              const gchar          *uevent_action,
              HasInterfaceFunc      has_func,
              ConnectInterfaceFunc   connect_func,
              UpdateInterfaceFunc   update_func,
              GType                 skeleton_type,
              gpointer              _interface_pointer)
{
  gboolean has;
  gboolean add;
  GDBusInterface **interface_pointer = _interface_pointer;

  g_return_if_fail (drive != NULL);
  g_return_if_fail (has_func != NULL);
  g_return_if_fail (update_func != NULL);
  g_return_if_fail (g_type_is_a (skeleton_type, G_TYPE_OBJECT));
  g_return_if_fail (g_type_is_a (skeleton_type, G_TYPE_DBUS_INTERFACE));
  g_return_if_fail (interface_pointer != NULL);
  g_return_if_fail (*interface_pointer == NULL || G_IS_DBUS_INTERFACE (*interface_pointer));

  add = FALSE;
  has = has_func (drive);
  if (*interface_pointer == NULL)
    {
      if (has)
        {
          *interface_pointer = g_object_new (skeleton_type, NULL);
          if (connect_func != NULL)
            connect_func (drive);
          add = TRUE;
        }
    }
  else
    {
      if (!has)
        {
          g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (drive), G_DBUS_INTERFACE_SKELETON (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      update_func (drive, uevent_action, G_DBUS_INTERFACE (*interface_pointer));
      if (add)
        g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (drive), G_DBUS_INTERFACE_SKELETON (*interface_pointer));
    }
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.Drive */

static const struct
{
  const gchar *udev_property;
  const gchar *media_name;
} drive_media_mapping[] =
{
  { "ID_DRIVE_FLASH", "flash" },
  { "ID_DRIVE_FLASH_CF", "flash_cf" },
  { "ID_DRIVE_FLASH_MS", "flash_ms" },
  { "ID_DRIVE_FLASH_SM", "flash_sm" },
  { "ID_DRIVE_FLASH_SD", "flash_sd" },
  { "ID_DRIVE_FLASH_SDHC", "flash_sdhc" },
  { "ID_DRIVE_FLASH_SDXC", "flash_sdxc" },
  { "ID_DRIVE_FLASH_MMC", "flash_mmc" },
  { "ID_DRIVE_FLOPPY", "floppy" },
  { "ID_DRIVE_FLOPPY_ZIP", "floppy_zip" },
  { "ID_DRIVE_FLOPPY_JAZ", "floppy_jaz" },
  { "ID_CDROM", "optical_cd" },
  { "ID_CDROM_CD_R", "optical_cd_r" },
  { "ID_CDROM_CD_RW", "optical_cd_rw" },
  { "ID_CDROM_DVD", "optical_dvd" },
  { "ID_CDROM_DVD_R", "optical_dvd_r" },
  { "ID_CDROM_DVD_RW", "optical_dvd_rw" },
  { "ID_CDROM_DVD_RAM", "optical_dvd_ram" },
  { "ID_CDROM_DVD_PLUS_R", "optical_dvd_plus_r" },
  { "ID_CDROM_DVD_PLUS_RW", "optical_dvd_plus_rw" },
  { "ID_CDROM_DVD_PLUS_R_DL", "optical_dvd_plus_r_dl" },
  { "ID_CDROM_DVD_PLUS_RW_DL", "optical_dvd_plus_rw_dl" },
  { "ID_CDROM_BD", "optical_bd" },
  { "ID_CDROM_BD_R", "optical_bd_r" },
  { "ID_CDROM_BD_RE", "optical_bd_re" },
  { "ID_CDROM_HDDVD", "optical_hddvd" },
  { "ID_CDROM_HDDVD_R", "optical_hddvd_r" },
  { "ID_CDROM_HDDVD_RW", "optical_hddvd_rw" },
  { "ID_CDROM_MO", "optical_mo" },
  { "ID_CDROM_MRW", "optical_mrw" },
  { "ID_CDROM_MRW_W", "optical_mrw_w" },
  { NULL, NULL }
};

static const struct
{
  const gchar *udev_property;
  const gchar *media_name;
} media_mapping[] =
{
  { "ID_DRIVE_MEDIA_FLASH", "flash" },
  { "ID_DRIVE_MEDIA_FLASH_CF", "flash_cf" },
  { "ID_DRIVE_MEDIA_FLASH_MS", "flash_ms" },
  { "ID_DRIVE_MEDIA_FLASH_SM", "flash_sm" },
  { "ID_DRIVE_MEDIA_FLASH_SD", "flash_sd" },
  { "ID_DRIVE_MEDIA_FLASH_SDHC", "flash_sdhc" },
  { "ID_DRIVE_MEDIA_FLASH_SDXC", "flash_sdxc" },
  { "ID_DRIVE_MEDIA_FLASH_MMC", "flash_mmc" },
  { "ID_DRIVE_MEDIA_FLOPPY", "floppy" },
  { "ID_DRIVE_MEDIA_FLOPPY_ZIP", "floppy_zip" },
  { "ID_DRIVE_MEDIA_FLOPPY_JAZ", "floppy_jaz" },
  { "ID_CDROM_MEDIA_CD", "optical_cd" },
  { "ID_CDROM_MEDIA_CD_R", "optical_cd_r" },
  { "ID_CDROM_MEDIA_CD_RW", "optical_cd_rw" },
  { "ID_CDROM_MEDIA_DVD", "optical_dvd" },
  { "ID_CDROM_MEDIA_DVD_R", "optical_dvd_r" },
  { "ID_CDROM_MEDIA_DVD_RW", "optical_dvd_rw" },
  { "ID_CDROM_MEDIA_DVD_RAM", "optical_dvd_ram" },
  { "ID_CDROM_MEDIA_DVD_PLUS_R", "optical_dvd_plus_r" },
  { "ID_CDROM_MEDIA_DVD_PLUS_RW", "optical_dvd_plus_rw" },
  { "ID_CDROM_MEDIA_DVD_PLUS_R_DL", "optical_dvd_plus_r_dl" },
  { "ID_CDROM_MEDIA_DVD_PLUS_RW_DL", "optical_dvd_plus_rw_dl" },
  { "ID_CDROM_MEDIA_BD", "optical_bd" },
  { "ID_CDROM_MEDIA_BD_R", "optical_bd_r" },
  { "ID_CDROM_MEDIA_BD_RE", "optical_bd_re" },
  { "ID_CDROM_MEDIA_HDDVD", "optical_hddvd" },
  { "ID_CDROM_MEDIA_HDDVD_R", "optical_hddvd_r" },
  { "ID_CDROM_MEDIA_HDDVD_RW", "optical_hddvd_rw" },
  { "ID_CDROM_MEDIA_MO", "optical_mo" },
  { "ID_CDROM_MEDIA_MRW", "optical_mrw" },
  { "ID_CDROM_MEDIA_MRW_W", "optical_mrw_w" },
  { NULL, NULL }
};

static gint
ptr_str_array_compare (const gchar **a,
                       const gchar **b)
{
  return g_strcmp0 (*a, *b);
}

static void
drive_set_media (UDisksLinuxDrive *drive,
                 UDisksDrive      *iface,
                 GUdevDevice      *device)
{
  guint n;
  GPtrArray *media_compat_array;
  const gchar *media_in_drive;

  media_compat_array = g_ptr_array_new ();
  for (n = 0; drive_media_mapping[n].udev_property != NULL; n++)
    {
      if (!g_udev_device_has_property (device, drive_media_mapping[n].udev_property))
        continue;
      g_ptr_array_add (media_compat_array, (gpointer) drive_media_mapping[n].media_name);
    }
  g_ptr_array_sort (media_compat_array, (GCompareFunc) ptr_str_array_compare);
  g_ptr_array_add (media_compat_array, NULL);

  media_in_drive = "";
  if (udisks_drive_get_size (iface) > 0)
    {
      for (n = 0; media_mapping[n].udev_property != NULL; n++)
        {
          if (!g_udev_device_has_property (device, media_mapping[n].udev_property))
            continue;

          media_in_drive = drive_media_mapping[n].media_name;
          break;
        }
      /* If the media isn't set (from e.g. udev rules), just pick the first one in media_compat - note
       * that this may be NULL (if we don't know what media is compatible with the drive) which is OK.
       */
      if (strlen (media_in_drive) == 0)
        media_in_drive = ((const gchar **) media_compat_array->pdata)[0];
    }

  udisks_drive_set_media_compatibility (iface, (const gchar* const *) media_compat_array->pdata);
  udisks_drive_set_media (iface, media_in_drive);
  g_ptr_array_free (media_compat_array, TRUE);
}

static void
drive_set_rotation_rate (UDisksLinuxDrive *drive,
                         UDisksDrive      *iface,
                         GUdevDevice      *device)
{
  gint rate;

  if (!g_udev_device_get_sysfs_attr_as_boolean (device, "queue/rotational"))
    {
      rate = 0;
    }
  else
    {
      rate = -1;
      if (g_udev_device_has_property (device, "ID_ATA_ROTATION_RATE_RPM"))
        rate = g_udev_device_get_property_as_int (device, "ID_ATA_ROTATION_RATE_RPM");
    }
  udisks_drive_set_rotation_rate (iface, rate);
}

static gboolean
drive_check (UDisksLinuxDrive *drive)
{
  return TRUE;
}

static UDisksObject *
find_block_object (GDBusObjectManagerServer *object_manager,
                   UDisksLinuxDrive         *drive)
{
  UDisksObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectSkeleton *object = G_DBUS_OBJECT_SKELETON (l->data);
      UDisksBlockDevice *block;
      GUdevDevice *device;
      gboolean is_disk;

      if (!UDISKS_IS_LINUX_BLOCK (object))
        continue;

      device = udisks_linux_block_get_device (UDISKS_LINUX_BLOCK (object));
      is_disk = (g_strcmp0 (g_udev_device_get_devtype (device), "disk") == 0);
      g_object_unref (device);

      if (!is_disk)
        continue;

      block = udisks_object_peek_block_device (UDISKS_OBJECT (object));

      if (g_strcmp0 (udisks_block_device_get_drive (block),
                     g_dbus_object_get_object_path (G_DBUS_OBJECT (drive))) == 0)
        {
          ret = g_object_ref (object);
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static gboolean
on_eject (UDisksDrive           *drive_iface,
          GDBusMethodInvocation *invocation,
          GVariant              *options,
          gpointer               user_data)
{
  UDisksLinuxDrive *drive = UDISKS_LINUX_DRIVE (user_data);
  UDisksObject *block_object;
  UDisksBlockDevice *block;
  UDisksDaemon *daemon;
  const gchar *action_id;
  gchar *error_message;

  daemon = NULL;
  block = NULL;
  error_message = NULL;

  daemon = udisks_linux_drive_get_daemon (drive);
  block_object = find_block_object (udisks_daemon_get_object_manager (daemon),
                                    drive);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unable to find physical block device for drive");
      goto out;
    }
  block = udisks_object_peek_block_device (block_object);

  /* TODO: ensure it's a physical device e.g. not mpath */

  /* TODO: is it a good idea to overload modify-device? */
  action_id = "org.freedesktop.udisks2.modify-device";
  if (udisks_block_device_get_hint_system (block))
    action_id = "org.freedesktop.udisks2.modify-device-system";

  /* Check that the user is actually authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    block_object,
                                                    action_id,
                                                    options,
                                                    N_("Authentication is required to eject $(udisks2.device)"),
                                                    invocation))
    goto out;

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              NULL,  /* GCancellable */
                                              0, /* uid_t run_as */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "eject \"%s\"",
                                              udisks_block_device_get_device (block)))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error eject %s: %s",
                                             udisks_block_device_get_device (block),
                                             error_message);
      goto out;
    }

  udisks_drive_complete_eject (drive_iface, invocation);

 out:
  if (block_object != NULL)
    g_object_unref (block_object);
  g_free (error_message);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static void
drive_connect (UDisksLinuxDrive *drive)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (drive->iface_drive),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
  g_signal_connect (drive->iface_drive,
                    "handle-eject",
                    G_CALLBACK (on_eject),
                    drive);
}

static void
drive_set_connection_bus (UDisksLinuxDrive *drive,
                          UDisksDrive      *iface,
                          GUdevDevice      *device)
{
  GUdevDevice *parent;

  /* note: @device may vary - it can be any path for drive */

  udisks_drive_set_connection_bus (iface, "");
  parent = g_udev_device_get_parent_with_subsystem (device, "usb", "usb_interface");
  if (parent != NULL)
    {
      /* TODO: should probably check that it's a storage interface */
      udisks_drive_set_connection_bus (iface, "usb");
      g_object_unref (parent);
      goto out;
    }

  parent = g_udev_device_get_parent_with_subsystem (device, "firewire", NULL);
  if (parent != NULL)
    {
      /* TODO: should probably check that it's a storage interface */
      udisks_drive_set_connection_bus (iface, "ieee1394");
      g_object_unref (parent);
      goto out;
    }

 out:
  ;
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
  else if (g_str_has_prefix (g_udev_device_get_name (device), "mmcblk"))
    {
      /* sigh, mmc is non-standard and using ID_NAME instead of ID_MODEL.. */
      udisks_drive_set_model (iface, g_udev_device_get_property (device, "ID_NAME"));
      udisks_drive_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL"));
      /* TODO:
       *  - lookup Vendor from manfid and oemid in sysfs
       *  - lookup Revision from fwrev and hwrev in sysfs
       */
    }
  else
    {
      const gchar *vendor;
      const gchar *model;
      const gchar *name;

      name = g_udev_device_get_name (device);

      /* generic fallback... */
      vendor = g_udev_device_get_property (device, "ID_VENDOR_ENC");
      if (vendor != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (vendor);
          g_strstrip (s);
          udisks_drive_set_vendor (iface, s);
          g_free (s);
        }
      else
        {
          vendor = g_udev_device_get_property (device, "ID_VENDOR");
          if (vendor != NULL)
            {
              udisks_drive_set_vendor (iface, vendor);
            }
          /* workaround for missing ID_VENDOR on virtio-blk */
          else if (g_str_has_prefix (name, "vd"))
            {
              /* TODO: could lookup the vendor sysfs attr on the virtio object */
              udisks_drive_set_vendor (iface, "");
            }
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
      else
        {
          model = g_udev_device_get_property (device, "ID_MODEL");
          if (model != NULL)
            {
              udisks_drive_set_model (iface, model);
            }
          /* workaround for missing ID_MODEL on virtio-blk */
          else if (g_str_has_prefix (name, "vd"))
            {
              udisks_drive_set_model (iface, "VirtIO Disk");
            }
        }

      udisks_drive_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      if (g_udev_device_has_property (device, "ID_SERIAL_SHORT"))
        udisks_drive_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL_SHORT"));
      else
        udisks_drive_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL"));
      if (g_udev_device_has_property (device, "ID_WWN_WITH_EXTENSION"))
        udisks_drive_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));
      else
        udisks_drive_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN"));
    }

  /* common bits go here */
  udisks_drive_set_media_removable (iface, g_udev_device_get_sysfs_attr_as_boolean (device, "removable"));
  udisks_drive_set_size (iface, udisks_daemon_util_block_get_size (device));
  drive_set_media (drive, iface, device);
  drive_set_rotation_rate (drive, iface, device);
  drive_set_connection_bus (drive, iface, device);
 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static GList *
find_link_for_sysfs_path (UDisksLinuxDrive *drive,
                          const gchar      *sysfs_path)
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
          udisks_warning ("Drive doesn't have device with sysfs path %s on remove event",
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

  update_iface (drive, action, drive_check, drive_connect, drive_update,
                UDISKS_TYPE_DRIVE_SKELETON, &drive->iface_drive);
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
  const gchar *serial;
  const gchar *wwn;
  gchar *vpd;

  ret = FALSE;
  vpd = NULL;

  /* The 'block' subsystem encompasses several objects with varying
   * DEVTYPE including
   *
   *  - disk
   *  - partition
   *
   * and we are only interested in the first.
   */
  if (g_strcmp0 (g_udev_device_get_devtype (device), "disk") != 0)
    goto out;

  /* prefer WWN to serial */
  serial = g_udev_device_get_property (device, "ID_SERIAL");
  wwn = g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION");
  if (wwn != NULL && strlen (wwn) > 0)
    {
      vpd = g_strdup (wwn);
    }
  else if (serial != NULL && strlen (serial) > 0)
    {
      vpd = g_strdup (serial);
    }
  else
    {
      const gchar *name = g_udev_device_get_name (device);
      /* workaround for missing serial/wwn on virtio-blk */
      if (g_str_has_prefix (name, "vd"))
        vpd = g_strdup (name);
    }

  if (vpd != NULL)
    {
      if (out_vpd != NULL)
        {
          *out_vpd = vpd;
          vpd = NULL;
        }
      ret = TRUE;
    }

 out:
  g_free (vpd);
  return ret;
}
