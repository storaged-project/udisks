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

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>

#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxblock.h"
#include "udisksmount.h"
#include "udisksmountmonitor.h"
#include "udiskslinuxdrive.h"
#include "udiskspersistentstore.h"

/**
 * SECTION:udiskslinuxblock
 * @title: UDisksLinuxBlock
 * @short_description: Linux block devices
 *
 * Object corresponding to a Linux block device.
 */

typedef struct _UDisksLinuxBlockClass   UDisksLinuxBlockClass;

/**
 * UDisksLinuxBlock:
 *
 * The #UDisksLinuxBlock structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxBlock
{
  GDBusObject parent_instance;

  UDisksDaemon *daemon;
  UDisksMountMonitor *mount_monitor;

  GUdevDevice *device;

  /* interface */
  UDisksLinuxSysfsDevice *iface_linux_sysfs_device;
  UDisksBlockDevice *iface_block_device;
};

struct _UDisksLinuxBlockClass
{
  GDBusObjectClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

G_DEFINE_TYPE (UDisksLinuxBlock, udisks_linux_block, G_TYPE_DBUS_OBJECT);

static void on_mount_monitor_mount_added   (UDisksMountMonitor  *monitor,
                                            UDisksMount         *mount,
                                            gpointer             user_data);
static void on_mount_monitor_mount_removed (UDisksMountMonitor  *monitor,
                                            UDisksMount         *mount,
                                            gpointer             user_data);

static void
udisks_linux_block_finalize (GObject *object)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (object);

  /* note: we don't hold a ref to block->daemon or block->mount_monitor */
  g_signal_handlers_disconnect_by_func (block->mount_monitor, on_mount_monitor_mount_added, block);
  g_signal_handlers_disconnect_by_func (block->mount_monitor, on_mount_monitor_mount_removed, block);

  g_object_unref (block->device);

  if (block->iface_linux_sysfs_device != NULL)
    g_object_unref (block->iface_linux_sysfs_device);
  if (block->iface_block_device != NULL)
    g_object_unref (block->iface_block_device);

  if (G_OBJECT_CLASS (udisks_linux_block_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_block_parent_class)->finalize (object);
}

static void
udisks_linux_block_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_block_get_daemon (block));
      break;

    case PROP_DEVICE:
      g_value_set_object (value, udisks_linux_block_get_device (block));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_block_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (block->daemon == NULL);
      /* we don't take a reference to the daemon */
      block->daemon = g_value_get_object (value);
      break;

    case PROP_DEVICE:
      g_assert (block->device == NULL);
      block->device = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
udisks_linux_block_init (UDisksLinuxBlock *block)
{
}

static void
udisks_linux_block_constructed (GObject *object)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (object);
  GString *str;

  block->mount_monitor = udisks_daemon_get_mount_monitor (block->daemon);
  g_signal_connect (block->mount_monitor,
                    "mount-added",
                    G_CALLBACK (on_mount_monitor_mount_added),
                    block);
  g_signal_connect (block->mount_monitor,
                    "mount-removed",
                    G_CALLBACK (on_mount_monitor_mount_removed),
                    block);

  /* initial coldplug */
  udisks_linux_block_uevent (block, "add", NULL);

  /* compute the object path */
  str = g_string_new ("/org/freedesktop/UDisks2/block_devices/");
  udisks_safe_append_to_object_path (str, g_udev_device_get_name (block->device));
  g_dbus_object_set_object_path (G_DBUS_OBJECT (block), str->str);
  g_string_free (str, TRUE);

  if (G_OBJECT_CLASS (udisks_linux_block_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_block_parent_class)->constructed (object);
}

static void
udisks_linux_block_class_init (UDisksLinuxBlockClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_block_finalize;
  gobject_class->constructed  = udisks_linux_block_constructed;
  gobject_class->set_property = udisks_linux_block_set_property;
  gobject_class->get_property = udisks_linux_block_get_property;

  /**
   * UDisksLinuxBlock:daemon:
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
   * UDisksLinuxBlock:device:
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
 * udisks_linux_block_new:
 * @daemon: A #UDisksDaemon.
 * @device: The #GUdevDevice for the sysfs block device.
 *
 * Create a new block object.
 *
 * Returns: A #UDisksLinuxBlock object. Free with g_object_unref().
 */
UDisksLinuxBlock *
udisks_linux_block_new (UDisksDaemon  *daemon,
                        GUdevDevice   *device)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_LINUX_BLOCK (g_object_new (UDISKS_TYPE_LINUX_BLOCK,
                                           "daemon", daemon,
                                           "device", device,
                                           NULL));
}

/**
 * udisks_linux_block_get_daemon:
 * @block: A #UDisksLinuxBlock.
 *
 * Gets the daemon used by @block.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @block.
 */
UDisksDaemon *
udisks_linux_block_get_daemon (UDisksLinuxBlock *block)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK (block), NULL);
  return block->daemon;
}

/**
 * udisks_linux_block_get_device:
 * @block: A #UDisksLinuxBlock.
 *
 * Gets the current #GUdevDevice for @block. Connect to
 * #GObject::notify to track changes to the #UDisksLinuxBlock:device
 * property.
 *
 * Returns: A #GUdevDevice. Free with g_object_unref().
 */
GUdevDevice *
udisks_linux_block_get_device (UDisksLinuxBlock *block)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK (block), NULL);
  return g_object_ref (block->device);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef gboolean (*HasInterfaceFunc)    (UDisksLinuxBlock     *block);
typedef void     (*ConnectInterfaceFunc) (UDisksLinuxBlock     *block);
typedef void     (*UpdateInterfaceFunc) (UDisksLinuxBlock     *block,
                                         const gchar    *uevent_action,
                                         GDBusInterface *interface);

