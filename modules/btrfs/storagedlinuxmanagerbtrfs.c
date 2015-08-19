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

#include <src/storageddaemon.h>
#include <src/storagedlogging.h>

#include "storagedlinuxmanagerbtrfs.h"
#include "storagedbtrfsstate.h"
#include "storaged-btrfs-generated.h"

/**
 * SECTION:storagedlinuxmanagerbtrfs
 * @title: StoragedLinuxManagerBTRFS
 * @short_description: Linux implementation of #StoragedLinuxManagerBTRFS
 *
 * This type provides an implementation of the #StoragedLinuxManagerBTRFS
 * interface on Linux.
 */

/**
 * StoragedLinuxManagerBTRFS:
 *
 * The #StoragedLinuxManagerBTRFS structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _StoragedLinuxManagerBTRFS{
  StoragedManagerBTRFSSkeleton parent_instance;

  StoragedDaemon *daemon;
};

struct _StoragedLinuxManagerBTRFSClass {
  StoragedManagerBTRFSSkeletonClass parent_class;
};

static void storaged_linux_manager_btrfs_iface_init (StoragedManagerBTRFSIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxManagerBTRFS, storaged_linux_manager_btrfs,
                         STORAGED_TYPE_MANAGER_BTRFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MANAGER_BTRFS,
                                                storaged_linux_manager_btrfs_iface_init));

enum
{
  PROP_0,
  PROP_DAEMON
};

static void
storaged_linux_manager_btrfs_get_property (GObject *object, guint property_id,
                                           GValue *value, GParamSpec *pspec)
{
  StoragedLinuxManagerBTRFS *manager = STORAGED_LINUX_MANAGER_BTRFS (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_manager_btrfs_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_btrfs_set_property (GObject *object, guint property_id,
                                           const GValue *value, GParamSpec *pspec)
{
  StoragedLinuxManagerBTRFS *manager = STORAGED_LINUX_MANAGER_BTRFS (object);

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
storaged_linux_manager_btrfs_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_btrfs_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_btrfs_parent_class)->dispose (object);
}

static void
storaged_linux_manager_btrfs_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_btrfs_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_btrfs_parent_class)->finalize (object);
}

static void
storaged_linux_manager_btrfs_class_init (StoragedLinuxManagerBTRFSClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);


  gobject_class->get_property = storaged_linux_manager_btrfs_get_property;
  gobject_class->set_property = storaged_linux_manager_btrfs_set_property;
  gobject_class->dispose = storaged_linux_manager_btrfs_dispose;
  gobject_class->finalize = storaged_linux_manager_btrfs_finalize;
}

static void
storaged_linux_manager_btrfs_init (StoragedLinuxManagerBTRFS *self)
{
}

/**
 * storaged_linux_manager_btrfs_new:
 * @daemon: A #StoragedDaemon.
 *
 * Creates a new #StoragedLinuxManagerBTRFS instance.
 *
 * Returns: A new #StoragedLinuxManagerBTRFS. Free with g_object_unref().
 */
StoragedLinuxManagerBTRFS *
storaged_linux_manager_btrfs_new (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_LINUX_MANAGER_BTRFS(g_object_new (STORAGED_TYPE_LINUX_MANAGER_BTRFS,
                                                    "daemon", daemon,
                                                    NULL));
}

/**
 * storaged_linux_manager_btrfs_get_daemon:
 * @manager: A #StoragedLinuxManagerBTRFS.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @manager.
 */
StoragedDaemon *
storaged_linux_manager_btrfs_get_daemon (StoragedLinuxManagerBTRFS *manager)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER_BTRFS (manager), NULL);
  return manager->daemon;
}

static void
storaged_linux_manager_btrfs_iface_init (StoragedManagerBTRFSIface *iface)
{
}
