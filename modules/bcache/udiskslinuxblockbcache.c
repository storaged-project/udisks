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

#include <blockdev/kbd.h>
#include <glib/gi18n.h>

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslogging.h>
#include <src/udiskslinuxblockobject.h>

#include "udiskslinuxblockbcache.h"
#include "udisksbcacheutil.h"
#include "udisks-bcache-generated.h"

/**
 * SECTION:udiskslinuxblockbcache
 * @title: UDisksLinuxBlockBcache
 * @short_description: Object representing bCache device.
 *
 * Object corresponding to the Bcache device.
 */

/**
 * UDisksLinuxBlockBcache:
 *
 * The #UDisksLinuxBlockBcache structure contains only private data
 * and should only be accessed using the provided API.
 */

struct _UDisksLinuxBlockBcache {
  UDisksBlockBcacheSkeleton parent_instance;
};

struct _UDisksLinuxBlockBcacheClass {
  UDisksBlockBcacheSkeletonClass parent_class;
};

static void udisks_linux_block_bcache_iface_init (UDisksBlockBcacheIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxBlockBcache, udisks_linux_block_bcache,
                         UDISKS_TYPE_BLOCK_BCACHE_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_BLOCK_BCACHE,
                                                udisks_linux_block_bcache_iface_init));

static void
udisks_linux_block_bcache_get_property (GObject       *object,
                                          guint        property_id,
                                          GValue      *value,
                                          GParamSpec  *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_block_bcache_set_property (GObject         *object,
                                          guint          property_id,
                                          const GValue  *value,
                                          GParamSpec    *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_block_bcache_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_block_bcache_parent_class))
    G_OBJECT_CLASS (udisks_linux_block_bcache_parent_class)->dispose (object);
}

static void
udisks_linux_block_bcache_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_block_bcache_parent_class))
    G_OBJECT_CLASS (udisks_linux_block_bcache_parent_class)->finalize (object);
}

static void
udisks_linux_block_bcache_class_init (UDisksLinuxBlockBcacheClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_block_bcache_get_property;
  gobject_class->set_property = udisks_linux_block_bcache_set_property;
  gobject_class->dispose = udisks_linux_block_bcache_dispose;
  gobject_class->finalize = udisks_linux_block_bcache_finalize;
}

static void
udisks_linux_block_bcache_init (UDisksLinuxBlockBcache *self)
{
    g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                         G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);

}

/**
 * udisks_linux_block_bcache_new:
 *
 * Creates a new #UDisksLinuxBlockBcache instance.
 *
 * Returns: A new #UDisksLinuxBlockBcache. Free with g_object_unref().
 */

UDisksLinuxBlockBcache *
udisks_linux_block_bcache_new (void)
{
  return g_object_new (UDISKS_TYPE_LINUX_BLOCK_BCACHE, NULL);
}

/**
 * udisks_linux_block_bcache_get_daemon:
 * @block: A #UDisksLinuxBlockBcache.
 *
 * Gets the daemon used by @block.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @block.
 */

UDisksDaemon *
udisks_linux_block_bcache_get_daemon (UDisksLinuxBlockBcache *block)
{
  GError *error = NULL;
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon = NULL;

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_BCACHE (block), NULL);

  object = udisks_daemon_util_dup_object (block, &error);
  if (object)
    {
      daemon = udisks_linux_block_object_get_daemon (object);
      g_clear_object (&object);
    }
  else
    {
      udisks_critical ("%s", error->message);
      g_clear_error (&error);
    }

  return daemon;
}

/**
 * udisks_linux_block_bcache_update:
 * @block: A #UDisksLinuxBlockBcache
 * @object: The enclosing #UDisksLinuxBlockBcache instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */

gboolean
udisks_linux_block_bcache_update (UDisksLinuxBlockBcache  *block,
                                  UDisksLinuxBlockObject  *object)
{
  UDisksBlockBcache *iface = UDISKS_BLOCK_BCACHE (block);
  GError *error = NULL;
  gchar *dev_file = NULL;
  gboolean rval = FALSE;
  BDKBDBcacheStats *stats;
  BDKBDBcacheMode mode;
  const gchar* mode_str = NULL;

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_BCACHE (block), FALSE);
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  dev_file = udisks_linux_block_object_get_device_file (object);

  stats = bd_kbd_bcache_status (dev_file, &error);
  if (! stats)
    {
      udisks_critical ("Can't get Bcache block device info for %s: %s", dev_file, error->message);
      rval = FALSE;
      goto out;
    }
  mode = bd_kbd_bcache_get_mode(dev_file, &error);
  if (mode == BD_KBD_MODE_UNKNOWN)
    {
      udisks_critical ("Can't get Bcache mode for %s: %s", dev_file, error->message);
      rval = FALSE;
      goto out;
    }
  mode_str = bd_kbd_bcache_get_mode_str(mode, &error);
  if (! mode_str)
    {
      udisks_critical ("Can't get Bcache mode string for %s: %s", dev_file, error->message);
      rval = FALSE;
      goto out;
    }

  udisks_block_bcache_set_mode (iface, mode_str);
  udisks_block_bcache_set_state (iface, stats->state);
  udisks_block_bcache_set_block_size (iface, stats->block_size);
  udisks_block_bcache_set_cache_size (iface, stats->cache_size);
  udisks_block_bcache_set_cache_used (iface, stats->cache_used);
  udisks_block_bcache_set_hits (iface, stats->hits);
  udisks_block_bcache_set_misses (iface, stats->misses);
  udisks_block_bcache_set_bypass_hits (iface, stats->bypass_hits);
  udisks_block_bcache_set_bypass_misses (iface, stats->bypass_misses);