static void
update_iface (UDisksLinuxBlock           *block,
              const gchar          *uevent_action,
              HasInterfaceFunc      has_func,
              ConnectInterfaceFunc   connect_func,
              UpdateInterfaceFunc   update_func,
              GType                 stub_type,
              gpointer              _interface_pointer)
{
  gboolean has;
  gboolean add;
  GDBusInterface **interface_pointer = _interface_pointer;

  g_return_if_fail (block != NULL);
  g_return_if_fail (has_func != NULL);
  g_return_if_fail (update_func != NULL);
  g_return_if_fail (g_type_is_a (stub_type, G_TYPE_OBJECT));
  g_return_if_fail (g_type_is_a (stub_type, G_TYPE_DBUS_INTERFACE));
  g_return_if_fail (interface_pointer != NULL);
  g_return_if_fail (*interface_pointer == NULL || G_IS_DBUS_INTERFACE (*interface_pointer));

  add = FALSE;
  has = has_func (block);
  if (*interface_pointer == NULL)
    {
      if (has)
        {
          *interface_pointer = g_object_new (stub_type, NULL);
          if (connect_func != NULL)
            connect_func (block);
          add = TRUE;
        }
    }
  else
    {
      if (!has)
        {
          g_dbus_object_remove_interface (G_DBUS_OBJECT (block), G_DBUS_INTERFACE (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      update_func (block, uevent_action, G_DBUS_INTERFACE (*interface_pointer));
      if (add)
        g_dbus_object_add_interface (G_DBUS_OBJECT (block), G_DBUS_INTERFACE (*interface_pointer));
    }
}

/* ---------------------------------------------------------------------------------------------------- */


/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.BlockDevice */

static gboolean
block_device_check (UDisksLinuxBlock *block)
{
  return TRUE;
}

static gboolean
handle_mount (UDisksBlockDevice      *block,
              GDBusMethodInvocation  *invocation,
              const gchar            *requested_fs_type,
              const gchar* const     *requested_options);

static gboolean
handle_unmount (UDisksBlockDevice      *block,
                GDBusMethodInvocation  *invocation,
                const gchar* const     *options);

static void
block_device_connect (UDisksLinuxBlock *block)
{
  g_signal_connect (block->iface_block_device,
                    "handle-filesystem-mount",
                    G_CALLBACK (handle_mount),
                    NULL);
  g_signal_connect (block->iface_block_device,
                    "handle-filesystem-unmount",
                    G_CALLBACK (handle_unmount),
                    NULL);
}

static gchar *
find_drive (GDBusObjectManager *object_manager,
            GUdevDevice        *block_device)
{
  const gchar *block_device_sysfs_path;
  gchar *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  block_device_sysfs_path = g_udev_device_get_sysfs_path (block_device);

  objects = g_dbus_object_manager_get_all (object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObject *object = G_DBUS_OBJECT (l->data);
      UDisksLinuxDrive *drive;
      GList *drive_devices;
      GList *j;

      if (!UDISKS_IS_LINUX_DRIVE (object))
        continue;

      drive = UDISKS_LINUX_DRIVE (object);
      drive_devices = udisks_linux_drive_get_devices (drive);

      for (j = drive_devices; j != NULL; j = j->next)
        {
          GUdevDevice *drive_device = G_UDEV_DEVICE (j->data);
          const gchar *drive_sysfs_path;

          drive_sysfs_path = g_udev_device_get_sysfs_path (drive_device);
          if (g_str_has_prefix (block_device_sysfs_path, drive_sysfs_path))
            {
              ret = g_dbus_object_get_object_path (object);
              g_list_foreach (drive_devices, (GFunc) g_object_unref, NULL);
              g_list_free (drive_devices);
              goto out;
            }
        }
      g_list_foreach (drive_devices, (GFunc) g_object_unref, NULL);
      g_list_free (drive_devices);
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static gchar *
find_block_device_by_sysfs_path (GDBusObjectManager *object_manager,
                                 const gchar        *sysfs_path)
{
  gchar *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  objects = g_dbus_object_manager_get_all (object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObject *object = G_DBUS_OBJECT (l->data);
      UDisksLinuxBlock *block;

      if (!UDISKS_IS_LINUX_BLOCK (object))
        continue;

      block = UDISKS_LINUX_BLOCK (object);

      if (g_strcmp0 (sysfs_path, g_udev_device_get_sysfs_path (block->device)) == 0)
        {
          ret = g_dbus_object_get_object_path (object);
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static void
block_device_update (UDisksLinuxBlock      *block,
                     const gchar     *uevent_action,
                     GDBusInterface  *_iface)
{
  UDisksBlockDevice *iface = UDISKS_BLOCK_DEVICE (_iface);
  GUdevDeviceNumber dev;
  GDBusObjectManager *object_manager;
  gchar *drive_object_path;
  gchar *s;
  GList *mounts;
  GList *l;
  GPtrArray *p;
  gboolean is_partition_table;
  gboolean is_partition_entry;
  const gchar *device_file;
  const gchar *const *symlinks;
  const gchar *preferred_device_file;

  dev = g_udev_device_get_device_number (block->device);
  device_file = g_udev_device_get_device_file (block->device);
  symlinks = g_udev_device_get_device_file_symlinks (block->device);

  udisks_block_device_set_device (iface, device_file);
  udisks_block_device_set_symlinks (iface, symlinks);
  udisks_block_device_set_major (iface, major (dev));
  udisks_block_device_set_minor (iface, minor (dev));
  udisks_block_device_set_size (iface, g_udev_device_get_sysfs_attr_as_uint64 (block->device, "size") * 512);

  /* Sort out preferred device... this is what UI shells should
   * display. We default to the block device name.
   *
   * This is mostly for things like device-mapper where device file is
   * a name of the form dm-%d and a symlink name conveys more
   * information.
   */
  preferred_device_file = g_udev_device_get_device_file (block->device);
  if (g_str_has_prefix (device_file, "/dev/dm-"))
    {
      guint n;
      for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
        {
          if (g_str_has_prefix (symlinks[n], "/dev/mapper/mpath"))
            {
              preferred_device_file = symlinks[n];
              break;
            }
        }
    }
  udisks_block_device_set_preferred_device (iface, preferred_device_file);

  /* Determine the drive this block device belongs to
   *
   * TODO: if this is slow we could have a cache or ensure that we
   * only do this once or something else
   */
  object_manager = udisks_daemon_get_object_manager (block->daemon);
  drive_object_path = find_drive (object_manager, block->device);
  if (drive_object_path != NULL)
    {
      udisks_block_device_set_drive (iface, drive_object_path);
      g_free (drive_object_path);
    }
  else
    {
      udisks_block_device_set_drive (iface, "/");
    }

  udisks_block_device_set_id_usage (iface, g_udev_device_get_property (block->device, "ID_FS_USAGE"));
  udisks_block_device_set_id_type (iface, g_udev_device_get_property (block->device, "ID_FS_TYPE"));
  udisks_block_device_set_id_version (iface, g_udev_device_get_property (block->device, "ID_FS_VERSION"));
  s = udisks_decode_udev_string (g_udev_device_get_property (block->device, "ID_FS_LABEL_ENC"));
  udisks_block_device_set_id_label (iface, s);
  g_free (s);
  s = udisks_decode_udev_string (g_udev_device_get_property (block->device, "ID_FS_UUID_ENC"));
  udisks_block_device_set_id_uuid (iface, s);
  g_free (s);

  p = g_ptr_array_new ();
  mounts = udisks_mount_monitor_get_mounts_for_dev (block->mount_monitor,
                                                    g_udev_device_get_device_number (block->device));
  /* we are guaranteed that the list is sorted so if there are
   * multiple mounts we'll always get the same order
   */
  for (l = mounts; l != NULL; l = l->next)
    {
      UDisksMount *mount = UDISKS_MOUNT (l->data);
      g_ptr_array_add (p, (gpointer) udisks_mount_get_mount_path (mount));
    }
  g_ptr_array_add (p, NULL);
  udisks_block_device_set_filesystem_mount_points (iface, (const gchar *const *) p->pdata);
  g_ptr_array_free (p, TRUE);
  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);

  /* TODO: port this to blkid properties */

  /* Update the partition table and partition entry properties */
  is_partition_table = FALSE;
  is_partition_entry = FALSE;
  if (g_strcmp0 (g_udev_device_get_devtype (block->device), "partition") == 0 ||
      g_udev_device_get_property_as_boolean (block->device, "UDISKS_PARTITION"))
    {
      is_partition_entry = TRUE;
    }
  else if (g_udev_device_get_property_as_boolean (block->device, "UDISKS_PARTITION_TABLE"))
    {
      is_partition_table = TRUE;
    }

  /* partition table */
  if (is_partition_table)
    {
      udisks_block_device_set_part_table (iface, TRUE);
      udisks_block_device_set_part_table_scheme (iface,
                                                 g_udev_device_get_property (block->device,
                                                                             "UDISKS_PARTITION_TABLE_SCHEME"));
    }
  else
    {
      udisks_block_device_set_part_table (iface, FALSE);
      udisks_block_device_set_part_table_scheme (iface, "");
    }

  /* partition entry */
  if (is_partition_entry)
    {
      gchar *slave_sysfs_path;
      udisks_block_device_set_part_entry (iface, TRUE);
      udisks_block_device_set_part_entry_scheme (iface,
                                                 g_udev_device_get_property (block->device,
                                                                             "UDISKS_PARTITION_SCHEME"));
      udisks_block_device_set_part_entry_type (iface,
                                               g_udev_device_get_property (block->device,
                                                                           "UDISKS_PARTITION_TYPE"));
      udisks_block_device_set_part_entry_flags (iface,
                                                g_udev_device_get_property (block->device,
                                                                            "UDISKS_PARTITION_FLAGS"));
      slave_sysfs_path = g_strdup (g_udev_device_get_property (block->device, "UDISKS_PARTITION_SLAVE"));
      if (slave_sysfs_path == NULL)
        {
          if (g_strcmp0 (g_udev_device_get_devtype (block->device), "partition") == 0)
            {
              GUdevDevice *parent;
              parent = g_udev_device_get_parent (block->device);
              slave_sysfs_path = g_strdup (g_udev_device_get_sysfs_path (parent));
              g_object_unref (parent);
            }
          else
            {
              g_warning ("No UDISKS_PARTITION_SLAVE property and DEVTYPE is not partition for block device %s",
                         g_udev_device_get_sysfs_path (block->device));
            }
        }
      if (slave_sysfs_path != NULL)
        {
          gchar *slave_object_path;
          slave_object_path = find_block_device_by_sysfs_path (udisks_daemon_get_object_manager (block->daemon),
                                                               slave_sysfs_path);
          if (slave_object_path != NULL)
            udisks_block_device_set_part_entry_table (iface, slave_object_path);
          else
            udisks_block_device_set_part_entry_table (iface, "/");
          g_free (slave_object_path);
          g_free (slave_sysfs_path);
        }
      else
        {
          udisks_block_device_set_part_entry_table (iface, "/");
        }
      udisks_block_device_set_part_entry_offset (iface,
                                                 g_udev_device_get_property_as_uint64 (block->device,
                                                                                       "UDISKS_PARTITION_OFFSET"));
      udisks_block_device_set_part_entry_size (iface,
                                               g_udev_device_get_property_as_uint64 (block->device,
                                                                                     "UDISKS_PARTITION_SIZE"));
    }
  else
    {
      udisks_block_device_set_part_entry (iface, FALSE);
      udisks_block_device_set_part_entry_scheme (iface, "");
      udisks_block_device_set_part_entry_type (iface, "");
      udisks_block_device_set_part_entry_flags (iface, "");
      udisks_block_device_set_part_entry_table (iface, "/");
      udisks_block_device_set_part_entry_offset (iface, 0);
      udisks_block_device_set_part_entry_size (iface, 0);
    }
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.LinuxSysfsDevice */

static gboolean
linux_sysfs_device_check (UDisksLinuxBlock *block)
{
  return TRUE;
}

static void
linux_sysfs_device_update (UDisksLinuxBlock      *block,
                           const gchar     *uevent_action,
                           GDBusInterface  *_iface)
{
  UDisksLinuxSysfsDevice *iface = UDISKS_LINUX_SYSFS_DEVICE (_iface);

  udisks_linux_sysfs_device_set_subsystem (iface, "block");
  udisks_linux_sysfs_device_set_sysfs_path (iface, g_udev_device_get_sysfs_path (block->device));

  if (uevent_action != NULL)
    udisks_linux_sysfs_device_emit_uevent (iface, uevent_action);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_block_uevent:
 * @block: A #UDisksLinuxBlock.
 * @action: Uevent action or %NULL
 * @device: A new #GUdevDevice device object or %NULL if the device hasn't changed.
 *
 * Updates all information on interfaces on @block.
 */
void
udisks_linux_block_uevent (UDisksLinuxBlock *block,
                           const gchar      *action,
                           GUdevDevice      *device)
{
  g_return_if_fail (UDISKS_IS_LINUX_BLOCK (block));
  g_return_if_fail (device == NULL || G_UDEV_IS_DEVICE (device));

  if (device != NULL)
    {
      g_object_unref (block->device);
      block->device = g_object_ref (device);
      g_object_notify (G_OBJECT (block), "device");
    }

  update_iface (block, action, linux_sysfs_device_check, NULL, linux_sysfs_device_update,
                UDISKS_TYPE_LINUX_SYSFS_DEVICE_STUB, &block->iface_linux_sysfs_device);
  update_iface (block, action, block_device_check, block_device_connect, block_device_update,
                UDISKS_TYPE_BLOCK_DEVICE_STUB, &block->iface_block_device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_mount_monitor_mount_added (UDisksMountMonitor  *monitor,
                              UDisksMount         *mount,
                              gpointer             user_data)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (user_data);
  if (udisks_mount_get_dev (mount) == g_udev_device_get_device_number (block->device))
    udisks_linux_block_uevent (block, NULL, NULL);
}

static void
on_mount_monitor_mount_removed (UDisksMountMonitor  *monitor,
                                UDisksMount         *mount,
                                gpointer             user_data)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (user_data);
  if (udisks_mount_get_dev (mount) == g_udev_device_get_device_number (block->device))
    udisks_linux_block_uevent (block, NULL, NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_uid_sync (GDBusMethodInvocation   *invocation,
              GCancellable            *cancellable,
              uid_t                   *out_uid,
              GError                 **error)
{
  gboolean ret;
  const gchar *caller;
  GVariant *value;
  GError *local_error;

  ret = FALSE;

  caller = g_dbus_method_invocation_get_sender (invocation);

  local_error = NULL;
  value = g_dbus_connection_call_sync (g_dbus_method_invocation_get_connection (invocation),
                                       "org.freedesktop.DBus",  /* bus name */
                                       "/org/freedesktop/DBus", /* object path */
                                       "org.freedesktop.DBus",  /* interface */
                                       "GetConnectionUnixUser", /* method */
                                       g_variant_new ("(s)", caller),
                                       G_VARIANT_TYPE ("(u)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1, /* timeout_msec */
                                       cancellable,
                                       &local_error);
  if (value == NULL)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Error determining uid of caller %s: %s (%s, %d)",
                   caller,
                   local_error->message,
                   g_quark_to_string (local_error->domain),
                   local_error->code);
      g_error_free (local_error);
      goto out;
    }

  G_STATIC_ASSERT (sizeof (uid_t) == sizeof (guint32));
  g_variant_get (value, "(u)", out_uid);

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  const gchar *fstype;
  const gchar * const *defaults;
  const gchar * const *allow;
  const gchar * const *allow_uid_self;
  const gchar * const *allow_gid_self;
} FSMountOptions;

/* ---------------------- vfat -------------------- */

static const gchar *vfat_defaults[] = { "uid=", "gid=", "shortname=mixed", "dmask=0077", "utf8=1", "showexec", NULL };
static const gchar *vfat_allow[] = { "flush", "utf8=", "shortname=", "umask=", "dmask=", "fmask=", "codepage=", "iocharset=", "usefree", "showexec", NULL };
static const gchar *vfat_allow_uid_self[] = { "uid=", NULL };
static const gchar *vfat_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- ntfs -------------------- */
/* this is assuming that ntfs-3g is used */

static const gchar *ntfs_defaults[] = { "uid=", "gid=", "dmask=0077", "fmask=0177", NULL };
static const gchar *ntfs_allow[] = { "umask=", "dmask=", "fmask=", NULL };
static const gchar *ntfs_allow_uid_self[] = { "uid=", NULL };
static const gchar *ntfs_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- iso9660 -------------------- */

static const gchar *iso9660_defaults[] = { "uid=", "gid=", "iocharset=utf8", "mode=0400", "dmode=0500", NULL };
static const gchar *iso9660_allow[] = { "norock", "nojoliet", "iocharset=", "mode=", "dmode=", NULL };
static const gchar *iso9660_allow_uid_self[] = { "uid=", NULL };
static const gchar *iso9660_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- udf -------------------- */

static const gchar *udf_defaults[] = { "uid=", "gid=", "iocharset=utf8", "umask=0077", NULL };
static const gchar *udf_allow[] = { "iocharset=", "umask=", NULL };
static const gchar *udf_allow_uid_self[] = { "uid=", NULL };
static const gchar *udf_allow_gid_self[] = { "gid=", NULL };

/* ------------------------------------------------ */
/* TODO: support context= */

static const gchar *any_allow[] = { "exec", "noexec", "nodev", "nosuid", "atime", "noatime", "nodiratime", "ro", "rw", "sync", "dirsync", NULL };

static const FSMountOptions fs_mount_options[] =
  {
    { "vfat", vfat_defaults, vfat_allow, vfat_allow_uid_self, vfat_allow_gid_self },
    { "ntfs", ntfs_defaults, ntfs_allow, ntfs_allow_uid_self, ntfs_allow_gid_self },
    { "iso9660", iso9660_defaults, iso9660_allow, iso9660_allow_uid_self, iso9660_allow_gid_self },
    { "udf", udf_defaults, udf_allow, udf_allow_uid_self, udf_allow_gid_self },
  };

/* ------------------------------------------------ */

static int num_fs_mount_options = sizeof(fs_mount_options) / sizeof(FSMountOptions);

static const FSMountOptions *
find_mount_options_for_fs (const gchar *fstype)
{
  int n;
  const FSMountOptions *fsmo;

  for (n = 0; n < num_fs_mount_options; n++)
    {
      fsmo = fs_mount_options + n;
      if (g_strcmp0 (fsmo->fstype, fstype) == 0)
        goto out;
    }

  fsmo = NULL;
 out:
  return fsmo;
}

static gid_t
find_primary_gid (uid_t uid)
{
  struct passwd *pw;
  gid_t gid;

  gid = (gid_t) - 1;

  pw = getpwuid (uid);
  if (pw == NULL)
    {
      g_warning ("Couldn't look up uid %d: %m", uid);
      goto out;
    }
  gid = pw->pw_gid;

 out:
  return gid;
}

static gboolean
is_uid_in_gid (uid_t uid,
               gid_t gid)
{
  gboolean ret;
  struct passwd *pw;
  static gid_t supplementary_groups[128];
  int num_supplementary_groups = 128;
  int n;

  /* TODO: use some #define instead of harcoding some random number like 128 */

  ret = FALSE;

  pw = getpwuid (uid);
  if (pw == NULL)
    {
      g_warning ("Couldn't look up uid %d: %m", uid);
      goto out;
    }
  if (pw->pw_gid == gid)
    {
      ret = TRUE;
      goto out;
    }

  if (getgrouplist (pw->pw_name, pw->pw_gid, supplementary_groups, &num_supplementary_groups) < 0)
    {
      g_warning ("Couldn't find supplementary groups for uid %d: %m", uid);
      goto out;
    }

  for (n = 0; n < num_supplementary_groups; n++)
    {
      if (supplementary_groups[n] == gid)
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  return ret;
}

static gboolean
is_mount_option_allowed (const FSMountOptions *fsmo,
                         const gchar          *option,
                         uid_t                 caller_uid)
{
  int n;
  gchar *endp;
  uid_t uid;
  gid_t gid;
  gboolean allowed;
  const gchar *ep;
  gsize ep_len;

  allowed = FALSE;

  /* first run through the allowed mount options */
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->allow != NULL && fsmo->allow[n] != NULL; n++)
        {
          ep = strstr (fsmo->allow[n], "=");
          if (ep != NULL && ep[1] == '\0')
            {
              ep_len = ep - fsmo->allow[n] + 1;
              if (strncmp (fsmo->allow[n], option, ep_len) == 0)
                {
                  allowed = TRUE;
                  goto out;
                }
            }
          else
            {
              if (strcmp (fsmo->allow[n], option) == 0)
                {
                  allowed = TRUE;
                  goto out;
                }
            }
        }
    }
  for (n = 0; any_allow[n] != NULL; n++)
    {
      ep = strstr (any_allow[n], "=");
      if (ep != NULL && ep[1] == '\0')
        {
          ep_len = ep - any_allow[n] + 1;
          if (strncmp (any_allow[n], option, ep_len) == 0)
            {
              allowed = TRUE;
              goto out;
            }
        }
      else
        {
          if (strcmp (any_allow[n], option) == 0)
            {
              allowed = TRUE;
              goto out;
            }
        }
    }

  /* .. then check for mount options where the caller is allowed to pass
   * in his own uid
   */
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->allow_uid_self != NULL && fsmo->allow_uid_self[n] != NULL; n++)
        {
          const gchar *r_mount_option = fsmo->allow_uid_self[n];
          if (g_str_has_prefix (option, r_mount_option))
            {
              uid = strtol (option + strlen (r_mount_option), &endp, 10);
              if (*endp != '\0')
                continue;
              if (uid == caller_uid)
                {
                  allowed = TRUE;
                  goto out;
                }
            }
        }
    }

  /* .. ditto for gid
   */
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->allow_gid_self != NULL && fsmo->allow_gid_self[n] != NULL; n++)
        {
          const gchar *r_mount_option = fsmo->allow_gid_self[n];
          if (g_str_has_prefix (option, r_mount_option))
            {
              gid = strtol (option + strlen (r_mount_option), &endp, 10);
              if (*endp != '\0')
                continue;
              if (is_uid_in_gid (caller_uid, gid))
                {
                  allowed = TRUE;
                  goto out;
                }
            }
        }
    }

 out:
  return allowed;
}

static gchar **
prepend_default_mount_options (const FSMountOptions *fsmo,
                               uid_t caller_uid,
                               const gchar * const *given_options)
{
  GPtrArray *options;
  int n;
  gchar *s;
  gid_t gid;

  options = g_ptr_array_new ();
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->defaults != NULL && fsmo->defaults[n] != NULL; n++)
        {
          const gchar *option = fsmo->defaults[n];

          if (strcmp (option, "uid=") == 0)
            {
              s = g_strdup_printf ("uid=%d", caller_uid);
              g_ptr_array_add (options, s);
            }
          else if (strcmp (option, "gid=") == 0)
            {
              gid = find_primary_gid (caller_uid);
              if (gid != (gid_t) - 1)
                {
                  s = g_strdup_printf ("gid=%d", gid);
                  g_ptr_array_add (options, s);
                }
            }
          else
            {
              g_ptr_array_add (options, g_strdup (option));
            }
        }
    }
  for (n = 0; given_options[n] != NULL; n++)
    {
      g_ptr_array_add (options, g_strdup (given_options[n]));
    }

  g_ptr_array_add (options, NULL);

  return (char **) g_ptr_array_free (options, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

/*
 * calculate_fs_type: <internal>
 * @block: A #UDisksBlockDevice.
 * @requested_fs_type: The requested file system type or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Calculates the file system type to use.
 *
 * Returns: A valid UTF-8 string with the filesystem type (may be "auto") or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_fs_type (UDisksBlockDevice         *block,
                   const gchar               *requested_fs_type,
                   GError                   **error)
{
  gchar *fs_type_to_use;
  const gchar *probed_fs_usage;
  const gchar *probed_fs_type;

  probed_fs_usage = NULL;
  probed_fs_type = NULL;
  if (block != NULL)
    {
      probed_fs_usage = udisks_block_device_get_id_usage (block);
      probed_fs_type = udisks_block_device_get_id_type (block);
    }

  fs_type_to_use = NULL;
  if (requested_fs_type != NULL && strlen (requested_fs_type) > 0)
    {
      /* TODO: maybe check that it's compatible with probed_fs_type */
      fs_type_to_use = g_strdup (requested_fs_type);
    }
  else
    {
      if (probed_fs_type != NULL && strlen (probed_fs_type) > 0)
        fs_type_to_use = g_strdup (probed_fs_type);
      else
        fs_type_to_use = g_strdup ("auto");
    }

  g_assert (fs_type_to_use == NULL || g_utf8_validate (fs_type_to_use, -1, NULL));

  return fs_type_to_use;
}

/*
 * calculate_mount_options: <internal>
 * @block: A #UDisksBlockDevice.
 * @caller_uid: The uid of the caller making the request.
 * @fs_type: The filesystem type to use or %NULL.
 * @requested_options: Options requested by the caller.
 * @out_auth_no_user_interaction: Return location for whether the 'auth_no_user_interaction' option was passed or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Calculates the mount option string to use. Ensures (by returning an
 * error) that only safe options are used.
 *
 * Returns: A string with mount options or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_mount_options (UDisksBlockDevice         *block,
                         uid_t                      caller_uid,
                         const gchar               *fs_type,
                         const gchar *const        *requested_options,
                         gboolean                  *out_auth_no_user_interaction,
                         GError                   **error)
{
  const FSMountOptions *fsmo;
  gchar **options_to_use;
  gchar *options_to_use_str;
  GString *str;
  guint n;
  gboolean auth_no_user_interaction;

  options_to_use = NULL;
  options_to_use_str = NULL;
  auth_no_user_interaction = FALSE;

  fsmo = find_mount_options_for_fs (fs_type);

  /* always prepend some reasonable default mount options; these are
   * chosen here; the user can override them if he wants to
   */
  options_to_use = prepend_default_mount_options (fsmo, caller_uid, requested_options);

  /* validate mount options */
  str = g_string_new ("uhelper=udisks2,nodev,nosuid");
  for (n = 0; options_to_use[n] != NULL; n++)
    {
      const gchar *option = options_to_use[n];

      if (g_strcmp0 (option, "auth_no_user_interaction") == 0)
        {
          auth_no_user_interaction = TRUE;
          continue;
        }

      /* avoid attacks like passing "shortname=lower,uid=0" as a single mount option */
      if (strstr (option, ",") != NULL)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_OPTION_NOT_PERMITTED,
                       "Malformed mount option `%s'",
                       option);
          g_string_free (str, TRUE);
          goto out;
        }

      /* first check if the mount option is allowed */
      if (!is_mount_option_allowed (fsmo, option, caller_uid))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_OPTION_NOT_PERMITTED,
                       "Mount option `%s' is not allowed",
                       option);
          g_string_free (str, TRUE);
          goto out;
        }

      g_string_append_c (str, ',');
      g_string_append (str, option);
    }
  options_to_use_str = g_string_free (str, FALSE);

 out:
  g_strfreev (options_to_use);

  g_assert (options_to_use_str == NULL || g_utf8_validate (options_to_use_str, -1, NULL));

  if (out_auth_no_user_interaction != NULL)
    *out_auth_no_user_interaction = auth_no_user_interaction;

  return options_to_use_str;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
