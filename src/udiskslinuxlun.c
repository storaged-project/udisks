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

#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxlun.h"

/**
 * SECTION:udiskslinuxlun
 * @title: UDisksLinuxLun
 * @short_description: Linux LUNs (ATA, SCSI, Software RAID, etc.)
 *
 * Object corresponding to a LUN on Linux.
 */

typedef struct _UDisksLinuxLunClass   UDisksLinuxLunClass;

/**
 * UDisksLinuxLun:
 *
 * The #UDisksLinuxLun structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxLun
{
  GDBusObjectStub parent_instance;

  UDisksDaemon *daemon;

  /* list of GUdevDevice objects for block objects */
  GList *devices;

  /* interfaces */
  UDisksLun *iface_lun;
};

struct _UDisksLinuxLunClass
{
  GDBusObjectStubClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

G_DEFINE_TYPE (UDisksLinuxLun, udisks_linux_lun, G_TYPE_DBUS_OBJECT_STUB);

static void
udisks_linux_lun_finalize (GObject *object)
{
  UDisksLinuxLun *lun = UDISKS_LINUX_LUN (object);

  /* note: we don't hold a ref to lun->daemon or lun->mount_monitor */

  g_list_foreach (lun->devices, (GFunc) g_object_unref, NULL);
  g_list_free (lun->devices);

  if (lun->iface_lun != NULL)
    g_object_unref (lun->iface_lun);

  if (G_OBJECT_CLASS (udisks_linux_lun_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_lun_parent_class)->finalize (object);
}

static void
udisks_linux_lun_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  UDisksLinuxLun *lun = UDISKS_LINUX_LUN (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_lun_get_daemon (lun));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_lun_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  UDisksLinuxLun *lun = UDISKS_LINUX_LUN (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (lun->daemon == NULL);
      /* we don't take a reference to the daemon */
      lun->daemon = g_value_get_object (value);
      break;

    case PROP_DEVICE:
      g_assert (lun->devices == NULL);
      lun->devices = g_list_prepend (NULL, g_value_dup_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
udisks_linux_lun_init (UDisksLinuxLun *lun)
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
udisks_linux_lun_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
  GObjectConstructParam *device_cp;
  GUdevDevice *device;

  device_cp = find_construct_property (n_construct_properties, construct_properties, "device");
  g_assert (device_cp != NULL);

  device = G_UDEV_DEVICE (g_value_get_object (device_cp->value));
  g_assert (device != NULL);

  if (!udisks_linux_lun_should_include_device (device, NULL))
    {
      return NULL;
    }
  else
    {
      return G_OBJECT_CLASS (udisks_linux_lun_parent_class)->constructor (type,
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
udisks_linux_lun_constructed (GObject *object)
{
  UDisksLinuxLun *lun = UDISKS_LINUX_LUN (object);
  gchar *vendor;
  gchar *model;
  gchar *serial;
  GString *str;

  /* initial coldplug */
  udisks_linux_lun_uevent (lun, "add", lun->devices->data);

  /* compute the object path */
  vendor = g_strdup (udisks_lun_get_vendor (lun->iface_lun));
  model = g_strdup (udisks_lun_get_model (lun->iface_lun));
  serial = g_strdup (udisks_lun_get_serial (lun->iface_lun));
  strip_and_replace_with_uscore (vendor);
  strip_and_replace_with_uscore (model);
  strip_and_replace_with_uscore (serial);
  str = g_string_new ("/org/freedesktop/UDisks2/LUNs/");
  if (vendor == NULL && model == NULL && serial == NULL)
    {
      g_string_append (str, "lun");
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
  g_dbus_object_stub_set_object_path (G_DBUS_OBJECT_STUB (lun), str->str);
  g_string_free (str, TRUE);

  if (G_OBJECT_CLASS (udisks_linux_lun_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_lun_parent_class)->constructed (object);
}

static void
udisks_linux_lun_class_init (UDisksLinuxLunClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructor  = udisks_linux_lun_constructor;
  gobject_class->finalize     = udisks_linux_lun_finalize;
  gobject_class->constructed  = udisks_linux_lun_constructed;
  gobject_class->set_property = udisks_linux_lun_set_property;
  gobject_class->get_property = udisks_linux_lun_get_property;

  /**
   * UDisksLinuxLun:daemon:
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
   * UDisksLinuxLun:device:
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
 * udisks_linux_lun_new:
 * @daemon: A #UDisksDaemon.
 * @device: The #GUdevDevice for the sysfs block device.
 *
 * Create a new lun object.
 *
 * Returns: A #UDisksLinuxLun object or %NULL if @device does not represent a lun. Free with g_object_unref().
 */
UDisksLinuxLun *
udisks_linux_lun_new (UDisksDaemon  *daemon,
                        GUdevDevice   *device)
{
  GObject *object;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), NULL);

  object = g_object_new (UDISKS_TYPE_LINUX_LUN,
                         "daemon", daemon,
                         "device", device,
                         NULL);

  if (object != NULL)
    return UDISKS_LINUX_LUN (object);
  else
    return NULL;
}

/**
 * udisks_linux_lun_get_daemon:
 * @lun: A #UDisksLinuxLun.
 *
 * Gets the daemon used by @lun.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @lun.
 */
UDisksDaemon *
udisks_linux_lun_get_daemon (UDisksLinuxLun *lun)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_LUN (lun), NULL);
  return lun->daemon;
}

/**
 * udisks_linux_lun_get_devices:
 * @lun: A #UDisksLinuxLun.
 *
 * Gets the current #GUdevDevice objects associated with @lun.
 *
 * Returns: A list of #GUdevDevice objects. Free each element with
 * g_object_unref(), then free the list with g_list_free().
 */
GList *
udisks_linux_lun_get_devices (UDisksLinuxLun *lun)
{
  GList *ret;
  g_return_val_if_fail (UDISKS_IS_LINUX_LUN (lun), NULL);
  ret = g_list_copy (lun->devices);
  g_list_foreach (ret, (GFunc) g_object_ref, NULL);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef gboolean (*HasInterfaceFunc)    (UDisksLinuxLun     *lun);
typedef void     (*UpdateInterfaceFunc) (UDisksLinuxLun     *lun,
                                         const gchar    *uevent_action,
                                         GDBusInterface *interface);

static void
update_iface (UDisksLinuxLun           *lun,
              const gchar          *uevent_action,
              HasInterfaceFunc      has_func,
              UpdateInterfaceFunc   update_func,
              GType                 stub_type,
              gpointer              _interface_pointer)
{
  gboolean has;
  gboolean add;
  GDBusInterface **interface_pointer = _interface_pointer;

  g_return_if_fail (lun != NULL);
  g_return_if_fail (has_func != NULL);
  g_return_if_fail (update_func != NULL);
  g_return_if_fail (g_type_is_a (stub_type, G_TYPE_OBJECT));
  g_return_if_fail (g_type_is_a (stub_type, G_TYPE_DBUS_INTERFACE));
  g_return_if_fail (interface_pointer != NULL);
  g_return_if_fail (*interface_pointer == NULL || G_IS_DBUS_INTERFACE (*interface_pointer));

  add = FALSE;
  has = has_func (lun);
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
          g_dbus_object_stub_remove_interface (G_DBUS_OBJECT_STUB (lun), G_DBUS_INTERFACE_STUB (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      update_func (lun, uevent_action, G_DBUS_INTERFACE (*interface_pointer));
      if (add)
        g_dbus_object_stub_add_interface (G_DBUS_OBJECT_STUB (lun), G_DBUS_INTERFACE_STUB (*interface_pointer));
    }
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.Lun */

static const gchar *
find_iscsi_target (GDBusObjectManagerServer *object_manager,
                   const gchar              *target_name)
{
  const gchar *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectStub *object = G_DBUS_OBJECT_STUB (l->data);
      UDisksIScsiTarget *target;

      target = UDISKS_PEEK_ISCSI_TARGET (object);
      if (target == NULL)
        continue;

      if (g_strcmp0 (udisks_iscsi_target_get_name (target), target_name) == 0)
        {
          ret = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

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
lun_set_media (UDisksLinuxLun *lun,
               UDisksLun      *iface,
               GUdevDevice    *device)
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
  if (udisks_lun_get_size (iface) > 0)
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
      if (media_in_drive == NULL)
        media_in_drive = ((const gchar **) media_compat_array->pdata)[0];
    }

  udisks_lun_set_media_compatibility (iface, (const gchar* const *) media_compat_array->pdata);
  udisks_lun_set_media (iface, media_in_drive);
  g_ptr_array_free (media_compat_array, TRUE);
}

static void
lun_set_rotation_rate (UDisksLinuxLun *lun,
                       UDisksLun      *iface,
                       GUdevDevice    *device)
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
  udisks_lun_set_rotation_rate (iface, rate);
}

static gboolean
lun_check (UDisksLinuxLun *lun)
{
  return TRUE;
}

static gboolean
find_iscsi_devices_for_block (GUdevClient  *udev_client,
                              GUdevDevice  *block_device,
                              GUdevDevice **out_session_device,
                              GUdevDevice **out_connection_device)
{
  gchar *s;
  gchar *session_sysfs_path;
  gchar *connection_sysfs_path;
  GDir *session_dir;
  GDir *connection_dir;
  const gchar *name;
  gboolean ret;
  GUdevDevice *session_device;
  GUdevDevice *connection_device;

  ret = FALSE;
  session_device = NULL;
  connection_device = NULL;

  session_dir = NULL;
  connection_dir = NULL;
  s = NULL;
  session_sysfs_path = NULL;
  connection_sysfs_path = NULL;

  /* This is a bit sketchy and includes assumptions about what sysfs
   * currently looks like...
   */

  if (out_session_device != NULL)
    {
      s = g_strdup_printf ("%s/device/../../iscsi_session", g_udev_device_get_sysfs_path (block_device));
      if (!g_file_test (s, G_FILE_TEST_IS_DIR))
        goto out;
      session_dir = g_dir_open (s, 0, NULL);
      if (session_dir == NULL)
        goto out;
      while ((name = g_dir_read_name (session_dir)) != NULL)
        {
          gint session_num;
          if (sscanf (name, "session%d", &session_num) == 1)
            {
              session_sysfs_path = g_strdup_printf ("%s/%s", s, name);
              break;
            }
        }
      if (session_sysfs_path == NULL)
        goto out;
      session_device = g_udev_client_query_by_sysfs_path (udev_client, session_sysfs_path);
      if (session_device == NULL)
        goto out;
    }

  if (out_connection_device != NULL)
    {
      /* here we assume there is only one connection per session... this could end up not being true */
      g_free (s);
      s = g_strdup_printf ("%s/device/../..", g_udev_device_get_sysfs_path (block_device));
      if (!g_file_test (s, G_FILE_TEST_IS_DIR))
        goto out;
      connection_dir = g_dir_open (s, 0, NULL);
      if (connection_dir == NULL)
        goto out;
      while ((name = g_dir_read_name (connection_dir)) != NULL)
        {
          gint connection_num;
          if (sscanf (name, "connection%d", &connection_num) == 1)
            {
              connection_sysfs_path = g_strdup_printf ("%s/%s/iscsi_connection/%s", s, name, name);
              break;
            }
        }
      if (connection_sysfs_path == NULL)
        goto out;
      connection_device = g_udev_client_query_by_sysfs_path (udev_client, connection_sysfs_path);
      if (connection_device == NULL)
        goto out;
    }

  ret = TRUE;

 out:
  g_free (s);
  g_free (session_sysfs_path);
  if (session_dir != NULL)
    g_dir_close (session_dir);
  g_free (connection_sysfs_path);
  if (connection_dir != NULL)
    g_dir_close (connection_dir);

  if (ret)
    {
      if (out_session_device != NULL)
        *out_session_device = session_device;
      else
        g_object_unref (session_device);

      if (out_connection_device != NULL)
        *out_connection_device = connection_device;
      else
        g_object_unref (connection_device);
    }
  else
    {
      if (session_device != NULL)
        g_object_unref (session_device);
      if (connection_device != NULL)
        g_object_unref (connection_device);
    }

  return ret;
}

static void
lun_update_iscsi (UDisksLinuxLun *lun,
                  UDisksLun      *iface,
                  GUdevDevice    *device)
{
  GUdevClient *udev_client;
  GUdevDevice *session_device;
  GUdevDevice *connection_device;

  /* note: @device may vary - it can be any path for lun */
  session_device = NULL;
  connection_device = NULL;

  udisks_lun_set_iscsi_target (iface, "/");

  udev_client = udisks_linux_provider_get_udev_client (udisks_daemon_get_linux_provider (lun->daemon));
  if (find_iscsi_devices_for_block (udev_client,
                                    device,
                                    &session_device,
                                    &connection_device))
    {
      GDBusObjectManagerServer *object_manager;
      const gchar *target_name;
      const gchar *target_object_path;

      target_name = g_udev_device_get_sysfs_attr (session_device, "targetname");
      if (target_name == NULL)
        {
          udisks_daemon_log (lun->daemon,
                             UDISKS_LOG_LEVEL_WARNING,
                             "Cannot find iSCSI target name for sysfs path %s",
                             g_udev_device_get_sysfs_path (session_device));
          goto out;
        }

      object_manager = udisks_daemon_get_object_manager (lun->daemon);
      target_object_path = find_iscsi_target (object_manager, target_name);
      if (target_object_path == NULL)
        {
          udisks_daemon_log (lun->daemon,
                             UDISKS_LOG_LEVEL_WARNING,
                             "Cannot find iSCSI target object for name `%s'",
                             target_name);
          goto out;
        }
      udisks_lun_set_iscsi_target (iface, target_object_path);

    }
 out:
  if (connection_device != NULL)
    g_object_unref (connection_device);
  if (session_device != NULL)
    g_object_unref (session_device);
}


static void
lun_update (UDisksLinuxLun      *lun,
            const gchar         *uevent_action,
            GDBusInterface      *_iface)
{
  UDisksLun *iface = UDISKS_LUN (_iface);
  GUdevDevice *device;

  if (lun->devices == NULL)
    goto out;

  device = G_UDEV_DEVICE (lun->devices->data);

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
          udisks_lun_set_model (iface, s);
          g_free (s);
        }

      udisks_lun_set_vendor (iface, g_udev_device_get_property (device, ""));
      udisks_lun_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      serial = g_udev_device_get_property (device, "ID_SERIAL_SHORT");
      if (serial == NULL)
        serial = g_udev_device_get_property (device, "ID_SERIAL");
      udisks_lun_set_serial (iface, serial);
      udisks_lun_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));
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
          udisks_lun_set_vendor (iface, s);
          g_free (s);
        }

      model = g_udev_device_get_property (device, "ID_MODEL_ENC");
      if (model != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (model);
          g_strstrip (s);
          udisks_lun_set_model (iface, s);
          g_free (s);
        }

      udisks_lun_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      udisks_lun_set_serial (iface, g_udev_device_get_property (device, "ID_SCSI_SERIAL"));
      udisks_lun_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));

      lun_update_iscsi (lun, iface, device);
    }
  else if (g_str_has_prefix (g_udev_device_get_name (device), "mmcblk"))
    {
      /* sigh, mmc is non-standard and using ID_NAME instead of ID_MODEL.. */
      udisks_lun_set_model (iface, g_udev_device_get_property (device, "ID_NAME"));
      udisks_lun_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL"));
      /* TODO:
       *  - lookup Vendor from manfid and oemid in sysfs
       *  - lookup Revision from fwrev and hwrev in sysfs
       */
    }
  else
    {
      const gchar *vendor;
      const gchar *model;

      /* generic fallback... */
      vendor = g_udev_device_get_property (device, "ID_VENDOR_ENC");
      if (vendor != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (vendor);
          g_strstrip (s);
          udisks_lun_set_vendor (iface, s);
          g_free (s);
        }
      else
        {
          udisks_lun_set_vendor (iface, g_udev_device_get_property (device, "ID_VENDOR"));
        }

      model = g_udev_device_get_property (device, "ID_MODEL_ENC");
      if (model != NULL)
        {
          gchar *s;
          s = udisks_decode_udev_string (model);
          g_strstrip (s);
          udisks_lun_set_model (iface, s);
          g_free (s);
        }
      else
        {
          udisks_lun_set_model (iface, g_udev_device_get_property (device, "ID_MODEL"));
        }

      udisks_lun_set_revision (iface, g_udev_device_get_property (device, "ID_REVISION"));
      if (g_udev_device_has_property (device, "ID_SERIAL_SHORT"))
        udisks_lun_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL_SHORT"));
      else
        udisks_lun_set_serial (iface, g_udev_device_get_property (device, "ID_SERIAL"));
      if (g_udev_device_has_property (device, "ID_WWN_WITH_EXTENSION"))
        udisks_lun_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION"));
      else
        udisks_lun_set_wwn (iface, g_udev_device_get_property (device, "ID_WWN"));
    }

