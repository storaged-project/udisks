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

#include "devkit-disks-mount.h"
#include "devkit-disks-private.h"

/*--------------------------------------------------------------------------------------------------------------*/

struct DevkitDisksMountPrivate
{
        gchar *mount_path;
        gchar *device_file;
};

G_DEFINE_TYPE (DevkitDisksMount, devkit_disks_mount, G_TYPE_OBJECT)

#define DEVKIT_DISKS_MOUNT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_TYPE_DISKS_MOUNT, DevkitDisksMountPrivate))

static void
devkit_disks_mount_finalize (GObject *object)
{
        DevkitDisksMount *mount = DEVKIT_DISKS_MOUNT (object);

        g_free (mount->priv->mount_path);
        g_free (mount->priv->device_file);

        if (G_OBJECT_CLASS (devkit_disks_mount_parent_class)->finalize)
                (* G_OBJECT_CLASS (devkit_disks_mount_parent_class)->finalize) (object);
}

static void
devkit_disks_mount_init (DevkitDisksMount *mount)
{
        mount->priv = DEVKIT_DISKS_MOUNT_GET_PRIVATE (mount);
}

static void
devkit_disks_mount_class_init (DevkitDisksMountClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;

        obj_class->finalize = devkit_disks_mount_finalize;

        g_type_class_add_private (klass, sizeof (DevkitDisksMountPrivate));
}


DevkitDisksMount *
_devkit_disks_mount_new (const gchar *device_file, const gchar *mount_path)
{
        DevkitDisksMount *mount;

        mount = DEVKIT_DISKS_MOUNT (g_object_new (DEVKIT_TYPE_DISKS_MOUNT, NULL));
        mount->priv->device_file = g_strdup (device_file);
        mount->priv->mount_path = g_strdup (mount_path);

        return mount;
}

const gchar *
devkit_disks_mount_get_mount_path (DevkitDisksMount *mount)
{
        g_return_val_if_fail (DEVKIT_IS_DISKS_MOUNT (mount), NULL);
        return mount->priv->mount_path;
}

const gchar *
devkit_disks_mount_get_device_file (DevkitDisksMount *mount)
{
        g_return_val_if_fail (DEVKIT_IS_DISKS_MOUNT (mount), NULL);
        return mount->priv->device_file;
}

gint
devkit_disks_mount_compare (DevkitDisksMount *a,
                            DevkitDisksMount *b)
{
        gint ret;

        ret = g_strcmp0 (a->priv->mount_path, b->priv->mount_path);
        if (ret != 0)
                goto out;

        ret = g_strcmp0 (a->priv->device_file, b->priv->device_file);

 out:
        return ret;
}
