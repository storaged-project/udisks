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

#include <libmount/libmount.h>

#include <glib.h>
#include <glib-object.h>

#include "udisksutabmonitor.h"
#include "udisksutabentry.h"
#include "udisksprivate.h"

/**
 * SECTION:udisksutabmonitor
 * @title: UDisksUtabMonitor
 * @short_description: Monitors entries in the utab file
 *
 * This type is used for monitoring entries in the
 * <filename>/run/mounts/utab</filename> file.
 */

/**
 * UDisksUtabMonitor:
 *
 * The #UDisksUtabMonitor structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksUtabMonitor
{
  GObject parent_instance;

  GIOChannel *utab_channel;
  GSource *utab_watch_source;

  struct libmnt_monitor *mn;

  gboolean have_data;
  GList *utab_entries;
};

typedef struct _UDisksUtabMonitorClass UDisksUtabMonitorClass;

struct _UDisksUtabMonitorClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (UDisksUtabMonitor, udisks_utab_monitor, G_TYPE_OBJECT)

static void udisks_utab_monitor_ensure (UDisksUtabMonitor *monitor);
static void udisks_utab_monitor_invalidate (UDisksUtabMonitor *monitor);
static void udisks_utab_monitor_constructed (GObject *object);

static void
udisks_utab_monitor_init (UDisksUtabMonitor *monitor)
{
  monitor->have_data = FALSE;
  monitor->utab_entries = NULL;
}

static void
udisks_utab_monitor_finalize (GObject *object)
{
  UDisksUtabMonitor *monitor = UDISKS_UTAB_MONITOR (object);

  if (monitor->utab_channel != NULL)
    g_io_channel_unref (monitor->utab_channel);
  if (monitor->utab_watch_source != NULL)
    g_source_destroy (monitor->utab_watch_source);
  if (monitor->mn)
    mnt_unref_monitor (monitor->mn);

  if (G_OBJECT_CLASS (udisks_utab_monitor_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_utab_monitor_parent_class)->finalize (object);
}


static void
udisks_utab_monitor_class_init (UDisksUtabMonitorClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize    = udisks_utab_monitor_finalize;
  gobject_class->constructed = udisks_utab_monitor_constructed;
}

static gboolean
fs_has_user_options (struct libmnt_fs *fs, void *user_data)
{
  return mnt_fs_get_user_options (fs) != NULL;
}

static void
reload_utab_entries (UDisksUtabMonitor *monitor)
{
  udisks_utab_monitor_invalidate (monitor);
  udisks_utab_monitor_ensure (monitor);
}

static gboolean
utab_changed_event (GIOChannel *channel,
                    GIOCondition cond,
                    gpointer user_data)
{
  UDisksUtabMonitor *monitor = UDISKS_UTAB_MONITOR (user_data);
  gboolean need_reload;
  gint r;

  if (cond & ~G_IO_IN)
    return G_SOURCE_CONTINUE;

  need_reload = FALSE;

  do
  {
    const char *filename;
    r = mnt_monitor_next_change (monitor->mn, &filename, NULL);

    if (r == 0)
      need_reload = TRUE;
  } while (r == 0);

  if (need_reload)
    reload_utab_entries (monitor);

  return G_SOURCE_CONTINUE;
}

static void
udisks_utab_monitor_constructed (GObject *object)
{
  UDisksUtabMonitor *monitor = UDISKS_UTAB_MONITOR (object);

  monitor->mn = mnt_new_monitor();
  // Monitor only changes in /run/mount/utab
  mnt_monitor_enable_userspace (monitor->mn, TRUE, NULL);

  monitor->utab_channel = g_io_channel_unix_new (mnt_monitor_get_fd (monitor->mn));
  monitor->utab_watch_source = g_io_create_watch (monitor->utab_channel, G_IO_IN);
  g_source_set_callback (monitor->utab_watch_source, (GSourceFunc) utab_changed_event, monitor, NULL);
  g_source_attach (monitor->utab_watch_source, g_main_context_get_thread_default ());
  g_source_unref (monitor->utab_watch_source);

  if (G_OBJECT_CLASS (udisks_utab_monitor_parent_class)->constructed != NULL)
    (*G_OBJECT_CLASS (udisks_utab_monitor_parent_class)->constructed) (object);
}

/**
 * udisks_utab_monitor_new:
 *
 * Creates a new #UDisksUtabMonitor object.
 *
 * Signals are emitted in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> that this function is called from.
 *
 * Returns: A #UDisksUtabMonitor. Free with g_object_unref().
 */
UDisksUtabMonitor *
udisks_utab_monitor_new (void)
{
  return UDISKS_UTAB_MONITOR (g_object_new (UDISKS_TYPE_UTAB_MONITOR, NULL));
}

/**
 * udisks_utab_monitor_get_entries:
 * @monitor: A #UDisksUtabMonitor.
 *
 * Gets all /run/mounts/utab entries
 *
 * Returns: (transfer full) (element-type UDisksUtabEntry): A list of #UDisksUtabEntry objects that must be freed with g_list_free() after each element has been freed with g_object_unref().
 */
GList *
udisks_utab_monitor_get_entries (UDisksUtabMonitor  *monitor)
{
  GList *ret;

  g_return_val_if_fail (UDISKS_IS_UTAB_MONITOR (monitor), NULL);

  udisks_utab_monitor_ensure (monitor);

  ret = g_list_copy (monitor->utab_entries);
  g_list_foreach (ret, (GFunc) g_object_ref, NULL);
  return ret;
}

static void
udisks_utab_monitor_ensure (UDisksUtabMonitor *monitor)
{
  struct libmnt_table *tb;
  struct libmnt_iter *itr;
  struct libmnt_fs *fs;
  int r;

  if (monitor->have_data)
    return;

  tb = mnt_new_table ();
  r = mnt_table_parse_mtab (tb, NULL);

  itr = mnt_new_iter (MNT_ITER_FORWARD);
  while(mnt_table_find_next_fs (tb, itr, fs_has_user_options, NULL, &fs) == 0)
  {
    UDisksUtabEntry *entry;
    entry = _udisks_utab_entry_new (fs);
    monitor->utab_entries = g_list_prepend (monitor->utab_entries, entry);
  }
  mnt_free_iter (itr);

  monitor->have_data = TRUE;
}

static void
udisks_utab_monitor_invalidate (UDisksUtabMonitor *monitor)
{
  monitor->have_data = FALSE;

  g_list_foreach (monitor->utab_entries, (GFunc) g_object_unref, NULL);
  g_list_free (monitor->utab_entries);
  monitor->utab_entries = NULL;
}
