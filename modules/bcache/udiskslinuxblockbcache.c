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
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>

#include "udiskslinuxblockbcache.h"
#include "udiskslinuxmodulebcache.h"

/**
 * SECTION:udiskslinuxblockbcache
 * @title: UDisksLinuxBlockBcache
 * @short_description: Object representing Bcache device.
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

  UDisksLinuxModuleBcache *module;
  UDisksLinuxBlockObject  *block_object;
};

struct _UDisksLinuxBlockBcacheClass {
  UDisksBlockBcacheSkeletonClass parent_class;
};

static void udisks_linux_block_bcache_iface_init (UDisksBlockBcacheIface *iface);
static void udisks_linux_block_bcache_module_object_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxBlockBcache, udisks_linux_block_bcache, UDISKS_TYPE_BLOCK_BCACHE_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_BLOCK_BCACHE, udisks_linux_block_bcache_iface_init)
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT, udisks_linux_block_bcache_module_object_iface_init));

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_BLOCK_OBJECT,
  N_PROPERTIES
};

static void
udisks_linux_block_bcache_get_property (GObject     *object,
                                        guint        property_id,
                                        GValue      *value,
                                        GParamSpec  *pspec)
{
  UDisksLinuxBlockBcache *block_bcache = UDISKS_LINUX_BLOCK_BCACHE (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_value_set_object (value, UDISKS_MODULE (block_bcache->module));
      break;

    case PROP_BLOCK_OBJECT:
      g_value_set_object (value, block_bcache->block_object);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_block_bcache_set_property (GObject       *object,
                                        guint          property_id,
                                        const GValue  *value,
                                        GParamSpec    *pspec)
{
  UDisksLinuxBlockBcache *block_bcache = UDISKS_LINUX_BLOCK_BCACHE (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_assert (block_bcache->module == NULL);
      block_bcache->module = UDISKS_LINUX_MODULE_BCACHE (g_value_dup_object (value));
      break;

    case PROP_BLOCK_OBJECT:
      g_assert (block_bcache->block_object == NULL);
      /* we don't take reference to block_object */
      block_bcache->block_object = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_block_bcache_finalize (GObject *object)
{
  UDisksLinuxBlockBcache *block_bcache = UDISKS_LINUX_BLOCK_BCACHE (object);

  /* we don't take reference to block_object */
  g_object_unref (block_bcache->module);

  if (G_OBJECT_CLASS (udisks_linux_block_bcache_parent_class))
    G_OBJECT_CLASS (udisks_linux_block_bcache_parent_class)->finalize (object);
}

static void
udisks_linux_block_bcache_class_init (UDisksLinuxBlockBcacheClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_block_bcache_get_property;
  gobject_class->set_property = udisks_linux_block_bcache_set_property;
  gobject_class->finalize = udisks_linux_block_bcache_finalize;

  /**
   * UDisksLinuxBlockBcache:module:
   *
   * The #UDisksModule for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MODULE,
                                   g_param_spec_object ("module",
                                                        "Module",
                                                        "The module for the object",
                                                        UDISKS_TYPE_MODULE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
  /**
   * UDisksLinuxBlockBcache:blockobject:
   *
   * The #UDisksLinuxBlockObject for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_BLOCK_OBJECT,
                                   g_param_spec_object ("blockobject",
                                                        "Block object",
                                                        "The block object for the interface",
                                                        UDISKS_TYPE_LINUX_BLOCK_OBJECT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
udisks_linux_block_bcache_init (UDisksLinuxBlockBcache *self)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_block_bcache_new:
 * @module: A #UDisksLinuxModuleBcache.
 * @block_object: A #UDisksLinuxBlockObject.
 *
 * Creates a new #UDisksLinuxBlockBcache instance.
 *
 * Returns: A new #UDisksLinuxBlockBcache. Free with g_object_unref().
 */
UDisksLinuxBlockBcache *
udisks_linux_block_bcache_new (UDisksLinuxModuleBcache *module,
                               UDisksLinuxBlockObject  *block_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_BCACHE (module), NULL);
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (block_object), NULL);
  return g_object_new (UDISKS_TYPE_LINUX_BLOCK_BCACHE,
                       "module", UDISKS_MODULE (module),
                       "blockobject", block_object,
                       NULL);
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
  mode = bd_kbd_bcache_get_mode (dev_file, &error);
  if (mode == BD_KBD_MODE_UNKNOWN)
    {
      udisks_critical ("Can't get Bcache mode for %s: %s", dev_file, error->message);
      rval = FALSE;
      goto out;
    }
  mode_str = bd_kbd_bcache_get_mode_str (mode, &error);
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
  return udisks_daemon_find_object (daemon, (gchar*) user_data);
}

