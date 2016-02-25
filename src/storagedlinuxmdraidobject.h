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

#ifndef __STORAGED_LINUX_MDRAID_OBJECT_H__
#define __STORAGED_LINUX_MDRAID_OBJECT_H__

#include "storageddaemontypes.h"
#include <gudev/gudev.h>

G_BEGIN_DECLS

#define STORAGED_TYPE_LINUX_MDRAID_OBJECT  (storaged_linux_mdraid_object_get_type ())
#define STORAGED_LINUX_MDRAID_OBJECT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_MDRAID_OBJECT, StoragedLinuxMDRaidObject))
#define STORAGED_IS_LINUX_MDRAID_OBJECT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_MDRAID_OBJECT))

GType                      storaged_linux_mdraid_object_get_type      (void) G_GNUC_CONST;
StoragedLinuxMDRaidObject *storaged_linux_mdraid_object_new           (StoragedDaemon              *daemon,
                                                                       const gchar                 *uuid);
void                       storaged_linux_mdraid_object_uevent        (StoragedLinuxMDRaidObject   *object,
                                                                       const gchar                 *action,
                                                                       StoragedLinuxDevice         *device,
                                                                       gboolean                     is_member);
const gchar               *storaged_linux_mdraid_object_get_uuid      (StoragedLinuxMDRaidObject   *object);
StoragedDaemon            *storaged_linux_mdraid_object_get_daemon    (StoragedLinuxMDRaidObject   *object);
GList                     *storaged_linux_mdraid_object_get_members   (StoragedLinuxMDRaidObject   *object);
StoragedLinuxDevice       *storaged_linux_mdraid_object_get_device    (StoragedLinuxMDRaidObject   *object);

gboolean                   storaged_linux_mdraid_object_have_devices  (StoragedLinuxMDRaidObject   *object);

StoragedBaseJob           *storaged_linux_mdraid_object_get_sync_job  (StoragedLinuxMDRaidObject   *object);
gboolean                   storaged_linux_mdraid_object_set_sync_job  (StoragedLinuxMDRaidObject   *object,
                                                                       StoragedBaseJob             *job);
gboolean                   storaged_linux_mdraid_object_complete_sync_job (StoragedLinuxMDRaidObject *object,
                                                                           gboolean                   success,
                                                                           const gchar               *message);
gboolean                   storaged_linux_mdraid_object_has_sync_job      (StoragedLinuxMDRaidObject *object);

G_END_DECLS

#endif /* __STORAGED_LINUX_MDRAID_OBJECT_H__ */
