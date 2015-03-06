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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <mntent.h>

#include <sys/ioctl.h>
#include <linux/fs.h>

#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>

#include "storagedlogging.h"
#include "storageddaemon.h"
#include "storageddaemonutil.h"
#include "storagedlinuxblockobject.h"
#include "storagedlinuxblock.h"
#include "storagedmount.h"
#include "storagedmountmonitor.h"
#include "storagedlinuxdriveobject.h"
#include "storagedlinuxdrive.h"
#include "storagedlinuxpartitiontable.h"
#include "storagedlinuxpartition.h"
#include "storagedlinuxfilesystem.h"
#include "storagedlinuxencrypted.h"
#include "storagedlinuxswapspace.h"
#include "storagedlinuxloop.h"
#include "storagedlinuxprovider.h"
#include "storagedfstabmonitor.h"
#include "storagedfstabentry.h"
#include "storagedcrypttabmonitor.h"
#include "storagedcrypttabentry.h"
#include "storagedlinuxdevice.h"
#include "storagedmodulemanager.h"

#include <modules/storagedmoduleifacetypes.h>

/**
 * SECTION:storagedlinuxblockobject
 * @title: StoragedLinuxBlockObject
 * @short_description: Object representing a block device on Linux.
 *
 * Object corresponding to a block device on Linux.
 */

typedef struct _StoragedLinuxBlockObjectClass   StoragedLinuxBlockObjectClass;

/**
 * StoragedLinuxBlockObject:
 *
 * The #StoragedLinuxBlockObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _StoragedLinuxBlockObject
{
  StoragedObjectSkeleton parent_instance;

  StoragedDaemon *daemon;
  StoragedMountMonitor *mount_monitor;

  StoragedLinuxDevice *device;

  /* interface */
  StoragedBlock *iface_block_device;
  StoragedPartition *iface_partition;
  StoragedPartitionTable *iface_partition_table;
  StoragedFilesystem *iface_filesystem;
  StoragedSwapspace *iface_swapspace;
  StoragedEncrypted *iface_encrypted;
  StoragedLoop *iface_loop;
  GHashTable *module_ifaces;
};

struct _StoragedLinuxBlockObjectClass
{
  StoragedObjectSkeletonClass parent_class;
};

typedef struct
{
  StoragedObject *interface;
  StoragedObjectHasInterfaceFunc has_func;
  StoragedObjectConnectInterfaceFunc connect_func;
  StoragedObjectUpdateInterfaceFunc update_func;
} ModuleInterfaceEntry;

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

G_DEFINE_TYPE (StoragedLinuxBlockObject, storaged_linux_block_object, STORAGED_TYPE_OBJECT_SKELETON);

static void on_mount_monitor_mount_added   (StoragedMountMonitor  *monitor,
                                            StoragedMount         *mount,
                                            gpointer               user_data);
static void on_mount_monitor_mount_removed (StoragedMountMonitor  *monitor,
                                            StoragedMount         *mount,
                                            gpointer               user_data);