ensure_utf8 (const gchar *s)
{
  const gchar *end;
  gchar *ret;

  if (!g_utf8_validate (s, -1, &end))
    {
      gchar *tmp;
      gint pos;
      /* TODO: could possibly return a nicer UTF-8 string  */
      pos = (gint) (end - s);
      tmp = g_strndup (s, end - s);
      ret = g_strdup_printf ("%s (Invalid UTF-8 at byte %d)", tmp, pos);
      g_free (tmp);
    }
  else
    {
      ret = g_strdup (s);
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/*
 * calculate_mount_point: <internal>
 * @block: A #UDisksBlockDevice.
 * @fs_type: The file system type to mount with
 * @error: Return location for error or %NULL.
 *
 * Calculates the mount point to use.
 *
 * Returns: A UTF-8 string with the mount point to use or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_mount_point (UDisksBlockDevice         *block,
                       const gchar               *fs_type,
                       GError                   **error)
{
  const gchar *label;
  const gchar *uuid;
  gchar *mount_point;
  gchar *orig_mount_point;
  GString *str;
  gchar *s;
  guint n;

  label = NULL;
  uuid = NULL;
  if (block != NULL)
    {
      label = udisks_block_device_get_id_label (block);
      uuid = udisks_block_device_get_id_uuid (block);
    }

  /* NOTE: UTF-8 has the nice property that valid UTF-8 strings only contains
   *       the byte 0x2F if it's for the '/' character (U+002F SOLIDUS).
   *
   *       See http://en.wikipedia.org/wiki/UTF-8 for details.
   */

  if (label != NULL && strlen (label) > 0)
    {
      str = g_string_new ("/media/");
      s = ensure_utf8 (label);
      for (n = 0; s[n] != '\0'; n++)
        {
          gint c = s[n];
          if (c == '/')
            g_string_append_c (str, '_');
          else
            g_string_append_c (str, c);
        }
      mount_point = g_string_free (str, FALSE);
      g_free (s);
    }
  else if (uuid != NULL && strlen (uuid) > 0)
    {
      str = g_string_new ("/media/");
      s = ensure_utf8 (uuid);
      for (n = 0; s[n] != '\0'; n++)
        {
          gint c = s[n];
          if (c == '/')
            g_string_append_c (str, '_');
          else
            g_string_append_c (str, c);
        }
      mount_point = g_string_free (str, FALSE);
      g_free (s);
    }
  else
    {
      mount_point = g_strdup ("/media/disk");
    }

  /* ... then uniqify the mount point */
  orig_mount_point = g_strdup (mount_point);
  n = 1;
  while (TRUE)
    {
      if (!g_file_test (mount_point, G_FILE_TEST_EXISTS))
        {
          break;
        }
      else
        {
          g_free (mount_point);
          mount_point = g_strdup_printf ("%s%d", orig_mount_point, n++);
        }
    }
  g_free (orig_mount_point);

  return mount_point;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling method call */
static gboolean
handle_mount (UDisksBlockDevice      *block,
              GDBusMethodInvocation  *invocation,
              const gchar            *requested_fs_type,
              const gchar* const     *requested_options)
{
  GDBusObject *object;
  UDisksDaemon *daemon;
  UDisksPersistentStore *store;
  gboolean ret;
  uid_t caller_uid;
  const gchar * const *existing_mount_points;
  const gchar *probed_fs_usage;
  const gchar *probed_fs_type;
  gchar *fs_type_to_use;
  gchar *mount_options_to_use;
  gchar *mount_point_to_use;
  gchar *escaped_fs_type_to_use;
  gchar *escaped_mount_options_to_use;
  gchar *escaped_mount_point_to_use;
  gchar *error_message;
  GError *error;
  PolkitSubject *auth_subject;
  const gchar *auth_action_id;
  PolkitDetails *auth_details;
  gboolean auth_no_user_interaction;
  PolkitCheckAuthorizationFlags auth_flags;
  PolkitAuthorizationResult *auth_result;

  ret = FALSE;
  object = NULL;
  daemon = NULL;
  error_message = NULL;
  fs_type_to_use = NULL;
  mount_options_to_use = NULL;
  mount_point_to_use = NULL;
  escaped_fs_type_to_use = NULL;
  escaped_mount_options_to_use = NULL;
  escaped_mount_point_to_use = NULL;
  auth_subject = NULL;
  auth_details = NULL;
  auth_result = NULL;
  auth_no_user_interaction = FALSE;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));
  store = udisks_daemon_get_persistent_store (daemon);

  /* TODO: check if mount point is managed by e.g. /etc/fstab or
   *       similar - if so, use that instead of managing mount points
   *       in /media
   */

  /* Fail if the device is already mounted */
  existing_mount_points = udisks_block_device_get_filesystem_mount_points (block);
  if (existing_mount_points != NULL && g_strv_length ((gchar **) existing_mount_points) > 0)
    {
      GString *str;
      guint n;
      str = g_string_new (NULL);
      for (n = 0; existing_mount_points[n] != NULL; n++)
        {
          if (n > 0)
            g_string_append (str, ", ");
          g_string_append_printf (str, "`%s'", existing_mount_points[n]);
        }
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_ALREADY_MOUNTED,
                                             "Device %s is already mounted at %s.\n",
                                             udisks_block_device_get_device (block),
                                             str->str);
      g_string_free (str, TRUE);
      goto out;
    }

  /* Fail if the device is not mountable - we actually allow mounting
   * devices that are not probed since since it could be that we just
   * don't have the data in the udev database but the device has a
   * filesystem *anyway*...
   *
   * For example, this applies to PC floppy devices - automatically
   * probing for media them creates annoying noise. So they won't
   * appear in the udev database.
   */
  probed_fs_usage = NULL;
  probed_fs_type = NULL;
  if (block != NULL)
    {
      probed_fs_usage = udisks_block_device_get_id_usage (block);
      probed_fs_type = udisks_block_device_get_id_type (block);
    }
  if (probed_fs_usage != NULL && strlen (probed_fs_usage) > 0 &&
      g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Cannot mount block device %s with probed usage `%s' - expected `filesystem'",
                                             udisks_block_device_get_device (block),
                                             probed_fs_usage);
      goto out;
    }

  /* we need the uid of the caller to check mount options */
  error = NULL;
  if (!get_uid_sync (invocation, NULL /* GCancellable */, &caller_uid, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* calculate filesystem type (guaranteed to be valid UTF-8) */
  error = NULL;
  fs_type_to_use = calculate_fs_type (block,
                                      requested_fs_type,
                                      &error);
  if (fs_type_to_use == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* calculate mount options (guaranteed to be valid UTF-8) */
  error = NULL;
  mount_options_to_use = calculate_mount_options (block,
                                                  caller_uid,
                                                  fs_type_to_use,
                                                  requested_options,
                                                  &auth_no_user_interaction,
                                                  &error);
  if (mount_options_to_use == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* calculate mount point (guaranteed to be valid UTF-8) */
  error = NULL;
  mount_point_to_use = calculate_mount_point (block,
                                              fs_type_to_use,
                                              &error);
  if (mount_point_to_use == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* now check that the user is actually authorized to mount the device
   *
   * (TODO: fill in details and pick the right action_id)
   */
  auth_action_id = "org.freedesktop.udisks2.filesystem-mount",
  auth_subject = polkit_system_bus_name_new (g_dbus_method_invocation_get_sender (invocation));
  auth_flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE;
  if (!auth_no_user_interaction)
    auth_flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;
  error = NULL;
  auth_result = polkit_authority_check_authorization_sync (udisks_daemon_get_authority (daemon),
                                                           auth_subject,
                                                           auth_action_id,
                                                           auth_details,
                                                           auth_flags,
                                                           NULL, /* GCancellable* */
                                                           &error);
  if (auth_result == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error checking authorization: %s (%s, %d)",
                                             error->message,
                                             g_quark_to_string (error->domain),
                                             error->code);
      g_error_free (error);
      goto out;
    }
  if (!polkit_authorization_result_get_is_authorized (auth_result))
    {
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     UDISKS_ERROR,
                                                     polkit_authorization_result_get_is_challenge (auth_result) ?
                                                     UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN :
                                                     UDISKS_ERROR_NOT_AUTHORIZED,
                                                     "Not authorized to perform operation");
      goto out;
    }

  /* create the mount point */
  if (g_mkdir (mount_point_to_use, 0700) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error creating mount point `%s': %m",
                                             mount_point_to_use);
      goto out;
    }

  /* update the mounted-fs file */
  if (!udisks_persistent_store_mounted_fs_add (store,
                                               udisks_block_device_get_device (block),
                                               mount_point_to_use,
                                               caller_uid,
                                               &error))
    goto out;

  escaped_fs_type_to_use       = g_strescape (fs_type_to_use, NULL);
  escaped_mount_options_to_use = g_strescape (mount_options_to_use, NULL);
  escaped_mount_point_to_use   = g_strescape (mount_point_to_use, NULL);

  /* run mount(8) */
  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              NULL,  /* GCancellable */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "mount -t \"%s\" -o \"%s\" \"%s\" \"%s\"",
                                              escaped_fs_type_to_use,
                                              escaped_mount_options_to_use,
                                              udisks_block_device_get_device (block),
                                              escaped_mount_point_to_use))
    {
      /* ugh, something went wrong.. we need to clean up the created mount point
       * and also remove the entry from our mounted-fs file
       *
       * Either of these operations shouldn't really fail...
       */
      error = NULL;
      if (!udisks_persistent_store_mounted_fs_remove (store,
                                                      mount_point_to_use,
                                                      &error))
        {
          udisks_daemon_log (daemon,
                             UDISKS_LOG_LEVEL_WARNING,
                             "Error removing mount point %s from filesystems file: %s (%s, %d)",
                             mount_point_to_use,
                             error->message,
                             g_quark_to_string (error->domain),
                             error->code);
          g_error_free (error);
        }
      if (g_rmdir (mount_point_to_use) != 0)
        {
          udisks_daemon_log (daemon,
                             UDISKS_LOG_LEVEL_WARNING,
                             "Error removing directory %s: %m",
                             mount_point_to_use);
        }
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error mounting %s at %s: %s",
                                             udisks_block_device_get_device (block),
                                             mount_point_to_use,
                                             error_message);
      goto out;
    }

  udisks_daemon_log (daemon,
                     UDISKS_LOG_LEVEL_INFO,
                     "Mounted %s at %s on behalf of uid %d",
                     udisks_block_device_get_device (block),
                     mount_point_to_use,
                     caller_uid);

  udisks_block_device_complete_filesystem_mount (block, invocation, mount_point_to_use);

 out:
  if (auth_subject != NULL)
    g_object_unref (auth_subject);
  if (auth_details != NULL)
    g_object_unref (auth_details);
  if (auth_result != NULL)
    g_object_unref (auth_result);
  g_free (error_message);
  g_free (escaped_fs_type_to_use);
  g_free (escaped_mount_options_to_use);
  g_free (escaped_mount_point_to_use);
  g_free (fs_type_to_use);
  g_free (mount_options_to_use);
  g_free (mount_point_to_use);
  if (object != NULL)
    g_object_unref (object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling method call */
static gboolean
handle_unmount (UDisksBlockDevice      *block,
                GDBusMethodInvocation  *invocation,
                const gchar* const     *options)
{
  GDBusObject *object;
  UDisksDaemon *daemon;
  UDisksPersistentStore *store;
  gchar *mount_point;
  gchar *escaped_mount_point;
  GError *error;
  uid_t mounted_by_uid;
  uid_t caller_uid;
  gchar *error_message;
  const gchar *const *mount_points;
  guint n;
  gboolean opt_force;
  gboolean rc;

  mount_point = NULL;
  escaped_mount_point = NULL;
  error_message = NULL;
  opt_force = FALSE;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));
  store = udisks_daemon_get_persistent_store (daemon);

  for (n = 0; options != NULL && options[n] != NULL; n++)
    {
      const gchar *option = options[n];
      if (g_strcmp0 (option, "force") == 0)
        {
          opt_force = TRUE;
        }
      else
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_OPTION_NOT_PERMITTED,
                                                 "Unsupported option `%s'",
                                                 option);
          goto out;
        }
    }

  mount_points = udisks_block_device_get_filesystem_mount_points (block);
  if (mount_points == NULL || g_strv_length ((gchar **) mount_points) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_MOUNTED,
                                             "Device `%s' is not mounted",
                                             udisks_block_device_get_device (block));
      goto out;
    }

  error = NULL;
  mount_point = udisks_persistent_store_mounted_fs_find (store,
                                                         udisks_block_device_get_device (block),
                                                         &mounted_by_uid,
                                                         &error);
  if (error != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error when looking for entry `%s' in mounted-fs: %s (%s, %d)",
                                             udisks_block_device_get_device (block),
                                             error->message,
                                             g_quark_to_string (error->domain),
                                             error->code);
      g_error_free (error);
      goto out;
    }
  if (mount_point == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Entry for `%s' not found in mounted-fs",
                                             udisks_block_device_get_device (block));
      goto out;
    }

  /* TODO: allow unmounting stuff not in the mounted-fs file? */

  error = NULL;
  if (!get_uid_sync (invocation, NULL, &caller_uid, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  if (caller_uid != 0 && (caller_uid != mounted_by_uid))
    {
      /* TODO: allow with special authorization (unmount-others) */
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_MOUNTED_BY_OTHER_USER,
                                             "Cannot unmount filesystem at `%s' mounted by other user with uid %d",
                                             mount_point,
                                             mounted_by_uid);
      goto out;
    }

  /* otherwise go ahead and unmount the filesystem */
  if (!udisks_persistent_store_mounted_fs_currently_unmounting_add (store, mount_point))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_ALREADY_UNMOUNTING,
                                             "Cannot unmount %s: Mount point `%s' is currently being unmounted",
                                             udisks_block_device_get_device (block),
                                             mount_point);
      goto out;
    }

  escaped_mount_point = g_strescape (mount_point, NULL);
  if (opt_force)
    {
      /* right now -l is the only way to "force unmount" file systems... */
      rc = udisks_daemon_launch_spawned_job_sync (daemon,
                                                  NULL,  /* GCancellable */
                                                  &error_message,
                                                  NULL,  /* input_string */
                                                  "umount -l \"%s\"",
                                                  escaped_mount_point);
    }
  else
    {
      rc = udisks_daemon_launch_spawned_job_sync (daemon,
                                                  NULL,  /* GCancellable */
                                                  &error_message,
                                                  NULL,  /* input_string */
                                                  "umount \"%s\"",
                                                  escaped_mount_point);
    }
  if (!rc)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error unmounting %s from %s: %s",
                                             udisks_block_device_get_device (block),
                                             mount_point,
                                             error_message);
      udisks_persistent_store_mounted_fs_currently_unmounting_remove (store, mount_point);
      goto out;
    }

  /* OK, filesystem unmounted.. now to remove the entry from mounted-fs as well as the mount point */
  error = NULL;
  if (!udisks_persistent_store_mounted_fs_remove (store,
                                                  mount_point,
                                                  &error))
    {
      if (error == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error removing entry for `%s' from mounted-fs: Entry not found",
                                                 mount_point);
        }
      else
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error removing entry for `%s' from mounted-fs: %s (%s, %d)",
                                                 mount_point,
                                                 error->message,
                                                 g_quark_to_string (error->domain),
                                                 error->code);
          g_error_free (error);
        }
      udisks_persistent_store_mounted_fs_currently_unmounting_remove (store, mount_point);
      goto out;
    }
  udisks_persistent_store_mounted_fs_currently_unmounting_remove (store, mount_point);

  /* OK, removed the entry. Finally: nuke the mount point */
  if (g_rmdir (mount_point) != 0)
    {
      udisks_daemon_log (daemon,
                         UDISKS_LOG_LEVEL_ERROR,
                         "Error removing mount point `%s': %m",
                         mount_point);
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error removing mount point `%s': %m",
                                             mount_point);
      goto out;
    }

  udisks_daemon_log (daemon,
                     UDISKS_LOG_LEVEL_INFO,
                     "Unmounted %s from %s on behalf of uid %d",
                     udisks_block_device_get_device (block),
                     mount_point,
                     caller_uid);

  udisks_block_device_complete_filesystem_unmount (block, invocation);

 out:
  g_free (error_message);
  g_free (escaped_mount_point);
  g_free (mount_point);
  g_object_unref (object);
  return TRUE;
}
