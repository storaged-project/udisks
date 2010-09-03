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

#include "linuxblock.h"

#include <gudev/gudev.h>

typedef struct
{
  GDBusObjectManager *object_manager;
  GUdevClient *gudev_client;

  /* maps from sysfs path to LinuxBlock instance */
  GHashTable *sysfs_to_block;
} LinuxBlockProvider;

typedef struct
{
  LinuxBlockProvider *provider; /* weak ref */
  GDBusObject *object;
  GUdevDevice *device;

  /* interfaces that are definitely implemented */
  UDisksLinuxSysfsDevice *iface_linux_sysfs_device;
  UDisksBlockDevice *iface_block_device;

  /* interfaces that may or may not be implemented */
  UDisksBlockDeviceProbed *iface_block_device_probed;
  UDisksFilesystem *iface_filesystem;
} LinuxBlock;

static void
linux_block_free (LinuxBlock *block)
{
  g_dbus_object_manager_unexport (block->provider->object_manager,
                                  g_dbus_object_get_object_path (block->object));
  g_object_unref (block->object);
  g_object_unref (block->device);
  if (block->iface_linux_sysfs_device != NULL)
    g_object_unref (block->iface_linux_sysfs_device);
  if (block->iface_block_device != NULL)
    g_object_unref (block->iface_block_device);
  if (block->iface_block_device_probed != NULL)
    g_object_unref (block->iface_block_device_probed);
  if (block->iface_filesystem != NULL)
    g_object_unref (block->iface_filesystem);
  g_free (block);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef gboolean (*HasInterfaceFunc)    (LinuxBlock     *block);
typedef void     (*UpdateInterfaceFunc) (LinuxBlock     *block,
                                         GDBusInterface *interface);

static void
update_iface (LinuxBlock           *block,
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
          g_dbus_object_remove_interface (block->object, G_DBUS_INTERFACE (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      update_func (block, G_DBUS_INTERFACE (*interface_pointer));
      if (add)
        g_dbus_object_add_interface (block->object, G_DBUS_INTERFACE (*interface_pointer));
    }
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.BlockDevice */

static gboolean
block_device_check (LinuxBlock *block)
{
  return TRUE;
}

static void
block_device_update (LinuxBlock      *block,
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

static gboolean
block_device_probed_check (LinuxBlock *block)
{
  return g_udev_device_has_property (block->device, "ID_FS_USAGE");
}

static void
block_device_probed_update (LinuxBlock      *block,
                            GDBusInterface  *_iface)
{
  UDisksBlockDeviceProbed *iface = UDISKS_BLOCK_DEVICE_PROBED (_iface);

  udisks_block_device_probed_set_usage (iface, g_udev_device_get_property (block->device, "ID_FS_USAGE"));
  udisks_block_device_probed_set_kind (iface, g_udev_device_get_property (block->device, "ID_FS_TYPE"));
  udisks_block_device_probed_set_version (iface, g_udev_device_get_property (block->device, "ID_FS_VERSION"));
  udisks_block_device_probed_set_label (iface, g_udev_device_get_property (block->device, "ID_FS_LABEL_ENC"));
  udisks_block_device_probed_set_uuid (iface, g_udev_device_get_property (block->device, "ID_FS_UUID_ENC"));
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.LinuxSysfsDevice */

static gboolean
linux_sysfs_device_check (LinuxBlock *block)
{
  return TRUE;
}

static void
linux_sysfs_device_update (LinuxBlock      *block,
                           GDBusInterface  *_iface)
{
  UDisksLinuxSysfsDevice *iface = UDISKS_LINUX_SYSFS_DEVICE (_iface);

  udisks_linux_sysfs_device_set_subsystem (iface, "block");
  udisks_linux_sysfs_device_set_sysfs_path (iface, g_udev_device_get_sysfs_path (block->device));
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.Filesystem */

static gboolean
filesystem_check (LinuxBlock *block)
{
  return g_strcmp0 (g_udev_device_get_property (block->device, "ID_FS_USAGE"), "filesystem") == 0;
}

static void
filesystem_update (LinuxBlock      *block,
                   GDBusInterface  *_iface)
{
  //UDisksFilesystem *iface = UDISKS_FILESYSTEM (_iface);
  /* TODO: use a derived class that implements the Mount() and Unmount() methods */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
linux_block_update (LinuxBlock  *block,
                    const gchar *uevent_action)
{
  update_iface (block, linux_sysfs_device_check, linux_sysfs_device_update,
                UDISKS_TYPE_LINUX_SYSFS_DEVICE_STUB, &block->iface_linux_sysfs_device);
  update_iface (block, block_device_check, block_device_update,
                UDISKS_TYPE_BLOCK_DEVICE_STUB, &block->iface_block_device);
  update_iface (block, block_device_probed_check, block_device_probed_update,
                UDISKS_TYPE_BLOCK_DEVICE_PROBED_STUB, &block->iface_block_device_probed);
  update_iface (block, filesystem_check, filesystem_update,
                UDISKS_TYPE_FILESYSTEM_STUB, &block->iface_filesystem);
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

static void
handle_uevent (LinuxBlockProvider *provider,
               const gchar        *action,
               GUdevDevice        *device)
{
  const gchar *sysfs_path;
  LinuxBlock *block;

  g_print ("%s:%s: %s %s\n", G_STRLOC, G_STRFUNC,
           action, g_udev_device_get_sysfs_path (device));

  sysfs_path = g_udev_device_get_sysfs_path (device);
  if (g_strcmp0 (action, "remove") == 0)
    {
      block = g_hash_table_lookup (provider->sysfs_to_block, sysfs_path);
      if (block != NULL)
        {
          g_print ("removing object with object path `%s'\n", g_dbus_object_get_object_path (block->object));
          g_warn_if_fail (g_hash_table_remove (provider->sysfs_to_block, sysfs_path));
        }
    }
  else
    {
      block = g_hash_table_lookup (provider->sysfs_to_block, sysfs_path);
      if (block != NULL)
        {
          g_object_unref (block->device);
          block->device = g_object_ref (device);
          linux_block_update (block, action);
        }
      else
        {
          gchar *object_path;

          object_path = util_compute_object_path (sysfs_path);

          block = g_new0 (LinuxBlock, 1);
          block->provider = provider;
          block->object = g_dbus_object_new (object_path);
          block->device = g_object_ref (device);

          linux_block_update (block, action);

          g_dbus_object_manager_export (provider->object_manager, block->object);
          g_hash_table_insert (provider->sysfs_to_block, g_strdup (sysfs_path), block);
          g_free (object_path);
        }
    }
}

static void
on_uevent (GUdevClient  *client,
           const gchar  *action,
           GUdevDevice  *device,
           gpointer      user_data)
{
  LinuxBlockProvider *provider = user_data;
  g_print ("%s:%s: entering\n", G_STRLOC, G_STRFUNC);
  handle_uevent (provider, action, device);
}

/* ---------------------------------------------------------------------------------------------------- */

static LinuxBlockProvider *_g_provider = NULL;

/* called when the system bus connection has been acquired but before the well-known
 * org.freedesktop.UDisks name is claimed
 */
void
linux_block_init (GDBusObjectManager *object_manager)
{
  const gchar *subsystems[] = {"block", NULL};
  LinuxBlockProvider *provider;
  GList *devices;
  GList *l;

  g_print ("%s:%s: entering\n", G_STRLOC, G_STRFUNC);

  _g_provider = provider = g_new0 (LinuxBlockProvider, 1);

  provider->object_manager = g_object_ref (object_manager);

  /* get ourselves an udev client */
  provider->gudev_client = g_udev_client_new (subsystems);
  g_signal_connect (provider->gudev_client,
                    "uevent",
                    G_CALLBACK (on_uevent),
                    provider);

  provider->sysfs_to_block = g_hash_table_new_full (g_str_hash,
                                                    g_str_equal,
                                                    g_free,
                                                    (GDestroyNotify) linux_block_free);

  /* TODO: maybe do two loops to properly handle dependency SNAFU? */
  devices = g_udev_client_query_by_subsystem (provider->gudev_client, "block");
  for (l = devices; l != NULL; l = l->next)
    handle_uevent (provider, "add", G_UDEV_DEVICE (l->data));
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);
}

/* called on shutdown */
void
linux_block_shutdown (void)
{
  LinuxBlockProvider *provider = _g_provider;

  g_print ("%s:%s: entering\n", G_STRLOC, G_STRFUNC);

  g_hash_table_unref (provider->sysfs_to_block);
  g_object_unref (provider->gudev_client);
  g_object_unref (provider->object_manager);
  g_free (provider);
}