static void
storaged_linux_block_object_finalize (GObject *_object)
{
  StoragedLinuxBlockObject *object = STORAGED_LINUX_BLOCK_OBJECT (_object);

  /* note: we don't hold a ref to block->daemon or block->mount_monitor */
  g_signal_handlers_disconnect_by_func (object->mount_monitor, on_mount_monitor_mount_added, object);
  g_signal_handlers_disconnect_by_func (object->mount_monitor, on_mount_monitor_mount_removed, object);

  g_object_unref (object->device);

  if (object->iface_block_device != NULL)
    g_object_unref (object->iface_block_device);
  if (object->iface_partition != NULL)
    g_object_unref (object->iface_partition);
  if (object->iface_partition_table != NULL)
    g_object_unref (object->iface_partition_table);
  if (object->iface_filesystem != NULL)
    g_object_unref (object->iface_filesystem);
  if (object->iface_swapspace != NULL)
    g_object_unref (object->iface_swapspace);
  if (object->iface_encrypted != NULL)
    g_object_unref (object->iface_encrypted);
  if (object->iface_loop != NULL)
    g_object_unref (object->iface_loop);
  if (object->module_ifaces != NULL)
    g_hash_table_destroy (object->module_ifaces);

  if (G_OBJECT_CLASS (storaged_linux_block_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_block_object_parent_class)->finalize (_object);
}

static void
storaged_linux_block_object_get_property (GObject    *__object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  StoragedLinuxBlockObject *object = STORAGED_LINUX_BLOCK_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_block_object_get_daemon (object));
      break;

    case PROP_DEVICE:
      g_value_set_object (value, storaged_linux_block_object_get_device (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_block_object_set_property (GObject      *__object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  StoragedLinuxBlockObject *object = STORAGED_LINUX_BLOCK_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (object->daemon == NULL);
      /* we don't take a reference to the daemon */
      object->daemon = g_value_get_object (value);
      break;

    case PROP_DEVICE:
      g_assert (object->device == NULL);
      object->device = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
storaged_linux_block_object_init (StoragedLinuxBlockObject *object)
{
}

static void
storaged_linux_block_object_constructed (GObject *_object)
{
  StoragedLinuxBlockObject *object = STORAGED_LINUX_BLOCK_OBJECT (_object);
  GString *str;

  object->mount_monitor = storaged_daemon_get_mount_monitor (object->daemon);
  g_signal_connect (object->mount_monitor,
                    "mount-added",
                    G_CALLBACK (on_mount_monitor_mount_added),
                    object);
  g_signal_connect (object->mount_monitor,
                    "mount-removed",
                    G_CALLBACK (on_mount_monitor_mount_removed),
                    object);

  /* initial coldplug */
  storaged_linux_block_object_uevent (object, "add", NULL);

  /* compute the object path */
  str = g_string_new ("/org/storaged/Storaged/block_devices/");
  storaged_safe_append_to_object_path (str, g_udev_device_get_name (object->device->udev_device));
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), str->str);
  g_string_free (str, TRUE);

  if (G_OBJECT_CLASS (storaged_linux_block_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (storaged_linux_block_object_parent_class)->constructed (_object);
}

static void
storaged_linux_block_object_class_init (StoragedLinuxBlockObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_linux_block_object_finalize;
  gobject_class->constructed  = storaged_linux_block_object_constructed;
  gobject_class->set_property = storaged_linux_block_object_set_property;
  gobject_class->get_property = storaged_linux_block_object_get_property;

  /**
   * StoragedLinuxBlockObject:daemon:
   *
   * The #StoragedDaemon the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the object is for",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedLinuxBlockObject:device:
   *
   * The #StoragedLinuxDevice for the object. Connect to the #GObject::notify
   * signal to get notified whenever this is updated.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DEVICE,
                                   g_param_spec_object ("device",
                                                        "Device",
                                                        "The device for the object",
                                                        STORAGED_TYPE_LINUX_DEVICE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

}

/**
 * storaged_linux_block_object_new:
 * @daemon: A #StoragedDaemon.
 * @device: The #StoragedLinuxDevice for the device.
 *
 * Create a new block object.
 *
 * Returns: A #StoragedLinuxBlockObject object. Free with g_object_unref().
 */
StoragedLinuxBlockObject *
storaged_linux_block_object_new (StoragedDaemon      *daemon,
                                 StoragedLinuxDevice *device)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_LINUX_BLOCK_OBJECT (g_object_new (STORAGED_TYPE_LINUX_BLOCK_OBJECT,
                                                  "daemon", daemon,
                                                  "device", device,
                                                  NULL));
}

/**
 * storaged_linux_block_object_get_daemon:
 * @object: A #StoragedLinuxBlockObject.
 *
 * Gets the daemon used by @object.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @object.
 */
StoragedDaemon *
storaged_linux_block_object_get_daemon (StoragedLinuxBlockObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_BLOCK_OBJECT (object), NULL);
  return object->daemon;
}

/**
 * storaged_linux_block_object_get_device:
 * @object: A #StoragedLinuxBlockObject.
 *
 * Gets the current #StoragedLinuxDevice for @object. Connect to
 * #GObject::notify to track changes to the #StoragedLinuxBlockObject:device
 * property.
 *
 * Returns: A #StoragedLinuxDevice. Free with g_object_unref().
 */
StoragedLinuxDevice *
storaged_linux_block_object_get_device (StoragedLinuxBlockObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_BLOCK_OBJECT (object), NULL);
  return g_object_ref (object->device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_iface (StoragedObject                     *object,
              const gchar                        *uevent_action,
              StoragedObjectHasInterfaceFunc      has_func,
              StoragedObjectConnectInterfaceFunc  connect_func,
              StoragedObjectUpdateInterfaceFunc   update_func,
              GType                               skeleton_type,
              gpointer                            _interface_pointer)
{
  gboolean has;
  gboolean add;
  GDBusInterface **interface_pointer = _interface_pointer;

  g_return_if_fail (object != NULL);
  g_return_if_fail (has_func != NULL);
  g_return_if_fail (update_func != NULL);
  g_return_if_fail (g_type_is_a (skeleton_type, G_TYPE_OBJECT));
  g_return_if_fail (g_type_is_a (skeleton_type, G_TYPE_DBUS_INTERFACE));
  g_return_if_fail (interface_pointer != NULL);
  g_return_if_fail (*interface_pointer == NULL || G_IS_DBUS_INTERFACE (*interface_pointer));

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
          g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (object),
                                                   G_DBUS_INTERFACE_SKELETON (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      update_func (object, uevent_action, G_DBUS_INTERFACE (*interface_pointer));
      if (add)
        g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                              G_DBUS_INTERFACE_SKELETON (*interface_pointer));
    }
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.Storaged.Block */

static gboolean
block_device_check (StoragedObject *object)
{
  return TRUE;
}

static void
block_device_connect (StoragedObject *object)
{
}

static gboolean
block_device_update (StoragedObject   *object,
                     const gchar    *uevent_action,
                     GDBusInterface *_iface)
{
  storaged_linux_block_update (STORAGED_LINUX_BLOCK (_iface), STORAGED_LINUX_BLOCK_OBJECT (object));
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
disk_is_partitioned_by_kernel (GUdevDevice *device)
{
  gboolean ret = FALSE;
  GDir *dir = NULL;
  const gchar *name;
  const gchar *device_name;

  g_return_val_if_fail (g_strcmp0 (g_udev_device_get_devtype (device), "disk") == 0, FALSE);

  dir = g_dir_open (g_udev_device_get_sysfs_path (device), 0 /* flags */, NULL /* GError */);
  if (dir == NULL)
    goto out;

  device_name = g_udev_device_get_name (device);
  while ((name = g_dir_read_name (dir)) != NULL)
    {
      /* TODO: could check that it's a block device - for now, just
       * checking the name suffices
       */
      if (g_str_has_prefix (name, device_name))
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  if (dir != NULL)
    g_dir_close (dir);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.Storaged.PartitionTable */

static gboolean
partition_table_check (StoragedObject *object)
{
  StoragedLinuxBlockObject *block_object = STORAGED_LINUX_BLOCK_OBJECT (object);
  gboolean ret = FALSE;

  /* only consider whole disks, never partitions */
  if (g_strcmp0 (g_udev_device_get_devtype (block_object->device->udev_device), "disk") != 0)
    goto out;

  /* if blkid(8) already identified the device as a partition table, it's all good */
  if (g_udev_device_has_property (block_object->device->udev_device, "ID_PART_TABLE_TYPE"))
    {
      /* however, if blkid(8) also think that we're a filesystem... then don't
       * mark us as a partition table ... except if we are partitioned by the
       * kernel
       *
       * (see filesystem_check() for the similar case where we don't pretend
       * to be a filesystem)
       */
      if (g_strcmp0 (g_udev_device_get_property (block_object->device->udev_device, "ID_FS_USAGE"), "filesystem") == 0)
        {
          if (!disk_is_partitioned_by_kernel (block_object->device->udev_device))
            {
              goto out;
            }
        }
      ret = TRUE;
      goto out;
    }

  /* Note that blkid(8) might not detect all partition table
   * formats that the kernel knows about.... so we need to
   * double check...
   *
   * Fortunately, note that the kernel guarantees that all children
   * block devices that are partitions are created before the uevent
   * for the parent block device.... so if the parent block device has
   * children... then it must be partitioned by the kernel, hence it
   * must contain a partition table.
   */
  if (disk_is_partitioned_by_kernel (block_object->device->udev_device))
    {
      ret = TRUE;
      goto out;
    }

 out:
  return ret;
}

static void
partition_table_connect (StoragedObject *object)
{
}

static gboolean
partition_table_update (StoragedObject   *object,
                        const gchar      *uevent_action,
                        GDBusInterface   *_iface)
{
  storaged_linux_partition_table_update (STORAGED_LINUX_PARTITION_TABLE (_iface), STORAGED_LINUX_BLOCK_OBJECT (object));
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.Storaged.Partition */

static gboolean
partition_check (StoragedObject *object)
{
  StoragedLinuxBlockObject *block_object = STORAGED_LINUX_BLOCK_OBJECT (object);
  gboolean ret = FALSE;

  /* could be partitioned by the kernel */
  if (g_strcmp0 (g_udev_device_get_devtype (block_object->device->udev_device), "partition") == 0)
    {
      ret = TRUE;
      goto out;
    }

  /* if blkid(8) already identified the device as a partition, it's all good */
  if (g_udev_device_has_property (block_object->device->udev_device, "ID_PART_ENTRY_SCHEME"))
    {
      ret = TRUE;
      goto out;
    }

 out:
  return ret;
}

static void
partition_connect (StoragedObject *object)
{
}

static gboolean
partition_update (StoragedObject   *object,
                  const gchar      *uevent_action,
                  GDBusInterface   *_iface)
{
  storaged_linux_partition_update (STORAGED_LINUX_PARTITION (_iface), STORAGED_LINUX_BLOCK_OBJECT (object));
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.Storaged.Filesystem */

static gboolean
drive_does_not_detect_media_change (StoragedLinuxBlockObject *object)
{
  gboolean ret = FALSE;
  StoragedObject *drive_object;

  drive_object = storaged_daemon_find_object (object->daemon, storaged_block_get_drive (object->iface_block_device));
  if (drive_object != NULL)
    {
      StoragedDrive *drive = storaged_object_get_drive (drive_object);
      if (drive != NULL)
        {
          ret = ! storaged_drive_get_media_change_detected (drive);
          g_object_unref (drive);
        }
      g_object_unref (drive_object);
    }
  return ret;
}

static gboolean
filesystem_check (StoragedObject *object)
{
  StoragedLinuxBlockObject *block_object = STORAGED_LINUX_BLOCK_OBJECT (object);
  gboolean ret = FALSE;
  gboolean detected_as_filesystem = FALSE;
  StoragedMountType mount_type;

  /* if blkid(8) has detected the device as a filesystem, trust that */
  if (g_strcmp0 (storaged_block_get_id_usage (block_object->iface_block_device), "filesystem") == 0)
    {
      detected_as_filesystem = TRUE;
      /* except, if we are a whole-disk device and the kernel has already partitioned us...
       * in that case, don't pretend we're a filesystem
       *
       * (see partition_table_check() above for the similar case where we don't pretend
       * to be a partition table)
       */
      if (g_strcmp0 (g_udev_device_get_devtype (block_object->device->udev_device), "disk") == 0 &&
          disk_is_partitioned_by_kernel (block_object->device->udev_device))
        {
          detected_as_filesystem = FALSE;
        }
    }

  if (drive_does_not_detect_media_change (block_object) ||
      detected_as_filesystem ||
      (storaged_mount_monitor_is_dev_in_use (block_object->mount_monitor,
                                           g_udev_device_get_device_number (block_object->device->udev_device),
                                           &mount_type) &&
       mount_type == STORAGED_MOUNT_TYPE_FILESYSTEM))
    ret = TRUE;

  return ret;
}


static void
filesystem_connect (StoragedObject *object)
{
}

static gboolean
filesystem_update (StoragedObject   *object,
                   const gchar      *uevent_action,
                   GDBusInterface   *_iface)
{
  storaged_linux_filesystem_update (STORAGED_LINUX_FILESYSTEM (_iface), STORAGED_LINUX_BLOCK_OBJECT (object));
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.Storaged.Swapspace */

static gboolean
swapspace_check (StoragedObject *object)
{
  StoragedLinuxBlockObject *block_object = STORAGED_LINUX_BLOCK_OBJECT (object);
  gboolean ret;
  StoragedMountType mount_type;

  ret = FALSE;
  if ((g_strcmp0 (storaged_block_get_id_usage (block_object->iface_block_device), "other") == 0 &&
       g_strcmp0 (storaged_block_get_id_type (block_object->iface_block_device), "swap") == 0)
      || (storaged_mount_monitor_is_dev_in_use (block_object->mount_monitor,
                                              g_udev_device_get_device_number (block_object->device->udev_device),
                                              &mount_type)
          && mount_type == STORAGED_MOUNT_TYPE_SWAP))
    ret = TRUE;

  return ret;
}

static void
swapspace_connect (StoragedObject *object)
{
}

static gboolean
swapspace_update (StoragedObject   *object,
                  const gchar    *uevent_action,
                  GDBusInterface *_iface)
{
  storaged_linux_swapspace_update (STORAGED_LINUX_SWAPSPACE (_iface), STORAGED_LINUX_BLOCK_OBJECT (object));
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
encrypted_check (StoragedObject *object)
{
  StoragedLinuxBlockObject *block_object = STORAGED_LINUX_BLOCK_OBJECT (object);
  gboolean ret;

  ret = FALSE;
  if (g_strcmp0 (storaged_block_get_id_usage (block_object->iface_block_device), "crypto") == 0 &&
      g_strcmp0 (storaged_block_get_id_type (block_object->iface_block_device), "crypto_LUKS") == 0)
    ret = TRUE;

  return ret;
}

static void
encrypted_connect (StoragedObject *object)
{
}

static gboolean
encrypted_update (StoragedObject   *object,
                  const gchar      *uevent_action,
                  GDBusInterface   *_iface)
{
  storaged_linux_encrypted_update (STORAGED_LINUX_ENCRYPTED (_iface), STORAGED_LINUX_BLOCK_OBJECT (object));
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
loop_check (StoragedObject *object)
{
  StoragedLinuxBlockObject *block_object = STORAGED_LINUX_BLOCK_OBJECT (object);
  gboolean ret;

  ret = FALSE;
  if (g_str_has_prefix (g_udev_device_get_name (block_object->device->udev_device), "loop") &&
      g_strcmp0 (g_udev_device_get_devtype (block_object->device->udev_device), "disk") == 0)
    ret = TRUE;

  return ret;
}

static void
loop_connect (StoragedObject *object)
{
}

static gboolean
loop_update (StoragedObject   *object,
             const gchar      *uevent_action,
             GDBusInterface   *_iface)
{
  storaged_linux_loop_update (STORAGED_LINUX_LOOP (_iface), STORAGED_LINUX_BLOCK_OBJECT (object));
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
free_module_interface_entry (ModuleInterfaceEntry *entry)
{
  if (entry->interface != NULL)
    g_object_unref (entry->interface);
  g_free (entry);
}

static void
ensure_module_ifaces (StoragedLinuxBlockObject *object,
                      StoragedModuleManager    *module_manager)
{
  GList *l;
  ModuleInterfaceEntry *entry;
  StoragedModuleInterfaceInfo *ii;

  /* Assume all modules are either unloaded or loaded at the same time, so don't regenerate entries */
  if (object->module_ifaces == NULL)
    {
      object->module_ifaces = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) free_module_interface_entry);

      l = storaged_module_manager_get_block_object_iface_infos (module_manager);
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
 * storaged_linux_block_object_uevent:
 * @object: A #StoragedLinuxBlockObject.
 * @action: Uevent action or %NULL
 * @device: A new #StoragedLinuxDevice device object or %NULL if the device hasn't changed.
 *
 * Updates all information on interfaces on @object.
 */
void
storaged_linux_block_object_uevent (StoragedLinuxBlockObject *object,
                                    const gchar              *action,
                                    StoragedLinuxDevice      *device)
{
  StoragedModuleManager *module_manager;
  GHashTableIter iter;
  gpointer key;
  ModuleInterfaceEntry *entry;

  g_return_if_fail (STORAGED_IS_LINUX_BLOCK_OBJECT (object));
  g_return_if_fail (device == NULL || STORAGED_IS_LINUX_DEVICE (device));

  if (device != NULL)
    {
      g_object_unref (object->device);
      object->device = g_object_ref (device);
      g_object_notify (G_OBJECT (object), "device");
    }

  update_iface (STORAGED_OBJECT (object), action, block_device_check, block_device_connect, block_device_update,
                STORAGED_TYPE_LINUX_BLOCK, &object->iface_block_device);
  update_iface (STORAGED_OBJECT (object), action, filesystem_check, filesystem_connect, filesystem_update,
                STORAGED_TYPE_LINUX_FILESYSTEM, &object->iface_filesystem);
  update_iface (STORAGED_OBJECT (object), action, swapspace_check, swapspace_connect, swapspace_update,
                STORAGED_TYPE_LINUX_SWAPSPACE, &object->iface_swapspace);
  update_iface (STORAGED_OBJECT (object), action, encrypted_check, encrypted_connect, encrypted_update,
                STORAGED_TYPE_LINUX_ENCRYPTED, &object->iface_encrypted);
  update_iface (STORAGED_OBJECT (object), action, loop_check, loop_connect, loop_update,
                STORAGED_TYPE_LINUX_LOOP, &object->iface_loop);
  update_iface (STORAGED_OBJECT (object), action, partition_table_check, partition_table_connect, partition_table_update,
                STORAGED_TYPE_LINUX_PARTITION_TABLE, &object->iface_partition_table);
  update_iface (STORAGED_OBJECT (object), action, partition_check, partition_connect, partition_update,
                STORAGED_TYPE_LINUX_PARTITION, &object->iface_partition);

  /* Attach interfaces from modules */
  module_manager = storaged_daemon_get_module_manager (object->daemon);
  if (storaged_module_manager_get_modules_available (module_manager))
    {
      ensure_module_ifaces (object, module_manager);
      g_hash_table_iter_init (&iter, object->module_ifaces);
      while (g_hash_table_iter_next (&iter, &key, (gpointer *) &entry))
        {
          update_iface (STORAGED_OBJECT (object), action, entry->has_func, entry->connect_func, entry->update_func,
                        (GType) key, &entry->interface);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_mount_monitor_mount_added (StoragedMountMonitor  *monitor,
                              StoragedMount         *mount,
                              gpointer               user_data)
{
  StoragedLinuxBlockObject *object = STORAGED_LINUX_BLOCK_OBJECT (user_data);
  if (storaged_mount_get_dev (mount) == g_udev_device_get_device_number (object->device->udev_device))
    storaged_linux_block_object_uevent (object, NULL, NULL);
}

static void
on_mount_monitor_mount_removed (StoragedMountMonitor  *monitor,
                                StoragedMount         *mount,
                                gpointer               user_data)
{
  StoragedLinuxBlockObject *object = STORAGED_LINUX_BLOCK_OBJECT (user_data);
  if (storaged_mount_get_dev (mount) == g_udev_device_get_device_number (object->device->udev_device))
    storaged_linux_block_object_uevent (object, NULL, NULL);
}

/* ---------------------------------------------------------------------------------------------------- */


/**
 * storaged_linux_block_object_trigger_uevent:
 * @object: A #StoragedLinuxBlockObject.
 *
 * Triggers a 'change' uevent in the kernel.
 *
 * The triggered event will bubble up from the kernel through the udev
 * stack and will eventually be received by the storaged daemon process
 * itself. This method does not wait for the event to be received.
 */
void
storaged_linux_block_object_trigger_uevent (StoragedLinuxBlockObject *object)
{
  gchar* path = NULL;
  gint fd = -1;

  g_return_if_fail (STORAGED_IS_LINUX_BLOCK_OBJECT (object));

  /* TODO: would be nice with a variant to wait until the request uevent has been received by ourselves */

  path = g_strconcat (g_udev_device_get_sysfs_path (object->device->udev_device), "/uevent", NULL);
  fd = open (path, O_WRONLY);
  if (fd < 0)
    {
      storaged_warning ("Error opening %s: %m", path);
      goto out;
    }

  if (write (fd, "change", sizeof "change" - 1) != sizeof "change" - 1)
    {
      storaged_warning ("Error writing 'change' to file %s: %m", path);
      goto out;
    }

 out:
  if (fd >= 0)
    close (fd);
  g_free (path);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_block_object_reread_partition_table:
 * @object: A #StoragedLinuxBlockObject.
 *
 * Requests the kernel to re-read the partition table for @object.
 *
 * The events from any change this may cause will bubble up from the
 * kernel through the udev stack and will eventually be received by
 * the storaged daemon process itself. This method does not wait for the
 * event to be received.
 */
void
storaged_linux_block_object_reread_partition_table (StoragedLinuxBlockObject *object)
{
  const gchar *device_file;
  gint fd;

  g_return_if_fail (STORAGED_IS_LINUX_BLOCK_OBJECT (object));

  device_file = g_udev_device_get_device_file (object->device->udev_device);
  fd = open (device_file, O_RDONLY);
  if (fd == -1)
    {
      storaged_warning ("Error opening %s: %m", device_file);
    }
  else
    {
      if (ioctl (fd, BLKRRPART) != 0)
        {
          storaged_warning ("Error issuing BLKRRPART to %s: %m", device_file);
        }
      close (fd);
    }
}
