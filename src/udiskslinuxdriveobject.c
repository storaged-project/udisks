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
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxdrive.h"
#include "udiskslinuxdriveata.h"
#include "udiskslinuxblockobject.h"
#include "udiskslinuxdevice.h"
#include "udisksmodulemanager.h"

#include <modules/udisksmoduleifacetypes.h>


/**
 * SECTION:udiskslinuxdriveobject
 * @title: UDisksLinuxDriveObject
 * @short_description: Object representing a drive on Linux
 *
 * Object corresponding to a drive on Linux.
 */

typedef struct _UDisksLinuxDriveObjectClass   UDisksLinuxDriveObjectClass;

/**
 * UDisksLinuxDriveObject:
 *
 * The #UDisksLinuxDriveObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxDriveObject
{
  UDisksObjectSkeleton parent_instance;

  UDisksDaemon *daemon;

  /* list of UDisksLinuxDevice objects for block objects */
  GList *devices;

  /* interfaces */
  UDisksDrive *iface_drive;
  UDisksDriveAta *iface_drive_ata;
  GHashTable *module_ifaces;
};

struct _UDisksLinuxDriveObjectClass
{
  UDisksObjectSkeletonClass parent_class;
};

typedef struct
{
  UDisksObject *interface;
  UDisksObjectHasInterfaceFunc has_func;
  UDisksObjectConnectInterfaceFunc connect_func;
  UDisksObjectUpdateInterfaceFunc update_func;
} ModuleInterfaceEntry;

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

G_DEFINE_TYPE (UDisksLinuxDriveObject, udisks_linux_drive_object, UDISKS_TYPE_OBJECT_SKELETON);

