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
#include <blockdev/swap.h>

#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslogging.h>
#include <src/udisksdaemon.h>
#include <src/udisksmoduleobject.h>

#include "udiskslinuxblockzram.h"
#include "udiskszramutil.h"

/**
 * SECTION:udiskslinuxblockzram
 * @title: UDisksLinuxBlockZRAM
 * @short_description: Object representing zRAM device.
 *
 * Object corresponding to the zRAM device.
 */

/**
 * UDisksLinuxBlockZRAM:
 *
 * The #UDisksLinuxBlockZRAM structure contains only private data
 * and should only be accessed using the provided API.
 */

struct _UDisksLinuxBlockZRAM {
  UDisksBlockZRAMSkeleton parent_instance;

  UDisksLinuxModuleZRAM  *module;
  UDisksLinuxBlockObject *block_object;
};

struct _UDisksLinuxBlockZRAMClass {
  UDisksBlockZRAMSkeletonClass parent_instance;
};

static void udisks_linux_block_zram_iface_init (UDisksBlockZRAMIface *iface);
static void udisks_linux_block_zram_module_object_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxBlockZRAM, udisks_linux_block_zram, UDISKS_TYPE_BLOCK_ZRAM_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_BLOCK_ZRAM, udisks_linux_block_zram_iface_init)
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT, udisks_linux_block_zram_module_object_iface_init));

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_BLOCK_OBJECT,
  N_PROPERTIES
};

