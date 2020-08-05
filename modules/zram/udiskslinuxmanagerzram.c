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
#include <src/udiskslinuxblockobject.h>
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

  UDisksLinuxModuleZRAM *module;
};

struct _UDisksLinuxManagerZRAMClass {
  UDisksManagerZRAMSkeletonClass parent_instance;
};

static void udisks_linux_manager_zram_iface_init (UDisksManagerZRAMIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerZRAM, udisks_linux_manager_zram, UDISKS_TYPE_MANAGER_ZRAM_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_ZRAM, udisks_linux_manager_zram_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

enum
{
  PROP_0,
  PROP_MODULE,
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
    case PROP_MODULE:
      g_value_set_object (value, udisks_linux_manager_zram_get_module (manager));
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
    case PROP_MODULE:
      g_assert (manager->module == NULL);
      manager->module = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
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
  gobject_class->finalize = udisks_linux_manager_zram_finalize;

  /**
   * UDisksLinuxManagerZRAM:daemon:
   *
   * The #UDisksLinuxModuleZRAM for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MODULE,
                                   g_param_spec_object ("module",
                                                        "Module",
                                                        "The module for the object",
                                                        UDISKS_TYPE_LINUX_MODULE_ZRAM,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
udisks_linux_manager_zram_init (UDisksLinuxManagerZRAM *self)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_manager_zram_new:
 * @module: A #UDisksLinuxModuleZRAM.
 *
 * Creates a new #UDisksLinuxManagerZRAM instance.
 *
 * Returns: A new #UDisksLinuxManagerZRAM. Free with g_object_unref ().
 */
UDisksLinuxManagerZRAM *
udisks_linux_manager_zram_new (UDisksLinuxModuleZRAM *module)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZRAM (module), NULL);
  return UDISKS_LINUX_MANAGER_ZRAM (g_object_new (UDISKS_TYPE_LINUX_MANAGER_ZRAM,
                                                  "module", module,
                                                  NULL));
}

/**
 * udisks_linux_manager_zram_get_daemon:
 * @manager: A #UDisksLinuxManagerZRAM.
 *
 * Gets the module used by @manager.
 *
 * Returns: A #UDisksLinuxModuleZRAM. Do not free, the object is owned by @manager.
 */

UDisksLinuxModuleZRAM *
udisks_linux_manager_zram_get_module (UDisksLinuxManagerZRAM *manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER_ZRAM (manager), NULL);
  return manager->module;
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
  contents = g_strdup ("# UDisks2 managed ZRAM configuration\n"
                       "# This file is autogenerated and your changes may get lost.\n"
                       "zram\n");

  if (! g_file_set_contents (filename , contents, -1, error))
    {
      rval = FALSE;
      goto out;
    }
  g_free (contents);
  g_free (filename);

  filename = g_build_filename (PACKAGE_MODPROBE_DIR, "zram.conf", NULL);
  contents = g_strdup_printf ("# UDisks2 managed ZRAM configuration\n"
                              "# Do not modify as your changes may get overwritten.\n"
                              "options zram num_devices=%" G_GUINT64_FORMAT "\n",
                              num_devices);

  if (! g_file_set_contents (filename , contents, -1, error))
    {
      rval = FALSE;
      goto out;
    }

  if (g_mkdir_with_parents (PACKAGE_ZRAMCONF_DIR, 0755) != 0)
      {
        rval = FALSE;
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                     "Error creating directory %s: %m", PACKAGE_ZRAMCONF_DIR);
        goto out;
      }

  for (i = 0; i < num_devices; i++)
    {
      g_free (filename);
      g_free (contents);

      g_snprintf (tmp, 255, "zram%" G_GUINT64_FORMAT, i);
      filename = g_build_filename (PACKAGE_ZRAMCONF_DIR, tmp, NULL);
      contents = g_strdup_printf ("#!/bin/bash\n"
                                  "# UDisks2 managed ZRAM configuration\n\n"
                                  "ZRAM_NUM_STR=%" G_GUINT64_FORMAT "\n"
                                  "ZRAM_DEV_SIZE=%" G_GUINT64_FORMAT "\n"
                                  "SWAP=n\n",
                                  num_streams[i],
                                  sizes[i]);
      if (! g_file_set_contents (filename, contents, -1, error))
        {
          rval = FALSE;
          goto out;
        }
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
  const gchar *name;

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

  while ((name = g_dir_read_name (zramconfd)))
  {
    g_free (filename);
    filename = g_build_filename (PACKAGE_ZRAMCONF_DIR, name, NULL);
    g_unlink (filename);
  }
  g_dir_close (zramconfd);

out:
  g_free (filename);
  return rval;

}

