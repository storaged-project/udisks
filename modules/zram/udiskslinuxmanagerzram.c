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

#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <blockdev/kbd.h>

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslogging.h>
#include "udisks-zram-generated.h"
#include "udiskslinuxblockzram.h"
#include "udiskslinuxmanagerzram.h"
#include "udiskszramutil.h"

/**
 * SECTION: udiskslinuxmanagerzram
 * @title: UDisksLinuxManagerZRAM
 * @short_description: Linux implementation  of #UDisksLinuxManagerZRAM
 *
 * This type provides an implementation of the #UDisksLinuxManagerZRAM
 * interface on Linux.
 */

/**
 * UDisksLinuxManagerZRAM:
 *
 * The #UDisksLinuxManagerZRAM structure contains only private data and
 * should only be accessed using the provided API.
 */

struct _UDisksLinuxManagerZRAM {
  UDisksManagerZRAMSkeleton parent_instance;

  UDisksDaemon *daemon;
};

struct _UDisksLinuxManagerZRAMClass {
  UDisksManagerZRAMSkeletonClass parent_instance;
};

static void udisks_linux_manager_zram_iface_init (UDisksManagerZRAMIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerZRAM, udisks_linux_manager_zram,
                         UDISKS_TYPE_MANAGER_ZRAM_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_ZRAM,
                                                udisks_linux_manager_zram_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

enum
{
  PROP_0,
  PROP_DAEMON,
  N_PROPERTIES
};

static void
udisks_linux_manager_zram_get_property (GObject     *object,
                                        guint        property_id,
                                        GValue      *value,
                                        GParamSpec  *pspec)
{
  UDisksLinuxManagerZRAM *manager = UDISKS_LINUX_MANAGER_ZRAM (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_manager_zram_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_zram_set_property (GObject       *object,
                                        guint          property_id,
                                        const GValue  *value,
                                        GParamSpec    *pspec)
{
  UDisksLinuxManagerZRAM *manager = UDISKS_LINUX_MANAGER_ZRAM (object);

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
udisks_linux_manager_zram_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_manager_zram_parent_class))
    G_OBJECT_CLASS (udisks_linux_manager_zram_parent_class)->dispose (object);
}

static void
udisks_linux_manager_zram_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_manager_zram_parent_class))
    G_OBJECT_CLASS (udisks_linux_manager_zram_parent_class)->finalize (object);
}

static void
udisks_linux_manager_zram_class_init (UDisksLinuxManagerZRAMClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_manager_zram_get_property;
  gobject_class->set_property = udisks_linux_manager_zram_set_property;
  gobject_class->dispose = udisks_linux_manager_zram_dispose;
  gobject_class->finalize = udisks_linux_manager_zram_finalize;

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
udisks_linux_manager_zram_init (UDisksLinuxManagerZRAM *self)
{
}

/**
 * udisks_linux_manager_zram_new:
 * @daemon: A #UDisksDaemon.
 *
 * Creates a new #UDisksLinuxManagerZRAM instance.
 *
 * Returns: A new #UDisksLinuxManagerZRAM. Free with g_object_unref ().
 */
UDisksLinuxManagerZRAM *
udisks_linux_manager_zram_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_LINUX_MANAGER_ZRAM (g_object_new (UDISKS_TYPE_LINUX_MANAGER_ZRAM,
                                                  "daemon", daemon,
                                                  NULL));
}

/**
 * udisks_linux_manager_zram_get_daemon:
 * @manager: A #UDisksLinuxManagerZRAM.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @manager.
 */

UDisksDaemon *udisks_linux_manager_zram_get_daemon (UDisksLinuxManagerZRAM* manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER_ZRAM (manager), NULL);
  return manager->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
