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

#include <glib.h>
#include <glib-object.h>

#include "udiskscrypttabmonitor.h"
#include "udiskscrypttabentry.h"
#include "udisksprivate.h"
#include "udiskslogging.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskscrypttabmonitor
 * @title: UDisksCrypttabMonitor
 * @short_description: Monitors entries in the crypttab file
 *
 * This type is used for monitoring entries in the
 * <filename>/etc/crypttab</filename> file.
 */

/**
 * UDisksCrypttabMonitor:
 *
 * The #UDisksCrypttabMonitor structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksCrypttabMonitor
{
  GObject parent_instance;

  GList *crypttab_entries;
  GMutex crypttab_entries_mutex;

  gchar *crypttab_checksum;

  GFileMonitor *file_monitor;
};

typedef struct _UDisksCrypttabMonitorClass UDisksCrypttabMonitorClass;

struct _UDisksCrypttabMonitorClass
{
  GObjectClass parent_class;

  void (*entry_added)   (UDisksCrypttabMonitor  *monitor,
                         UDisksCrypttabEntry    *entry);
  void (*entry_removed) (UDisksCrypttabMonitor  *monitor,
                         UDisksCrypttabEntry    *entry);
};

/*--------------------------------------------------------------------------------------------------------------*/

#define CRYPPTAB_FILENAME "/etc/crypttab"

enum
  {
    ENTRY_ADDED_SIGNAL,
    ENTRY_REMOVED_SIGNAL,
    LAST_SIGNAL,
  };

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (UDisksCrypttabMonitor, udisks_crypttab_monitor, G_TYPE_OBJECT)

static void udisks_crypttab_monitor_ensure (UDisksCrypttabMonitor *monitor);
static void udisks_crypttab_monitor_constructed (GObject *object);

static void
udisks_crypttab_monitor_finalize (GObject *object)
{
  UDisksCrypttabMonitor *monitor = UDISKS_CRYPTTAB_MONITOR (object);

  g_object_unref (monitor->file_monitor);
  g_free (monitor->crypttab_checksum);

  g_list_free_full (monitor->crypttab_entries, g_object_unref);

  g_mutex_clear (&monitor->crypttab_entries_mutex);

  if (G_OBJECT_CLASS (udisks_crypttab_monitor_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_crypttab_monitor_parent_class)->finalize (object);
}

static void
udisks_crypttab_monitor_init (UDisksCrypttabMonitor *monitor)
{
  monitor->crypttab_entries = NULL;
  g_mutex_init (&monitor->crypttab_entries_mutex);
}

static void
udisks_crypttab_monitor_class_init (UDisksCrypttabMonitorClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize    = udisks_crypttab_monitor_finalize;
  gobject_class->constructed = udisks_crypttab_monitor_constructed;

  /**
   * UDisksCrypttabMonitor::entry-added
   * @monitor: A #UDisksCrypttabMonitor.
   * @entry: The #UDisksCrypttabEntry that was added.
   *
   * Emitted when a crypttab entry is added.
   *
   * This signal is emitted in the
   * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
   * that @monitor was created in.
   */
  signals[ENTRY_ADDED_SIGNAL] = g_signal_new ("entry-added",
                                              G_OBJECT_CLASS_TYPE (klass),
                                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                              G_STRUCT_OFFSET (UDisksCrypttabMonitorClass, entry_added),
                                              NULL,
                                              NULL,
                                              g_cclosure_marshal_VOID__OBJECT,
                                              G_TYPE_NONE,
                                              1,
                                              UDISKS_TYPE_CRYPTTAB_ENTRY);

  /**
   * UDisksCrypttabMonitor::entry-removed
   * @monitor: A #UDisksCrypttabMonitor.
   * @entry: The #UDisksCrypttabEntry that was removed.
   *
   * Emitted when a crypttab entry is removed.
   *
   * This signal is emitted in the
   * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
   * that @monitor was created in.
   */
  signals[ENTRY_REMOVED_SIGNAL] = g_signal_new ("entry-removed",
                                                G_OBJECT_CLASS_TYPE (klass),
                                                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                                G_STRUCT_OFFSET (UDisksCrypttabMonitorClass, entry_removed),
                                                NULL,
                                                NULL,
                                                g_cclosure_marshal_VOID__OBJECT,
                                                G_TYPE_NONE,
                                                1,
                                                UDISKS_TYPE_CRYPTTAB_ENTRY);
}

static void
diff_sorted_lists (GList *list1,
                   GList *list2,
                   GCompareFunc compare,
                   GList **added,
                   GList **removed)
{
  int order;

  *added = *removed = NULL;

  while (list1 != NULL && list2 != NULL)
    {
      order = (*compare) (list1->data, list2->data);
      if (order < 0)
        {
          *removed = g_list_prepend (*removed, list1->data);
          list1 = list1->next;
        }
      else if (order > 0)
        {
          *added = g_list_prepend (*added, list2->data);
          list2 = list2->next;
        }
      else
        { /* same item */
          list1 = list1->next;
          list2 = list2->next;
        }
    }

  while (list1 != NULL)
    {
      *removed = g_list_prepend (*removed, list1->data);
      list1 = list1->next;
    }
  while (list2 != NULL)
    {
      *added = g_list_prepend (*added, list2->data);
      list2 = list2->next;
    }
}

static void
on_file_monitor_changed (GFileMonitor      *file_monitor,
                         GFile             *file,
                         GFile             *other_file,
                         GFileMonitorEvent  event_type,
                         gpointer           user_data)
{
  UDisksCrypttabMonitor *monitor = UDISKS_CRYPTTAB_MONITOR (user_data);
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
      event_type == G_FILE_MONITOR_EVENT_CREATED ||
      event_type == G_FILE_MONITOR_EVENT_DELETED)
    {
      udisks_debug (CRYPPTAB_FILENAME " changed!");
      udisks_crypttab_monitor_ensure (monitor);
    }
}

