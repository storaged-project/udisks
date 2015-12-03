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

#include <src/storageddaemonutil.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storagedlogging.h>
#include <src/storageddaemon.h>
#include <blockdev/kbd.h>
#include <blockdev/swap.h>

#include "storagedlinuxblockzram.h"
#include "storagedzramutil.h"
#include "storaged-zram-generated.h"

/**
 * SECTION:storagedlinuxblockzram
 * @title: StoragedLinuxBlockZRAM
 * @short_description: Object representing zRAM device.
 *
 * Object corresponding to the zRAM device.
 */

/**
 * StoragedLinuxBlockZRAM:
 *
 * The #StoragedLinuxBlockZRAM structure contains only private data
 * and should only be accessed using the provided API.
 */

struct _StoragedLinuxBlockZRAM {
  StoragedBlockZRAMSkeleton parent_instance;
};

struct _StoragedLinuxBlockZRAMClass {
  StoragedBlockZRAMSkeletonClass parent_instance;
};

static void storaged_linux_block_zram_iface_init (StoragedBlockZRAMIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxBlockZRAM, storaged_linux_block_zram,
                         STORAGED_TYPE_BLOCK_ZRAM_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_BLOCK_ZRAM,
                                                storaged_linux_block_zram_iface_init));

static void
storaged_linux_block_zram_get_property (GObject     *object,
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
storaged_linux_block_zram_set_property (GObject       *object,
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
storaged_linux_block_zram_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_block_zram_parent_class))
    G_OBJECT_CLASS (storaged_linux_block_zram_parent_class)->dispose (object);
}

static void
storaged_linux_block_zram_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_block_zram_parent_class))
    G_OBJECT_CLASS (storaged_linux_block_zram_parent_class)->finalize (object);
}

static void
storaged_linux_block_zram_class_init (StoragedLinuxBlockZRAMClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = storaged_linux_block_zram_get_property;
  gobject_class->set_property = storaged_linux_block_zram_set_property;
  gobject_class->dispose = storaged_linux_block_zram_dispose;
  gobject_class->finalize = storaged_linux_block_zram_finalize;
}

static void
storaged_linux_block_zram_init (StoragedLinuxBlockZRAM *zramblock)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (zramblock),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * storaged_linux_block_zram_new:
 *
 * Creates a new #StoragedLinuxBlockZRAM instance.
 *
 * Returns: A new #StoragedLinuxBlockZRAM. Free with g_object_unref().
 */

StoragedLinuxBlockZRAM*
storaged_linux_block_zram_new (void)
{
  return g_object_new (STORAGED_TYPE_LINUX_BLOCK_ZRAM, NULL);
}

/**
 * storaged_linux_block_zram_get_daemon:
 * @zramblock: A #StoragedLinuxBlockZRAM.
 *
 * Gets the daemon used by @zramblock.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @zramblock.
 */

StoragedDaemon *
storaged_linux_block_zram_get_daemon (StoragedLinuxBlockZRAM *zramblock)
{
  GError *error = NULL;
  StoragedLinuxBlockObject *object;
  StoragedDaemon *daemon = NULL;

  g_return_val_if_fail (STORAGED_IS_LINUX_BLOCK_ZRAM (zramblock), NULL);

  object = storaged_daemon_util_dup_object (zramblock, &error);
  if (object)
    {
      daemon = storaged_linux_block_object_get_daemon (object);
      g_clear_object (&object);
    }
  else
    {
      storaged_error ("%s", error->message);
      g_error_free (error);
    }

  return daemon;
}

/**
 * storaged_linux_block_zram_update:
 * @zramblock: A #StoragedLinuxBlockZRAM
 * @object: The enclosing #StoragedLinuxBlockZRAM instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */

