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
#include "udiskslinuxlun.h"
#include "udiskslinuxfilesystem.h"
#include "udiskspersistentstore.h"
#include "udiskslinuxprovider.h"

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
  GDBusObjectStub parent_instance;

  UDisksDaemon *daemon;
  UDisksMountMonitor *mount_monitor;

  GUdevDevice *device;

  /* interface */
  UDisksLinuxSysfsDevice *iface_linux_sysfs_device;
  UDisksBlockDevice *iface_block_device;
  UDisksFilesystem *iface_filesystem;
  UDisksSwapspace *iface_swapspace;
};

struct _UDisksLinuxBlockClass
{
  GDBusObjectStubClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

G_DEFINE_TYPE (UDisksLinuxBlock, udisks_linux_block, G_TYPE_DBUS_OBJECT_STUB);

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
  g_dbus_object_stub_set_object_path (G_DBUS_OBJECT_STUB (block), str->str);
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
          g_dbus_object_stub_remove_interface (G_DBUS_OBJECT_STUB (block), G_DBUS_INTERFACE_STUB (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      update_func (block, uevent_action, G_DBUS_INTERFACE (*interface_pointer));
      if (add)
        g_dbus_object_stub_add_interface (G_DBUS_OBJECT_STUB (block), G_DBUS_INTERFACE_STUB (*interface_pointer));
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
handle_set_label (UDisksBlockDevice      *block,
                  GDBusMethodInvocation  *invocation,
                  const gchar            *label,
                  const gchar* const     *options,
                  gpointer                user_data);

static void
block_device_connect (UDisksLinuxBlock *block)
{
  g_signal_connect (block->iface_block_device,
                    "handle-set-label",
                    UDISKS_BLOCK_DEVICE_HANDLE_SET_LABEL_CALLBACK (handle_set_label, gpointer),
                    NULL);
}

static gchar *
find_lun (GDBusObjectManagerServer *object_manager,
          GUdevDevice              *block_device)
{
  const gchar *block_device_sysfs_path;
  gchar *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  block_device_sysfs_path = g_udev_device_get_sysfs_path (block_device);

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectStub *object = G_DBUS_OBJECT_STUB (l->data);
      UDisksLinuxLun *lun;
      GList *lun_devices;
      GList *j;

      if (!UDISKS_IS_LINUX_LUN (object))
        continue;

      lun = UDISKS_LINUX_LUN (object);
      lun_devices = udisks_linux_lun_get_devices (lun);

      for (j = lun_devices; j != NULL; j = j->next)
        {
          GUdevDevice *lun_device = G_UDEV_DEVICE (j->data);
          const gchar *lun_sysfs_path;

          lun_sysfs_path = g_udev_device_get_sysfs_path (lun_device);
          if (g_str_has_prefix (block_device_sysfs_path, lun_sysfs_path))
            {
              ret = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
              g_list_foreach (lun_devices, (GFunc) g_object_unref, NULL);
              g_list_free (lun_devices);
              goto out;
            }
        }
      g_list_foreach (lun_devices, (GFunc) g_object_unref, NULL);
      g_list_free (lun_devices);
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static gchar *
find_block_device_by_sysfs_path (GDBusObjectManagerServer *object_manager,
                                 const gchar              *sysfs_path)
{
  gchar *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectStub *object = G_DBUS_OBJECT_STUB (l->data);
      UDisksLinuxBlock *block;

      if (!UDISKS_IS_LINUX_BLOCK (object))
        continue;

      block = UDISKS_LINUX_BLOCK (object);

      if (g_strcmp0 (sysfs_path, g_udev_device_get_sysfs_path (block->device)) == 0)
        {
          ret = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static gchar *
get_sysfs_attr (GUdevDevice *device,
                const gchar *attr)
{
  gchar *filename;
  gchar *value;
  filename = g_strconcat (g_udev_device_get_sysfs_path (device),
                          "/",
                          attr,
                          NULL);
  value = NULL;
  /* don't care about errors */
  g_file_get_contents (filename,
                       &value,
                       NULL,
                       NULL);
  g_free (filename);
  return value;
}

static void
block_device_update (UDisksLinuxBlock *block,
                     const gchar      *uevent_action,
                     GDBusInterface   *_iface)
{
  UDisksBlockDevice *iface = UDISKS_BLOCK_DEVICE (_iface);
  GUdevDeviceNumber dev;
  GDBusObjectManagerServer *object_manager;
  gchar *lun_object_path;
  gchar *s;
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
  udisks_block_device_set_size (iface, udisks_daemon_util_block_get_size (block->device));

  if (g_str_has_prefix (g_udev_device_get_name (block->device), "loop"))
    {
      gchar *filename;
      gchar *backing_file;
      GError *error;
      filename = g_strconcat (g_udev_device_get_sysfs_path (block->device),
                              "/loop/backing_file",
                              NULL);
      error = NULL;
      if (!g_file_get_contents (filename,
                               &backing_file,
                               NULL,
                               &error))
        {
          /* ENOENT is not unexpected */
          if (!(error->domain == G_FILE_ERROR && error->code == G_FILE_ERROR_NOENT))
            {
              udisks_daemon_log (block->daemon,
                                 UDISKS_LOG_LEVEL_WARNING,
                                 "Error loading %s: %s (%s, %d)",
                                 filename,
                                 error->message,
                                 g_quark_to_string (error->domain),
                                 error->code);
            }
          g_error_free (error);
          udisks_block_device_set_loop_backing_file (iface, "");
        }
      else
        {
          g_strstrip (backing_file);
          udisks_block_device_set_loop_backing_file (iface, backing_file);
          g_free (backing_file);
        }
      g_free (filename);
    }
  else
    {
      udisks_block_device_set_loop_backing_file (iface, "");
    }


  /* dm-crypt
   *
   * TODO: this might not be the best way to determine if the device-mapper device
   *       is a dm-crypt device.. but unfortunately device-mapper keeps all this stuff
   *       in user-space and wants you to use libdevmapper to obtain it...
   */
  udisks_block_device_set_crypto_backing_device (iface, "/");
  if (g_str_has_prefix (g_udev_device_get_name (block->device), "dm-"))
    {
      gchar *dm_uuid;
      dm_uuid = get_sysfs_attr (block->device, "dm/uuid");
      if (dm_uuid != NULL && g_str_has_prefix (dm_uuid, "CRYPT-LUKS1"))
        {
          gchar **slaves;
          slaves = udisks_daemon_util_resolve_links (g_udev_device_get_sysfs_path (block->device),
                                                     "slaves");
          if (g_strv_length (slaves) == 1)
            {
              gchar *slave_object_path;
              slave_object_path = find_block_device_by_sysfs_path (udisks_daemon_get_object_manager (block->daemon),
                                                                   slaves[0]);
              if (slave_object_path != NULL)
                {
                  udisks_block_device_set_crypto_backing_device (iface, slave_object_path);
                }
              g_free (slave_object_path);
            }
          g_strfreev (slaves);
        }
      g_free (dm_uuid);
    }

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
              /* multipath */
              preferred_device_file = symlinks[n];
              break;
            }
          else if (g_str_has_prefix (symlinks[n], "/dev/mapper/vg_"))
            {
              /* LVM2 */
              preferred_device_file = symlinks[n];
              break;
            }
        }
    }
  udisks_block_device_set_preferred_device (iface, preferred_device_file);

  /* Determine the lun this block device belongs to
   *
   * TODO: if this is slow we could have a cache or ensure that we
   * only do this once or something else
   */
  object_manager = udisks_daemon_get_object_manager (block->daemon);
  lun_object_path = find_lun (object_manager, block->device);
  if (lun_object_path != NULL)
    {
      udisks_block_device_set_lun (iface, lun_object_path);
      g_free (lun_object_path);
    }
  else
    {
      udisks_block_device_set_lun (iface, "/");
    }

  udisks_block_device_set_id_usage (iface, g_udev_device_get_property (block->device, "ID_FS_USAGE"));
  udisks_block_device_set_id_type (iface, g_udev_device_get_property (block->device, "ID_FS_TYPE"));
  s = udisks_decode_udev_string (g_udev_device_get_property (block->device, "ID_FS_VERSION"));
  udisks_block_device_set_id_version (iface, s);
  g_free (s);
  s = udisks_decode_udev_string (g_udev_device_get_property (block->device, "ID_FS_LABEL_ENC"));
  udisks_block_device_set_id_label (iface, s);
  g_free (s);
  s = udisks_decode_udev_string (g_udev_device_get_property (block->device, "ID_FS_UUID_ENC"));
  udisks_block_device_set_id_uuid (iface, s);
  g_free (s);

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
      udisks_block_device_set_part_entry_number (iface,
                                                 g_udev_device_get_property_as_int (block->device,
                                                                                    "UDISKS_PARTITION_NUMBER"));
      udisks_block_device_set_part_entry_type (iface,
                                               g_udev_device_get_property (block->device,
                                                                           "UDISKS_PARTITION_TYPE"));
      udisks_block_device_set_part_entry_flags (iface,
                                                g_udev_device_get_property (block->device,
                                                                            "UDISKS_PARTITION_FLAGS"));
      udisks_block_device_set_part_entry_label (iface,
                                                g_udev_device_get_property (block->device,
                                                                            "UDISKS_PARTITION_LABEL"));
      udisks_block_device_set_part_entry_uuid (iface,
                                               g_udev_device_get_property (block->device,
                                                                           "UDISKS_PARTITION_UUID"));
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
/* org.freedesktop.UDisks.Filesystem */

static gboolean
filesystem_check (UDisksLinuxBlock *block)
{
  gboolean ret;
  UDisksMountType mount_type;

  ret = FALSE;
  if (g_strcmp0 (udisks_block_device_get_id_usage (block->iface_block_device), "filesystem") == 0 ||
      (udisks_mount_monitor_is_dev_in_use (block->mount_monitor,
                                           g_udev_device_get_device_number (block->device),
                                           &mount_type) &&
       mount_type == UDISKS_MOUNT_TYPE_FILESYSTEM))
    ret = TRUE;

  return ret;
}

static void
filesystem_update (UDisksLinuxBlock  *block,
                   const gchar       *uevent_action,
                   GDBusInterface    *_iface)
{
  UDisksFilesystem *iface = UDISKS_FILESYSTEM (_iface);
  GPtrArray *p;
  GList *mounts;
  GList *l;

  p = g_ptr_array_new ();
  mounts = udisks_mount_monitor_get_mounts_for_dev (block->mount_monitor,
                                                    g_udev_device_get_device_number (block->device));
  /* we are guaranteed that the list is sorted so if there are
   * multiple mounts we'll always get the same order
   */
  for (l = mounts; l != NULL; l = l->next)
    {
      UDisksMount *mount = UDISKS_MOUNT (l->data);
      if (udisks_mount_get_mount_type (mount) == UDISKS_MOUNT_TYPE_FILESYSTEM)
        g_ptr_array_add (p, (gpointer) udisks_mount_get_mount_path (mount));
    }
  g_ptr_array_add (p, NULL);
  udisks_filesystem_set_mount_points (iface, (const gchar *const *) p->pdata);
  g_ptr_array_free (p, TRUE);
  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.Swapspace */

static void
swapspace_start_on_job_completed (UDisksJob   *job,
                                  gboolean     success,
                                  const gchar *message,
                                  gpointer     user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (user_data);
  UDisksSwapspace *swapspace;
  swapspace = UDISKS_SWAPSPACE (g_dbus_method_invocation_get_user_data (invocation));
  if (success)
    udisks_swapspace_complete_start (swapspace, invocation);
  else
    g_dbus_method_invocation_return_error (invocation,
                                           UDISKS_ERROR,
                                           UDISKS_ERROR_FAILED,
                                           "Error activating swap: %s",
                                           message);
}

static gboolean
swapspace_handle_start (UDisksSwapspace        *swapspace,
                        GDBusMethodInvocation  *invocation,
                        const gchar* const     *options,
                        gpointer                user_data)
{
  GDBusObject *object;
  UDisksDaemon *daemon;
  UDisksBlockDevice *block;
  gboolean auth_no_user_interaction;
  guint n;
  UDisksBaseJob *job;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (swapspace));
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));
  block = UDISKS_PEEK_BLOCK_DEVICE (object);

  auth_no_user_interaction = FALSE;
  for (n = 0; options != NULL && options[n] != NULL; n++)
    {
      const gchar *option = options[n];
      if (g_strcmp0 (option, "auth_no_user_interaction") == 0)
        {
          auth_no_user_interaction = TRUE;
          continue;
        }
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_OPTION_NOT_PERMITTED,
                                             "Option `%s' is not allowed",
                                             option);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.swap",
                                                    auth_no_user_interaction,
                                                    N_("Authentication is required to activate swapspace on $(udisks2.device)"),
                                                    invocation))
    goto out;

