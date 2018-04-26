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

#include "udisksfstabmonitor.h"
#include "udisksfstabentry.h"
#include "udisksprivate.h"
#include "udiskslogging.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udisksfstabmonitor
 * @title: UDisksFstabMonitor
 * @short_description: Monitors entries in the fstab file
 *
 * This type is used for monitoring entries in the
 * <filename>/etc/fstab</filename> file.
 */

/**
 * UDisksFstabMonitor:
 *
 * The #UDisksFstabMonitor structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksFstabMonitor
{
  GObject parent_instance;

  gboolean have_data;
  GList *fstab_entries;

  GFileMonitor *file_monitor;
};

typedef struct _UDisksFstabMonitorClass UDisksFstabMonitorClass;

struct _UDisksFstabMonitorClass
{
  GObjectClass parent_class;

  void (*entry_added)   (UDisksFstabMonitor  *monitor,
                         UDisksFstabEntry    *entry);
  void (*entry_removed) (UDisksFstabMonitor  *monitor,
                         UDisksFstabEntry    *entry);
};

/*--------------------------------------------------------------------------------------------------------------*/

enum
  {
    ENTRY_ADDED_SIGNAL,
    ENTRY_REMOVED_SIGNAL,
    LAST_SIGNAL,
  };

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (UDisksFstabMonitor, udisks_fstab_monitor, G_TYPE_OBJECT)

static void udisks_fstab_monitor_ensure (UDisksFstabMonitor *monitor);
static void udisks_fstab_monitor_invalidate (UDisksFstabMonitor *monitor);
static void udisks_fstab_monitor_constructed (GObject *object);

