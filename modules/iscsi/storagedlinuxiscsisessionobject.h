/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Peter Hatina <phatina@redhat.com>
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

#ifndef __STORAGED_LINUX_ISCSI_SESSION_OBJECT_H__
#define __STORAGED_LINUX_ISCSI_SESSION_OBJECT_H__

#include <src/storageddaemontypes.h>
#include "storagediscsitypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_LINUX_ISCSI_SESSION_OBJECT            (storaged_linux_iscsi_session_object_get_type ())
#define STORAGED_LINUX_ISCSI_SESSION_OBJECT(o)              (G_TYPE_CHECK_INSTANCE_CAST  ((o), STORAGED_TYPE_LINUX_ISCSI_SESSION_OBJECT, StoragedLinuxISCSISessionObject))
#define STORAGED_IS_LINUX_ISCSI_SESSION_OBJECT(o)           (G_TYPE_CHECK_INSTANCE_TYPE  ((o), STORAGED_TYPE_LINUX_ISCSI_SESSION_OBJECT))
#define STORAGED_LINUX_ISCSI_SESSION_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), STORAGED_TYPE_LINUX_ISCSI_SESSION_OBJECT, StoragedLinuxISCSISessionObjectClass))
#define STORAGED_IS_LINUX_ISCSI_SESSION_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), STORAGED_TYPE_LINUX_ISCSI_SESSION_OBJECT))
#define STORAGED_LINUX_ISCSI_SESSION_OBJECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), STORAGED_TYPE_LINUX_ISCSI_SESSION_OBJECT, StoragedLinuxISCSISessionObjectClass))

GType                            storaged_linux_iscsi_session_object_get_type
                                                            (void) G_GNUC_CONST;
StoragedLinuxISCSISessionObject *storaged_linux_iscsi_session_object_new
                                                            (StoragedDaemon *daemon,
                                                             const gchar    *session_id);
StoragedDaemon                  *storaged_linux_iscsi_session_object_get_daemon
                                                            (StoragedLinuxISCSISessionObject *session_object);
const gchar                     *storaged_linux_iscsi_session_object_get_session_id
                                                            (StoragedLinuxISCSISessionObject *session_object);
gchar                           *storaged_linux_iscsi_session_object_get_object_path
                                                            (StoragedLinuxISCSISessionObject *session_object);

gchar                           *storaged_linux_iscsi_session_object_make_object_path
                                                            (const gchar *session_id);
gchar                           *storaged_linux_iscsi_session_object_get_session_id_from_sysfs_path
                                                            (const gchar *sysfs_path);

G_END_DECLS

#endif /* __STORAGED_LINUX_ISCSI_SESSION_OBJECT_H__ */