  job = udisks_daemon_launch_spawned_job (daemon,
                                          NULL, /* cancellable */
                                          NULL, /* input_string */
                                          "swapon %s",
                                          udisks_block_device_get_device (block));
  g_signal_connect (job,
                    "completed",
                    G_CALLBACK (swapspace_start_on_job_completed),
                    invocation);

 out:
  return TRUE;
}

static void
swapspace_stop_on_job_completed (UDisksJob   *job,
                                 gboolean     success,
                                 const gchar *message,
                                  gpointer     user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (user_data);
  UDisksSwapspace *swapspace;
  swapspace = UDISKS_SWAPSPACE (g_dbus_method_invocation_get_user_data (invocation));
  if (success)
    udisks_swapspace_complete_start (swapspace, invocation);
  else
    g_dbus_method_invocation_return_error (invocation,
                                           UDISKS_ERROR,
                                           UDISKS_ERROR_FAILED,
                                           "Error deactivating swap: %s",
                                           message);
}

static gboolean
swapspace_handle_stop (UDisksSwapspace        *swapspace,
                       GDBusMethodInvocation  *invocation,
                       const gchar* const     *options,
                       gpointer                user_data)
{
  GDBusObject *object;
  UDisksDaemon *daemon;
  UDisksBlockDevice *block;
  gboolean auth_no_user_interaction;
  guint n;
  UDisksBaseJob *job;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (swapspace));
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));
  block = UDISKS_PEEK_BLOCK_DEVICE (object);

  auth_no_user_interaction = FALSE;
  for (n = 0; options != NULL && options[n] != NULL; n++)
    {
      const gchar *option = options[n];
      if (g_strcmp0 (option, "auth_no_user_interaction") == 0)
        {
          auth_no_user_interaction = TRUE;
          continue;
        }
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_OPTION_NOT_PERMITTED,
                                             "Option `%s' is not allowed",
                                             option);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.swap",
                                                    auth_no_user_interaction,
                                                    N_("Authentication is required to deactivate swapspace on $(udisks2.device)"),
                                                    invocation))
    goto out;

  job = udisks_daemon_launch_spawned_job (daemon,
                                          NULL, /* cancellable */
                                          NULL, /* input_string */
                                          "swapoff %s",
                                          udisks_block_device_get_device (block));
  g_signal_connect (job,
                    "completed",
                    G_CALLBACK (swapspace_stop_on_job_completed),
                    invocation);

 out:
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
swapspace_check (UDisksLinuxBlock *block)
{
  gboolean ret;
  UDisksMountType mount_type;

  ret = FALSE;
  if ((g_strcmp0 (udisks_block_device_get_id_usage (block->iface_block_device), "other") == 0 &&
       g_strcmp0 (udisks_block_device_get_id_type (block->iface_block_device), "swap") == 0)
      || (udisks_mount_monitor_is_dev_in_use (block->mount_monitor,
                                              g_udev_device_get_device_number (block->device),
                                              &mount_type)
          && mount_type == UDISKS_MOUNT_TYPE_SWAP))
    ret = TRUE;

  return ret;
}

