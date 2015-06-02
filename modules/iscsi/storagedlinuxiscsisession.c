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

#include "config.h"

#include <src/storagedlogging.h>
#include <src/storageddaemon.h>

#include "storagediscsitypes.h"
#include "storaged-iscsi-generated.h"
#include "storagedlinuxiscsisession.h"

/**
 * SECTION:storagedlinuxiscsisession
 * @title: StoragedLinuxISCSISession
 * @short_description: Linux implementation of #StoragedISCSISession
 *
 * This type provides an implementation of #StoragedISCSISession interface
 * on Linux.
 */

/**
 * StoragedLinuxISCSISession:
 *
 * The #StoragedLinuxISCSISession structire contains only private data and
 * should only be accessed using provided API.
 */
struct _StoragedLinuxISCSISession {
  StoragedISCSISessionSkeleton parent_instance;
};

struct _StoragedLinuxISCSISessionClass {
  StoragedISCSISessionSkeletonClass parent_class;
};

static void storaged_linux_iscsi_session_iface_init (StoragedISCSISessionIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxISCSISession, storaged_linux_iscsi_session,
                         STORAGED_TYPE_ISCSI_SESSION_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_ISCSI_SESSION,
                                                storaged_linux_iscsi_session_iface_init));

static void
storaged_linux_iscsi_session_class_init (StoragedLinuxISCSISessionClass *klass)
{
}

static void
storaged_linux_iscsi_session_init (StoragedLinuxISCSISession *self)
{
}

/**
 * storaged_linux_iscsi_session_new:
 *
 * Creates a new #StoragedLinuxISCSISession instance.
 *
 * Returns: A new #StoragedLinuxISCSISession. Free with g_object_unref().
 */
StoragedLinuxISCSISession *
storaged_linux_iscsi_session_new (void)
{
  return g_object_new (STORAGED_TYPE_LINUX_ISCSI_SESSION, NULL);
}

/**
 * storaged_linux_iscsi_session_update:
 * @session: A #StoragedLinuxISCSISession
 * @object: The enclosing #StoragedLinuxISCSISessionObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
storaged_linux_iscsi_session_update (StoragedLinuxISCSISession        *session,
                                     StoragedLinuxISCSISessionObject  *object)
{
  return FALSE;
}

/* -------------------------------------------------------------------------- */

void storaged_linux_iscsi_session_iface_init (StoragedISCSISessionIface *iface)
{
}