gboolean
storaged_linux_block_zram_update (StoragedLinuxBlockZRAM    *zramblock,
                                  StoragedLinuxBlockObject  *object)
{
  StoragedBlockZRAM *iface = STORAGED_BLOCK_ZRAM (zramblock);
  GError *error = NULL;
  gchar *dev_file = NULL;
  gboolean rval = FALSE;
  BDKBDZramStats *zram_info;

  g_return_val_if_fail (STORAGED_IS_LINUX_BLOCK_ZRAM (zramblock), FALSE);
  g_return_val_if_fail (STORAGED_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  dev_file = storaged_linux_block_object_get_device_file (object);

  zram_info = bd_kbd_zram_get_stats (dev_file, &error);

  if (! zram_info)
    {
      storaged_error ("Can't get ZRAM block device info for %s", dev_file);
      rval = FALSE;
      goto out;
    }

  /* Update the interface */
  storaged_block_zram_set_disksize (iface, zram_info->disksize);
  storaged_block_zram_set_num_reads (iface, zram_info->num_reads);
  storaged_block_zram_set_num_writes (iface, zram_info->num_writes);
  storaged_block_zram_set_invalid_io (iface, zram_info->invalid_io);
  storaged_block_zram_set_zero_pages (iface, zram_info->zero_pages);
  storaged_block_zram_set_max_comp_streams (iface, zram_info->max_comp_streams);
  storaged_block_zram_set_comp_algorithm (iface, zram_info->comp_algorithm);
  storaged_block_zram_set_orig_data_size (iface, zram_info->orig_data_size);
  storaged_block_zram_set_compr_data_size (iface, zram_info->compr_data_size);
  storaged_block_zram_set_mem_used_total (iface, zram_info->mem_used_total);

  storaged_block_zram_set_active (iface, bd_swap_swapstatus (dev_file, &error));
out:
  if (zram_info)
    bd_kbd_zram_stats_free (zram_info);
  if (error)
    g_error_free (error);
  g_free (dev_file);

  return rval;
}

static gboolean
zram_device_activate (StoragedBlockZRAM      *zramblock_,
                      GDBusMethodInvocation  *invocation,
                      gint                    priority,
                      const gchar            *label_,
                      GVariant               *options)
{
  StoragedLinuxBlockZRAM *zramblock = STORAGED_LINUX_BLOCK_ZRAM (zramblock_);
  StoragedLinuxBlockObject *object = NULL;
  gchar *dev_file = NULL;
  gchar *filename = NULL;
  gchar *label;
  GError *error = NULL;

  label = g_strdup (label_);

  object = storaged_daemon_util_dup_object (zramblock, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_block_zram_get_daemon(zramblock),
                                       STORAGED_OBJECT (object),
                                       zram_policy_action_id,
                                       options,
                                       N_("Authentication is required to enable zRAM device"),
                                       invocation);

  dev_file = storaged_linux_block_object_get_device_file (object);

  if (! bd_swap_mkswap (dev_file, label, &error))
  {
    g_dbus_method_invocation_take_error (invocation, error);
    goto out;
  }

  if (! bd_swap_swapon (dev_file, priority, &error))
  {
    g_dbus_method_invocation_take_error (invocation, error);
    goto out;
  }

  filename = g_strdup_printf("%s/%s-env", PACKAGE_ZRAMCONF_DIR, (strrchr(dev_file,'/')+1));
  if (! set_conf_property (filename, "SWAP", "y", &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_block_zram_set_active (zramblock_, TRUE);
  storaged_block_zram_complete_activate(zramblock_,invocation);

out:
  g_clear_object (&object);
  g_free (filename);
  g_free (dev_file);
  g_free (label);
  return TRUE;
}

static gboolean
handle_refresh (StoragedBlockZRAM      *zramblock_,
                GDBusMethodInvocation  *invocation)
{
  StoragedLinuxBlockZRAM *zramblock = STORAGED_LINUX_BLOCK_ZRAM (zramblock_);
  StoragedObject *object = NULL;
  GError *error = NULL;

  object = storaged_daemon_util_dup_object (zramblock,&error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_linux_block_zram_update (zramblock, STORAGED_LINUX_BLOCK_OBJECT (object));
  storaged_block_zram_complete_refresh(zramblock_,invocation);

out:
  g_clear_object (&object);
  return TRUE;
}

static gboolean
handle_activate_labeled (StoragedBlockZRAM      *zramblock_,
                         GDBusMethodInvocation  *invocation,
                         gint                    priority,
                         const gchar            *label,
                         GVariant               *options)
{
  return zram_device_activate(zramblock_, invocation, priority, label, options);
}

static gboolean
handle_activate (StoragedBlockZRAM      *zramblock_,
                 GDBusMethodInvocation  *invocation,
                 const gint              priority,
                 GVariant               *options)
{
  return zram_device_activate(zramblock_, invocation, priority, NULL, options);
}

static gboolean
handle_deactivate (StoragedBlockZRAM      *zramblock_,
                   GDBusMethodInvocation  *invocation,
                   GVariant               *options)
{
  StoragedLinuxBlockZRAM *zramblock = STORAGED_LINUX_BLOCK_ZRAM (zramblock_);
  StoragedLinuxBlockObject *object = NULL;
  gchar *dev_file = NULL;
  gchar *filename = NULL;
  GError *error = NULL;

  object = storaged_daemon_util_dup_object (zramblock, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_block_zram_get_daemon(zramblock),
                                       STORAGED_OBJECT (object),
                                       zram_policy_action_id,
                                       options,
                                       N_("Authentication is required to disable zRAM device"),
                                       invocation);

  if (! storaged_block_zram_get_active (zramblock_))
    {
      return TRUE;
    }

  dev_file = storaged_linux_block_object_get_device_file (object);

  if (! bd_swap_swapoff (dev_file, &error))
  {
    g_dbus_method_invocation_take_error (invocation, error);
    goto out;
  }

  if (! set_conf_property (filename, "SWAP", "n", &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_block_zram_set_active (zramblock_, FALSE);
  storaged_block_zram_complete_deactivate(zramblock_,invocation);

out:
  g_clear_object (&object);
  g_free (filename);
  g_free (dev_file);

  return TRUE;
}

static void
storaged_linux_block_zram_iface_init (StoragedBlockZRAMIface *iface)
{
  iface->handle_refresh = handle_refresh;
  iface->handle_activate = handle_activate;
  iface->handle_activate_labeled = handle_activate_labeled;
  iface->handle_deactivate = handle_deactivate;
}
