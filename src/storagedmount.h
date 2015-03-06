/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2010 David Zeuthen <zeuthen@gmail.com>
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

#ifndef __STORAGED_MOUNT_H__
#define __STORAGED_MOUNT_H__

#include "storageddaemontypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_MOUNT         (storaged_mount_get_type ())
#define STORAGED_MOUNT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_MOUNT, StoragedMount))
#define STORAGED_IS_MOUNT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_MOUNT))

GType              storaged_mount_get_type       (void) G_GNUC_CONST;
StoragedMountType  storaged_mount_get_mount_type (StoragedMount *mount);
const gchar       *storaged_mount_get_mount_path (StoragedMount *mount);
dev_t              storaged_mount_get_dev        (StoragedMount *mount);
gint               storaged_mount_compare        (StoragedMount *mount,
                                                  StoragedMount *other_mount);

G_END_DECLS

#endif /* __STORAGED_MOUNT_H__ */