create_conf_files (guint64   num_devices,
                   guint64  *sizes,
                   guint64  *num_streams,
                   GError  **error)
{
  gboolean rval = TRUE;
  gchar *filename;
  gchar *contents;
  gchar tmp[255];
  guint64 i;

  filename = g_build_filename (PACKAGE_MODLOAD_DIR, "zram.conf", NULL);
  contents = g_strdup ("zram\n");

  if (! g_file_set_contents (filename , contents, -1, error))
    {
      rval = FALSE;
      goto out;
    }
  g_free (contents);
  g_free (filename);

  filename = g_build_filename (PACKAGE_MODPROBE_DIR, "zram.conf", NULL);
  contents = g_strdup_printf ("options zram num_devices=%lu\n", num_devices);

  if (! g_file_set_contents (filename , contents, -1, error))
    {
      rval = FALSE;
      goto out;
    }

  for (i = 0; i < num_devices; i++)
    {
      g_free (filename);
      g_free (contents);

      g_snprintf (tmp, 255, "zram%lu", i);
      filename = g_build_filename (PACKAGE_ZRAMCONF_DIR, tmp, NULL);
      contents = g_strdup_printf ("#!/bin/bash\n\n"
                                  "ZRAM_NUM_STR=%lu\n"
                                  "ZRAM_DEV_SIZE=%lu\n"
                                  "SWAP=n\n",
                                  num_streams[i],
                                  sizes[i]);
      g_file_set_contents (filename, contents, -1, error);
    }
out:
  g_free (filename);
  g_free (contents);
  return rval;
}

static gboolean
delete_conf_files (GError **error)
{
  gboolean rval = TRUE;
  GDir *zramconfd;
  gchar *filename = NULL;

  filename = g_build_filename (PACKAGE_MODLOAD_DIR, "/zram.conf", NULL);

  if (g_unlink (filename))
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),"%m");
      rval = FALSE;
      goto out;
    }

  g_free (filename);
  filename = g_build_filename (PACKAGE_MODPROBE_DIR, "/zram.conf", NULL);

  if (g_unlink (filename))
    {
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),"%m");
      rval = FALSE;
      goto out;
    }

  zramconfd = g_dir_open (PACKAGE_ZRAMCONF_DIR, 0, error);
  if (! zramconfd)
  {
    rval = FALSE;
    goto out;
  }
  do
  {
    g_free (filename);
    filename = g_build_filename(PACKAGE_ZRAMCONF_DIR,
                                g_dir_read_name (zramconfd),
                                NULL);
  } while (! g_unlink (filename));
  g_dir_close (zramconfd);

out:
  g_free (filename);
  return rval;

}
static gboolean
handle_create_devices (UDisksManagerZRAM     *object,
                       GDBusMethodInvocation *invocation,
                       guint64                num_devices,
                       GVariant              *sizes_,
                       GVariant              *num_streams_,
                       GVariant              *options)
{
  UDisksLinuxManagerZRAM *manager = UDISKS_LINUX_MANAGER_ZRAM (object);
  GError *error = NULL;
  guint64 *sizes;
  guint64 *num_streams;

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (manager->daemon,
                                     NULL,
                                     zram_policy_action_id,
                                     options,
                                     N_("Authentication is required to add zRAM kernel module"),
                                     invocation);

  sizes = (guint64*) g_variant_get_fixed_array (sizes_, &num_devices, sizeof (guint64));
  num_streams = (guint64*) g_variant_get_fixed_array (num_streams_, &num_devices, sizeof (guint64));

  if (! create_conf_files (num_devices, sizes, num_streams, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (! bd_kbd_zram_create_devices (num_devices, sizes, num_streams, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      delete_conf_files (&error);
      goto out;
    }
  udisks_manager_zram_complete_create_devices(object, invocation);
out:
  return TRUE;
}

static gboolean
handle_destroy_devices (UDisksManagerZRAM     *object,
                        GDBusMethodInvocation *invocation,
                        GVariant              *options)
{

  GError *error = NULL;
  UDisksLinuxManagerZRAM *manager = UDISKS_LINUX_MANAGER_ZRAM (object);

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_manager_zram_get_daemon (manager),
                                     NULL,
                                     zram_policy_action_id,
                                     options,
                                     N_("Authentication is required to remove zRAM kernel module"),
                                     invocation);

  if (! bd_kbd_zram_destroy_devices (&error))
    {
     g_dbus_method_invocation_take_error (invocation, error);
     goto out;
    }

  if (! delete_conf_files (&error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_manager_zram_complete_destroy_devices (object, invocation);
out:
  return TRUE;
}

static void
udisks_linux_manager_zram_iface_init (UDisksManagerZRAMIface *iface)
{
  iface->handle_create_devices = handle_create_devices;
  iface->handle_destroy_devices = handle_destroy_devices;
}
