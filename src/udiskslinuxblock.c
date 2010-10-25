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

#include "udisksdaemon.h"
#include "udiskslinuxblock.h"
#include "udisksfilesystemimpl.h"
#include "udisksmount.h"
#include "udisksmountmonitor.h"

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
  UDisksBlockDeviceProbed *iface_block_device_probed;
  UDisksFilesystem *iface_filesystem;
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
  if (block->iface_block_device_probed != NULL)
    g_object_unref (block->iface_block_device_probed);
  if (block->iface_filesystem != NULL)
    g_object_unref (block->iface_filesystem);

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
udisks_linux_block_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
  GObjectConstructParam *device_cp;
  GObjectConstructParam *object_path_cp;
  GUdevDevice *device;
  gchar *object_path;

  device_cp = find_construct_property (n_construct_properties, construct_properties, "device");
  object_path_cp = find_construct_property (n_construct_properties, construct_properties, "object-path");
  g_assert (device_cp != NULL && object_path_cp != NULL);

  device = G_UDEV_DEVICE (g_value_get_object (device_cp->value));
  g_assert (device != NULL);

  if (g_value_get_string (object_path_cp->value) == NULL)
    {
      object_path = util_compute_object_path (g_udev_device_get_sysfs_path (device));
      g_value_take_string (object_path_cp->value, object_path);
    }

  return G_OBJECT_CLASS (udisks_linux_block_parent_class)->constructor (type,
                                                                        n_construct_properties,
                                                                        construct_properties);
}

static void
udisks_linux_block_constructed (GObject *object)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (object);

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

  if (G_OBJECT_CLASS (udisks_linux_block_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_block_parent_class)->constructed (object);
}

static void
udisks_linux_block_class_init (UDisksLinuxBlockClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructor  = udisks_linux_block_constructor;
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
typedef void     (*UpdateInterfaceFunc) (UDisksLinuxBlock     *block,
                                         const gchar    *uevent_action,
                                         GDBusInterface *interface);

static void
update_iface (UDisksLinuxBlock           *block,
              const gchar          *uevent_action,
              HasInterfaceFunc      has_func,
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

static void
block_device_update (UDisksLinuxBlock      *block,
                     const gchar     *uevent_action,
                     GDBusInterface  *_iface)
{
  UDisksBlockDevice *iface = UDISKS_BLOCK_DEVICE (_iface);
  GUdevDeviceNumber dev;

  dev = g_udev_device_get_device_number (block->device);

  udisks_block_device_set_device (iface, g_udev_device_get_device_file (block->device));
  udisks_block_device_set_symlinks (iface, g_udev_device_get_device_file_symlinks (block->device));
  udisks_block_device_set_major (iface, major (dev));
  udisks_block_device_set_minor (iface, minor (dev));
  udisks_block_device_set_size (iface, g_udev_device_get_sysfs_attr_as_uint64 (block->device, "size") * 512);
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.BlockDeviceProbed */

/* Unescapes sequences like \x20 to " " and ensures the returned string is valid UTF-8.
 *
 * If the string is not valid UTF-8, return as much as possible and complain on stderr.
 *
 * See udev_util_encode_string() in libudev/libudev-util.c in the udev
 * tree for the encoder.
 */
static gchar *
decode_udev_encoded_string (const gchar *str)
{
  GString *s;
  gchar *ret;
  const gchar *end_valid;
  guint n;

  s = g_string_new (NULL);
  for (n = 0; str[n] != '\0'; n++)
    {
      if (str[n] == '\\')
        {
          gint val;

          if (str[n + 1] != 'x' || str[n + 2] == '\0' || str[n + 3] == '\0')
            {
              g_warning ("**** NOTE: malformed encoded string `%s'", str);
              break;
            }

          val = (g_ascii_xdigit_value (str[n + 2]) << 4) | g_ascii_xdigit_value (str[n + 3]);

          g_string_append_c (s, val);

          n += 3;
        }
      else
        {
          g_string_append_c (s, str[n]);
        }
    }

  if (!g_utf8_validate (s->str, -1, &end_valid))
    {
      g_warning ("The string `%s' is not valid UTF-8. Invalid characters begins at `%s'", s->str, end_valid);
      ret = g_strndup (s->str, end_valid - s->str);
      g_string_free (s, TRUE);
    }
  else
    {
      ret = g_string_free (s, FALSE);
    }

  return ret;
}

static gboolean
block_device_probed_check (UDisksLinuxBlock *block)
{
  return g_udev_device_has_property (block->device, "ID_FS_USAGE");
}

static void
block_device_probed_update (UDisksLinuxBlock      *block,
                            const gchar     *uevent_action,
                            GDBusInterface  *_iface)
{
  UDisksBlockDeviceProbed *iface = UDISKS_BLOCK_DEVICE_PROBED (_iface);
  gchar *s;

  udisks_block_device_probed_set_usage (iface, g_udev_device_get_property (block->device, "ID_FS_USAGE"));
  udisks_block_device_probed_set_type (iface, g_udev_device_get_property (block->device, "ID_FS_TYPE"));
  udisks_block_device_probed_set_version (iface, g_udev_device_get_property (block->device, "ID_FS_VERSION"));

  s = decode_udev_encoded_string (g_udev_device_get_property (block->device, "ID_FS_LABEL_ENC"));
  udisks_block_device_probed_set_label (iface, s);
  g_free (s);

  s = decode_udev_encoded_string (g_udev_device_get_property (block->device, "ID_FS_UUID_ENC"));
  udisks_block_device_probed_set_uuid (iface, s);
  g_free (s);
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
  return g_strcmp0 (g_udev_device_get_property (block->device, "ID_FS_USAGE"), "filesystem") == 0;
}

static void
filesystem_update (UDisksLinuxBlock      *block,
                   const gchar     *uevent_action,
                   GDBusInterface  *_iface)
{
  UDisksFilesystem *iface = UDISKS_FILESYSTEM (_iface);
  GList *mounts;
  GList *l;
  GPtrArray *p;

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
  udisks_filesystem_set_mount_points (iface, (const gchar *const *) p->pdata);
  g_ptr_array_free (p, TRUE);

  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);
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

  update_iface (block, action, linux_sysfs_device_check, linux_sysfs_device_update,
                UDISKS_TYPE_LINUX_SYSFS_DEVICE_STUB, &block->iface_linux_sysfs_device);
  update_iface (block, action, block_device_check, block_device_update,
                UDISKS_TYPE_BLOCK_DEVICE_STUB, &block->iface_block_device);
  update_iface (block, action, block_device_probed_check, block_device_probed_update,
                UDISKS_TYPE_BLOCK_DEVICE_PROBED_STUB, &block->iface_block_device_probed);
  update_iface (block, action, filesystem_check, filesystem_update,
                UDISKS_TYPE_FILESYSTEM_IMPL, &block->iface_filesystem);
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
