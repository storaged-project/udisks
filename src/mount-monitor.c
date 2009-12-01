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

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <mntent.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>

#include "mount-monitor.h"
#include "mount.h"
#include "private.h"

/*--------------------------------------------------------------------------------------------------------------*/

enum
  {
    MOUNT_ADDED_SIGNAL,
    MOUNT_REMOVED_SIGNAL,
    LAST_SIGNAL,
  };

static guint signals[LAST_SIGNAL] = { 0 };

struct MountMonitorPrivate
{
  GIOChannel *mounts_channel;
  gboolean have_data;
  GList *mounts;
};

G_DEFINE_TYPE (MountMonitor, mount_monitor, G_TYPE_OBJECT)

static void mount_monitor_ensure (MountMonitor *monitor);

static void
mount_monitor_finalize (GObject *object)
{
  MountMonitor *monitor = MOUNT_MONITOR (object);

  if (monitor->priv->mounts_channel != NULL)
    g_io_channel_unref (monitor->priv->mounts_channel);

  g_list_foreach (monitor->priv->mounts, (GFunc) g_object_unref, NULL);
  g_list_free (monitor->priv->mounts);

  if (G_OBJECT_CLASS (mount_monitor_parent_class)->finalize != NULL)
    (*G_OBJECT_CLASS (mount_monitor_parent_class)->finalize) (object);
}

static void
mount_monitor_init (MountMonitor *monitor)
{
  monitor->priv = G_TYPE_INSTANCE_GET_PRIVATE (monitor, TYPE_MOUNT_MONITOR, MountMonitorPrivate);

  monitor->priv->mounts = NULL;
}