static void
udisks_fstab_monitor_finalize (GObject *object)
{
  UDisksFstabMonitor *monitor = UDISKS_FSTAB_MONITOR (object);

  g_object_unref (monitor->file_monitor);

  g_list_free_full (monitor->fstab_entries, g_object_unref);

  if (G_OBJECT_CLASS (udisks_fstab_monitor_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_fstab_monitor_parent_class)->finalize (object);
}

static void
udisks_fstab_monitor_init (UDisksFstabMonitor *monitor)
{
  monitor->fstab_entries = NULL;
}

static void
udisks_fstab_monitor_class_init (UDisksFstabMonitorClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize    = udisks_fstab_monitor_finalize;
  gobject_class->constructed = udisks_fstab_monitor_constructed;

  /**
   * UDisksFstabMonitor::entry-added
   * @monitor: A #UDisksFstabMonitor.
   * @entry: The #UDisksFstabEntry that was added.
   *
   * Emitted when a fstab entry is added.
   *
   * This signal is emitted in the
   * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
   * that @monitor was created in.
   */
  signals[ENTRY_ADDED_SIGNAL] = g_signal_new ("entry-added",
                                              G_OBJECT_CLASS_TYPE (klass),
                                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                              G_STRUCT_OFFSET (UDisksFstabMonitorClass, entry_added),
                                              NULL,
                                              NULL,
                                              g_cclosure_marshal_VOID__OBJECT,
                                              G_TYPE_NONE,
                                              1,
                                              UDISKS_TYPE_FSTAB_ENTRY);

  /**
   * UDisksFstabMonitor::entry-removed
   * @monitor: A #UDisksFstabMonitor.
   * @entry: The #UDisksFstabEntry that was removed.
   *
   * Emitted when a fstab entry is removed.
   *
   * This signal is emitted in the
   * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
   * that @monitor was created in.
   */
  signals[ENTRY_REMOVED_SIGNAL] = g_signal_new ("entry-removed",
                                                G_OBJECT_CLASS_TYPE (klass),
                                                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                                G_STRUCT_OFFSET (UDisksFstabMonitorClass, entry_removed),
                                                NULL,
                                                NULL,
                                                g_cclosure_marshal_VOID__OBJECT,
                                                G_TYPE_NONE,
                                                1,
                                                UDISKS_TYPE_FSTAB_ENTRY);
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
reload_fstab_entries (UDisksFstabMonitor *monitor)
{
  GList *old_fstab_entries;
  GList *cur_fstab_entries;
  GList *added;
  GList *removed;
  GList *l;

  udisks_fstab_monitor_ensure (monitor);

  old_fstab_entries = g_list_copy_deep (monitor->fstab_entries, (GCopyFunc) udisks_g_object_ref_copy, NULL);

  udisks_fstab_monitor_invalidate (monitor);
  udisks_fstab_monitor_ensure (monitor);

  cur_fstab_entries = g_list_copy (monitor->fstab_entries);

  old_fstab_entries = g_list_sort (old_fstab_entries, (GCompareFunc) udisks_fstab_entry_compare);
  cur_fstab_entries = g_list_sort (cur_fstab_entries, (GCompareFunc) udisks_fstab_entry_compare);
  diff_sorted_lists (old_fstab_entries, cur_fstab_entries, (GCompareFunc) udisks_fstab_entry_compare, &added, &removed);

  for (l = removed; l != NULL; l = l->next)
    {
      UDisksFstabEntry *entry = UDISKS_FSTAB_ENTRY (l->data);
      g_signal_emit (monitor, signals[ENTRY_REMOVED_SIGNAL], 0, entry);
    }

  for (l = added; l != NULL; l = l->next)
    {
      UDisksFstabEntry *entry = UDISKS_FSTAB_ENTRY (l->data);
      g_signal_emit (monitor, signals[ENTRY_ADDED_SIGNAL], 0, entry);
    }

  g_list_free_full (old_fstab_entries, g_object_unref);
  g_list_free (cur_fstab_entries);
  g_list_free (removed);
  g_list_free (added);
}

static void
on_file_monitor_changed (GFileMonitor      *file_monitor,
                         GFile             *file,
                         GFile             *other_file,
                         GFileMonitorEvent  event_type,
                         gpointer           user_data)
{
  UDisksFstabMonitor *monitor = UDISKS_FSTAB_MONITOR (user_data);
  if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
      event_type == G_FILE_MONITOR_EVENT_CREATED ||
      event_type == G_FILE_MONITOR_EVENT_DELETED)
    {
      udisks_debug ("/etc/fstab changed!");
      reload_fstab_entries (monitor);
    }
}

static void
udisks_fstab_monitor_constructed (GObject *object)
{
  UDisksFstabMonitor *monitor = UDISKS_FSTAB_MONITOR (object);
  GError *error;
  GFile *file;

  file = g_file_new_for_path ("/etc/fstab");
  error = NULL;
  monitor->file_monitor = g_file_monitor_file (file,
                                               G_FILE_MONITOR_NONE,
                                               NULL, /* cancellable */
                                               &error);
  if (monitor->file_monitor == NULL)
    {
      udisks_critical ("Error monitoring /etc/fstab: %s (%s, %d)",
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

  if (G_OBJECT_CLASS (udisks_fstab_monitor_parent_class)->constructed != NULL)
    (*G_OBJECT_CLASS (udisks_fstab_monitor_parent_class)->constructed) (object);
}

/**
 * udisks_fstab_monitor_new:
 *
 * Creates a new #UDisksFstabMonitor object.
 *
 * Signals are emitted in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> that this function is called from.
 *
 * Returns: A #UDisksFstabMonitor. Free with g_object_unref().
 */
UDisksFstabMonitor *
udisks_fstab_monitor_new (void)
{
  return UDISKS_FSTAB_MONITOR (g_object_new (UDISKS_TYPE_FSTAB_MONITOR, NULL));
}

static void
udisks_fstab_monitor_invalidate (UDisksFstabMonitor *monitor)
{
  monitor->have_data = FALSE;

  g_list_free_full (monitor->fstab_entries, g_object_unref);
  monitor->fstab_entries = NULL;
}


/* ---------------------------------------------------------------------------------------------------- */

static gboolean
have_entry (UDisksFstabMonitor *monitor,
            UDisksFstabEntry   *entry)
{
  GList *l;
  gboolean ret;

  ret = FALSE;
  for (l = monitor->fstab_entries; l != NULL; l = l->next)
    {
      UDisksFstabEntry *other_entry = UDISKS_FSTAB_ENTRY (l->data);
      if (udisks_fstab_entry_compare (entry, other_entry) == 0)
        {
          ret = TRUE;
          goto out;
        }
    }
 out:
  return ret;
}

static void
udisks_fstab_monitor_ensure (UDisksFstabMonitor *monitor)
{
  FILE *f;
  char buf[8192];
  struct mntent mbuf;
  struct mntent *m;

  f = NULL;

  if (monitor->have_data)
    goto out;

  f = fopen ("/etc/fstab", "r");
  if (f == NULL)
    {
      if (errno != ENOENT)
        {
          udisks_warning ("Error opening /etc/fstab file: %m");
        }
      goto out;
    }

  while ((m = getmntent_r (f, &mbuf, buf, sizeof (buf))) != NULL)
    {
      UDisksFstabEntry *entry;
      entry = _udisks_fstab_entry_new (m);
      if (!have_entry (monitor, entry))
        {
          monitor->fstab_entries = g_list_prepend (monitor->fstab_entries, entry);
        }
      else
        {
          g_object_unref (entry);
        }
    }

  monitor->have_data = TRUE;

 out:
  if (f != NULL)
    fclose (f);
}

/**
 * udisks_fstab_monitor_get_entries:
 * @monitor: A #UDisksFstabMonitor.
 *
 * Gets all /etc/fstab entries
 *
 * Returns: (transfer full) (element-type UDisksFstabEntry): A list of #UDisksFstabEntry objects that must be freed with g_list_free() after each element has been freed with g_object_unref().
 */
GList *
udisks_fstab_monitor_get_entries (UDisksFstabMonitor  *monitor)
{
  GList *ret;

  g_return_val_if_fail (UDISKS_IS_FSTAB_MONITOR (monitor), NULL);

  udisks_fstab_monitor_ensure (monitor);

  ret = g_list_copy_deep (monitor->fstab_entries, (GCopyFunc) udisks_g_object_ref_copy, NULL);
  return ret;
}
