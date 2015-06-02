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

#ifndef __STORAGED_LINUX_ISCS_ISESSION_H__
#define __STORAGED_LINUX_ISCS_ISESSION_H__

#include <src/storageddaemontypes.h>
#include "storagediscsitypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_LINUX_ISCSI_SESSION            (storaged_linux_iscsi_session_get_type ())
#define STORAGED_LINUX_ISCSI_SESSION(o)              (G_TYPE_CHECK_INSTANCE_CAST  ((o), STORAGED_TYPE_LINUX_ISCSI_SESSION, StoragedLinuxISCSISession))
#define STORAGED_IS_LINUX_ISCSI_SESSION(o)           (G_TYPE_CHECK_INSTANCE_TYPE  ((o), STORAGED_TYPE_LINUX_ISCSI_SESSION))
#define STORAGED_LINUX_ISCSI_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), STORAGED_TYPE_LINUX_ISCSI_SESSION, StoragedLinuxISCSISessionClass))
#define STORAGED_IS_LINUX_ISCSI_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), STORAGED_TYPE_LINUX_ISCSI_SESSION))
#define STORAGED_LINUX_ISCSI_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), STORAGED_TYPE_LINUX_ISCSI_SESSION, StoragedLinuxISCSISessionClass))

GType                      storaged_linux_iscsi_session_get_type (void) G_GNUC_CONST;
StoragedLinuxISCSISession *storaged_linux_iscsi_session_new      (void);
gboolean                   storaged_linux_iscsi_session_update   (StoragedLinuxISCSISession        *session,
                                                                  StoragedLinuxISCSISessionObject  *object);

G_END_DECLS

#endif /* __STORAGED_LINUX_ISCS_ISESSION_H__ */
