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

/*--------------------------------------------------------------------------------------------------------------*/

enum
{
        MOUNTS_CHANGED_SIGNAL,
        LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

struct DevkitDisksMountMonitorPrivate
{
	GIOChannel              *mounts_channel;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (DevkitDisksMountMonitor, devkit_disks_mount_monitor, G_TYPE_OBJECT)

#define DEVKIT_DISKS_MOUNT_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_TYPE_DISKS_MOUNT_MONITOR, DevkitDisksMountMonitorPrivate))

static void
devkit_disks_mount_monitor_finalize (DevkitDisksMountMonitor *mount_monitor)
{
        if (mount_monitor->priv->mounts_channel != NULL)
                g_io_channel_unref (mount_monitor->priv->mounts_channel);
        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (mount_monitor));
}

static void
devkit_disks_mount_monitor_init (DevkitDisksMountMonitor *mount_monitor)
{
        mount_monitor->priv = DEVKIT_DISKS_MOUNT_MONITOR_GET_PRIVATE (mount_monitor);
}

static void
devkit_disks_mount_monitor_class_init (DevkitDisksMountMonitorClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) devkit_disks_mount_monitor_finalize;

        g_type_class_add_private (klass, sizeof (DevkitDisksMountMonitorPrivate));

        signals[MOUNTS_CHANGED_SIGNAL] =
                g_signal_new ("mounts-changed",
                              G_OBJECT_CLASS_TYPE (klass),
                              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

static gboolean
mounts_changed_event (GIOChannel *channel, GIOCondition cond, gpointer user_data)
{
        DevkitDisksMountMonitor *mount_monitor = DEVKIT_DISKS_MOUNT_MONITOR (user_data);

	if (cond & ~G_IO_ERR)
                goto out;

        g_signal_emit (mount_monitor, signals[MOUNTS_CHANGED_SIGNAL], 0);

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

struct DevkitDisksMount {
        char *mount_path;
        char *device_path;
};

void
devkit_disks_mount_unref (DevkitDisksMount *mount)
{
        g_free (mount->mount_path);
        g_free (mount->device_path);
        g_free (mount);
}

const char *
devkit_disks_mount_get_mount_path (DevkitDisksMount *mount)
{
        return mount->mount_path;
}

const char *
devkit_disks_mount_get_device_path (DevkitDisksMount *mount)
{
        return mount->device_path;
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
                g_warning ("major=%d minor=%d", major (statbuf.st_rdev), minor (statbuf.st_rdev));
                if (major (statbuf.st_rdev) == 253) {
                        return g_strdup_printf ("/dev/dm-%d", minor (statbuf.st_rdev));
                }
        }

        return NULL;
}

GList *
devkit_disks_mount_monitor_get_mounts (DevkitDisksMountMonitor *mount_monitor)
{
        GList *ret;
        struct mntent *m;
        FILE *f;

        /* TODO: cache this list */

        ret = NULL;

        f = fopen ("/proc/mounts", "r");
        if (f == NULL) {
                g_warning ("error opening /proc/mounts: %m");
                goto out;
        }
        while ((m = getmntent (f)) != NULL) {
                DevkitDisksMount *mount;
                mount = g_new0 (DevkitDisksMount, 1);
                if (strcmp (m->mnt_fsname, "/dev/root") == 0) {
                        mount->device_path = g_strdup (_resolve_dev_root ());
                } else {
                        mount->device_path = g_strdup (m->mnt_fsname);
                }

                if (g_str_has_prefix (mount->device_path, "/dev/mapper/")) {
                        char *s;
                        s = _check_lvm (m->mnt_fsname);
                        if (s != NULL) {
                                g_free (mount->device_path);
                                mount->device_path = s;
                        }
                }

                mount->mount_path = g_strdup (m->mnt_dir);
                ret = g_list_prepend (ret, mount);
        }
        fclose (f);

        ret = g_list_reverse (ret);

out:
        return ret;
}
