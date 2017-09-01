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

#include <src/udiskslogging.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>

#include "udisksiscsistate.h"
#include "udisksiscsitypes.h"
#include "udisksiscsiutil.h"
#include "udisks-iscsi-generated.h"
#include "udiskslinuxiscsisession.h"
#include "udiskslinuxiscsisessionobject.h"

/**
 * SECTION:udiskslinuxiscsisession
 * @title: UDisksLinuxISCSISession
 * @short_description: Linux implementation of #UDisksISCSISession
 *
 * This type provides an implementation of #UDisksISCSISession interface
 * on Linux.
 */

/**
 * UDisksLinuxISCSISession:
 *
 * The #UDisksLinuxISCSISession structure contains only private data and
 * should only be accessed using provided API.
 */
struct _UDisksLinuxISCSISession {
  UDisksISCSISessionSkeleton parent_instance;
};

struct _UDisksLinuxISCSISessionClass {
  UDisksISCSISessionSkeletonClass parent_class;
};

static void udisks_linux_iscsi_session_iface_init (UDisksISCSISessionIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxISCSISession, udisks_linux_iscsi_session,
                         UDISKS_TYPE_ISCSI_SESSION_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_ISCSI_SESSION,
                                                udisks_linux_iscsi_session_iface_init));

static void
udisks_linux_iscsi_session_class_init (UDisksLinuxISCSISessionClass *klass)
{
}

static void
udisks_linux_iscsi_session_init (UDisksLinuxISCSISession *self)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (self),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_iscsi_session_new:
 *
 * Creates a new #UDisksLinuxISCSISession instance.
 *
 * Returns: A new #UDisksLinuxISCSISession. Free with g_object_unref().
 */
UDisksLinuxISCSISession *
udisks_linux_iscsi_session_new (void)
{
  return g_object_new (UDISKS_TYPE_LINUX_ISCSI_SESSION, NULL);
}

/**
 * udisks_linux_iscsi_session_update:
 * @session: A #UDisksLinuxISCSISession
 * @object: The enclosing #UDisksLinuxISCSISessionObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_iscsi_session_update (UDisksLinuxISCSISession        *session,
                                   UDisksLinuxISCSISessionObject  *object)
{
  return FALSE;
}

static gboolean
handle_logout_interface (UDisksISCSISession    *session,
                         GDBusMethodInvocation *invocation,
                         const gchar           *arg_iface,
                         GVariant              *arg_options)
{
  UDisksDaemon *daemon = NULL;
  UDisksISCSIState *state = NULL;
  UDisksLinuxISCSISessionObject *object = NULL;
  GError *error = NULL;
  gchar *errorstr = NULL;
  gint err;
  const gchar *name;
  const gchar *address;
  guint32 tpgt;
  guint32 port;

  object = udisks_daemon_util_dup_object (session, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  daemon = udisks_linux_iscsi_session_object_get_daemon (object);

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     iscsi_policy_action_id,
                                     arg_options,
                                     N_("Authentication is required to perform iSCSI logout"),
                                     invocation);

  state = udisks_linux_iscsi_session_object_get_state (object);

  /* Parameters */
  name = udisks_iscsi_session_get_target_name (session);
  address = udisks_iscsi_session_get_address (session);
  tpgt = udisks_iscsi_session_get_tpgt (session);
  port = udisks_iscsi_session_get_persistent_port (session);

  /* Enter a critical section. */
  udisks_iscsi_state_lock_libiscsi_context (state);

  /* Logout */
  err = iscsi_logout (daemon,
                      name,
                      tpgt,
                      address,
                      port,
                      arg_iface,
                      arg_options,
                      &errorstr);

  /* Leave the critical section. */
  udisks_iscsi_state_unlock_libiscsi_context (state);

  if (err != 0)
    {
      /* Logout failed. */
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             N_("Logout failed: %s"),
                                             errorstr);
      goto out;
    }

  /* now sit and wait until the device and session disappear on dbus */
  if (!udisks_daemon_wait_for_object_to_disappear_sync (daemon,
                                                        wait_for_iscsi_object,
                                                        g_strdup (name),
                                                        g_free,
                                                        15, /* timeout_seconds */
                                                        &error))
    {
      g_prefix_error (&error, "Error waiting for iSCSI device to disappear: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_wait_for_object_to_disappear_sync (daemon,
                                                        wait_for_iscsi_session_object,
                                                        g_strdup (name),
                                                        g_free,
                                                        15, /* timeout_seconds */
                                                        &error))
    {
      g_prefix_error (&error, "Error waiting for iSCSI session object to disappear: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  udisks_iscsi_session_complete_logout (session,
                                        invocation);

out:
  g_clear_object (&object);
  g_free ((gpointer) errorstr);

  /* Indicate that we handled the method invocation. */
  return TRUE;
}

static gboolean
handle_logout (UDisksISCSISession    *object,
               GDBusMethodInvocation *invocation,
               GVariant              *arg_options)
{
  /* Logout the "default" interface. */
  return handle_logout_interface (object, invocation, "default", arg_options);
}

/* -------------------------------------------------------------------------- */

void udisks_linux_iscsi_session_iface_init (UDisksISCSISessionIface *iface)
{
  iface->handle_logout_interface = handle_logout_interface;
  iface->handle_logout = handle_logout;
}
