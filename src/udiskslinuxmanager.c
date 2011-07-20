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

#include "udiskslogging.h"
#include "udiskslinuxmanager.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxmanager
 * @title: UDisksLinuxManager
 * @short_description: Manager implementation on Linux
 *
 * This type provides an implementation of the #UDisksManager
 * interface on Linux.
 */

typedef struct _UDisksLinuxManagerClass   UDisksLinuxManagerClass;

/**
 * UDisksLinuxManager:
 *
 * The #UDisksLinuxManager structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxManager
{
  UDisksManagerSkeleton parent_instance;

  UDisksDaemon *daemon;
};

struct _UDisksLinuxManagerClass
{
  UDisksManagerSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON
};

static void manager_iface_init (UDisksManagerIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManager, udisks_linux_manager, UDISKS_TYPE_MANAGER_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER, manager_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_manager_finalize (GObject *object)
{
  /* UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (object); */

  G_OBJECT_CLASS (udisks_linux_manager_parent_class)->finalize (object);
}

static void
udisks_linux_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_manager_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  UDisksLinuxManager *manager = UDISKS_LINUX_MANAGER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (manager->daemon == NULL);
      /* we don't take a reference to the daemon */
      manager->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_init (UDisksLinuxManager *manager)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_manager_class_init (UDisksLinuxManagerClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_manager_finalize;
  gobject_class->set_property = udisks_linux_manager_set_property;
  gobject_class->get_property = udisks_linux_manager_get_property;

  /**
   * UDisksLinuxManager:daemon:
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

/**
 * udisks_linux_manager_new:
 * @daemon: A #UDisksDaemon.
 *
 * Creates a new #UDisksLinuxManager instance.
 *
 * Returns: A new #UDisksLinuxManager. Free with g_object_unref().
 */
UDisksManager *
udisks_linux_manager_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_MANAGER (g_object_new (UDISKS_TYPE_LINUX_MANAGER,
                                       "daemon", daemon,
                                       "version", PACKAGE_VERSION,
                                       NULL));
}

/**
 * udisks_linux_manager_get_daemon:
 * @manager: A #UDisksLinuxManager.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @manager.
 */
UDisksDaemon *
udisks_linux_manager_get_daemon (UDisksLinuxManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER (manager), NULL);
  return manager->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
manager_iface_init (UDisksManagerIface *iface)
{
}
