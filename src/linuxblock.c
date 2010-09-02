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
  g_free (block);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
linux_block_update (LinuxBlock  *block,
                    const gchar *uevent_action)
{
  /* org.freedesktop.UDisks.LinuxSysfsDevice */
  if (block->iface_linux_sysfs_device == NULL)
    {
      block->iface_linux_sysfs_device = udisks_linux_sysfs_device_stub_new ();
      udisks_linux_sysfs_device_set_subsystem (block->iface_linux_sysfs_device, "block");
      udisks_linux_sysfs_device_set_sysfs_path (block->iface_linux_sysfs_device,
                                                g_udev_device_get_sysfs_path (block->device));
      g_dbus_object_add_interface (block->object, G_DBUS_INTERFACE (block->iface_linux_sysfs_device));
    }
  if (uevent_action != NULL)
    udisks_linux_sysfs_device_emit_uevent (block->iface_linux_sysfs_device, uevent_action);

  /* org.freedesktop.UDisks.BlockDevice */
  if (block->iface_block_device == NULL)
    {
      block->iface_block_device = udisks_block_device_stub_new ();
      udisks_block_device_set_device_file (block->iface_block_device,
                                           g_udev_device_get_device_file (block->device));
      g_dbus_object_add_interface (block->object, G_DBUS_INTERFACE (block->iface_block_device));
    }
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

