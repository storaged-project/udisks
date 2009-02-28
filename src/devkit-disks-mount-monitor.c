/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#include "devkit-disks-mount-monitor.h"
#include "devkit-disks-mount.h"
#include "devkit-disks-private.h"

/*--------------------------------------------------------------------------------------------------------------*/

enum
{
        MOUNTED_SIGNAL,
        UNMOUNTED_SIGNAL,
        LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

struct DevkitDisksMountMonitorPrivate
{
	GIOChannel *mounts_channel;
        GHashTable *mounts;
        gboolean    have_data;
};

G_DEFINE_TYPE (DevkitDisksMountMonitor, devkit_disks_mount_monitor, G_TYPE_OBJECT)

static void devkit_disks_mount_monitor_ensure (DevkitDisksMountMonitor *monitor);

static void
devkit_disks_mount_monitor_finalize (GObject *object)
{
        DevkitDisksMountMonitor *monitor = DEVKIT_DISKS_MOUNT_MONITOR (object);

        if (monitor->priv->mounts_channel != NULL)
                g_io_channel_unref (monitor->priv->mounts_channel);

        g_hash_table_unref (monitor->priv->mounts);

        if (G_OBJECT_CLASS (devkit_disks_mount_monitor_parent_class)->finalize != NULL)
                (* G_OBJECT_CLASS (devkit_disks_mount_monitor_parent_class)->finalize) (object);
}

static void
devkit_disks_mount_monitor_init (DevkitDisksMountMonitor *monitor)
{
        monitor->priv = G_TYPE_INSTANCE_GET_PRIVATE (monitor,
                                                     DEVKIT_TYPE_DISKS_MOUNT_MONITOR,
                                                     DevkitDisksMountMonitorPrivate);

        monitor->priv->mounts = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       g_object_unref);
}

static void
devkit_disks_mount_monitor_class_init (DevkitDisksMountMonitorClass *klass)
{
        GObjectClass *gobject_class = (GObjectClass *) klass;

        gobject_class->finalize = devkit_disks_mount_monitor_finalize;

        g_type_class_add_private (klass, sizeof (DevkitDisksMountMonitorPrivate));

        /**
         * DevkitDisksMountMonitor::mounted
         * @monitor: A #DevkitDisksMountMonitor.
         * @mount: The #DevkitDisksMount that was mounted.
         *
         * Emitted when a filesystem is mounted.
         */
        signals[MOUNTED_SIGNAL] =
                g_signal_new ("mounted",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE,
                              1,
                              DEVKIT_TYPE_DISKS_MOUNT);

        /**
         * DevkitDisksMountMonitor::unmounted
         * @monitor: A #DevkitDisksMountMonitor.
         * @mount: The #DevkitDisksMount that was unmounted.
         *
         * Emitted when a filesystem is unmounted.
         */
        signals[UNMOUNTED_SIGNAL] =
                g_signal_new ("unmounted",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE,
                              1,
                              DEVKIT_TYPE_DISKS_MOUNT);
}

