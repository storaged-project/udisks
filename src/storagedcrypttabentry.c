/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008 David Zeuthen <zeuthen@gmail.com>
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
#include <glib/gi18n-lib.h>

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mntent.h>

#include <glib.h>
#include <glib-object.h>

#include "storagedcrypttabentry.h"
#include "storagedprivate.h"

/**
 * StoragedCrypttabEntry:
 *
 * The #StoragedCrypttabEntry structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedCrypttabEntry
{
  GObject parent_instance;

  gchar *name;
  gchar *device;
  gchar *passphrase_path;
  gchar *options;
};

typedef struct _StoragedCrypttabEntryClass StoragedCrypttabEntryClass;

struct _StoragedCrypttabEntryClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (StoragedCrypttabEntry, storaged_crypttab_entry, G_TYPE_OBJECT);

static void
storaged_crypttab_entry_finalize (GObject *object)
{
  StoragedCrypttabEntry *entry = STORAGED_CRYPTTAB_ENTRY (object);

  g_free (entry->name);
  g_free (entry->device);
  g_free (entry->passphrase_path);
  g_free (entry->options);

  if (G_OBJECT_CLASS (storaged_crypttab_entry_parent_class)->finalize)
    G_OBJECT_CLASS (storaged_crypttab_entry_parent_class)->finalize (object);
}

static void
storaged_crypttab_entry_init (StoragedCrypttabEntry *crypttab_entry)
{
}

static void
storaged_crypttab_entry_class_init (StoragedCrypttabEntryClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = storaged_crypttab_entry_finalize;
}

StoragedCrypttabEntry *
_storaged_crypttab_entry_new (const gchar *name,
                              const gchar *device,
                              const gchar *passphrase_path,
                              const gchar *options)
{
  StoragedCrypttabEntry *entry;

  entry = STORAGED_CRYPTTAB_ENTRY (g_object_new (STORAGED_TYPE_CRYPTTAB_ENTRY, NULL));
  entry->name = g_strdup (name);
  entry->device = g_strdup (device);
  entry->passphrase_path = g_strdup (passphrase_path);
  entry->options = g_strdup (options);

  return entry;
}

/**
 * storaged_crypttab_entry_compare:
 * @entry: A #StoragedCrypttabEntry
 * @other_entry: Another #StoragedCrypttabEntry.
 *
 * Comparison function for comparing two #StoragedCrypttabEntry objects.
 *
 * Returns: Negative value if @entry < @other_entry; zero if @entry = @other_entry; positive value if @entry > @other_entry.
 */
gint
storaged_crypttab_entry_compare (StoragedCrypttabEntry  *entry,
                                 StoragedCrypttabEntry  *other_entry)
{
  gint ret;

  g_return_val_if_fail (STORAGED_IS_CRYPTTAB_ENTRY (entry), 0);
  g_return_val_if_fail (STORAGED_IS_CRYPTTAB_ENTRY (other_entry), 0);

  ret = g_strcmp0 (other_entry->name, entry->name);
  if (ret != 0)
    goto out;

  ret = g_strcmp0 (other_entry->device, entry->device);
  if (ret != 0)
    goto out;

  ret = g_strcmp0 (other_entry->passphrase_path, entry->passphrase_path);
  if (ret != 0)
    goto out;

  ret = g_strcmp0 (other_entry->options, entry->options);

 out:
  return ret;
}

/**
 * storaged_crypttab_entry_get_name:
 * @entry: A #StoragedCrypttabEntry.
 *
 * Gets the name field of @entry.
 *
 * Returns: The name field.
 */
const gchar *
storaged_crypttab_entry_get_name (StoragedCrypttabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_CRYPTTAB_ENTRY (entry), NULL);
  return entry->name;
}

/**
 * storaged_crypttab_entry_get_device:
 * @entry: A #StoragedCrypttabEntry.
 *
 * Gets the device field of @entry.
 *
 * Returns: The device field.
 */
const gchar *
storaged_crypttab_entry_get_device (StoragedCrypttabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_CRYPTTAB_ENTRY (entry), NULL);
  return entry->device;
}

/**
 * storaged_crypttab_entry_get_passphrase_path:
 * @entry: A #StoragedCrypttabEntry.
 *
 * Gets the passphrase path field of @entry.
 *
 * Returns: The passphrase path field.
 */
const gchar *
storaged_crypttab_entry_get_passphrase_path (StoragedCrypttabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_CRYPTTAB_ENTRY (entry), NULL);
  return entry->passphrase_path;
}

/**
 * storaged_crypttab_entry_get_options:
 * @entry: A #StoragedCrypttabEntry.
 *
 * Gets the options field of @entry.
 *
 * Returns: The options field.
 */
const gchar *
storaged_crypttab_entry_get_options (StoragedCrypttabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_CRYPTTAB_ENTRY (entry), NULL);
  return entry->options;
}

