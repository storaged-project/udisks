/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2017 Andrea Azzarone <andrea.azzarone@canonical.com>
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

#include <glib.h>
#include <glib-object.h>

#include "udisksutabentry.h"
#include "udisksprivate.h"

/**
 * UDisksUtabEntry:
 *
 * The #UDisksUtabEntry structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksUtabEntry
{
  GObject parent_instance;

  gchar  *source;
  gchar **opts;
};

typedef struct _UDisksUtabEntryClass UDisksUtabEntryClass;

struct _UDisksUtabEntryClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (UDisksUtabEntry, udisks_utab_entry, G_TYPE_OBJECT);

static void
udisks_utab_entry_init (UDisksUtabEntry *entry)
{
  entry->source = NULL;
  entry->opts = NULL;
}

static void
udisks_utab_entry_finalize (GObject *object)
{
  UDisksUtabEntry *entry = UDISKS_UTAB_ENTRY (object);

  g_free (entry->source);
  g_strfreev (entry->opts);

  if (G_OBJECT_CLASS (udisks_utab_entry_parent_class)->finalize)
    G_OBJECT_CLASS (udisks_utab_entry_parent_class)->finalize (object);
}

static void
udisks_utab_entry_class_init (UDisksUtabEntryClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = udisks_utab_entry_finalize;
}


UDisksUtabEntry *
_udisks_utab_entry_new (struct libmnt_fs *fs)
{
  UDisksUtabEntry *entry;

  entry = UDISKS_UTAB_ENTRY (g_object_new (UDISKS_TYPE_UTAB_ENTRY, NULL));

  entry->source = g_strdup (mnt_fs_get_source (fs));
  entry->opts = g_strsplit (mnt_fs_get_user_options (fs), ",", -1);

  return entry;
}

/**
 * udisks_utab_entry_get_source:
 * @entry: A #UDisksUtabEntry.
 *
 * Gets the source field of @entry.
 *
 * Returns: The source field.
 */
const gchar *
udisks_utab_entry_get_source (UDisksUtabEntry *entry)
{
  g_return_val_if_fail (UDISKS_IS_UTAB_ENTRY (entry), NULL);
  return entry->source;
}

/**
 * udisks_utab_entry_get_opts:
 * @entry: A #UDisksUtabEntry.
 *
 * Gets the opts field of @entry.
 *
 * Returns: The opts field.
 */
const gchar * const *
udisks_utab_entry_get_opts (UDisksUtabEntry *entry)
{
  g_return_val_if_fail (UDISKS_IS_UTAB_ENTRY (entry), NULL);
  return (const gchar * const *) entry->opts;
}