static void
diff_sorted_lists (GList         *list1,
                   GList         *list2,
                   GCompareFunc   compare,
                   GList        **added,
                   GList        **removed)
{
  int order;

  *added = *removed = NULL;

  while (list1 != NULL &&
         list2 != NULL)
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
mounts_changed_event (GIOChannel *channel, GIOCondition cond, gpointer user_data)
{
        DevkitDisksMountMonitor *monitor = DEVKIT_DISKS_MOUNT_MONITOR (user_data);
        GList *old_mounts;
        GList *cur_mounts;
        GList *added;
        GList *removed;
        GList *l;

	if (cond & ~G_IO_ERR)
                goto out;

        devkit_disks_mount_monitor_ensure (monitor);
        old_mounts = g_hash_table_get_values (monitor->priv->mounts);
        g_list_foreach (old_mounts, (GFunc) g_object_ref, NULL);

        devkit_disks_mount_monitor_invalidate (monitor);
        devkit_disks_mount_monitor_ensure (monitor);

        cur_mounts = g_hash_table_get_values (monitor->priv->mounts);

        old_mounts = g_list_sort (old_mounts, (GCompareFunc) devkit_disks_mount_compare);
        cur_mounts = g_list_sort (cur_mounts, (GCompareFunc) devkit_disks_mount_compare);

        diff_sorted_lists (old_mounts,
                           cur_mounts,
                           (GCompareFunc) devkit_disks_mount_compare,
                           &added,
                           &removed);

        for (l = removed; l != NULL; l = l->next) {
                DevkitDisksMount *mount = DEVKIT_DISKS_MOUNT (l->data);
                g_signal_emit (monitor,
                               signals[UNMOUNTED_SIGNAL],
                               0,
                               mount);
        }

        for (l = added; l != NULL; l = l->next) {
                DevkitDisksMount *mount = DEVKIT_DISKS_MOUNT (l->data);
                g_signal_emit (monitor,
                               signals[MOUNTED_SIGNAL],
                               0,
                               mount);
        }


        g_list_foreach (old_mounts, (GFunc) g_object_unref, NULL);
        g_list_free (old_mounts);
        g_list_free (cur_mounts);
        g_list_free (removed);
        g_list_free (added);

out:
	return TRUE;
}

DevkitDisksMountMonitor *
devkit_disks_mount_monitor_new (void)
{
        DevkitDisksMountMonitor *mount_monitor;
        GError *error;

        mount_monitor = DEVKIT_DISKS_MOUNT_MONITOR (g_object_new (DEVKIT_TYPE_DISKS_MOUNT_MONITOR, NULL));

        error = NULL;
	mount_monitor->priv->mounts_channel = g_io_channel_new_file ("/proc/mounts", "r", &error);
	if (mount_monitor->priv->mounts_channel != NULL) {
		g_io_add_watch (mount_monitor->priv->mounts_channel, G_IO_ERR, mounts_changed_event, mount_monitor);
	} else {
                g_warning ("No /proc/mounts file: %s", error->message);
                g_error_free (error);
                g_object_unref (mount_monitor);
                mount_monitor = NULL;
                goto out;
	}

out:
        return mount_monitor;
}

static const char *
_resolve_dev_root (void)
{
        static gboolean have_real_dev_root = FALSE;
        static char real_dev_root[256];
        struct stat statbuf;

        /* see if it's cached already */
        if (have_real_dev_root)
                goto found;

        /* otherwise we're going to find it right away.. */
        have_real_dev_root = TRUE;

        if (stat ("/dev/root", &statbuf) == 0) {
                if (! S_ISLNK (statbuf.st_mode)) {
                        dev_t root_dev = statbuf.st_dev;
                        FILE *f;
                        char buf[1024];

                        /* see if device with similar major:minor as /dev/root is mentioned
                         * in /etc/mtab (it usually is)
                         */
                        f = fopen ("/etc/mtab", "r");
                        if (f != NULL) {
                                struct mntent *entp;
                                struct mntent ent;
                                while ((entp = getmntent_r (f, &ent, buf, sizeof (buf))) != NULL) {
                                        if (stat (entp->mnt_fsname, &statbuf) == 0 &&
                                            statbuf.st_dev == root_dev) {
                                                strncpy (real_dev_root, entp->mnt_fsname, sizeof (real_dev_root) - 1);
                                                real_dev_root[sizeof (real_dev_root) - 1] = '\0';
                                                fclose (f);
                                                goto found;
                                        }
                                }
                                endmntent (f);
                        }
                        /* no, that didn't work.. next we could scan /dev ... but I digress.. */
                } else {
                        char resolved[PATH_MAX];
                        if (realpath ("/dev/root", resolved) != NULL) {
                                strncpy (real_dev_root, resolved, sizeof (real_dev_root) - 1);
                                real_dev_root[sizeof (real_dev_root) - 1] = '\0';
                                goto found;
                        }
                }
        }

        /* bah sucks.. */
        strcpy (real_dev_root, "/dev/root");
found:
        return real_dev_root;
}

/* device-mapper likes to create it's own device nodes a'la /dev/mapper/VolGroup00-LogVol00;
 * this is pretty useless... So check the major/minor and map back to /dev/dm-<minor>
 * if major==253.
 */
static char *
_check_lvm (const char *device_path)
{
        struct stat statbuf;
        if (stat (device_path, &statbuf) == 0) {
                if (major (statbuf.st_rdev) == 253) {
                        /* TODO: should check that /dev/dm-%d exists and has same major/minor */
                        return g_strdup_printf ("/dev/dm-%d", minor (statbuf.st_rdev));
                }
        }

        return NULL;
}

void
devkit_disks_mount_monitor_invalidate (DevkitDisksMountMonitor *monitor)
{
        monitor->priv->have_data = FALSE;
        g_hash_table_remove_all (monitor->priv->mounts);
}

static void
devkit_disks_mount_monitor_ensure (DevkitDisksMountMonitor *monitor)
{
        struct mntent *m;
        FILE *f;

        if (monitor->priv->have_data)
                goto out;

        f = fopen ("/proc/mounts", "r");
        if (f == NULL) {
                g_warning ("error opening /proc/mounts: %m");
                goto out;
        }
        while ((m = getmntent (f)) != NULL) {
                DevkitDisksMount *mount;
                const gchar *device_file;
                gchar *s;
                gchar real_path[PATH_MAX];

                /* ignore if not an absolute patch */
                if (m->mnt_fsname[0] != '/')
                        continue;

                /* canonicalize */
                realpath (m->mnt_fsname, real_path);
                if (strcmp (real_path, "/dev/root") == 0) {
                        device_file = _resolve_dev_root ();
                } else {
                        device_file = real_path;
                }

                s = NULL;
                if (g_str_has_prefix (device_file, "/dev/mapper/")) {
                        s = _check_lvm (m->mnt_fsname);
                        if (s != NULL) {
                                device_file = s;
                        }
                }

                mount = _devkit_disks_mount_new (device_file, m->mnt_dir);

                g_free (s);

                g_hash_table_insert (monitor->priv->mounts,
                                     g_strdup (device_file),
                                     mount);
        }
        fclose (f);

        monitor->priv->have_data = TRUE;

out:
        ;
}

DevkitDisksMount *
devkit_disks_mount_monitor_get_mount_for_device_file (DevkitDisksMountMonitor *monitor,
                                                      const gchar             *device_file)
{
        devkit_disks_mount_monitor_ensure (monitor);

        return g_hash_table_lookup (monitor->priv->mounts, device_file);
}
