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

  GRWLock lock;

  GIOChannel *utab_channel;
  GSource *utab_watch_source;

  struct libmnt_monitor *mn;
  struct libmnt_table *current_tb;
};

typedef struct _UDisksUtabMonitorClass UDisksUtabMonitorClass;

struct _UDisksUtabMonitorClass
{
  GObjectClass parent_class;

  void (*entry_added)   (UDisksUtabMonitor *monitor,
                         UDisksUtabEntry   *entry);
  void (*entry_removed) (UDisksUtabMonitor *monitor,
                         UDisksUtabEntry   *entry);
};

enum
{
  ENTRY_ADDED_SIGNAL,
  ENTRY_REMOVED_SIGNAL,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (UDisksUtabMonitor, udisks_utab_monitor, G_TYPE_OBJECT)

static void udisks_utab_monitor_ensure (UDisksUtabMonitor *monitor);
static void udisks_utab_monitor_invalidate (UDisksUtabMonitor *monitor);
static void udisks_utab_monitor_constructed (GObject *object);

static void
udisks_utab_monitor_init (UDisksUtabMonitor *monitor)
{
  g_rw_lock_init (&monitor->lock);
  monitor->utab_channel = NULL;
  monitor->utab_watch_source = NULL;
  monitor->mn = NULL;
  monitor->current_tb = NULL;
}

static void
udisks_utab_monitor_finalize (GObject *object)
{
  UDisksUtabMonitor *monitor = UDISKS_UTAB_MONITOR (object);

  g_rw_lock_clear (&monitor->lock);
  if (monitor->utab_channel != NULL)
    g_io_channel_unref (monitor->utab_channel);
  if (monitor->utab_watch_source != NULL)
    g_source_destroy (monitor->utab_watch_source);
  if (monitor->mn)
    mnt_unref_monitor (monitor->mn);
  if (monitor->current_tb)
    mnt_free_table (monitor->current_tb);

  if (G_OBJECT_CLASS (udisks_utab_monitor_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_utab_monitor_parent_class)->finalize (object);
}


static void
udisks_utab_monitor_class_init (UDisksUtabMonitorClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize    = udisks_utab_monitor_finalize;
  gobject_class->constructed = udisks_utab_monitor_constructed;

  /**
   * UDisksUtabMonitor::entry-added
   * @monitor: A #UDisksUtabMonitor.
   * @entry: The #UDisksUtabEntry that was added.
   *
   * Emitted when a utab entry is added.
   *
   * This signal is emitted in the
   * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
   * that @monitor was created in.
   */
  signals[ENTRY_ADDED_SIGNAL] = g_signal_new ("entry-added",
                                              G_OBJECT_CLASS_TYPE (klass),
                                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                              G_STRUCT_OFFSET (UDisksUtabMonitorClass, entry_added),
                                              NULL,
                                              NULL,
                                              g_cclosure_marshal_VOID__OBJECT,
                                              G_TYPE_NONE,
                                              1,
                                              UDISKS_TYPE_UTAB_ENTRY);

  /**
   * UDisksFstabMonitor::entry-removed
   * @monitor: A #UDisksUtabMonitor.
   * @entry: The #UDisksUtabEntry that was removed.
   *
   * Emitted when a utab entry is removed.
   *
   * This signal is emitted in the
   * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
   * that @monitor was created in.
   */
  signals[ENTRY_REMOVED_SIGNAL] = g_signal_new ("entry-removed",
                                                G_OBJECT_CLASS_TYPE (klass),
                                                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                                G_STRUCT_OFFSET (UDisksUtabMonitorClass, entry_removed),
                                                NULL,
                                                NULL,
                                                g_cclosure_marshal_VOID__OBJECT,
                                                G_TYPE_NONE,
                                                1,
                                                UDISKS_TYPE_UTAB_ENTRY);
}

static gboolean
fs_has_user_options (struct libmnt_fs *fs)
{
  return mnt_fs_get_user_options (fs) != NULL;
}

static gboolean
fs_has_user_options_match_func (struct libmnt_fs *fs, void *data)
{
  return fs_has_user_options (fs);
}

static void
reload_utab_entries (UDisksUtabMonitor *monitor)
{
  struct libmnt_table *old_tb;
  struct libmnt_tabdiff *diff = NULL;
  struct libmnt_iter *itr;
  struct libmnt_fs *old, *new;
  int rc = -1, change;

  g_rw_lock_writer_lock (&monitor->lock);
  udisks_utab_monitor_ensure (monitor);
  old_tb = monitor->current_tb;
  mnt_ref_table (old_tb);
  g_rw_lock_writer_unlock (&monitor->lock);

  g_rw_lock_writer_lock (&monitor->lock);
  udisks_utab_monitor_invalidate (monitor);
  udisks_utab_monitor_ensure (monitor);
  g_rw_lock_writer_unlock (&monitor->lock);

  g_rw_lock_reader_lock (&monitor->lock);
  diff = mnt_new_tabdiff ();
  itr = mnt_new_iter (MNT_ITER_FORWARD);
  g_rw_lock_reader_unlock (&monitor->lock);

  if (!old_tb || !monitor->current_tb || !diff || !itr)
    goto out;

  rc = mnt_diff_tables (diff, old_tb, monitor->current_tb);

  if (rc < 0)
    goto out;

  while (mnt_tabdiff_next_change (diff, itr, &old, &new, &change) == 0)
    {
      if (!fs_has_user_options (old) && !fs_has_user_options (new))
        continue;

      switch (change)
        {
          case MNT_TABDIFF_UMOUNT:
            if (fs_has_user_options (old))
              {
                g_autoptr (UDisksUtabEntry) entry = _udisks_utab_entry_new (old);
                g_signal_emit (monitor, signals[ENTRY_REMOVED_SIGNAL], 0, entry);
              }
            break;
          case MNT_TABDIFF_REMOUNT:
            if (fs_has_user_options (old))
              {
                g_autoptr (UDisksUtabEntry) entry = _udisks_utab_entry_new (old);
                g_signal_emit (monitor, signals[ENTRY_REMOVED_SIGNAL], 0, entry);
              }
            if (fs_has_user_options (new))
              {
                g_autoptr (UDisksUtabEntry) entry = _udisks_utab_entry_new (new);
                g_signal_emit (monitor, signals[ENTRY_ADDED_SIGNAL], 0, entry);
              }
            break;
          case MNT_TABDIFF_MOUNT:
              if (fs_has_user_options (new))
              {
                g_autoptr (UDisksUtabEntry) entry = _udisks_utab_entry_new (new);
                g_signal_emit (monitor, signals[ENTRY_ADDED_SIGNAL], 0, entry);
              }
            break;
          }
    }

out:
  if (old_tb)
    mnt_unref_table (old_tb);
  if (diff)
    mnt_free_tabdiff (diff);
  if (itr)
    mnt_free_iter (itr);
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
      r = mnt_monitor_next_change (monitor->mn, NULL, NULL);

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
#if __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
/* parameters of the callback depend on the source and can be different
 * from the required "generic" GSourceFunc, see:
 * https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#g-source-set-callback
 */
  g_source_set_callback (monitor->utab_watch_source, (GSourceFunc) utab_changed_event, monitor, NULL);
#if __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif
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
 * Returns: (transfer full) (element-type UDisksUtabEntry): A list of #UDisksUtabEntry objects that must be freed with g_slist_free() after each element has been freed with g_object_unref().
 */
GSList *
udisks_utab_monitor_get_entries (UDisksUtabMonitor  *monitor)
{
  GSList *ret;
  struct libmnt_iter *itr;
  struct libmnt_fs *fs;

  g_return_val_if_fail (UDISKS_IS_UTAB_MONITOR (monitor), NULL);

  g_rw_lock_writer_lock (&monitor->lock);
  udisks_utab_monitor_ensure (monitor);
  g_rw_lock_writer_unlock (&monitor->lock);

  g_rw_lock_reader_lock (&monitor->lock);

  ret = NULL;
  itr = mnt_new_iter (MNT_ITER_FORWARD);
  while (mnt_table_find_next_fs (monitor->current_tb, itr, fs_has_user_options_match_func, NULL, &fs) == 0)
    {
      UDisksUtabEntry *entry;
      entry = _udisks_utab_entry_new (fs);
      ret = g_slist_prepend (ret, entry);
    }
  mnt_free_iter (itr);

  g_rw_lock_reader_unlock (&monitor->lock);

  return ret;
}

static void
udisks_utab_monitor_ensure (UDisksUtabMonitor *monitor)
{
  if (monitor->current_tb)
    return;

  monitor->current_tb = mnt_new_table ();
  mnt_table_parse_mtab (monitor->current_tb, NULL);
}

static void
udisks_utab_monitor_invalidate (UDisksUtabMonitor *monitor)
{
  if (!monitor->current_tb)
    return;

  mnt_unref_table (monitor->current_tb);
  monitor->current_tb = NULL;
}
