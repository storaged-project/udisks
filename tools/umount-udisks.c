/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <udisks/udisks.h>

/* TODO: Temporary include */
#include <gdbusproxymanager.h>

static GDBusObjectProxy *
lookup_object_proxy_by_block_device_file (GDBusProxyManager *manager,
                                          const gchar       *block_device_file)
{
  GDBusObjectProxy *ret;
  GList *object_proxies;
  GList *l;

  ret = NULL;

  object_proxies = g_dbus_proxy_manager_get_all (manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksBlockDevice *block;

      block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
      if (block != NULL)
        {
          const gchar * const *symlinks;
          guint n;

          if (g_strcmp0 (udisks_block_device_get_device (block), block_device_file) == 0)
            {
              ret = g_object_ref (object_proxy);
              goto out;
            }

          symlinks = udisks_block_device_get_symlinks (block);
          for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
            {
              if (g_strcmp0 (symlinks[n], block_device_file) == 0)
                {
                  ret = g_object_ref (object_proxy);
                  goto out;
                }
            }
        }
    }

 out:
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);

  return ret;
}

int
main (int argc, char *argv[])
{
  gint ret;
  gchar *block_device_file;
  GDBusProxyManager *manager;
  GError *error;
  struct stat statbuf;
  GDBusObjectProxy *object_proxy;
  UDisksFilesystem *filesystem;
  const gchar *unmount_options[1] = {NULL};

  ret = 1;
  manager = NULL;
  block_device_file = NULL;
  object_proxy = NULL;
  filesystem = NULL;

  g_type_init ();

  if (argc < 2 || strlen (argv[1]) == 0)
    {
      g_printerr ("%s: this program is only supposed to be invoked by umount(8).\n", argv[0]);
      goto out;
    }

  if (stat (argv[1], &statbuf) < 0)
    {
      g_printerr ("%s: error calling stat on %s: %m\n", argv[0], argv[1]);
      goto out;
    }

  if (S_ISBLK (statbuf.st_mode))
    {
      block_device_file = g_strdup (argv[1]);
    }
  else
    {
      block_device_file = g_strdup_printf ("/dev/block/%d:%d", major (statbuf.st_dev), minor (statbuf.st_dev));
    }

  error = NULL;
  manager = udisks_proxy_manager_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                   G_DBUS_PROXY_MANAGER_FLAGS_NONE,
                                                   "org.freedesktop.UDisks2",
                                                   "/org/freedesktop/UDisks2",
                                                   NULL, /* GCancellable */
                                                   &error);
  if (manager == NULL)
    {
      g_printerr ("Error connecting to the udisks daemon: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  object_proxy = lookup_object_proxy_by_block_device_file (manager, block_device_file);
  if (object_proxy == NULL)
    {
      g_printerr ("Error finding object for block device file %s\n", block_device_file);
      goto out;
    }

  filesystem = UDISKS_GET_FILESYSTEM (object_proxy);
  if (filesystem == NULL)
    {
      g_printerr ("Object for block device file %s does not appear to be a file system\n", block_device_file);
      goto out;
    }

  error = NULL;
  if (!udisks_filesystem_call_unmount_sync (filesystem,
                                            unmount_options,
                                            NULL, /* GCancellable */
                                            &error))
    {
      g_printerr ("Error unmounting %s: %s\n", block_device_file, error->message);
      g_error_free (error);
      goto out;
    }

  ret = 0;

 out:
  if (filesystem != NULL)
    g_object_unref (filesystem);
  if (object_proxy != NULL)
    g_object_unref (object_proxy);
  g_free (block_device_file);
  if (manager != NULL)
    g_object_unref (manager);
  return ret;
}
