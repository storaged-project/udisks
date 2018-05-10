/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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
#include <sys/sysmacros.h>
#include <unistd.h>
#include <string.h>

#include <udisks/udisks.h>

static UDisksObject *
lookup_object_for_block (UDisksClient  *client,
                         dev_t          block_device)
{
  UDisksObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block != NULL)
        {
          if (block_device == udisks_block_get_device_number (block))
            {
              ret = g_object_ref (object);
              goto out;
            }
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);

  return ret;
}

int
main (int argc, char *argv[])
{
  gint ret;
  dev_t block_device;
  UDisksClient *client;
  GError *error;
  struct stat statbuf;
  UDisksObject *object;
  UDisksFilesystem *filesystem;
  GVariantBuilder builder;

  ret = 1;
  client = NULL;
  object = NULL;

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
    block_device = statbuf.st_rdev;
  else
    block_device = statbuf.st_dev;

  error = NULL;
  client = udisks_client_new_sync (NULL, /* GCancellable */
                                   &error);
  if (client == NULL)
    {
      g_printerr ("Error connecting to the udisks daemon: %s\n", error->message);
      g_clear_error (&error);
      goto out;
    }

  object = lookup_object_for_block (client, block_device);
  if (object == NULL)
    {
      g_printerr ("Error finding object for block device %u:%u\n", major (block_device), minor (block_device));
      goto out;
    }

  filesystem = udisks_object_peek_filesystem (object);
  if (filesystem == NULL)
    {
      g_printerr ("Block device %u:%u is not a mountable filesystem.\n", major (block_device), minor (block_device));
      goto out;
    }

  error = NULL;
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  if (!udisks_filesystem_call_unmount_sync (filesystem,
                                            g_variant_builder_end (&builder), /* options */
                                            NULL, /* GCancellable */
                                            &error))
    {
      g_printerr ("Error unmounting block device %u:%u: %s\n", major (block_device), minor (block_device), error->message);
      g_clear_error (&error);
      goto out;
    }

  ret = 0;

 out:
  if (object != NULL)
    g_object_unref (object);
  if (client != NULL)
    g_object_unref (client);
  return ret;
}
