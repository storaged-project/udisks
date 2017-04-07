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
#include <src/udiskslinuxblock.h>
#include <src/udiskslinuxblockobject.h>
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
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
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

static UDisksObject *
wait_for_bcache_object (UDisksDaemon *daemon,
                        gpointer      user_data)
{
  UDisksObject *object = NULL;
  UDisksBlock *block = NULL;

  object = udisks_daemon_find_block_by_device_file (daemon, (const gchar *) user_data);
  if (object == NULL)
      goto out;

  block = udisks_object_peek_block (object);
  if (block == NULL)
    {
      g_clear_object (&object);
      goto out;
    }

  out:
    return object;
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

  UDisksObject *backing_dev_object = NULL;
  UDisksBlock *backing_dev_block = NULL;
  const gchar *backing_dev_path = NULL;
  UDisksObject *cache_dev_object = NULL;
  UDisksBlock *cache_dev_block = NULL;
  const gchar *cache_dev_path = NULL;
  gchar *bcache_name = NULL;
  gchar *bcache_file = NULL;
  UDisksObject *bcache_object = NULL;

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_manager_bcache_get_daemon (manager),
                                     NULL,
                                     bcache_policy_action_id,
                                     options,
                                     N_("Authentication is required to create bcache device."),
                                     invocation);

  /* get path for the backing device */
  backing_dev_object = udisks_daemon_find_object (manager->daemon, arg_backing_dev);
  if (backing_dev_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Invalid object path %s",
                                             arg_backing_dev);
      goto out;
    }

  backing_dev_block = udisks_object_get_block (backing_dev_object);
  if (backing_dev_block == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Object path %s is not a block device",
                                             arg_backing_dev);
      goto out;
    }

  backing_dev_path = udisks_block_dup_device (backing_dev_block);

  /* get path for the cache device */
  cache_dev_object = udisks_daemon_find_object (manager->daemon, arg_cache_dev);
  if (cache_dev_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Invalid object path %s",
                                             arg_cache_dev);
      goto out;
    }

  cache_dev_block = udisks_object_get_block (cache_dev_object);
  if (cache_dev_block == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Object path %s is not a block device",
                                             arg_cache_dev);
      goto out;
    }

  cache_dev_path = udisks_block_dup_device (cache_dev_block);

  /* XXX: the type casting below looks like a bug in libblockdev */
  if (! bd_kbd_bcache_create (backing_dev_path, cache_dev_path, NULL, (const gchar **) &bcache_name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  bcache_file = g_strdup_printf ("/dev/%s", bcache_name);
  /* sit and wait for the bcache object to show up */
  bcache_object = udisks_daemon_wait_for_object_sync (manager->daemon,
                                                      wait_for_bcache_object,
                                                      bcache_file,
                                                      NULL,
                                                      10, /* timeout_seconds */
                                                      &error);

  if (bcache_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for bcache object after creating %s",
                      bcache_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_manager_bcache_complete_bcache_create (object,
                                                invocation,
                                                g_dbus_object_get_object_path (G_DBUS_OBJECT (bcache_object)));
out:
  g_free ((gchar *) backing_dev_path);
  g_free ((gchar *) cache_dev_path);
  g_free (bcache_name);
  g_free (bcache_file);
  g_object_unref (bcache_object);
  g_clear_object (&backing_dev_object);
  g_clear_object (&backing_dev_block);
  g_clear_object (&cache_dev_object);
  g_clear_object (&cache_dev_block);

  return TRUE;
}


static void
udisks_linux_manager_bcache_iface_init (UDisksManagerBcacheIface *iface)
{
  iface->handle_bcache_create = handle_bcache_create;
}
