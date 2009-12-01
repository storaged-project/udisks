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

#ifndef __MOUNT_H__
#define __MOUNT_H__

#include <sys/types.h>

#include "types.h"

G_BEGIN_DECLS

#define TYPE_MOUNT         (mount_get_type ())
#define MOUNT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_MOUNT, Mount))
#define MOUNT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_MOUNT, MountClass))
#define IS_MOUNT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_MOUNT))
#define IS_MOUNT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_MOUNT))
#define MOUNT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_MOUNT, MountClass))

typedef struct MountClass MountClass;
typedef struct MountPrivate MountPrivate;

struct Mount
{
  GObject parent;
  MountPrivate *priv;
};

struct MountClass
{
  GObjectClass parent_class;
};

GType mount_get_type (void) G_GNUC_CONST;
const gchar * mount_get_mount_path (Mount *mount);
dev_t mount_get_dev (Mount *mount);

gint mount_compare (Mount *a, Mount *b);

G_END_DECLS

#endif /* __MOUNT_H__ */