static UDisksObject **
wait_for_zram_objects (UDisksDaemon *daemon,
                       gpointer      user_data)
{
  gchar **zram_p = NULL;
  gint next_obj = 0;
  gboolean success = TRUE;
  gint num_zrams = 0;
  UDisksObject **objects = NULL;
  gchar **zram_paths = (gchar **) user_data;

  num_zrams = g_strv_length (zram_paths);
  objects = g_new0 (UDisksObject *, num_zrams + 1);

  for (zram_p = zram_paths; *zram_p != NULL; zram_p++, next_obj++)
    {
      UDisksObject *object = NULL;
      UDisksBlock *block = NULL;

      object = udisks_daemon_find_block_by_device_file (daemon, *zram_p);
      if (object == NULL)
        {
          success = FALSE;
          break;
        }

      block = udisks_object_peek_block (object);
      if (block == NULL)
        {
          success = FALSE;
          g_object_unref (object);
          break;
        }

      success = TRUE;
      objects[next_obj] = object;
    }

  if (! success && objects)
    {
      for (int i = 0; i < num_zrams; i++)
        if (objects[i])
          g_object_unref (objects[i]);
      g_free (objects);
      return NULL;
    }

  return objects;
}

static gboolean
handle_create_devices (UDisksManagerZRAM     *object,
                       GDBusMethodInvocation *invocation,
                       GVariant              *sizes_,
                       GVariant              *num_streams_,
                       GVariant              *options)
{
  UDisksLinuxManagerZRAM *manager = UDISKS_LINUX_MANAGER_ZRAM (object);
  UDisksDaemon *daemon;
  GError *error = NULL;
  gsize sizes_len;
  gsize streams_len;
  guint64 *sizes;
  guint64 *num_streams;
  gchar **zram_paths = NULL;
  UDisksObject **zram_objects = NULL;
  gchar **zram_object_paths = NULL;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (manager->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     ZRAM_POLICY_ACTION_ID,
                                     options,
                                     N_("Authentication is required to add zRAM kernel module"),
                                     invocation);

  sizes = (guint64*) g_variant_get_fixed_array (sizes_,
                                                &sizes_len,
                                                sizeof (guint64));
  num_streams = (guint64*) g_variant_get_fixed_array (num_streams_,
                                                      &streams_len,
                                                      sizeof (guint64));

  if (! create_conf_files ((guint64) streams_len, sizes, num_streams, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (! bd_kbd_zram_create_devices ((guint64) sizes_len, sizes, num_streams, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      delete_conf_files (&error);
      goto out;
    }

  zram_paths = g_new0 (gchar *, sizes_len + 1);
  for (gsize i = 0; i < sizes_len; i++)
    zram_paths[i] = g_strdup_printf ("/dev/zram%" G_GSIZE_FORMAT, i);

  /* sit and wait for the zram objects to show up */
  zram_objects = udisks_daemon_wait_for_objects_sync (daemon,
                                                      wait_for_zram_objects,
                                                      zram_paths,
                                                      NULL,
                                                      UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                      &error);

  if (zram_objects == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for ZRAM objects after creating: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  for (UDisksObject **object_p = zram_objects; *object_p; object_p++) {
    UDisksLinuxBlockObject *lb_object = UDISKS_LINUX_BLOCK_OBJECT (*object_p);
    udisks_linux_block_object_trigger_uevent (lb_object);
  }

  zram_object_paths = g_new0 (gchar *, sizes_len + 1);
  for (gsize i = 0; i < sizes_len; i++) {
    zram_object_paths[i] = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (zram_objects[i])));
    g_object_unref (zram_objects[i]);
  }

  udisks_manager_zram_complete_create_devices (object,
                                               invocation,
                                               (const gchar **) zram_object_paths);
out:
  g_free (zram_objects);
  g_strfreev (zram_paths);
  g_strfreev (zram_object_paths);

  return TRUE;
}

static UDisksObject *
wait_for_any_zram_object (UDisksDaemon *daemon,
                          gpointer      user_data) {
  GList *objects, *l;
  UDisksObject *ret = NULL;

  objects = udisks_daemon_get_objects (daemon);
  for (l = objects; !ret && l != NULL; l = l->next)
    if (g_dbus_object_get_interface (G_DBUS_OBJECT (l->data), "org.freedesktop.UDisks2.Block.ZRAM"))
      ret = g_object_ref (l->data);

  g_list_free_full (objects, g_object_unref);
  return ret;
}

static gboolean
handle_destroy_devices (UDisksManagerZRAM     *object,
                        GDBusMethodInvocation *invocation,
                        GVariant              *options)
{
  UDisksLinuxManagerZRAM *manager = UDISKS_LINUX_MANAGER_ZRAM (object);
  GError *error = NULL;
  UDisksDaemon *daemon;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (manager->module));

  /* Policy check */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     NULL,
                                     ZRAM_POLICY_ACTION_ID,
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

  if (! udisks_daemon_wait_for_object_to_disappear_sync (daemon,
                                                         wait_for_any_zram_object,
                                                         NULL,
                                                         NULL,
                                                         UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                         &error))
    {
      g_prefix_error (&error, "Error waiting for zram objects to disappear: ");
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