static gboolean
handle_bcache_destroy (UDisksBlockBcache      *block_,
                       GDBusMethodInvocation  *invocation,
                       GVariant               *options)
{
  UDisksLinuxBlockBcache *block = UDISKS_LINUX_BLOCK_BCACHE (block_);
  UDisksLinuxBlockObject *object = NULL;
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar *devname = NULL;
  const gchar *object_path;

  object = udisks_daemon_util_dup_object (block, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_module_get_daemon (UDISKS_MODULE (block->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     BCACHE_POLICY_ACTION_ID,
                                     options,
                                     N_("Authentication is required to destroy bcache device."),
                                     invocation);

  devname = udisks_linux_block_object_get_device_file (object);

  if (! bd_kbd_bcache_destroy (devname, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
  if (! udisks_daemon_wait_for_object_to_disappear_sync (daemon,
                                                         wait_for_bcache,
                                                         (gpointer) object_path,
                                                         NULL,
                                                         UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                         &error))
    {
      g_prefix_error (&error, "Error waiting for bcache to disappear: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_block_bcache_complete_bcache_destroy (block_, invocation);

out:
  g_free (devname);
  g_clear_object (&object);
  return TRUE;
}

static gboolean
handle_set_mode (UDisksBlockBcache      *block_,
                 GDBusMethodInvocation  *invocation,
                 const gchar            *arg_mode,
                 GVariant               *options)
{
  UDisksLinuxBlockBcache *block = UDISKS_LINUX_BLOCK_BCACHE (block_);
  UDisksLinuxBlockObject *object = NULL;
  UDisksDaemon *daemon;
  GError *error = NULL;
  gchar *devname = NULL;
  BDKBDBcacheMode mode;

  object = udisks_daemon_util_dup_object (block, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_module_get_daemon (UDISKS_MODULE (block->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     BCACHE_POLICY_ACTION_ID,
                                     options,
                                     N_("Authentication is required to set mode of bcache device."),
                                     invocation);

  devname = udisks_linux_block_object_get_device_file (object);

  mode = bd_kbd_bcache_get_mode_from_str (arg_mode, &error);
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
  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  udisks_block_bcache_complete_set_mode (block_, invocation);

out:
  g_free (devname);
  g_clear_object (&object);
  return TRUE;
}

static void
udisks_linux_block_bcache_iface_init (UDisksBlockBcacheIface *iface)
{
  iface->handle_bcache_destroy = handle_bcache_destroy;
  iface->handle_set_mode = handle_set_mode;
}

/* -------------------------------------------------------------------------- */

static gboolean
udisks_linux_block_bcache_module_object_process_uevent (UDisksModuleObject *module_object,
                                                        const gchar        *action,
                                                        UDisksLinuxDevice  *device,
                                                        gboolean           *keep)
{
  UDisksLinuxBlockBcache *block_bcache = UDISKS_LINUX_BLOCK_BCACHE (module_object);

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_BCACHE (module_object), FALSE);

  if (device == NULL)
    return FALSE;

  /* Check device name */
  *keep = g_str_has_prefix (g_udev_device_get_device_file (device->udev_device), "/dev/bcache");
  if (*keep)
    {
      udisks_linux_block_bcache_update (block_bcache, block_bcache->block_object);
    }

  return TRUE;
}

static void
udisks_linux_block_bcache_module_object_iface_init (UDisksModuleObjectIface *iface)
{
  iface->process_uevent = udisks_linux_block_bcache_module_object_process_uevent;
}
