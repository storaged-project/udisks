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

#ifndef __UDISKS_LINUX_ISCSI_SESSION_OBJECT_H__
#define __UDISKS_LINUX_ISCSI_SESSION_OBJECT_H__

#include <src/udisksdaemontypes.h>
#include "udisksiscsitypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_ISCSI_SESSION_OBJECT            (udisks_linux_iscsi_session_object_get_type ())
#define UDISKS_LINUX_ISCSI_SESSION_OBJECT(o)              (G_TYPE_CHECK_INSTANCE_CAST  ((o), UDISKS_TYPE_LINUX_ISCSI_SESSION_OBJECT, UDisksLinuxISCSISessionObject))
#define UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT(o)           (G_TYPE_CHECK_INSTANCE_TYPE  ((o), UDISKS_TYPE_LINUX_ISCSI_SESSION_OBJECT))
#define UDISKS_LINUX_ISCSI_SESSION_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), UDISKS_TYPE_LINUX_ISCSI_SESSION_OBJECT, UDisksLinuxISCSISessionObjectClass))
#define UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), UDISKS_TYPE_LINUX_ISCSI_SESSION_OBJECT))
#define UDISKS_LINUX_ISCSI_SESSION_OBJECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), UDISKS_TYPE_LINUX_ISCSI_SESSION_OBJECT, UDisksLinuxISCSISessionObjectClass))

GType                          udisks_linux_iscsi_session_object_get_type
                                                            (void) G_GNUC_CONST;
UDisksLinuxISCSISessionObject *udisks_linux_iscsi_session_object_new
                                                            (UDisksDaemon *daemon,
                                                             const gchar    *session_id);
UDisksDaemon                  *udisks_linux_iscsi_session_object_get_daemon
                                                            (UDisksLinuxISCSISessionObject *session_object);
const gchar                   *udisks_linux_iscsi_session_object_get_session_id
                                                            (UDisksLinuxISCSISessionObject *session_object);
UDisksISCSIState              *udisks_linux_iscsi_session_object_get_state
                                                            (UDisksLinuxISCSISessionObject *session_object);
gchar                         *udisks_linux_iscsi_session_object_get_object_path
                                                            (UDisksLinuxISCSISessionObject *session_object);

gchar                         *udisks_linux_iscsi_session_object_make_object_path
                                                            (const gchar *session_id);
gchar                         *udisks_linux_iscsi_session_object_get_session_id_from_sysfs_path
                                                            (const gchar *sysfs_path);

G_END_DECLS

#endif /* __UDISKS_LINUX_ISCSI_SESSION_OBJECT_H__ */
