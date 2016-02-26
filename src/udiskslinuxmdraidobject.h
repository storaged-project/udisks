/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#ifndef __UDISKS_LINUX_MDRAID_OBJECT_H__
#define __UDISKS_LINUX_MDRAID_OBJECT_H__

#include "udisksdaemontypes.h"
#include <gudev/gudev.h>

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_MDRAID_OBJECT  (udisks_linux_mdraid_object_get_type ())
#define UDISKS_LINUX_MDRAID_OBJECT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_MDRAID_OBJECT, UDisksLinuxMDRaidObject))
#define UDISKS_IS_LINUX_MDRAID_OBJECT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_MDRAID_OBJECT))

GType                    udisks_linux_mdraid_object_get_type      (void) G_GNUC_CONST;
UDisksLinuxMDRaidObject *udisks_linux_mdraid_object_new           (UDisksDaemon              *daemon,
                                                                   const gchar               *uuid);
void                     udisks_linux_mdraid_object_uevent        (UDisksLinuxMDRaidObject   *object,
                                                                   const gchar               *action,
                                                                   UDisksLinuxDevice         *device,
                                                                   gboolean                   is_member);
const gchar             *udisks_linux_mdraid_object_get_uuid      (UDisksLinuxMDRaidObject   *object);
UDisksDaemon            *udisks_linux_mdraid_object_get_daemon    (UDisksLinuxMDRaidObject   *object);
GList                   *udisks_linux_mdraid_object_get_members   (UDisksLinuxMDRaidObject   *object);
UDisksLinuxDevice       *udisks_linux_mdraid_object_get_device    (UDisksLinuxMDRaidObject   *object);

gboolean                 udisks_linux_mdraid_object_have_devices  (UDisksLinuxMDRaidObject   *object);

UDisksBaseJob             *udisks_linux_mdraid_object_get_sync_job  (UDisksLinuxMDRaidObject   *object);
gboolean                   udisks_linux_mdraid_object_set_sync_job  (UDisksLinuxMDRaidObject   *object,
                                                                     UDisksBaseJob             *job);
gboolean                   udisks_linux_mdraid_object_complete_sync_job (UDisksLinuxMDRaidObject *object,
                                                                         gboolean                 success,
                                                                         const gchar             *message);
gboolean                   udisks_linux_mdraid_object_has_sync_job      (UDisksLinuxMDRaidObject *object);

G_END_DECLS

#endif /* __UDISKS_LINUX_MDRAID_OBJECT_H__ */
