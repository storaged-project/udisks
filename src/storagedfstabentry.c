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

#include "storagedfstabentry.h"
#include "storagedprivate.h"

/**
 * StoragedFstabEntry:
 *
 * The #StoragedFstabEntry structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedFstabEntry
{
  GObject parent_instance;

  gchar *fsname;
  gchar *dir;
  gchar *type;
  gchar *opts;
  gint freq;
  gint passno;
};

typedef struct _StoragedFstabEntryClass StoragedFstabEntryClass;

struct _StoragedFstabEntryClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (StoragedFstabEntry, storaged_fstab_entry, G_TYPE_OBJECT);

static void
storaged_fstab_entry_finalize (GObject *object)
{
  StoragedFstabEntry *entry = STORAGED_FSTAB_ENTRY (object);

  g_free (entry->fsname);
  g_free (entry->dir);
  g_free (entry->type);
  g_free (entry->opts);

  if (G_OBJECT_CLASS (storaged_fstab_entry_parent_class)->finalize)
    G_OBJECT_CLASS (storaged_fstab_entry_parent_class)->finalize (object);
}

static void
storaged_fstab_entry_init (StoragedFstabEntry *fstab_entry)
{
}

static void
storaged_fstab_entry_class_init (StoragedFstabEntryClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = storaged_fstab_entry_finalize;
}

StoragedFstabEntry *
_storaged_fstab_entry_new (const struct mntent *mntent)
{
  StoragedFstabEntry *entry;

  entry = STORAGED_FSTAB_ENTRY (g_object_new (STORAGED_TYPE_FSTAB_ENTRY, NULL));
  entry->fsname = g_strdup (mntent->mnt_fsname);
  entry->dir = g_strdup (mntent->mnt_dir);
  entry->type = g_strdup (mntent->mnt_type);
  entry->opts = g_strdup (mntent->mnt_opts);
  entry->freq = mntent->mnt_freq;
  entry->passno = mntent->mnt_passno;

  return entry;
}

/**
 * storaged_fstab_entry_compare:
 * @entry: A #StoragedFstabEntry
 * @other_entry: Another #StoragedFstabEntry.
 *
 * Comparison function for comparing two #StoragedFstabEntry objects.
 *
 * Returns: Negative value if @entry < @other_entry; zero if @entry = @other_entry; positive value if @entry > @other_entry.
 */
gint
storaged_fstab_entry_compare (StoragedFstabEntry  *entry,
                              StoragedFstabEntry  *other_entry)
{
  gint ret;

  g_return_val_if_fail (STORAGED_IS_FSTAB_ENTRY (entry), 0);
  g_return_val_if_fail (STORAGED_IS_FSTAB_ENTRY (other_entry), 0);

  ret = g_strcmp0 (other_entry->fsname, entry->fsname);
  if (ret != 0)
    goto out;

  ret = g_strcmp0 (other_entry->dir, entry->dir);
  if (ret != 0)
    goto out;

  ret = g_strcmp0 (other_entry->type, entry->type);
  if (ret != 0)
    goto out;

  ret = g_strcmp0 (other_entry->opts, entry->opts);
  if (ret != 0)
    goto out;

  ret = entry->freq - other_entry->freq;
  if (ret != 0)
    goto out;

  ret = entry->passno - other_entry->passno;

 out:
  return ret;
}

/**
 * storaged_fstab_entry_get_fsname:
 * @entry: A #StoragedFstabEntry.
 *
 * Gets the fsname field of @entry.
 *
 * Returns: The fsname field.
 */
const gchar *
storaged_fstab_entry_get_fsname (StoragedFstabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_FSTAB_ENTRY (entry), NULL);
  return entry->fsname;
}

/**
 * storaged_fstab_entry_get_dir:
 * @entry: A #StoragedFstabEntry.
 *
 * Gets the dir field of @entry.
 *
 * Returns: The dir field.
 */
const gchar *
storaged_fstab_entry_get_dir (StoragedFstabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_FSTAB_ENTRY (entry), NULL);
  return entry->dir;
}

/**
 * storaged_fstab_entry_get_fstype:
 * @entry: A #StoragedFstabEntry.
 *
 * Gets the type field of @entry.
 *
 * Returns: The type field.
 */
const gchar *
storaged_fstab_entry_get_fstype (StoragedFstabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_FSTAB_ENTRY (entry), NULL);
  return entry->type;
}

/**
 * storaged_fstab_entry_get_opts:
 * @entry: A #StoragedFstabEntry.
 *
 * Gets the opts field of @entry.
 *
 * Returns: The opts field.
 */
const gchar *
storaged_fstab_entry_get_opts (StoragedFstabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_FSTAB_ENTRY (entry), NULL);
  return entry->opts;
}

/**
 * storaged_fstab_entry_get_freq:
 * @entry: A #StoragedFstabEntry.
 *
 * Gets the freq field of @entry.
 *
 * Returns: The freq field.
 */
gint
storaged_fstab_entry_get_freq (StoragedFstabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_FSTAB_ENTRY (entry), 0);
  return entry->freq;
}

/**
 * storaged_fstab_entry_get_passno:
 * @entry: A #StoragedFstabEntry.
 *
 * Gets the passno field of @entry.
 *
 * Returns: The passno field.
 */
gint
storaged_fstab_entry_get_passno (StoragedFstabEntry *entry)
{
  g_return_val_if_fail (STORAGED_IS_FSTAB_ENTRY (entry), 0);
  return entry->passno;
}