out:
  if (stats)
    bd_kbd_bcache_stats_free (stats);
  if (error)
    g_clear_error (&error);
  g_free (dev_file);

  return rval;
}

static UDisksObject *
wait_for_bcache (UDisksDaemon *daemon,
                 gpointer      user_data)
{
  UDisksObject *ret = udisks_daemon_find_object (daemon, (gchar*) user_data);
  /* find_object() increments the ref count, we need to decrement it back
     otherwise the ref count potentially grows in a cycle (when waiting for the
     object to disappear) */
  if (ret)
    g_object_unref (ret);
  return ret;
}

static gboolean
handle_bcache_destroy (UDisksBlockBcache      *block_,
                       GDBusMethodInvocation  *invocation,
                       GVariant               *options)
{
  GError *error = NULL;
  UDisksLinuxBlockBcache *block = UDISKS_LINUX_BLOCK_BCACHE (block_);
  UDisksLinuxBlockObject *object = NULL;
  UDisksDaemon *u_daemon = NULL;
  gchar *devname = NULL;
  gboolean bcache_object_disappeared = FALSE;
  gchar *object_path = NULL;

  object = udisks_daemon_util_dup_object (block, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_block_bcache_get_daemon (block),
                                     NULL,
                                     bcache_policy_action_id,
                                     options,
                                     N_("Authentication is required to destroy bcache device."),
                                     invocation);

  devname = udisks_linux_block_object_get_device_file (object);

  if (! bd_kbd_bcache_destroy (devname, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  u_daemon = udisks_linux_block_bcache_get_daemon (block);
  object_path = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  bcache_object_disappeared = udisks_daemon_wait_for_object_to_disappear_sync (u_daemon,
                                                                               wait_for_bcache,
                                                                               object_path,
                                                                               NULL,
                                                                               10,
                                                                               &error);
  if (!bcache_object_disappeared)
    {
      g_prefix_error (&error, "Error waiting for bcache to disappear: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_block_bcache_complete_bcache_destroy (block_, invocation);
out:
  g_free (devname);
  g_free (object_path);
  g_clear_object (&object);
  return TRUE;
}

static gboolean
handle_set_mode (UDisksBlockBcache      *block_,
                 GDBusMethodInvocation  *invocation,
                 const gchar            *arg_mode,
                 GVariant               *options)
{
  GError *error = NULL;
  UDisksLinuxBlockBcache *block = UDISKS_LINUX_BLOCK_BCACHE (block_);
  UDisksLinuxBlockObject *object = NULL;
  gchar *devname = NULL;
  BDKBDBcacheMode mode;
  gchar *modestr = NULL;

  object = udisks_daemon_util_dup_object (block, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_block_bcache_get_daemon (block),
                                     NULL,
                                     bcache_policy_action_id,
                                     options,
                                     N_("Authentication is required to set mode of bcache device."),
                                     invocation);

  devname = udisks_linux_block_object_get_device_file (object);

  modestr = g_strdup (arg_mode);
  mode = bd_kbd_bcache_get_mode_from_str (modestr, &error);

  if (error != NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (! bd_kbd_bcache_set_mode (devname, mode, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* update the property value -- there is no change event from bcache */
  udisks_linux_block_object_trigger_uevent (UDISKS_LINUX_BLOCK_OBJECT (object));
  udisks_block_bcache_complete_set_mode (block_, invocation);

out:
  g_free (devname);
  g_free (modestr);
  g_clear_object (&object);
  return TRUE;
}

static void
udisks_linux_block_bcache_iface_init (UDisksBlockBcacheIface *iface)
{
  iface->handle_bcache_destroy = handle_bcache_destroy;
  iface->handle_set_mode = handle_set_mode;
}