static void
mount_monitor_class_init (MountMonitorClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = mount_monitor_finalize;

  g_type_class_add_private (klass, sizeof(MountMonitorPrivate));

  /**
   * MountMonitor::mount-added
   * @monitor: A #MountMonitor.
   * @mount: The #Mount that was added.
   *
   * Emitted when a mount is added.
   */
  signals[MOUNT_ADDED_SIGNAL] = g_signal_new ("mount-added",
                                              G_OBJECT_CLASS_TYPE (klass),
                                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                              G_STRUCT_OFFSET (MountMonitorClass, mount_added),
                                              NULL,
                                              NULL,
                                              g_cclosure_marshal_VOID__OBJECT,
                                              G_TYPE_NONE,
                                              1,
                                              TYPE_MOUNT);

  /**
   * MountMonitor::mount-removed
   * @monitor: A #MountMonitor.
   * @mount: The #Mount that was removed.
   *
   * Emitted when a mount is removed.
   */
  signals[MOUNT_REMOVED_SIGNAL] = g_signal_new ("mount-removed",
                                                G_OBJECT_CLASS_TYPE (klass),
                                                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                                G_STRUCT_OFFSET (MountMonitorClass, mount_removed),
                                                NULL,
                                                NULL,
                                                g_cclosure_marshal_VOID__OBJECT,
                                                G_TYPE_NONE,
                                                1,
                                                TYPE_MOUNT);
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

static gboolean
mounts_changed_event (GIOChannel *channel,
                      GIOCondition cond,
                      gpointer user_data)
{
  MountMonitor *monitor = MOUNT_MONITOR (user_data);
  GList *old_mounts;
  GList *cur_mounts;
  GList *added;
  GList *removed;
  GList *l;

  if (cond & ~G_IO_ERR)
    goto out;

  g_print ("**** /proc/self/mountinfo changed\n");

  mount_monitor_ensure (monitor);

  old_mounts = g_list_copy (monitor->priv->mounts);
  g_list_foreach (old_mounts, (GFunc) g_object_ref, NULL);

  mount_monitor_invalidate (monitor);
  mount_monitor_ensure (monitor);

  cur_mounts = g_list_copy (monitor->priv->mounts);

  old_mounts = g_list_sort (old_mounts, (GCompareFunc) mount_compare);
  cur_mounts = g_list_sort (cur_mounts, (GCompareFunc) mount_compare);

  diff_sorted_lists (old_mounts, cur_mounts, (GCompareFunc) mount_compare, &added, &removed);

  for (l = removed; l != NULL; l = l->next)
    {
      Mount *mount = MOUNT (l->data);
      g_signal_emit (monitor, signals[MOUNT_REMOVED_SIGNAL], 0, mount);
    }

  for (l = added; l != NULL; l = l->next)
    {
      Mount *mount = MOUNT (l->data);
      g_signal_emit (monitor, signals[MOUNT_ADDED_SIGNAL], 0, mount);
    }

  g_list_foreach (old_mounts, (GFunc) g_object_unref, NULL);
  g_list_free (old_mounts);
  g_list_free (cur_mounts);
  g_list_free (removed);
  g_list_free (added);

 out:
  return TRUE;
}

MountMonitor *
mount_monitor_new (void)
{
  MountMonitor *mount_monitor;
  GError *error;

  mount_monitor = MOUNT_MONITOR (g_object_new (TYPE_MOUNT_MONITOR, NULL));

  error = NULL;
  mount_monitor->priv->mounts_channel = g_io_channel_new_file ("/proc/self/mountinfo", "r", &error);
  if (mount_monitor->priv->mounts_channel != NULL)
    {
      g_io_add_watch (mount_monitor->priv->mounts_channel, G_IO_ERR, mounts_changed_event, mount_monitor);
    }
  else
    {
      g_warning ("No /proc/self/mountinfo file: %s", error->message);
      g_error_free (error);
      g_object_unref (mount_monitor);
      mount_monitor = NULL;
      goto out;
    }

 out:
  return mount_monitor;
}

void
mount_monitor_invalidate (MountMonitor *monitor)
{
  monitor->priv->have_data = FALSE;

  g_list_foreach (monitor->priv->mounts, (GFunc) g_object_unref, NULL);
  g_list_free (monitor->priv->mounts);
  monitor->priv->mounts = NULL;
}

static gboolean
have_mount (MountMonitor *monitor,
            dev_t dev,
            const gchar *mount_point)
{
  GList *l;
  gboolean ret;

  ret = FALSE;

  for (l = monitor->priv->mounts; l != NULL; l = l->next)
    {
      Mount *mount = MOUNT (l->data);
      if (mount_get_dev (mount) == dev && g_strcmp0 (mount_get_mount_path (mount), mount_point) == 0)
        {
          ret = TRUE;
          break;
        }
    }

  return ret;
}

static void
mount_monitor_ensure (MountMonitor *monitor)
{
  gchar *contents;
  gchar **lines;
  GError *error;
  guint n;

  contents = NULL;
  lines = NULL;

  if (monitor->priv->have_data)
    goto out;

  error = NULL;
  if (!g_file_get_contents ("/proc/self/mountinfo", &contents, NULL, &error))
    {
      g_warning ("Error reading /proc/self/mountinfo: %s", error->message);
      g_error_free (error);
      goto out;
    }

  /* See Documentation/filesystems/proc.txt for the format of /proc/self/mountinfo
   *
   * Note that things like space are encoded as \020.
   */

  lines = g_strsplit (contents, "\n", 0);
  for (n = 0; lines[n] != NULL; n++)
    {
      guint mount_id;
      guint parent_id;
      guint major, minor;
      gchar encoded_root[PATH_MAX];
      gchar encoded_mount_point[PATH_MAX];
      gchar *mount_point;
      dev_t dev;

      if (strlen (lines[n]) == 0)
        continue;

      if (sscanf (lines[n],
                  "%d %d %d:%d %s %s",
                  &mount_id,
                  &parent_id,
                  &major,
                  &minor,
                  encoded_root,
                  encoded_mount_point) != 6)
        {
          g_warning ("Error parsing line '%s'", lines[n]);
          continue;
        }

      /* ignore mounts where only a subtree of a filesystem is mounted */
      if (g_strcmp0 (encoded_root, "/") != 0)
        continue;

      /* Temporary work-around for btrfs, see
       *
       *  https://bugzilla.redhat.com/show_bug.cgi?id=495152#c31
       *  http://article.gmane.org/gmane.comp.file-systems.btrfs/2851
       *
       * for details.
       */
      if (major == 0)
        {
          const gchar *sep;
          sep = strstr (lines[n], " - ");
          if (sep != NULL)
            {
              gchar fstype[PATH_MAX];
              gchar mount_source[PATH_MAX];
              struct stat statbuf;

              if (sscanf (sep + 3, "%s %s", fstype, mount_source) != 2)
                {
                  g_warning ("Error parsing things past - for '%s'", lines[n]);
                  continue;
                }

              if (g_strcmp0 (fstype, "btrfs") != 0)
                continue;

              if (!g_str_has_prefix (mount_source, "/dev/"))
                continue;

              if (stat (mount_source, &statbuf) != 0)
                {
                  g_warning ("Error statting %s: %m", mount_source);
                  continue;
                }

              if (!S_ISBLK (statbuf.st_mode))
                {
                  g_warning ("%s is not a block device", mount_source);
                  continue;
                }

              dev = statbuf.st_rdev;
            }
          else
            {
              continue;
            }
        }
      else
        {
          dev = makedev (major, minor);
        }

      mount_point = g_strcompress (encoded_mount_point);

      /* TODO: we can probably use a hash table or something if this turns out to be slow */
      if (!have_mount (monitor, dev, mount_point))
        {
          Mount *mount;
          mount = _mount_new (dev, mount_point);
          monitor->priv->mounts = g_list_prepend (monitor->priv->mounts, mount);
          //g_debug ("SUP ADDING %d:%d on %s", major, minor, mount_point);
        }

      g_free (mount_point);
    }

  monitor->priv->have_data = TRUE;

 out:
  g_free (contents);
  g_strfreev (lines);
}

GList *
mount_monitor_get_mounts_for_dev (MountMonitor *monitor,
                                  dev_t dev)
{
  GList *ret;
  GList *l;

  ret = NULL;

  mount_monitor_ensure (monitor);

  for (l = monitor->priv->mounts; l != NULL; l = l->next)
    {
      Mount *mount = MOUNT (l->data);

      if (mount_get_dev (mount) == dev)
        {
          ret = g_list_prepend (ret, g_object_ref (mount));
        }
    }

  /* Sort the list to ensure that shortest mount paths appear first */
  ret = g_list_sort (ret, (GCompareFunc) mount_compare);

  return ret;
}
