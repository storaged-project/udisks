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

#include "storagedmount.h"
#include "storagedprivate.h"

/**
 * StoragedMount:
 *
 * The #StoragedMount structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedMount
{
  GObject parent_instance;

  gchar *mount_path;
  dev_t dev;
  StoragedMountType type;
};

typedef struct _StoragedMountClass StoragedMountClass;

struct _StoragedMountClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (StoragedMount, storaged_mount, G_TYPE_OBJECT);

static void
storaged_mount_finalize (GObject *object)
{
  StoragedMount *mount = STORAGED_MOUNT (object);

  g_free (mount->mount_path);

  if (G_OBJECT_CLASS (storaged_mount_parent_class)->finalize)
    G_OBJECT_CLASS (storaged_mount_parent_class)->finalize (object);
}

static void
storaged_mount_init (StoragedMount *mount)
{
}

static void
storaged_mount_class_init (StoragedMountClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = storaged_mount_finalize;
}

StoragedMount *
_storaged_mount_new (dev_t             dev,
                     const gchar      *mount_path,
                     StoragedMountType type)
{
  StoragedMount *mount;

  mount = STORAGED_MOUNT (g_object_new (STORAGED_TYPE_MOUNT, NULL));
  mount->dev = dev;
  mount->mount_path = g_strdup (mount_path);
  mount->type = type;

  return mount;
}

/**
 * storaged_mount_get_mount_path:
 * @mount: A #StoragedMount
 *
 * Gets the mount path for a #STORAGED_MOUNT_TYPE_FILESYSTEM<!-- -->-type mount.
 *
 * It is a programming error to call this on any other type of #StoragedMount.
 *
 * Returns: A string owned by @mount. Do not free.
 */
const gchar *
storaged_mount_get_mount_path (StoragedMount *mount)
{
  g_return_val_if_fail (STORAGED_IS_MOUNT (mount), NULL);
  g_return_val_if_fail (mount->type == STORAGED_MOUNT_TYPE_FILESYSTEM, NULL);
  return mount->mount_path;
}

/**
 * storaged_mount_get_dev:
 * @mount: A #StoragedMount.
 *
 * Gets the device number for @mount.
 *
 * Returns: A #dev_t.
 */
dev_t
storaged_mount_get_dev (StoragedMount *mount)
{
  g_return_val_if_fail (STORAGED_IS_MOUNT (mount), 0);
  return mount->dev;
}

/**
 * storaged_mount_compare:
 * @mount: A #StoragedMount
 * @other_mount: Another #StoragedMount.
 *
 * Comparison function for comparing two #StoragedMount objects.
 *
 * Returns: Negative value if @mount < @other_mount; zero if @mount = @other_mount; positive value if @mount > @other_mount.
 */
gint
storaged_mount_compare (StoragedMount  *mount,
                        StoragedMount  *other_mount)
{
  gint ret;

  g_return_val_if_fail (STORAGED_IS_MOUNT (mount), 0);
  g_return_val_if_fail (STORAGED_IS_MOUNT (other_mount), 0);

  ret = g_strcmp0 (mount->mount_path, other_mount->mount_path);
  if (ret != 0)
    goto out;

  ret = (other_mount->dev - mount->dev);
  if (ret != 0)
    goto out;

  ret = other_mount->type - mount->type;

 out:
  return ret;
}

/**
 * storaged_mount_get_mount_type:
 * @mount: A #StoragedMount.
 *
 * Gets the #StoragedMountType for @mount.
 *
 * Returns: A value from the #StoragedMountType enumeration.
 */
StoragedMountType
storaged_mount_get_mount_type (StoragedMount *mount)
{
  return mount->type;
}