static void
udisks_linux_drive_object_finalize (GObject *_object)
{
  UDisksLinuxDriveObject *object = UDISKS_LINUX_DRIVE_OBJECT (_object);

  /* note: we don't hold a ref to drive_object->daemon or drive_object->mount_monitor */
  g_list_free_full (object->devices, g_object_unref);

  if (object->iface_drive != NULL)
    g_object_unref (object->iface_drive);
  if (object->iface_drive_ata != NULL)
    g_object_unref (object->iface_drive_ata);
  if (object->module_ifaces != NULL)
    g_hash_table_destroy (object->module_ifaces);

  if (G_OBJECT_CLASS (udisks_linux_drive_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_drive_object_parent_class)->finalize (_object);
}

static void
udisks_linux_drive_object_get_property (GObject    *__object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  UDisksLinuxDriveObject *object = UDISKS_LINUX_DRIVE_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_drive_object_get_daemon (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_drive_object_set_property (GObject      *__object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  UDisksLinuxDriveObject *object = UDISKS_LINUX_DRIVE_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (object->daemon == NULL);
      /* we don't take a reference to the daemon */
      object->daemon = g_value_get_object (value);
      break;

    case PROP_DEVICE:
      g_assert (object->devices == NULL);
      object->devices = g_list_prepend (NULL, g_value_dup_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
udisks_linux_drive_object_init (UDisksLinuxDriveObject *object)
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
udisks_linux_drive_object_constructor (GType                  type,
                                       guint                  n_construct_properties,
                                       GObjectConstructParam *construct_properties)
{
  GObjectConstructParam *cp;
  UDisksDaemon *daemon;
  GUdevClient *client;
  UDisksLinuxDevice *device;

  cp = find_construct_property (n_construct_properties, construct_properties, "daemon");
  g_assert (cp != NULL);
  daemon = UDISKS_DAEMON (g_value_get_object (cp->value));
  g_assert (daemon != NULL);

  client = udisks_linux_provider_get_udev_client (udisks_daemon_get_linux_provider (daemon));

  cp = find_construct_property (n_construct_properties, construct_properties, "device");
  g_assert (cp != NULL);
  device = g_value_get_object (cp->value);
  g_assert (device != NULL);

  if (!udisks_linux_drive_object_should_include_device (client, device, NULL))
    {
      return NULL;
    }
  else
    {
      return G_OBJECT_CLASS (udisks_linux_drive_object_parent_class)->constructor (type,
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

static gboolean
is_dm_multipath (UDisksLinuxDevice *device)
{
  const gchar *dm_uuid;

  dm_uuid = g_udev_device_get_sysfs_attr (device->udev_device, "dm/uuid");
  return dm_uuid != NULL && g_str_has_prefix (dm_uuid, "mpath-");
}

static void
udisks_linux_drive_object_constructed (GObject *_object)
{
  UDisksLinuxDriveObject *object = UDISKS_LINUX_DRIVE_OBJECT (_object);
  gchar *vendor;
  gchar *model;
  gchar *serial;
  GString *str;

  /* initial coldplug */
  udisks_linux_drive_object_uevent (object, "add", object->devices->data);

  /* compute the object path */
  vendor = g_strdup (udisks_drive_get_vendor (object->iface_drive));
  model = g_strdup (udisks_drive_get_model (object->iface_drive));
  serial = g_strdup (udisks_drive_get_serial (object->iface_drive));
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
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), str->str);
  g_string_free (str, TRUE);

  if (G_OBJECT_CLASS (udisks_linux_drive_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_drive_object_parent_class)->constructed (_object);
}

static void
udisks_linux_drive_object_class_init (UDisksLinuxDriveObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructor  = udisks_linux_drive_object_constructor;
  gobject_class->finalize     = udisks_linux_drive_object_finalize;
  gobject_class->constructed  = udisks_linux_drive_object_constructed;
  gobject_class->set_property = udisks_linux_drive_object_set_property;
  gobject_class->get_property = udisks_linux_drive_object_get_property;

  /**
   * UDisksLinuxDriveObject:daemon:
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
   * UDisksLinuxDriveObject:device:
   *
   * The #UDisksLinuxDevice for the object. Connect to the #GObject::notify
   * signal to get notified whenever this is updated.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DEVICE,
                                   g_param_spec_object ("device",
                                                        "Device",
                                                        "The device for the object",
                                                        UDISKS_TYPE_LINUX_DEVICE,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

}

/**
 * udisks_linux_drive_object_new:
 * @daemon: A #UDisksDaemon.
 * @device: The #UDisksLinuxDevice for the sysfs block device.
 *
 * Create a new drive object.
 *
 * Returns: A #UDisksLinuxDriveObject object or %NULL if @device does not represent a drive. Free with g_object_unref().
 */
UDisksLinuxDriveObject *
udisks_linux_drive_object_new (UDisksDaemon      *daemon,
                               UDisksLinuxDevice *device)
{
  GObject *object;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (UDISKS_IS_LINUX_DEVICE (device), NULL);

  object = g_object_new (UDISKS_TYPE_LINUX_DRIVE_OBJECT,
                         "daemon", daemon,
                         "device", device,
                         NULL);

  if (object != NULL)
    return UDISKS_LINUX_DRIVE_OBJECT (object);
  else
    return NULL;
}

/**
 * udisks_linux_drive_object_get_daemon:
 * @object: A #UDisksLinuxDriveObject.
 *
 * Gets the daemon used by @object.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @object.
 */
UDisksDaemon *
udisks_linux_drive_object_get_daemon (UDisksLinuxDriveObject *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_OBJECT (object), NULL);
  return object->daemon;
}

/**
 * udisks_linux_drive_object_get_devices:
 * @object: A #UDisksLinuxDriveObject.
 *
 * Gets the current #UDisksLinuxDevice objects associated with @object.
 *
 * Returns: A list of #UDisksLinuxDevice objects. Free each element with
 * g_object_unref(), then free the list with g_list_free().
 */
GList *
udisks_linux_drive_object_get_devices (UDisksLinuxDriveObject *object)
{
  GList *ret;
  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_OBJECT (object), NULL);
  ret = g_list_copy_deep (object->devices, (GCopyFunc) udisks_g_object_ref_copy, NULL);
  return ret;
}

/**
 * udisks_linux_drive_object_get_device:
 * @object: A #UDisksLinuxDriveObject.
 * @get_hw: If the drive is multipath, set to %TRUE to get a path device instead of the multipath device.
 *
 * Gets one of the #UDisksLinuxDevice object associated with @object.
 *
 * If @get_hw is %TRUE and @object represents a multipath device then
 * one of the paths is returned rather than the multipath device. This
 * is useful if you e.g. need to configure the physical hardware.
 *
 * Returns: A #UDisksLinuxDevice or %NULL. The returned object must be freed
 * with g_object_unref().
 */
UDisksLinuxDevice *
udisks_linux_drive_object_get_device (UDisksLinuxDriveObject *object,
                                      gboolean                get_hw)
{
  UDisksLinuxDevice *ret = NULL;
  GList *devices;

  for (devices = object->devices; devices; devices = devices->next)
    {
      if (!get_hw || !is_dm_multipath (UDISKS_LINUX_DEVICE (devices->data)))
        {
          ret = devices->data;
          break;
        }
    }

  if (ret != NULL)
    g_object_ref (ret);
  return ret;
}

/**
 * udisks_linux_drive_object_get_block:
 * @object: A #UDisksLinuxDriveObject.
 * @get_hw: If the drive is multipath, set to %TRUE to get a path device instead of the multipath device.
 *
 * Gets a #UDisksLinuxBlockObject representing a block device associated with @object.
 *
 * Returns: A #UDisksLinuxBlockObject or %NULL. The returned object
 * must be freed with g_object_unref().
 */
UDisksLinuxBlockObject *
udisks_linux_drive_object_get_block (UDisksLinuxDriveObject *object,
                                     gboolean                get_hw)
{
  GDBusObjectManagerServer *object_manager;
  UDisksLinuxBlockObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  object_manager = udisks_daemon_get_object_manager (object->daemon);
  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectSkeleton *iter_object = G_DBUS_OBJECT_SKELETON (l->data);
      UDisksBlock *block;
      UDisksLinuxDevice *device;
      gboolean skip;

      if (!UDISKS_IS_LINUX_BLOCK_OBJECT (iter_object))
        continue;

      device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (iter_object));
      skip = (g_strcmp0 (g_udev_device_get_devtype (device->udev_device), "disk") != 0
              || (get_hw && is_dm_multipath (device)));
      g_object_unref (device);

      if (skip)
        continue;

      block = udisks_object_peek_block (UDISKS_OBJECT (iter_object));
      if (g_strcmp0 (udisks_block_get_drive (block),
                     g_dbus_object_get_object_path (G_DBUS_OBJECT (object))) == 0)
        {
          ret = UDISKS_LINUX_BLOCK_OBJECT (g_object_ref (iter_object));
          goto out;
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
update_iface (UDisksObject                     *object,
              const gchar                      *uevent_action,
              UDisksObjectHasInterfaceFunc      has_func,
              UDisksObjectConnectInterfaceFunc  connect_func,
              UDisksObjectUpdateInterfaceFunc   update_func,
              GType                             skeleton_type,
              gpointer                          _interface_pointer)
{
  gboolean ret = FALSE;
  gboolean has;
  gboolean add;
  GDBusInterface **interface_pointer = _interface_pointer;
  GDBusInterfaceInfo *interface_info = NULL;
  GDBusInterface *tmp_iface = NULL;

  g_return_val_if_fail (object != NULL, FALSE);
  g_return_val_if_fail (has_func != NULL, FALSE);
  g_return_val_if_fail (update_func != NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (skeleton_type, G_TYPE_OBJECT), FALSE);
  g_return_val_if_fail (g_type_is_a (skeleton_type, G_TYPE_DBUS_INTERFACE), FALSE);
  g_return_val_if_fail (interface_pointer != NULL, FALSE);
  g_return_val_if_fail (*interface_pointer == NULL || G_IS_DBUS_INTERFACE (*interface_pointer), FALSE);

  add = FALSE;
  has = has_func (object);
  if (*interface_pointer == NULL)
    {
      if (has)
        {
          *interface_pointer = g_object_new (skeleton_type, NULL);
          if (connect_func != NULL)
            connect_func (object);
          add = TRUE;
        }
    }
  else
    {
      if (!has)
        {
          /* Check before we remove interface from object  */
          interface_info = g_dbus_interface_get_info (*interface_pointer);
          tmp_iface = g_dbus_object_get_interface ((GDBusObject *) object,
                                                   interface_info->name);

          if (tmp_iface)
            {
              g_dbus_object_skeleton_remove_interface
                (G_DBUS_OBJECT_SKELETON (object),
                 G_DBUS_INTERFACE_SKELETON (*interface_pointer));
              g_object_unref(tmp_iface);
            }

          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      if (update_func (object, uevent_action, G_DBUS_INTERFACE (*interface_pointer)))
        ret = TRUE;
      if (add)
        g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                              G_DBUS_INTERFACE_SKELETON (*interface_pointer));
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
drive_check (UDisksObject *object)
{
  return TRUE;
}

static void
drive_connect (UDisksObject *object)
{
}

static gboolean
drive_update (UDisksObject   *object,
              const gchar    *uevent_action,
              GDBusInterface *_iface)
{
  UDisksLinuxDriveObject *drive_object = UDISKS_LINUX_DRIVE_OBJECT (object);

  return udisks_linux_drive_update (UDISKS_LINUX_DRIVE (drive_object->iface_drive), drive_object);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
drive_ata_check (UDisksObject *object)
{
  UDisksLinuxDriveObject *drive_object = UDISKS_LINUX_DRIVE_OBJECT (object);
  gboolean ret;
  UDisksLinuxDevice *device;

  ret = FALSE;
  if (drive_object->devices == NULL)
    goto out;

  device = drive_object->devices->data;
  if (device->ata_identify_device_data != NULL || device->ata_identify_packet_device_data != NULL)
    ret = TRUE;

 out:
  return ret;
}

static void
drive_ata_connect (UDisksObject *object)
{

}

static gboolean
drive_ata_update (UDisksObject   *object,
                  const gchar    *uevent_action,
                  GDBusInterface *_iface)
{
  UDisksLinuxDriveObject *drive_object = UDISKS_LINUX_DRIVE_OBJECT (object);

  return udisks_linux_drive_ata_update (UDISKS_LINUX_DRIVE_ATA (drive_object->iface_drive_ata), drive_object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void apply_configuration (UDisksLinuxDriveObject *object);

static GList *
find_link_for_sysfs_path (UDisksLinuxDriveObject *object,
                          const gchar            *sysfs_path)
{
  GList *l;
  GList *ret;
  ret = NULL;
  for (l = object->devices; l != NULL; l = l->next)
    {
      UDisksLinuxDevice *device = l->data;
      if (g_strcmp0 (g_udev_device_get_sysfs_path (device->udev_device), sysfs_path) == 0)
        {
          ret = l;
          goto out;
        }
    }
 out:
  return ret;
}

static void
free_module_interface_entry (ModuleInterfaceEntry *entry)
{
  if (entry->interface != NULL)
    g_object_unref (entry->interface);
  g_free (entry);
}

static void
ensure_module_ifaces (UDisksLinuxDriveObject *object,
                      UDisksModuleManager    *module_manager)
{
  GList *l;
  ModuleInterfaceEntry *entry;
  UDisksModuleInterfaceInfo *ii;

  /* Assume all modules are either unloaded or loaded at the same time, so don't regenerate entries */
  if (object->module_ifaces == NULL)
    {
      object->module_ifaces = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) free_module_interface_entry);

      l = udisks_module_manager_get_drive_object_iface_infos (module_manager);
      for (; l; l = l->next)
        {
          ii = l->data;
          entry = g_new0 (ModuleInterfaceEntry, 1);
          entry->has_func = ii->has_func;
          entry->connect_func = ii->connect_func;
          entry->update_func = ii->update_func;
          g_hash_table_replace (object->module_ifaces, GSIZE_TO_POINTER (ii->skeleton_type), entry);
        }
    }
}

/**
 * udisks_linux_drive_object_uevent:
 * @object: A #UDisksLinuxDriveObject.
 * @action: Uevent action or %NULL
 * @device: A #UDisksLinuxDevice device object or %NULL if the device hasn't changed.
 *
 * Updates all information on interfaces on @drive.
 */
void
udisks_linux_drive_object_uevent (UDisksLinuxDriveObject *object,
                                  const gchar            *action,
                                  UDisksLinuxDevice      *device)
{
  GList *link;
  gboolean conf_changed;
  UDisksModuleManager *module_manager;
  GHashTableIter iter;
  gpointer key;
  ModuleInterfaceEntry *entry;

  g_return_if_fail (UDISKS_IS_LINUX_DRIVE_OBJECT (object));
  g_return_if_fail (device == NULL || UDISKS_IS_LINUX_DEVICE (device));

  link = NULL;
  if (device != NULL)
    link = find_link_for_sysfs_path (object, g_udev_device_get_sysfs_path (device->udev_device));
  if (g_strcmp0 (action, "remove") == 0)
    {
      if (link != NULL)
        {
          g_object_unref (UDISKS_LINUX_DEVICE (link->data));
          object->devices = g_list_delete_link (object->devices, link);
        }
      else
        {
          udisks_warning ("Drive doesn't have device with sysfs path %s on remove event",
                          device ? g_udev_device_get_sysfs_path (device->udev_device) : "(null device)");
        }
    }
  else
    {
      if (link != NULL)
        {
          g_object_unref (UDISKS_LINUX_DEVICE (link->data));
          link->data = g_object_ref (device);
        }
      else
        {
          if (device != NULL)
            object->devices = g_list_append (object->devices, g_object_ref (device));
        }
    }

  conf_changed = FALSE;
  conf_changed |= update_iface (UDISKS_OBJECT (object), action, drive_check, drive_connect, drive_update,
                                UDISKS_TYPE_LINUX_DRIVE, &object->iface_drive);
  conf_changed |= update_iface (UDISKS_OBJECT (object), action, drive_ata_check, drive_ata_connect, drive_ata_update,
                                UDISKS_TYPE_LINUX_DRIVE_ATA, &object->iface_drive_ata);

  /* Attach interfaces from modules */
  module_manager = udisks_daemon_get_module_manager (object->daemon);
  if (udisks_module_manager_get_modules_available (module_manager))
    {
      ensure_module_ifaces (object, module_manager);
      g_hash_table_iter_init (&iter, object->module_ifaces);
      while (g_hash_table_iter_next (&iter, &key, (gpointer *) &entry))
        {
          conf_changed |= update_iface (UDISKS_OBJECT (object), action, entry->has_func, entry->connect_func, entry->update_func,
                                        (GType) key, &entry->interface);
        }
    }

  if (g_strcmp0 (action, "reconfigure") == 0)
    conf_changed = TRUE;

  if (conf_changed)
    apply_configuration (object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
apply_configuration (UDisksLinuxDriveObject *object)
{
  GVariant *configuration = NULL;
  UDisksLinuxDevice *device = NULL;

  if (object->iface_drive == NULL)
    goto out;

  configuration = udisks_drive_dup_configuration (object->iface_drive);
  if (configuration == NULL)
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    goto out;

  if (object->iface_drive_ata != NULL)
    {
      udisks_linux_drive_ata_apply_configuration (UDISKS_LINUX_DRIVE_ATA (object->iface_drive_ata),
                                                  device,
                                                  configuration);
    }

 out:
  g_clear_object (&device);
  if (configuration != NULL)
    g_variant_unref (configuration);
}

/* ---------------------------------------------------------------------------------------------------- */

/* utility routine to blacklist WWNs that are not suitable to use
 * for identification purposes
 */
static gboolean
is_wwn_black_listed (const gchar *wwn)
{
  g_return_val_if_fail (wwn != NULL, FALSE);

  if (g_str_has_prefix (wwn, "0x") || g_str_has_prefix (wwn, "0X"))
    wwn += 2;

  if (g_ascii_strcasecmp (wwn, "50f0000000000000") == 0)
    {
      /* SAMSUNG SP1604N (PATA), see https://bugzilla.redhat.com/show_bug.cgi?id=838691#c4 */
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static gchar *
check_for_vpd (GUdevDevice *device)
{
  gchar *ret = NULL;
  const gchar *serial;
  const gchar *wwn;
  const gchar *path;
  const gchar *model;

  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), FALSE);

  /* order of preference: WWN_serial, WWN, Model_serial, serial, path */
  serial = g_udev_device_get_property (device, "ID_SERIAL");
  wwn = g_udev_device_get_property (device, "ID_WWN_WITH_EXTENSION");
  path = g_udev_device_get_property (device, "ID_PATH");
  model = g_udev_device_get_property (device, "ID_MODEL");
  if (wwn != NULL && strlen (wwn) > 0 && !is_wwn_black_listed (wwn))
    {
      if (serial != NULL && strlen (serial) > 0)
        ret = g_strdup_printf ("%s_%s", wwn, serial);
      else
        ret = g_strdup (wwn);
    }
  else if (serial != NULL && strlen (serial) > 0)
    {
      if (model != NULL && strlen (model) > 0)
        ret = g_strdup_printf ("%s_%s", model, serial);
      else
        ret = g_strdup (serial);
    }
  else if (path != NULL && strlen (path) > 0)
    {
      ret = g_strdup (path);
    }
  return ret;
}

/* <internal>
 * udisks_linux_drive_object_should_include_device:
 * @client: A #GUdevClient.
 * @device: A #UDisksLinuxDevice.
 * @out_vpd: Return location for unique ID or %NULL.
 *
 * Checks if we should even construct a #UDisksLinuxDriveObject for @device.
 *
 * Returns: %TRUE if we should construct an object, %FALSE otherwise.
 */
gboolean
udisks_linux_drive_object_should_include_device (GUdevClient        *client,
                                                 UDisksLinuxDevice  *device,
                                                 gchar             **out_vpd)
{
  gboolean ret;
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
  if (g_strcmp0 (g_udev_device_get_devtype (device->udev_device), "disk") != 0)
    goto out;

  vpd = check_for_vpd (device->udev_device);

  if (vpd == NULL)
    {
      const gchar *name;
      const gchar *vendor;
      const gchar *model;
      GUdevDevice *parent;

      name = g_udev_device_get_name (device->udev_device);

      /* workaround for floppy devices */
      if (g_str_has_prefix (name, "fd"))
        {
          vpd = g_strdup_printf ("pcfloppy_%s", name);
          goto found;
        }

      /* workaround for missing serial/wwn on virtio-blk */
      if (g_str_has_prefix (name, "vd"))
        {
          vpd = g_strdup (name);
          goto found;
        }

      /* workaround for missing serial/wwn on VMware */
      vendor = g_udev_device_get_property (device->udev_device, "ID_VENDOR");
      model = g_udev_device_get_property (device->udev_device, "ID_MODEL");
      if (g_str_has_prefix (name, "sd") &&
          vendor != NULL && g_strcmp0 (vendor, "VMware") == 0 &&
          model != NULL && g_str_has_prefix (model, "Virtual"))
        {
          vpd = g_strdup (name);
          goto found;
        }

      /* workaround for missing serial/wwn on firewire devices */
      parent = g_udev_device_get_parent_with_subsystem (device->udev_device, "firewire", NULL);
      if (parent != NULL)
        {
          vpd = g_strdup (name);
          g_object_unref (parent);
          goto found;
        }

      /* dm-multipath */
      if (is_dm_multipath (device))
        {
          gchar **slaves;
          guint n;
          slaves = udisks_daemon_util_resolve_links (g_udev_device_get_sysfs_path (device->udev_device), "slaves");
          for (n = 0; slaves[n] != NULL; n++)
            {
              GUdevDevice *slave;
              slave = g_udev_client_query_by_sysfs_path (client, slaves[n]);
              if (slave != NULL)
                {
                  vpd = check_for_vpd (slave);
                  if (vpd != NULL)
                    {
                      g_object_unref (slave);
                      g_strfreev (slaves);
                      goto found;
                    }
                  g_object_unref (slave);
                }
            }
          g_strfreev (slaves);
        }
    }

 found:
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

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_drive_object_housekeeping:
 * @object: A #UDisksLinuxDriveObject.
 * @secs_since_last: Number of seconds sincex the last housekeeping or 0 if the first housekeeping ever.
 * @cancellable: A %GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Called periodically (every ten minutes or so) to perform
 * housekeeping tasks such as refreshing ATA SMART data.
 *
 * The function runs in a dedicated thread and is allowed to perform
 * blocking I/O.
 *
 * Long-running tasks should periodically check @cancellable to see if
 * they have been cancelled.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
udisks_linux_drive_object_housekeeping (UDisksLinuxDriveObject  *object,
                                        guint                    secs_since_last,
                                        GCancellable            *cancellable,
                                        GError                 **error)
{
  gboolean ret;

  ret = FALSE;

  if (object->iface_drive_ata != NULL &&
      udisks_drive_ata_get_smart_supported (object->iface_drive_ata) &&
      udisks_drive_ata_get_smart_enabled (object->iface_drive_ata))
    {
      GError *local_error;
      gboolean nowakeup;

      /* Wake-up only on start-up */
      nowakeup = TRUE;
      if (secs_since_last == 0)
        nowakeup = FALSE;

      udisks_info ("Refreshing SMART data on %s (nowakeup=%d)",
                   g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                   nowakeup);

      local_error = NULL;
      if (!udisks_linux_drive_ata_refresh_smart_sync (UDISKS_LINUX_DRIVE_ATA (object->iface_drive_ata),
                                                      nowakeup,
                                                      NULL, /* simulate_path */
                                                      cancellable,
                                                      &local_error))
        {
          if (nowakeup && (local_error->domain == UDISKS_ERROR &&
                           local_error->code == UDISKS_ERROR_WOULD_WAKEUP))
            {
              udisks_info ("Drive %s is in a sleep state",
                           g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
              g_clear_error (&local_error);
            }
          else if (nowakeup && (local_error->domain == UDISKS_ERROR &&
                                local_error->code == UDISKS_ERROR_DEVICE_BUSY))
            {
              /* typically because a "secure erase" operation is pending */
              udisks_info ("Drive %s is busy",
                           g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
              g_clear_error (&local_error);
            }
          else
            {
              g_propagate_prefixed_error (error, local_error, "Error updating SMART data: ");
              goto out;
            }
        }
    }

  ret = TRUE;

 out:
  return ret;
}

static gboolean
is_block_unlocked (GList *objects, const gchar *crypto_object_path)
{
  gboolean ret = FALSE;
  GList *l;
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;
      block = udisks_object_peek_block (object);
      if (block != NULL)
        {
          if (g_strcmp0 (udisks_block_get_crypto_backing_device (block), crypto_object_path) == 0)
            {
              ret = TRUE;
              goto out;
            }
        }
    }
 out:
  return ret;
}

/**
 * udisks_linux_drive_object_is_not_in_use:
 * @object: A #UDisksLinuxDriveObject.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: A #GError or %NULL.
 *
 * Checks if the drive represented by @object is in use and sets
 * @error if so.
 *
 * Returns: %TRUE if @object is not is use, %FALSE if @error is set.
 */
gboolean
udisks_linux_drive_object_is_not_in_use (UDisksLinuxDriveObject  *object,
                                         GCancellable            *cancellable,
                                         GError                 **error)
{
  GDBusObjectManagerServer *object_manager;
  const gchar *drive_object_path;
  gboolean ret = TRUE;
  GList *objects = NULL;
  GList *l;

  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_OBJECT (object), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  drive_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));

  object_manager = udisks_daemon_get_object_manager (object->daemon);
  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));

  /* Visit all block devices related to the drive... */
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectSkeleton *iter_object = G_DBUS_OBJECT_SKELETON (l->data);
      UDisksBlock *block;
      UDisksFilesystem *filesystem;

      if (!UDISKS_IS_LINUX_BLOCK_OBJECT (iter_object))
        continue;

      block = udisks_object_peek_block (UDISKS_OBJECT (iter_object));
      filesystem = udisks_object_peek_filesystem (UDISKS_OBJECT (iter_object));

      if (g_strcmp0 (udisks_block_get_drive (block), drive_object_path) != 0)
        continue;

      /* bail if block device is mounted */
      if (filesystem != NULL)
        {
          if (g_strv_length ((gchar **) udisks_filesystem_get_mount_points (filesystem)) > 0)
            {
              g_set_error (error,
                           UDISKS_ERROR,
                           UDISKS_ERROR_DEVICE_BUSY,
                           "Device %s is mounted",
                           udisks_block_get_preferred_device (block));
              ret = FALSE;
              goto out;
            }
        }

      /* bail if block device is unlocked (LUKS) */
      if (is_block_unlocked (objects, g_dbus_object_get_object_path (G_DBUS_OBJECT (iter_object))))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_DEVICE_BUSY,
                       "Encrypted device %s is unlocked",
                       udisks_block_get_preferred_device (block));
          ret = FALSE;
          goto out;
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_drive_object_get_siblings:
 * @object: A #UDisksLinuxDriveObject.
 *
 * Gets the siblings for @object, if any.
 *
 * Returns: (transfer full) (element-type UDisksLinuxDriveObject): A list of #UDisksLinuxDriveObject
 *   instances. The returned list should be freed with g_list_free() after each element has been
 *   freed with g_object_unref().
 */
GList *
udisks_linux_drive_object_get_siblings (UDisksLinuxDriveObject *object)
{
  GDBusObjectManagerServer *object_manager;
  GList *ret = NULL;
  GList *objects = NULL;
  GList *l;
  gchar *sibling_id = NULL;

  if (object->iface_drive == NULL)
    goto out;

  sibling_id = udisks_drive_dup_sibling_id (object->iface_drive);
  if (sibling_id == NULL || strlen (sibling_id) == 0)
    goto out;

  object_manager = udisks_daemon_get_object_manager (object->daemon);
  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectSkeleton *iter_object = G_DBUS_OBJECT_SKELETON (l->data);
      UDisksLinuxDriveObject *iter_linux_drive_object;

      if (!UDISKS_IS_LINUX_DRIVE_OBJECT (iter_object))
        continue;

      iter_linux_drive_object = UDISKS_LINUX_DRIVE_OBJECT (iter_object);
      if (iter_linux_drive_object->iface_drive != NULL &&
          g_strcmp0 (udisks_drive_get_sibling_id (iter_linux_drive_object->iface_drive), sibling_id) == 0)
        {
          ret = g_list_prepend (ret, g_object_ref (iter_object));
        }
    }

 out:
  ret = g_list_reverse (ret);
  g_list_free_full (objects, g_object_unref);
  g_free (sibling_id);
  return ret;
}
