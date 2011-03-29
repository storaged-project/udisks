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

#include "config.h"
#include <glib/gi18n.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <udisks/udisks.h>

static GDBusObject *
lookup_object_for_block_device_file (UDisksClient  *client,
                                     const gchar   *block_device_file)
{
  GDBusObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObject *object = G_DBUS_OBJECT (l->data);
      UDisksBlockDevice *block;

      block = UDISKS_PEEK_BLOCK_DEVICE (object);
      if (block != NULL)
        {
          const gchar * const *symlinks;
          guint n;

          if (g_strcmp0 (udisks_block_device_get_device (block), block_device_file) == 0)
            {
              ret = g_object_ref (object);
              goto out;
            }

          symlinks = udisks_block_device_get_symlinks (block);
          for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
            {
              if (g_strcmp0 (symlinks[n], block_device_file) == 0)
                {
                  ret = g_object_ref (object);
                  goto out;
                }
            }
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);

  return ret;
}

int
main (int argc, char *argv[])
{
  gint ret;
  gchar *block_device_file;
  UDisksClient *client;
  GError *error;
  struct stat statbuf;
  GDBusObject *object;
  UDisksFilesystem *filesystem;
  const gchar *unmount_options[1] = {NULL};

  ret = 1;
  client = NULL;
  block_device_file = NULL;
  object = NULL;

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
  client = udisks_client_new_sync (NULL, /* GCancellable */
                                   &error);
  if (client == NULL)
    {
      g_printerr ("Error connecting to the udisks daemon: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  object = lookup_object_for_block_device_file (client, block_device_file);
  if (object == NULL)
    {
      g_printerr ("Error finding object for block device file %s\n", block_device_file);
      goto out;
    }

  filesystem = UDISKS_PEEK_FILESYSTEM (object);
  if (filesystem == NULL)
    {
      g_printerr ("Block device file %s is not a mountable filesystem.\n", block_device_file);
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
  if (object != NULL)
    g_object_unref (object);
  g_free (block_device_file);
  if (client != NULL)
    g_object_unref (client);
  return ret;
}
