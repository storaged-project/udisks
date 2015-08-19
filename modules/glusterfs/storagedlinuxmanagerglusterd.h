/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Samikshan Bairagya <sbairagy@redhat.com>
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
 */

#ifndef __STORAGED_LINUX_MANAGER_GLUSTERD_H__
#define __STORAGED_LINUX_MANAGER_GLUSTERD_H__

#include <src/storageddaemontypes.h>
#include "storagedglusterfstypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_LINUX_MANAGER_GLUSTERD                (storaged_linux_manager_glusterd_get_type ())
#define STORAGED_LINUX_MANAGER_GLUSTERD(o)                  (G_TYPE_CHECK_INSTANCE_CAST  ((o), STORAGED_TYPE_LINUX_MANAGER_GLUSTERD, StoragedLinuxManagerGlusterD))
#define STORAGED_IS_LINUX_MANAGER_GLUSTERD(o)               (G_TYPE_CHECK_INSTANCE_TYPE  ((o), STORAGED_TYPE_LINUX_MANAGER_GLUSTERD))
#define STORAGED_LINUX_MANAGER_GLUSTERD_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), STORAGED_TYPE_LINUX_MANAGER_GLUSTERD, StoragedLinuxManagerGlusterDClass))
#define STORAGED_IS_LINUX_MANAGER_GLUSTERD_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), STORAGED_TYPE_LINUX_MANAGER_GLUSTERD))
#define STORAGED_LINUX_MANAGER_GLUSTERD_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), STORAGED_TYPE_LINUX_MANAGER_GLUSTERD, StoragedLinuxManagerGlusterDClass))

GType                               storaged_linux_manager_glusterd_get_type   (void) G_GNUC_CONST;
StoragedLinuxManagerGlusterD       *storaged_linux_manager_glusterd_new        (StoragedDaemon *daemon);
StoragedDaemon                     *storaged_linux_manager_glusterd_get_daemon (StoragedLinuxManagerGlusterD *manager);

G_END_DECLS

#endif /* __STORAGED_LINUX_MANAGER_GLUSTERD_H__ */