static void
udisks_linux_block_zram_get_property (GObject     *object,
                                      guint        property_id,
                                      GValue      *value,
                                      GParamSpec  *pspec)
{
  UDisksLinuxBlockZRAM *block_zram = UDISKS_LINUX_BLOCK_ZRAM (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_value_set_object (value, UDISKS_MODULE (block_zram->module));
      break;

    case PROP_BLOCK_OBJECT:
      g_value_set_object (value, block_zram->block_object);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_block_zram_set_property (GObject       *object,
                                      guint          property_id,
                                      const GValue  *value,
                                      GParamSpec    *pspec)
{
  UDisksLinuxBlockZRAM *block_zram = UDISKS_LINUX_BLOCK_ZRAM (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_assert (block_zram->module == NULL);
      block_zram->module = UDISKS_LINUX_MODULE_ZRAM (g_value_dup_object (value));
      break;

    case PROP_BLOCK_OBJECT:
      g_assert (block_zram->block_object == NULL);
      /* we don't take reference to block_object */
      block_zram->block_object = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_block_zram_finalize (GObject *object)
{
  UDisksLinuxBlockZRAM *block_zram = UDISKS_LINUX_BLOCK_ZRAM (object);

  /* we don't take reference to block_object */
  g_object_unref (block_zram->module);

  if (G_OBJECT_CLASS (udisks_linux_block_zram_parent_class))
    G_OBJECT_CLASS (udisks_linux_block_zram_parent_class)->finalize (object);
}

static void
udisks_linux_block_zram_class_init (UDisksLinuxBlockZRAMClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_block_zram_get_property;
  gobject_class->set_property = udisks_linux_block_zram_set_property;
  gobject_class->finalize = udisks_linux_block_zram_finalize;

  /**
   * UDisksLinuxBlockZRAM:module:
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
   * UDisksLinuxBlockZRAM:blockobject:
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
udisks_linux_block_zram_init (UDisksLinuxBlockZRAM *zramblock)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (zramblock),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_block_zram_new:
 * @module: A #UDisksLinuxModuleZRAM.
 * @block_object: A #UDisksLinuxBlockObject.
 *
 * Creates a new #UDisksLinuxBlockZRAM instance.
 *
 * Returns: A new #UDisksLinuxBlockZRAM. Free with g_object_unref().
 */
UDisksLinuxBlockZRAM *
udisks_linux_block_zram_new (UDisksLinuxModuleZRAM  *module,
                             UDisksLinuxBlockObject *block_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZRAM (module), NULL);
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (block_object), NULL);
  return g_object_new (UDISKS_TYPE_LINUX_BLOCK_ZRAM,
                       "module", UDISKS_MODULE (module),
                       "blockobject", block_object,
                       NULL);
}

static gchar *
extract_comp_algorithm (const gchar *alg_str)
{
  gchar *begin = NULL;
  gchar *end = NULL;

  begin = strchr (alg_str, '[');
  end = strchr (alg_str, ']');
  if (! begin || ! end)
      return NULL;
  begin++;

  return g_strndup (begin, end - begin);
}

/**
 * udisks_linux_block_zram_update:
 * @zramblock: A #UDisksLinuxBlockZRAM
 * @object: The enclosing #UDisksLinuxBlockZRAM instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_block_zram_update (UDisksLinuxBlockZRAM    *zramblock,
                                UDisksLinuxBlockObject  *object)
{
  UDisksBlockZRAM *iface = UDISKS_BLOCK_ZRAM (zramblock);
  GError *error = NULL;
  gchar *dev_file = NULL;
  gboolean rval = FALSE;
  BDKBDZramStats *zram_info;
  gchar *algorithm = NULL;

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_ZRAM (zramblock), FALSE);
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  dev_file = udisks_linux_block_object_get_device_file (object);

  zram_info = bd_kbd_zram_get_stats (dev_file, &error);

  if (! zram_info)
    {
      udisks_critical ("Can't get ZRAM block device info for %s", dev_file);
      rval = FALSE;
      goto out;
    }

  algorithm = extract_comp_algorithm (zram_info->comp_algorithm);
  if (! algorithm)
    {
      udisks_critical ("Failed to determine comp algorithm from '%s'", zram_info->comp_algorithm);
      rval = FALSE;
      goto out;
    }

  /* Update the interface */
  udisks_block_zram_set_disksize (iface, zram_info->disksize);
  udisks_block_zram_set_num_reads (iface, zram_info->num_reads);
  udisks_block_zram_set_num_writes (iface, zram_info->num_writes);
  udisks_block_zram_set_invalid_io (iface, zram_info->invalid_io);
  udisks_block_zram_set_zero_pages (iface, zram_info->zero_pages);
  udisks_block_zram_set_max_comp_streams (iface, zram_info->max_comp_streams);
  udisks_block_zram_set_comp_algorithm (iface, algorithm);
  udisks_block_zram_set_orig_data_size (iface, zram_info->orig_data_size);
  udisks_block_zram_set_compr_data_size (iface, zram_info->compr_data_size);
  udisks_block_zram_set_mem_used_total (iface, zram_info->mem_used_total);

  udisks_block_zram_set_active (iface, bd_swap_swapstatus (dev_file, &error));
out:
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (iface));
  if (zram_info)
    bd_kbd_zram_stats_free (zram_info);
  if (error)
    g_clear_error (&error);
  g_free (algorithm);
  g_free (dev_file);

  return rval;
}

static gboolean
zram_device_activate (UDisksBlockZRAM       *zramblock_,
                      GDBusMethodInvocation *invocation,
                      gint                   priority,
                      const gchar           *label_,
                      GVariant              *options)
{
  UDisksLinuxBlockZRAM *zramblock = UDISKS_LINUX_BLOCK_ZRAM (zramblock_);
  UDisksLinuxBlockObject *object = NULL;
  UDisksDaemon *daemon;
  gchar *dev_file = NULL;
  gchar *filename = NULL;
  gchar *label;
  GError *error = NULL;

  label = g_strdup (label_);

  object = udisks_daemon_util_dup_object (zramblock, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_module_get_daemon (UDISKS_MODULE (zramblock->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZRAM_POLICY_ACTION_ID,
                                     options,
                                     N_("Authentication is required to enable zRAM device"),
                                     invocation);

  dev_file = udisks_linux_block_object_get_device_file (object);

  if (! bd_swap_mkswap (dev_file, label, NULL, &error))
  {
    g_dbus_method_invocation_take_error (invocation, error);
    goto out;
  }

  if (! bd_swap_swapon (dev_file, priority, &error))
  {
    g_dbus_method_invocation_take_error (invocation, error);
    goto out;
  }

  filename = g_build_filename (PACKAGE_ZRAMCONF_DIR, g_path_get_basename (dev_file), NULL);
  if (! set_conf_property (filename, "SWAP", "y", &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_block_zram_set_active (zramblock_, TRUE);
  udisks_block_zram_complete_activate (zramblock_, invocation);

out:
  g_clear_object (&object);
  g_free (filename);
  g_free (dev_file);
  g_free (label);
  return TRUE;
}

static gboolean
handle_refresh (UDisksBlockZRAM       *zramblock_,
                GDBusMethodInvocation *invocation,
                GVariant              *options)
{
  UDisksLinuxBlockZRAM *zramblock = UDISKS_LINUX_BLOCK_ZRAM (zramblock_);
  UDisksObject *object = NULL;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (zramblock,&error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_linux_block_zram_update (zramblock, UDISKS_LINUX_BLOCK_OBJECT (object));
  udisks_block_zram_complete_refresh (zramblock_, invocation);

out:
  g_clear_object (&object);
  return TRUE;
}

static gboolean
handle_activate_labeled (UDisksBlockZRAM       *zramblock_,
                         GDBusMethodInvocation *invocation,
                         gint                   priority,
                         const gchar           *label,
                         GVariant              *options)
{
  return zram_device_activate (zramblock_, invocation, priority, label, options);
}

static gboolean
handle_activate (UDisksBlockZRAM       *zramblock_,
                 GDBusMethodInvocation *invocation,
                 const gint             priority,
                 GVariant              *options)
{
  return zram_device_activate (zramblock_, invocation, priority, NULL, options);
}

static gboolean
handle_deactivate (UDisksBlockZRAM       *zramblock_,
                   GDBusMethodInvocation *invocation,
                   GVariant              *options)
{
  UDisksLinuxBlockZRAM *zramblock = UDISKS_LINUX_BLOCK_ZRAM (zramblock_);
  UDisksLinuxBlockObject *object = NULL;
  UDisksDaemon *daemon;
  gchar *dev_file = NULL;
  gchar *filename = NULL;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (zramblock, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_module_get_daemon (UDISKS_MODULE (zramblock->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     ZRAM_POLICY_ACTION_ID,
                                     options,
                                     N_("Authentication is required to disable zRAM device"),
                                     invocation);

  if (! udisks_block_zram_get_active (zramblock_))
    {
      return TRUE;
    }

  dev_file = udisks_linux_block_object_get_device_file (object);

  if (! bd_swap_swapoff (dev_file, &error))
  {
    g_dbus_method_invocation_take_error (invocation, error);
    goto out;
  }

  filename = g_build_filename (PACKAGE_ZRAMCONF_DIR, g_path_get_basename (dev_file), NULL);
  if (! set_conf_property (filename, "SWAP", "n", &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_block_zram_set_active (zramblock_, FALSE);
  udisks_block_zram_complete_deactivate (zramblock_, invocation);

out:
  g_clear_object (&object);
  g_free (filename);
  g_free (dev_file);

  return TRUE;
}

static void
udisks_linux_block_zram_iface_init (UDisksBlockZRAMIface *iface)
{
  iface->handle_refresh = handle_refresh;
  iface->handle_activate = handle_activate;
  iface->handle_activate_labeled = handle_activate_labeled;
  iface->handle_deactivate = handle_deactivate;
}

/* -------------------------------------------------------------------------- */

static gboolean
udisks_linux_block_zram_module_object_process_uevent (UDisksModuleObject *module_object,
                                                      const gchar        *action,
                                                      UDisksLinuxDevice  *device,
                                                      gboolean           *keep)
{
  UDisksLinuxBlockZRAM *block_zram = UDISKS_LINUX_BLOCK_ZRAM (module_object);

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_ZRAM (module_object), FALSE);

  if (device == NULL)
    return FALSE;

  /* Check device name */
  *keep = g_str_has_prefix (g_udev_device_get_device_file (device->udev_device), "/dev/zram");
  if (*keep)
    {
      udisks_linux_block_zram_update (block_zram, block_zram->block_object);
    }

  return TRUE;
}

static void
udisks_linux_block_zram_module_object_iface_init (UDisksModuleObjectIface *iface)
{
  iface->process_uevent = udisks_linux_block_zram_module_object_process_uevent;
}
