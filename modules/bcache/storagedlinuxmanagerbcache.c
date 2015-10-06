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

#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlogging.h>
#include "storaged-bcache-generated.h"
#include "storagedlinuxblockbcache.h"
#include "storagedlinuxmanagerbcache.h"
#include "storagedbcacheutil.h"

#include "storagedlinuxmanagerbcache.h"

/**
 * SECTION: storagedlinuxmanagerbcache
 * @title: StoragedLinuxManagerBcache
 * @short_description: Linux implementation  of #StoragedLinuxManagerBcache
 *
 * This type provides an implementation of the #StoragedLinuxManagerBcache
 * interface on Linux.
 */

/**
 * StoragedLinuxManagerBcache:
 *
 * The #StoragedLinuxManagerBcache structure contains only private data and
 * should only be accessed using the provided API.
 */

struct _StoragedLinuxManagerBcache {
  StoragedManagerBcacheSkeleton parent_instance;

  StoragedDaemon *daemon;
};

struct _StoragedLinuxManagerBcacheClass {
  StoragedManagerBcacheSkeletonClass parent_class;
};

static void storaged_linux_manager_bcache_iface_init (StoragedManagerBcacheIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxManagerBcache, storaged_linux_manager_bcache,
                         STORAGED_TYPE_MANAGER_BCACHE_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MANAGER_BCACHE,
                                                storaged_linux_manager_bcache_iface_init));

enum
{
  PROP_0,
  PROP_DAEMON,
  N_PROPERTIES
};

static void
storaged_linux_manager_bcache_get_property (GObject     *object,
                                            guint        property_id,
                                            GValue      *value,
                                            GParamSpec  *pspec)
{
  StoragedLinuxManagerBcache *manager = STORAGED_LINUX_MANAGER_BCACHE (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_manager_bcache_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_bcache_set_property (GObject       *object,
                                            guint          property_id,
                                            const GValue  *value,
                                            GParamSpec    *pspec)
{
  StoragedLinuxManagerBcache *manager = STORAGED_LINUX_MANAGER_BCACHE (object);

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
storaged_linux_manager_bcache_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_bcache_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_bcache_parent_class)->dispose (object);
}

static void
storaged_linux_manager_bcache_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_bcache_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_bcache_parent_class)->finalize (object);
}

static void
storaged_linux_manager_bcache_class_init (StoragedLinuxManagerBcacheClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = storaged_linux_manager_bcache_get_property;
  gobject_class->set_property = storaged_linux_manager_bcache_set_property;
  gobject_class->dispose = storaged_linux_manager_bcache_dispose;
  gobject_class->finalize = storaged_linux_manager_bcache_finalize;

/**
 * StoragedLinuxManager:daemon
 *
 * The #StoragedDaemon for the object.
 */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
storaged_linux_manager_bcache_init (StoragedLinuxManagerBcache *self)
{
}

/**
 * storaged_linux_manager_bcache_new:
 * @daemon: A #StoragedDaemon.
 *
 * Creates a new #StoragedLinuxManagerBcache instance.
 *
 * Returns: A new #StoragedLinuxManagerBcache. Free with g_object_unref ().
 */

StoragedLinuxManagerBcache *
storaged_linux_manager_bcache_new (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_LINUX_MANAGER_BCACHE (g_object_new (STORAGED_TYPE_LINUX_MANAGER_BCACHE,
                                                    "daemon", daemon,
                                                    NULL));
}

/**
 * storaged_linux_manager_bcache_get_daemon:
 * @manager: A #StoragedLinuxManagerBcache.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @manager.
 */

StoragedDaemon *storaged_linux_manager_bcache_get_daemon (StoragedLinuxManagerBcache* manager)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER_BCACHE (manager), NULL);
  return manager->daemon;
}

static gboolean
handle_bcache_create (StoragedManagerBcache  *object,
                      GDBusMethodInvocation  *invocation,
                      const gchar            *arg_backing_dev,
                      const gchar            *arg_cache_dev,
                      GVariant               *options)
{
  GError *error = NULL;
  StoragedLinuxManagerBcache *manager = STORAGED_LINUX_MANAGER_BCACHE (object);
  gchar *backing_dev;
  gchar *cache_dev;
  gchar *bcache = NULL;

  backing_dev = g_strdup (arg_backing_dev);
  cache_dev = g_strdup (arg_cache_dev);
  
  /* Policy check */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_manager_bcache_get_daemon (manager),
                                       NULL,
                                       bcache_policy_action_id,
                                       options,
                                       N_("Authentication is required to create bcache device."),
                                       invocation);

  if (! bd_kbd_bcache_create (backing_dev, cache_dev, &bcache, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (! bcache)
    bcache = g_strdup("Failed to get bcache device name");

    storaged_manager_bcache_complete_bcache_create (object, invocation, bcache);
out:
  g_free (backing_dev);
  g_free (cache_dev);
  return TRUE;
}


static void
storaged_linux_manager_bcache_iface_init (StoragedManagerBcacheIface *iface)
{
  iface->handle_bcache_create = handle_bcache_create;
}
