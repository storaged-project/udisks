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

static void
iscsi_make_auth_info (struct libiscsi_auth_info *auth_info,
                      const gchar               *username,
                      const gchar               *password,
                      const gchar               *reverse_username,
                      const gchar               *reverse_password)
{
  g_return_if_fail (auth_info);

  memset (auth_info, 0, sizeof (struct libiscsi_auth_info));
  auth_info->method = libiscsi_auth_none;

  /* CHAP username + password */
  if (username && *username)
    {
      auth_info->method = libiscsi_auth_chap;
      strncpy (auth_info->chap.username, username, LIBISCSI_VALUE_MAXLEN);
      if (password && *password)
        strncpy (auth_info->chap.password, password, LIBISCSI_VALUE_MAXLEN);
    }

  /* CHAP reverse username + reverse password */
  if (reverse_username && *reverse_username)
    {
      auth_info->method = libiscsi_auth_chap;
      strncpy (auth_info->chap.reverse_username, reverse_username, LIBISCSI_VALUE_MAXLEN);
      if (reverse_password && *reverse_password)
        strncpy (auth_info->chap.reverse_password, reverse_password, LIBISCSI_VALUE_MAXLEN);
    }
}

static gint
iscsi_perform_login_action (StoragedDaemon             *daemon,
                            libiscsi_login_action       action,
                            const gchar                *name,
                            const gint                  tpgt,
                            const gchar                *address,
                            const gint                  port,
                            const gchar                *iface,
                            struct libiscsi_auth_info  *auth_info,
                            gchar                     **errorstr)
{
  struct libiscsi_context *ctx;
  struct libiscsi_node node;
  gint err;

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), 1);

  ctx = iscsi_get_libiscsi_context (daemon);

  /* Fill libiscsi parameters. */
  strncpy (node.name, name, LIBISCSI_VALUE_MAXLEN);
  strncpy (node.address, address, NI_MAXHOST);
  strncpy (node.iface, iface, LIBISCSI_VALUE_MAXLEN);
  node.tpgt = tpgt;
  node.port = port;

  if (action == ACTION_LOGIN &&
      auth_info && auth_info->method == libiscsi_auth_chap)
    {
      libiscsi_node_set_auth (ctx, &node, auth_info);
    }

  /* Login or Logout */
  err = action == ACTION_LOGIN ?
        libiscsi_node_login  (ctx, &node) :
        libiscsi_node_logout (ctx, &node);

  if (errorstr && err != 0)
    *errorstr = g_strdup (libiscsi_get_error_string (ctx));

  return err;
}

gint
iscsi_login (StoragedDaemon  *daemon,
             const gchar     *name,
             const gint       tpgt,
             const gchar     *address,
             const gint       port,
             const gchar     *iface,
             const gchar     *username,
             const gchar     *password,
             const gchar     *reverse_username,
             const gchar     *reverse_password,
             gchar          **errorstr)
{
  struct libiscsi_auth_info auth_info;

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), 1);

  /* Prepare authentication data */
  iscsi_make_auth_info (&auth_info,
                        username,
                        password,
                        reverse_username,
                        reverse_password);

  /* Login */
  return iscsi_perform_login_action (daemon,
                                     ACTION_LOGIN,
                                     name,
                                     tpgt,
                                     address,
                                     port,
                                     iface,
                                     &auth_info,
                                     errorstr);
}

gint
iscsi_logout (StoragedDaemon  *daemon,
              const gchar     *name,
              const gint       tpgt,
              const gchar     *address,
              const gint       port,
              const gchar     *iface,
              gchar          **errorstr)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), 1);

  /* Logout */
  return iscsi_perform_login_action (daemon,
                                     ACTION_LOGOUT,
                                     name,
                                     tpgt,
                                     address,
                                     port,
                                     iface,
                                     NULL,
                                     errorstr);
}

gint
iscsi_discover_send_targets (StoragedDaemon  *daemon,
                             const gchar     *address,
                             const guint16    port,
                             const gchar     *username,
                             const gchar     *password,
                             const gchar     *reverse_username,
                             const gchar     *reverse_password,
                             GVariant       **nodes,
                             gint            *nodes_cnt,
                             gchar          **errorstr)
{
  struct libiscsi_context *ctx;
  struct libiscsi_auth_info auth_info;
  struct libiscsi_node *found_nodes;
  gint err;

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), 1);

  ctx = iscsi_get_libiscsi_context (daemon);

  /* Prepare authentication data */
  iscsi_make_auth_info (&auth_info,
                        username,
                        password,
                        reverse_username,
                        reverse_password);

  /* Discovery */
  err = libiscsi_discover_sendtargets (ctx,
                                        address,
                                        port,
                                        &auth_info,
                                        nodes_cnt,
                                        &found_nodes);

  if (err == 0)
      *nodes = iscsi_libiscsi_nodes_to_gvariant (found_nodes, *nodes_cnt);
  else if (errorstr)
      *errorstr = g_strdup (libiscsi_get_error_string (ctx));

  /* Release the resources */
  iscsi_libiscsi_nodes_free (found_nodes);

  return err;
}

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
