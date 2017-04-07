/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Peter Hatina <phatina@redhat.com>
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
 */

#include "config.h"

#include <glib/gi18n.h>
#include <blockdev/btrfs.h>

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslogging.h>

#include "udiskslinuxmanagerbtrfs.h"
#include "udisksbtrfsstate.h"
#include "udisksbtrfsutil.h"
#include "udisks-btrfs-generated.h"

/**
 * SECTION:udiskslinuxmanagerbtrfs
 * @title: UDisksLinuxManagerBTRFS
 * @short_description: Linux implementation of #UDisksLinuxManagerBTRFS
 *
 * This type provides an implementation of the #UDisksLinuxManagerBTRFS
 * interface on Linux.
 */

/**
 * UDisksLinuxManagerBTRFS:
 *
 * The #UDisksLinuxManagerBTRFS structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxManagerBTRFS{
  UDisksManagerBTRFSSkeleton parent_instance;

  UDisksDaemon *daemon;
};

struct _UDisksLinuxManagerBTRFSClass {
  UDisksManagerBTRFSSkeletonClass parent_class;
};

static void udisks_linux_manager_btrfs_iface_init (UDisksManagerBTRFSIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerBTRFS, udisks_linux_manager_btrfs,
                         UDISKS_TYPE_MANAGER_BTRFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_BTRFS,
                                                udisks_linux_manager_btrfs_iface_init));

enum
{
  PROP_0,
  PROP_DAEMON,
  N_PROPERTIES
};

static void
udisks_linux_manager_btrfs_get_property (GObject *object, guint property_id,
                                         GValue *value, GParamSpec *pspec)
{
  UDisksLinuxManagerBTRFS *manager = UDISKS_LINUX_MANAGER_BTRFS (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_manager_btrfs_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_btrfs_set_property (GObject *object, guint property_id,
                                         const GValue *value, GParamSpec *pspec)
{
  UDisksLinuxManagerBTRFS *manager = UDISKS_LINUX_MANAGER_BTRFS (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_assert (manager->daemon == NULL);
      /* We don't take a reference to the daemon */
      manager->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_btrfs_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_manager_btrfs_parent_class))
    G_OBJECT_CLASS (udisks_linux_manager_btrfs_parent_class)->dispose (object);
}

static void
udisks_linux_manager_btrfs_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_manager_btrfs_parent_class))
    G_OBJECT_CLASS (udisks_linux_manager_btrfs_parent_class)->finalize (object);
}

static void
udisks_linux_manager_btrfs_class_init (UDisksLinuxManagerBTRFSClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_manager_btrfs_get_property;
  gobject_class->set_property = udisks_linux_manager_btrfs_set_property;
  gobject_class->dispose = udisks_linux_manager_btrfs_dispose;
  gobject_class->finalize = udisks_linux_manager_btrfs_finalize;

  /**
   * UDisksLinuxManager:daemon
   *
   * The #UDisksDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
udisks_linux_manager_btrfs_init (UDisksLinuxManagerBTRFS *self)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_manager_btrfs_new:
 * @daemon: A #UDisksDaemon.
 *
 * Creates a new #UDisksLinuxManagerBTRFS instance.
 *
 * Returns: A new #UDisksLinuxManagerBTRFS. Free with g_object_unref().
 */
UDisksLinuxManagerBTRFS *
udisks_linux_manager_btrfs_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_LINUX_MANAGER_BTRFS(g_object_new (UDISKS_TYPE_LINUX_MANAGER_BTRFS,
                                                  "daemon", daemon,
                                                  NULL));
}

/**
 * udisks_linux_manager_btrfs_get_daemon:
 * @manager: A #UDisksLinuxManagerBTRFS.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @manager.
 */
UDisksDaemon *
udisks_linux_manager_btrfs_get_daemon (UDisksLinuxManagerBTRFS *manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER_BTRFS (manager), NULL);
  return manager->daemon;
}

static gboolean
handle_create_volume (UDisksManagerBTRFS    *manager,
                      GDBusMethodInvocation *invocation,
                      const gchar *const    *arg_blocks,
                      const gchar           *arg_label,
                      const gchar           *arg_data_level,
                      const gchar           *arg_md_level,
                      GVariant              *arg_options)
{
  UDisksLinuxManagerBTRFS *l_manager = UDISKS_LINUX_MANAGER_BTRFS (manager);
  GError *error = NULL;
  const gchar **devices = NULL;
  guint num_devices = 0;

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_manager_btrfs_get_daemon (l_manager),
                                     NULL,
                                     btrfs_policy_action_id,
                                     arg_options,
                                     N_("Authentication is required to create a new volume"),
                                     invocation);

  num_devices = g_strv_length ((gchar**) arg_blocks);
  devices = alloca (sizeof (const gchar*) * (num_devices + 1));
  devices[num_devices] = NULL;

  for (guint n = 0; arg_blocks != NULL && arg_blocks[n] != NULL; n++)
    {
      UDisksObject *object = NULL;
      UDisksBlock *block = NULL;

      object = udisks_daemon_find_object (l_manager->daemon, arg_blocks[n]);
      if (object == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Invalid object path %s",
                                                 arg_blocks[n]);
          goto out;
        }

      block = udisks_object_get_block (object);
      if (block == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Object path %s is not a block device",
                                                 arg_blocks[n]);
          g_object_unref (object);
          goto out;
        }

      devices[n] = udisks_block_dup_device (block);
      g_object_unref (object);
      g_object_unref (block);
    }

  if (!bd_btrfs_create_volume (devices, arg_label, arg_data_level, arg_md_level, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  udisks_manager_btrfs_complete_create_volume (manager, invocation);

out:

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static void
udisks_linux_manager_btrfs_iface_init (UDisksManagerBTRFSIface *iface)
{
  iface->handle_create_volume = handle_create_volume;
}