static void
swapspace_connect (UDisksLinuxBlock *block)
{
  /* indicate that we want to handle the method invocations in a thread */
  g_dbus_interface_stub_set_flags (G_DBUS_INTERFACE_STUB (block->iface_swapspace),
                                   G_DBUS_INTERFACE_STUB_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
  g_signal_connect (block->iface_swapspace,
                    "handle-start",
                    UDISKS_SWAPSPACE_HANDLE_START_CALLBACK (swapspace_handle_start, gpointer),
                    NULL);
  g_signal_connect (block->iface_swapspace,
                    "handle-stop",
                    UDISKS_SWAPSPACE_HANDLE_START_CALLBACK (swapspace_handle_stop, gpointer),
                    NULL);
}

static void
swapspace_update (UDisksLinuxBlock  *block,
                  const gchar       *uevent_action,
                  GDBusInterface    *_iface)
{
  UDisksSwapspace *iface = UDISKS_SWAPSPACE (_iface);
  UDisksMountType mount_type;
  gboolean active;

  active = FALSE;
  if (udisks_mount_monitor_is_dev_in_use (block->mount_monitor,
                                          g_udev_device_get_device_number (block->device),
                                          &mount_type)
      && mount_type == UDISKS_MOUNT_TYPE_SWAP)
    active = TRUE;
  udisks_swapspace_set_active (iface, active);
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
  update_iface (block, action, filesystem_check, NULL, filesystem_update,
                UDISKS_TYPE_LINUX_FILESYSTEM, &block->iface_filesystem);
  /* TODO: need to hook up Start() and Stop() methods */
  update_iface (block, action, swapspace_check, swapspace_connect, swapspace_update,
                UDISKS_TYPE_SWAPSPACE_STUB, &block->iface_swapspace);
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

static void
on_set_label_job_completed (UDisksJob   *job,
                            gboolean     success,
                            const gchar *message,
                            gpointer     user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (user_data);
  UDisksBlockDevice *block;

  block = UDISKS_BLOCK_DEVICE (g_dbus_method_invocation_get_user_data (invocation));

  if (success)
    udisks_block_device_complete_set_label (block, invocation);
  else
    g_dbus_method_invocation_return_error (invocation,
                                           UDISKS_ERROR,
                                           UDISKS_ERROR_FAILED,
                                           "Error setting label: %s",
                                           message);
}

/* runs in thread dedicated to handling method call */
static gboolean
handle_set_label (UDisksBlockDevice      *block,
                  GDBusMethodInvocation  *invocation,
                  const gchar            *label,
                  const gchar* const     *requested_options,
                  gpointer                user_data)
{
  UDisksFilesystem *filesystem;
  GDBusObject *object;
  UDisksDaemon *daemon;
  const gchar *probed_fs_usage;
  const gchar *probed_fs_type;
  gboolean auth_no_user_interaction;
  gboolean supports_online;
  guint n;
  UDisksBaseJob *job;
  gchar *escaped_label;

  object = NULL;
  daemon = NULL;
  auth_no_user_interaction = FALSE;
  supports_online = FALSE;
  escaped_label = NULL;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));
  filesystem = UDISKS_PEEK_FILESYSTEM (object);

  probed_fs_usage = udisks_block_device_get_id_usage (block);
  probed_fs_type = udisks_block_device_get_id_type (block);

  /* TODO: add support for other fstypes */
  if (!(g_strcmp0 (probed_fs_usage, "filesystem") == 0 &&
        (g_strcmp0 (probed_fs_type, "ext2") == 0 ||
         g_strcmp0 (probed_fs_type, "ext3") == 0 ||
         g_strcmp0 (probed_fs_type, "ext4") == 0)))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Don't know how to change label on device of type %s:%s",
                                             probed_fs_usage,
                                             probed_fs_type);
      goto out;
    }
  supports_online = TRUE;

  /* Fail if the device is already mounted and the tools/drivers doesn't
   * support changing the label in that case
   */
  if (filesystem != NULL && !supports_online)
    {
      const gchar * const *existing_mount_points;
      existing_mount_points = udisks_filesystem_get_mount_points (filesystem);
      if (existing_mount_points != NULL && g_strv_length ((gchar **) existing_mount_points) > 0)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_NOT_SUPPORTED,
                                                 "Cannot change label on mounted device of type %s:%s.\n",
                                                 probed_fs_usage,
                                                 probed_fs_type);
          goto out;
        }
    }

  for (n = 0; requested_options != NULL && requested_options[n] != NULL; n++)
    {
      const gchar *option = requested_options[n];
      if (g_strcmp0 (option, "auth_no_user_interaction") == 0)
        {
          auth_no_user_interaction = TRUE;
          continue;
        }

      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_OPTION_NOT_PERMITTED,
                                             "Option `%s' is not allowed",
                                             option);
      goto out;
    }

  /* Check that the user is actually authorized to change the
   * filesystem label.
   *
   * TODO: want nicer authentication message + special treatment for system-internal
   */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.modify",
                                                    auth_no_user_interaction,
                                                    N_("Authentication is required to change the label on $(udisks2.device)"),
                                                    invocation))
    goto out;

  escaped_label = g_shell_quote (label);
  job = udisks_daemon_launch_spawned_job (daemon,
                                          NULL, /* cancellable */
                                          NULL, /* input_string */
                                          "e2label %s %s",
                                          udisks_block_device_get_device (block),
                                          escaped_label);
  g_signal_connect (job,
                    "completed",
                    G_CALLBACK (on_set_label_job_completed),
                    invocation);

 out:
  g_free (escaped_label);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}
