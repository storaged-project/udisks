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

#include <glib/gi18n-lib.h>

#include <src/storagedlogging.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>

#include "storagediscsistate.h"
#include "storagediscsitypes.h"
#include "storagediscsiutil.h"
#include "storaged-iscsi-generated.h"
#include "storagedlinuxiscsisession.h"
#include "storagedlinuxiscsisessionobject.h"

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
 * The #StoragedLinuxISCSISession structure contains only private data and
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

static gboolean
handle_logout_interface (StoragedISCSISession  *session,
                         GDBusMethodInvocation *invocation,
                         const gchar           *arg_iface,
                         GVariant              *arg_options)
{
  StoragedDaemon *daemon = NULL;
  StoragedISCSIState *state = NULL;
  StoragedLinuxISCSISessionObject *object = NULL;
  GError *error = NULL;
  gchar *errorstr = NULL;
  gint err;
  const gchar *name;
  const gchar *address;
  guint32 tpgt;
  guint32 port;

  object = storaged_daemon_util_dup_object (session, &error);
  daemon = storaged_linux_iscsi_session_object_get_daemon (object);

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (daemon,
                                       NULL,
                                       iscsi_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to perform iSCSI logout"),
                                       invocation);

  state = storaged_linux_iscsi_session_object_get_state (object);

  /* Parameters */
  name = storaged_iscsi_session_get_target_name (session);
  address = storaged_iscsi_session_get_address (session);
  tpgt = storaged_iscsi_session_get_tpgt (session);
  port = storaged_iscsi_session_get_persistent_port (session);

  /* Enter a critical section. */
  storaged_iscsi_state_lock_libiscsi_context (state);

  /* Logout */
  err = iscsi_perform_login_action (daemon,
                                    ACTION_LOGOUT,
                                    name,
                                    tpgt,
                                    address,
                                    port,
                                    arg_iface,
                                    &errorstr);

  if (err != 0)
    {
      /* Logout failed. */
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             N_("Logout failed: %s"),
                                             errorstr);
      goto out;
    }
  /* Complete DBus call. */
  storaged_iscsi_session_complete_logout (session,
                                          invocation);

  /* Leave the critical section. */
  storaged_iscsi_state_unlock_libiscsi_context (state);

out:
  if (errorstr)
    g_free (errorstr);
  g_clear_object (&object);

  /* Indicate that we handled the method invocation. */
  return TRUE;
}

static gboolean
handle_logout (StoragedISCSISession  *object,
               GDBusMethodInvocation *invocation,
               GVariant              *arg_options)
{
  /* Logout the "default" interface. */
  return handle_logout_interface (object, invocation, "default", arg_options);
}

/* -------------------------------------------------------------------------- */

void storaged_linux_iscsi_session_iface_init (StoragedISCSISessionIface *iface)
{
  iface->handle_logout_interface = handle_logout_interface;
  iface->handle_logout = handle_logout;
}
