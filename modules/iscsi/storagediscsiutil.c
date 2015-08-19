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

#include <string.h>
#include <libiscsi.h>

#include <src/storageddaemon.h>
#include <src/storagedmodulemanager.h>

#include "storagediscsitypes.h"
#include "storagediscsistate.h"
#include "storagediscsiutil.h"

const gchar *iscsi_nodes_fmt = "a(sisis)";
const gchar *iscsi_node_fmt = "(sisis)";
const gchar *iscsi_policy_action_id = "org.storaged.Storaged.iscsi.manage-iscsi";

static struct libiscsi_context *
iscsi_get_libiscsi_context (StoragedDaemon *daemon)
{
  StoragedISCSIState *state;
  StoragedModuleManager *module_manager;

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);

  module_manager = storaged_daemon_get_module_manager (daemon);
  state = storaged_module_manager_get_module_state_pointer (module_manager,
                                                            ISCSI_MODULE_NAME);

  return storaged_iscsi_state_get_libiscsi_context (state);
}

/**
 * iscsi_perform_login_action
 * @daemon: A #StoragedDaemon
 * @action: A #libiscsi_login_action
 * @name: An iSCSI iqn for the node
 * @tpgt: A portal group number
 * @address: A portal hostname or IP-address
 * @port: A portal port number
 * @iface: An interface to connect through
 * @errorstr: An error string pointer; may be NULL. Free with g_free().
 *
 * Logs in or out to a iSCSI node.
 *
 * Returns: 0 if login/logout was successful; standard error code otherwise.
 */
gint
iscsi_perform_login_action (StoragedDaemon        *daemon,
                            libiscsi_login_action  action,
                            const gchar           *name,
                            const gint            tpgt,
                            const gchar           *address,
                            const gint             port,
                            const gchar           *iface,
                            gchar                **errorstr)
{
  struct libiscsi_context *ctx = iscsi_get_libiscsi_context (daemon);
  struct libiscsi_node node;
  gint rval;

  /* Fill libiscsi parameters. */
  strncpy (node.name, name, LIBISCSI_VALUE_MAXLEN);
  strncpy (node.address, address, NI_MAXHOST);
  strncpy (node.iface, iface, LIBISCSI_VALUE_MAXLEN);
  node.tpgt = tpgt;
  node.port = port;

  /* Login or Logout */
  rval = action == ACTION_LOGIN ?
        libiscsi_node_login  (ctx, &node) :
        libiscsi_node_logout (ctx, &node);

  if (errorstr && rval != 0)
    *errorstr = g_strdup (libiscsi_get_error_string (ctx));

  return rval;
}

/**
 *
 */
GVariant *
iscsi_libiscsi_nodes_to_gvariant (const struct libiscsi_node *nodes,
                                  const gint                  nodes_cnt)
{
  gint i;
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE (iscsi_nodes_fmt));
  for (i = 0; i < nodes_cnt; ++i)
    {
      g_variant_builder_add (&builder,
                             iscsi_node_fmt,
                             nodes[i].name,
                             nodes[i].tpgt,
                             nodes[i].address,
                             nodes[i].port,
                             nodes[i].iface);
    }
  return g_variant_builder_end (&builder);
}

void
iscsi_libiscsi_nodes_free (const struct libiscsi_node *nodes)
{
  g_free ((gpointer) nodes);
}