  /* common bits go here */
  udisks_lun_set_media_removable (iface, g_udev_device_get_sysfs_attr_as_boolean (device, "removable"));
  udisks_lun_set_size (iface, udisks_daemon_util_block_get_size (device));
  lun_set_media (lun, iface, device);
  lun_set_rotation_rate (lun, iface, device);
 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static GList *
find_link_for_sysfs_path (UDisksLinuxLun *lun,
                          const gchar *sysfs_path)
{
  GList *l;
  GList *ret;
  ret = NULL;
  for (l = lun->devices; l != NULL; l = l->next)
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
 * udisks_linux_lun_uevent:
 * @lun: A #UDisksLinuxLun.
 * @action: Uevent action or %NULL
 * @device: A new #GUdevDevice device object or %NULL if the device hasn't changed.
 *
 * Updates all information on interfaces on @lun.
 */
void
udisks_linux_lun_uevent (UDisksLinuxLun *lun,
                         const gchar      *action,
                         GUdevDevice      *device)
{
  GList *link;

  g_return_if_fail (UDISKS_IS_LINUX_LUN (lun));
  g_return_if_fail (G_UDEV_IS_DEVICE (device));

  link = find_link_for_sysfs_path (lun, g_udev_device_get_sysfs_path (device));
  if (g_strcmp0 (action, "remove") == 0)
    {
      if (link != NULL)
        {
          g_object_unref (G_UDEV_DEVICE (link->data));
          lun->devices = g_list_delete_link (lun->devices, link);
        }
      else
        {
          udisks_daemon_log (lun->daemon,
                             UDISKS_LOG_LEVEL_WARNING,
                             "Lun doesn't have device with sysfs path %s on remove event",
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
          lun->devices = g_list_append (lun->devices, g_object_ref (device));
        }
    }

  update_iface (lun, action, lun_check, lun_update,
                UDISKS_TYPE_LUN_STUB, &lun->iface_lun);
}

/* ---------------------------------------------------------------------------------------------------- */

/* <internal>
 * udisks_linux_lun_should_include_device:
 * @device: A #GUdevDevice.
 * @out_vpd: Return location for unique ID or %NULL.
 *
 * Checks if we should even construct a #UDisksLinuxLun for @device.
 *
 * Returns: %TRUE if we should construct an object, %FALSE otherwise.
 */
gboolean
udisks_linux_lun_should_include_device (GUdevDevice  *device,
                                        gchar       **out_vpd)
{
  gboolean ret;
  const gchar *serial;
  const gchar *wwn;
  const gchar *vpd;

  ret = FALSE;

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