static void
udisks_crypttab_monitor_constructed (GObject *object)
{
  UDisksCrypttabMonitor *monitor = UDISKS_CRYPTTAB_MONITOR (object);
  GError *error;
  GFile *file;

  file = g_file_new_for_path (CRYPPTAB_FILENAME);
  error = NULL;
  monitor->file_monitor = g_file_monitor_file (file,
                                               G_FILE_MONITOR_NONE,
                                               NULL, /* cancellable */
                                               &error);
  if (monitor->file_monitor == NULL)
    {
      udisks_critical ("Error monitoring " CRYPPTAB_FILENAME ": %s (%s, %d)",
                    error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  else
    {
      g_signal_connect (monitor->file_monitor,
                        "changed",
                        G_CALLBACK (on_file_monitor_changed),
                        monitor);
    }
  g_object_unref (file);

  if (G_OBJECT_CLASS (udisks_crypttab_monitor_parent_class)->constructed != NULL)
    (*G_OBJECT_CLASS (udisks_crypttab_monitor_parent_class)->constructed) (object);
}

/**
 * udisks_crypttab_monitor_new:
 *
 * Creates a new #UDisksCrypttabMonitor object.
 *
 * Signals are emitted in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> that this function is called from.
 *
 * Returns: A #UDisksCrypttabMonitor. Free with g_object_unref().
 */
UDisksCrypttabMonitor *
udisks_crypttab_monitor_new (void)
{
  return UDISKS_CRYPTTAB_MONITOR (g_object_new (UDISKS_TYPE_CRYPTTAB_MONITOR, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksCrypttabMonitor *monitor;
  UDisksCrypttabEntry *entry;
  guint signal_id;
} CrypttabEntryChangedData;

static gboolean
crypttab_entry_changed_cb (gpointer user_data)
{
  CrypttabEntryChangedData *data = user_data;

  g_signal_emit (data->monitor, signals[data->signal_id], 0, data->entry);

  return G_SOURCE_REMOVE;
}

static void
free_crypttab_entry_changed_data (gpointer user_data)
{
  CrypttabEntryChangedData *data = user_data;

  g_object_unref (data->entry);
  g_free (data);
}

/**
 * split_crypttab_line:
 *
 * Splits line from /etc/crypttab to parts (device, name, password and options).
 * This function also handles columns with multiple delimiters (tabs or spaces).
 *
 */
static gchar **
split_crypttab_line (const gchar *line, guint *num_tokens)
{
  GSList *list = NULL;
  gint next = 0;
  gchar **tokens = NULL;
  gchar *line_copy = NULL;
  gchar *str1, *str2, *token, *subtoken;
  gchar *saveptr1, *saveptr2;

  /* strtok will modify the line, so we need to make a copy */
  line_copy = g_strdup (line);

  for (str1 = line_copy; ; str1 = NULL)
    {
      token = strtok_r (str1, " ", &saveptr1);
      if (token == NULL)
          break;
      for (str2 = token; ; str2 = NULL)
        {
            subtoken = strtok_r (str2, "\t", &saveptr2);
            if (subtoken == NULL)
                break;
            list = g_slist_append (list, g_strdup (subtoken));
        }
     }

  *num_tokens = g_slist_length (list);
  tokens = g_new0 (gchar *, *num_tokens + 1);
  for (GSList *list_p = list; list_p != NULL; list_p = list_p->next)
    tokens[next++] = list_p->data;
  tokens[next] = NULL;

  g_slist_free (list);
  g_free (line_copy);

  return tokens;
}

static void
udisks_crypttab_monitor_ensure (UDisksCrypttabMonitor *monitor)
{
  GList *entries;
  GError *error = NULL;
  gchar *contents = NULL;
  gsize contents_len = 0;
  gchar **lines = NULL;
  gchar *contents_checksum = NULL;
  guint n;
  GList *added;
  GList *removed;
  GList *l;

  /* Compare cache validity by matching contents checksum */
  g_mutex_lock (&monitor->crypttab_entries_mutex);

  /* Read the contents */
  if (!g_file_get_contents (CRYPPTAB_FILENAME,
                            &contents,
                            &contents_len,
                            &error))
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          udisks_warning ("Error opening " CRYPPTAB_FILENAME ": %s (%s, %d)",
                          error->message, g_quark_to_string (error->domain), error->code);
        }
      g_clear_error (&error);
      goto out;
    }

  contents_checksum = g_compute_checksum_for_data (G_CHECKSUM_SHA1, (const guchar *) contents, contents_len);
  if (g_strcmp0 (contents_checksum, monitor->crypttab_checksum) == 0)
    {
      goto out;
    }

  /* Parse the contents */
  entries = NULL;
  lines = g_strsplit (contents, "\n", 0);
  for (n = 0; lines != NULL && lines[n] != NULL; n++)
    {
      gchar **tokens;
      guint num_tokens;
      UDisksCrypttabEntry *entry;

      const gchar *line = lines[n];
      if (strlen (line) == 0)
        continue;
      if (line[0] == '#')
        continue;

      tokens = split_crypttab_line (line, &num_tokens);
      if (num_tokens > 1)
        {
          entry = _udisks_crypttab_entry_new (tokens[0],
                                              tokens[1],
                                              num_tokens >= 3 ? tokens[2] : NULL,
                                              num_tokens >= 4 ? tokens[3] : NULL);
          entries = g_list_prepend (entries, entry);
        }
      else
        {
          udisks_warning ("Line %u of " CRYPPTAB_FILENAME " only contains %u tokens", n, num_tokens);
        }
      g_strfreev (tokens);
    }

  /* Compare and emit changes */
  diff_sorted_lists (monitor->crypttab_entries, entries, (GCompareFunc) udisks_crypttab_entry_compare, &added, &removed);

  for (l = removed; l != NULL; l = l->next)
    {
      CrypttabEntryChangedData *data;

      data = g_new0 (CrypttabEntryChangedData, 1);
      data->monitor = monitor;
      data->signal_id = ENTRY_REMOVED_SIGNAL;
      data->entry = g_object_ref (UDISKS_CRYPTTAB_ENTRY (l->data));
      g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, crypttab_entry_changed_cb, data, free_crypttab_entry_changed_data);
    }

  for (l = added; l != NULL; l = l->next)
    {
      CrypttabEntryChangedData *data;

      data = g_new0 (CrypttabEntryChangedData, 1);
      data->monitor = monitor;
      data->signal_id = ENTRY_ADDED_SIGNAL;
      data->entry = g_object_ref (UDISKS_CRYPTTAB_ENTRY (l->data));
      g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, crypttab_entry_changed_cb, data, free_crypttab_entry_changed_data);
    }

  g_list_free (removed);
  g_list_free (added);
  g_list_free_full (monitor->crypttab_entries, g_object_unref);

  monitor->crypttab_entries = entries;
  g_free (monitor->crypttab_checksum);
  monitor->crypttab_checksum = g_steal_pointer (&contents_checksum);

 out:
  g_mutex_unlock (&monitor->crypttab_entries_mutex);
  g_free (contents);
  g_free (contents_checksum);
  g_strfreev (lines);
}

/**
 * udisks_crypttab_monitor_get_entries:
 * @monitor: A #UDisksCrypttabMonitor.
 *
 * Gets all /etc/crypttab entries
 *
 * Returns: (transfer full) (element-type UDisksCrypttabEntry): A list of #UDisksCrypttabEntry objects that must be freed with g_list_free() after each element has been freed with g_object_unref().
 */
GList *
udisks_crypttab_monitor_get_entries (UDisksCrypttabMonitor  *monitor)
{
  GList *ret;

  g_return_val_if_fail (UDISKS_IS_CRYPTTAB_MONITOR (monitor), NULL);

  udisks_crypttab_monitor_ensure (monitor);

  g_mutex_lock (&monitor->crypttab_entries_mutex);
  ret = g_list_copy_deep (monitor->crypttab_entries, (GCopyFunc) udisks_g_object_ref_copy, NULL);
  g_mutex_unlock (&monitor->crypttab_entries_mutex);

  return ret;
}
