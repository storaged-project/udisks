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
#include <src/udiskslinuxblockobject.h>

#include "udiskslinuxmanagerbtrfs.h"
#include "udiskslinuxmodulebtrfs.h"
#include "udisksbtrfsutil.h"

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

  UDisksLinuxModuleBTRFS *module;
};

struct _UDisksLinuxManagerBTRFSClass {
  UDisksManagerBTRFSSkeletonClass parent_class;
};

static void udisks_linux_manager_btrfs_iface_init (UDisksManagerBTRFSIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerBTRFS, udisks_linux_manager_btrfs, UDISKS_TYPE_MANAGER_BTRFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_BTRFS, udisks_linux_manager_btrfs_iface_init));

enum
{
  PROP_0,
  PROP_MODULE,
  N_PROPERTIES
};

static void
udisks_linux_manager_btrfs_get_property (GObject *object, guint property_id,
                                         GValue *value, GParamSpec *pspec)
{
  UDisksLinuxManagerBTRFS *manager = UDISKS_LINUX_MANAGER_BTRFS (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_value_set_object (value, udisks_linux_manager_btrfs_get_module (manager));
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
    case PROP_MODULE:
      g_assert (manager->module == NULL);
      manager->module = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_btrfs_finalize (GObject *object)
{
  UDisksLinuxManagerBTRFS *manager = UDISKS_LINUX_MANAGER_BTRFS (object);

  g_object_unref (manager->module);

  if (G_OBJECT_CLASS (udisks_linux_manager_btrfs_parent_class))
    G_OBJECT_CLASS (udisks_linux_manager_btrfs_parent_class)->finalize (object);
}

static void
udisks_linux_manager_btrfs_class_init (UDisksLinuxManagerBTRFSClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_manager_btrfs_get_property;
  gobject_class->set_property = udisks_linux_manager_btrfs_set_property;
  gobject_class->finalize = udisks_linux_manager_btrfs_finalize;

  /**
   * UDisksLinuxManager:module
   *
   * The #UDisksLinuxModuleBTRFS for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MODULE,
                                   g_param_spec_object ("module",
                                                        "Module",
                                                        "The module for the object",
                                                        UDISKS_TYPE_LINUX_MODULE_BTRFS,
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
 * @module: A #UDisksLinuxModuleBTRFS.
 *
 * Creates a new #UDisksLinuxManagerBTRFS instance.
 *
 * Returns: A new #UDisksLinuxManagerBTRFS. Free with g_object_unref().
 */
UDisksLinuxManagerBTRFS *
udisks_linux_manager_btrfs_new (UDisksLinuxModuleBTRFS *module)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_BTRFS (module), NULL);
  return UDISKS_LINUX_MANAGER_BTRFS (g_object_new (UDISKS_TYPE_LINUX_MANAGER_BTRFS,
                                                   "module", module,
                                                   NULL));
}

/**
 * udisks_linux_manager_btrfs_get_module:
 * @manager: A #UDisksLinuxManagerBTRFS.
 *
 * Gets the module used by @manager.
 *
 * Returns: A #UDisksLinuxModuleBTRFS. Do not free, the object is owned by @manager.
 */
UDisksLinuxModuleBTRFS *
udisks_linux_manager_btrfs_get_module (UDisksLinuxManagerBTRFS *manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER_BTRFS (manager), NULL);
  return manager->module;
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
  UDisksDaemon *daemon;
  GError *error = NULL;
  GPtrArray *devices = NULL;
  GList *objects = NULL;
  GList *l;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (l_manager->module));

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     BTRFS_POLICY_ACTION_ID,
                                     arg_options,
                                     N_("Authentication is required to create a new volume"),
                                     invocation);

  devices = g_ptr_array_new_with_free_func (g_free);
  for (guint n = 0; arg_blocks != NULL && arg_blocks[n] != NULL; n++)
    {
      UDisksObject *object;
      UDisksBlock *block;

      object = udisks_daemon_find_object (daemon, arg_blocks[n]);
      if (object == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Invalid object path %s",
                                                 arg_blocks[n]);
          goto out;
        }

      block = udisks_object_peek_block (object);
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

      g_ptr_array_add (devices, udisks_block_dup_device (block));
      objects = g_list_append (objects, object);
    }
  g_ptr_array_add (devices, NULL);

  if (!bd_btrfs_create_volume ((const gchar **) devices->pdata, arg_label, arg_data_level, arg_md_level, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Trigger uevent on all block devices */
  for (l = objects; l; l = g_list_next (l))
    {
      udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (l->data),
                                                     UDISKS_DEFAULT_WAIT_TIMEOUT);
    }

  /* Complete DBus call. */
  udisks_manager_btrfs_complete_create_volume (manager, invocation);

out:
  if (devices != NULL)
    g_ptr_array_free (devices, TRUE);
  g_list_free_full (objects, g_object_unref);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static void
udisks_linux_manager_btrfs_iface_init (UDisksManagerBTRFSIface *iface)
{
  iface->handle_create_volume = handle_create_volume;
}
