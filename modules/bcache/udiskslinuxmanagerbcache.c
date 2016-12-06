/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Dominika Hodovska <dhodovsk@redhat.com>
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
#include <blockdev/kbd.h>

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslogging.h>
#include "udisks-bcache-generated.h"
#include "udiskslinuxblockbcache.h"
#include "udiskslinuxmanagerbcache.h"
#include "udisksbcacheutil.h"

#include "udiskslinuxmanagerbcache.h"

/**
 * SECTION: udiskslinuxmanagerbcache
 * @title: UDisksLinuxManagerBcache
 * @short_description: Linux implementation  of #UDisksLinuxManagerBcache
 *
 * This type provides an implementation of the #UDisksLinuxManagerBcache
 * interface on Linux.
 */

/**
 * UDisksLinuxManagerBcache:
 *
 * The #UDisksLinuxManagerBcache structure contains only private data and
 * should only be accessed using the provided API.
 */

struct _UDisksLinuxManagerBcache {
  UDisksManagerBcacheSkeleton parent_instance;

  UDisksDaemon *daemon;
};

struct _UDisksLinuxManagerBcacheClass {
  UDisksManagerBcacheSkeletonClass parent_class;
};

static void udisks_linux_manager_bcache_iface_init (UDisksManagerBcacheIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerBcache, udisks_linux_manager_bcache,
                         UDISKS_TYPE_MANAGER_BCACHE_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_BCACHE,
                                                udisks_linux_manager_bcache_iface_init));

enum
{
  PROP_0,
  PROP_DAEMON,
  N_PROPERTIES
};

static void
udisks_linux_manager_bcache_get_property (GObject     *object,
                                          guint        property_id,
                                          GValue      *value,
                                          GParamSpec  *pspec)
{
  UDisksLinuxManagerBcache *manager = UDISKS_LINUX_MANAGER_BCACHE (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_manager_bcache_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_bcache_set_property (GObject       *object,
                                          guint          property_id,
                                          const GValue  *value,
                                          GParamSpec    *pspec)
{
  UDisksLinuxManagerBcache *manager = UDISKS_LINUX_MANAGER_BCACHE (object);

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
udisks_linux_manager_bcache_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_manager_bcache_parent_class))
    G_OBJECT_CLASS (udisks_linux_manager_bcache_parent_class)->dispose (object);
}

static void
udisks_linux_manager_bcache_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_manager_bcache_parent_class))
    G_OBJECT_CLASS (udisks_linux_manager_bcache_parent_class)->finalize (object);
}

static void
udisks_linux_manager_bcache_class_init (UDisksLinuxManagerBcacheClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_manager_bcache_get_property;
  gobject_class->set_property = udisks_linux_manager_bcache_set_property;
  gobject_class->dispose = udisks_linux_manager_bcache_dispose;
  gobject_class->finalize = udisks_linux_manager_bcache_finalize;

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
udisks_linux_manager_bcache_init (UDisksLinuxManagerBcache *self)
{
}

/**
 * udisks_linux_manager_bcache_new:
 * @daemon: A #UDisksDaemon.
 *
 * Creates a new #UDisksLinuxManagerBcache instance.
 *
 * Returns: A new #UDisksLinuxManagerBcache. Free with g_object_unref ().
 */

UDisksLinuxManagerBcache *
udisks_linux_manager_bcache_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_LINUX_MANAGER_BCACHE (g_object_new (UDISKS_TYPE_LINUX_MANAGER_BCACHE,
                                                    "daemon", daemon,
                                                    NULL));
}

/**
 * udisks_linux_manager_bcache_get_daemon:
 * @manager: A #UDisksLinuxManagerBcache.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @manager.
 */

UDisksDaemon *udisks_linux_manager_bcache_get_daemon (UDisksLinuxManagerBcache* manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER_BCACHE (manager), NULL);
  return manager->daemon;
}

static gboolean
handle_bcache_create (UDisksManagerBcache    *object,
                      GDBusMethodInvocation  *invocation,
                      const gchar            *arg_backing_dev,
                      const gchar            *arg_cache_dev,
                      GVariant               *options)
{
  GError *error = NULL;
  UDisksLinuxManagerBcache *manager = UDISKS_LINUX_MANAGER_BCACHE (object);
  gchar *backing_dev;
  gchar *cache_dev;
  gchar *bcache = NULL;

  backing_dev = g_strdup (arg_backing_dev);
  cache_dev = g_strdup (arg_cache_dev);

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_manager_bcache_get_daemon (manager),
                                     NULL,
                                     bcache_policy_action_id,
                                     options,
                                     N_("Authentication is required to create bcache device."),
                                     invocation);

  /* XXX: the type casting below looks like a bug in libblockdev */
  if (! bd_kbd_bcache_create (backing_dev, cache_dev, NULL, (const gchar **) &bcache, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_manager_bcache_complete_bcache_create (object, invocation, bcache);
out:
  g_free (backing_dev);
  g_free (cache_dev);
  return TRUE;
}


static void
udisks_linux_manager_bcache_iface_init (UDisksManagerBcacheIface *iface)
{
  iface->handle_bcache_create = handle_bcache_create;
}
