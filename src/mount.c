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

#include "mount.h"
#include "private.h"

/*--------------------------------------------------------------------------------------------------------------*/

struct MountPrivate
{
  gchar *mount_path;
  dev_t dev;
};

G_DEFINE_TYPE (Mount, mount, G_TYPE_OBJECT)

#define MOUNT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TYPE_MOUNT, MountPrivate))

static void
mount_finalize (GObject *object)
{
  Mount *mount = MOUNT (object);

  g_free (mount->priv->mount_path);

  if (G_OBJECT_CLASS (mount_parent_class)->finalize)
    (* G_OBJECT_CLASS (mount_parent_class)->finalize) (object);
}

static void
mount_init (Mount *mount)
{
  mount->priv = MOUNT_GET_PRIVATE (mount);
}

static void
mount_class_init (MountClass *klass)
{
  GObjectClass *obj_class = (GObjectClass *) klass;

  obj_class->finalize = mount_finalize;

  g_type_class_add_private (klass, sizeof(MountPrivate));
}

Mount *
_mount_new (dev_t dev,
            const gchar *mount_path)
{
  Mount *mount;

  mount = MOUNT (g_object_new (TYPE_MOUNT, NULL));
  mount->priv->dev = dev;
  mount->priv->mount_path = g_strdup (mount_path);

  return mount;
}

const gchar *
mount_get_mount_path (Mount *mount)
{
  g_return_val_if_fail (IS_MOUNT (mount), NULL);
  return mount->priv->mount_path;
}

dev_t
mount_get_dev (Mount *mount)
{
  g_return_val_if_fail (IS_MOUNT (mount), 0);
  return mount->priv->dev;
}

gint
mount_compare (Mount *a,
               Mount *b)
{
  gint ret;

  ret = g_strcmp0 (b->priv->mount_path, a->priv->mount_path);
  if (ret != 0)
    goto out;

  ret = (a->priv->dev - b->priv->dev);

 out:
  return ret;
}
