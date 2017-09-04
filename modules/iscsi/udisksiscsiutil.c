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

#include <string.h>
#include <libiscsi.h>

#include <src/udisksdaemon.h>
#include <src/udisksmodulemanager.h>
#include <src/udiskslogging.h>

#include "udisksiscsitypes.h"
#include "udisksiscsistate.h"
#include "udisksiscsiutil.h"
#include "udisksiscsidbusutil.h"
#include "udisks-iscsi-generated.h"

#ifndef HAVE_LIBISCSI_ERR
/* XXX: We need to expose these in libiscsi.h.  If we can't make it appear in
 *      the libiscsi.h, then we need to keep this in sync with iscsi_err.h.
 */
enum {
  ISCSI_SUCCESS                    = 0,
  /* Generic error */
  ISCSI_ERR                        = 1,
  /* session could not be found */
  ISCSI_ERR_SESS_NOT_FOUND         = 2,
  /* Could not allocate resource for operation */
  ISCSI_ERR_NOMEM                  = 3,
  /* Transport error caused operation to fail */
  ISCSI_ERR_TRANS                  = 4,
  /* Generic login failure */
  ISCSI_ERR_LOGIN                  = 5,
  /* Error accessing/managing iSCSI DB */
  ISCSI_ERR_IDBM                   = 6,
  /* Invalid argument */
  ISCSI_ERR_INVAL                  = 7,
  /* Connection timer exired while trying to connect */
  ISCSI_ERR_TRANS_TIMEOUT          = 8,
  /* Generic internal iscsid failure */
  ISCSI_ERR_INTERNAL               = 9,
  /* Logout failed */
  ISCSI_ERR_LOGOUT                 = 10,
  /* iSCSI PDU timedout */
  ISCSI_ERR_PDU_TIMEOUT            = 11,
  /* iSCSI transport module not loaded in kernel or iscsid */
  ISCSI_ERR_TRANS_NOT_FOUND        = 12,
  /* Permission denied */
  ISCSI_ERR_ACCESS                 = 13,
  /* Transport module did not support operation */
  ISCSI_ERR_TRANS_CAPS             = 14,
  /* Session is logged in */
  ISCSI_ERR_SESS_EXISTS            = 15,
  /* Invalid IPC MGMT request */
  ISCSI_ERR_INVALID_MGMT_REQ       = 16,
  /* iSNS service is not supported */
  ISCSI_ERR_ISNS_UNAVAILABLE       = 17,
  /* A read/write to iscsid failed */
  ISCSI_ERR_ISCSID_COMM_ERR        = 18,
  /* Fatal login error */
  ISCSI_ERR_FATAL_LOGIN            = 19,
  /* Could ont connect to iscsid */
  ISCSI_ERR_ISCSID_NOTCONN         = 20,
  /* No records/targets/sessions/portals found to execute operation on */
  ISCSI_ERR_NO_OBJS_FOUND          = 21,
  /* Could not lookup object in sysfs */
  ISCSI_ERR_SYSFS_LOOKUP           = 22,
  /* Could not lookup host */
  ISCSI_ERR_HOST_NOT_FOUND         = 23,
  /* Login failed due to authorization failure */
  ISCSI_ERR_LOGIN_AUTH_FAILED      = 24,
  /* iSNS query failure */
  ISCSI_ERR_ISNS_QUERY             = 25,
  /* iSNS registration/deregistration failed */
  ISCSI_ERR_ISNS_REG_FAILED        = 26,
  /* operation not supported */
  ISCSI_ERR_OP_NOT_SUPP            = 27,
  /* device or resource in use */
  ISCSI_ERR_BUSY                   = 28,
  /* Operation failed, but retrying layer may succeed */
  ISCSI_ERR_AGAIN                  = 29,
  /* unknown discovery type */
  ISCSI_ERR_UNKNOWN_DISCOVERY_TYPE = 30,

  /* Always last. Indicates end of error code space */
  ISCSI_MAX_ERR_VAL,
};
#endif /* HAVE_LIBISCSI_ERR */

const gchar *iscsi_nodes_fmt = "a(sisis)";
const gchar *iscsi_node_fmt = "(sisis)";
const gchar *iscsi_policy_action_id = "org.freedesktop.udisks2.iscsi.manage-iscsi";

static struct libiscsi_context *
iscsi_get_libiscsi_context (UDisksDaemon *daemon)
{
  UDisksISCSIState *state;
  UDisksModuleManager *module_manager;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);

  module_manager = udisks_daemon_get_module_manager (daemon);
  state = udisks_module_manager_get_module_state_pointer (module_manager,
                                                          ISCSI_MODULE_NAME);

  return udisks_iscsi_state_get_libiscsi_context (state);
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
      strncpy (auth_info->chap.username, username, LIBISCSI_VALUE_MAXLEN - 1);
      if (password && *password)
        strncpy (auth_info->chap.password, password, LIBISCSI_VALUE_MAXLEN - 1);
    }

  /* CHAP reverse username + reverse password */
  if (reverse_username && *reverse_username)
    {
      auth_info->method = libiscsi_auth_chap;
      strncpy (auth_info->chap.reverse_username, reverse_username, LIBISCSI_VALUE_MAXLEN - 1);
      if (reverse_password && *reverse_password)
        strncpy (auth_info->chap.reverse_password, reverse_password, LIBISCSI_VALUE_MAXLEN - 1);
    }
}

static void
iscsi_make_node (struct libiscsi_node *node,
                 const gchar          *name,
                 const gint            tpgt,
                 const gchar          *address,
                 const gint            port,
                 const gchar          *iface)
{
  g_return_if_fail (node);

  memset (node, 0, sizeof (struct libiscsi_node));

  /* Fill libiscsi parameters. */
  strncpy (node->name, name, LIBISCSI_VALUE_MAXLEN - 1);
  strncpy (node->address, address, NI_MAXHOST - 1);
  strncpy (node->iface, iface, LIBISCSI_VALUE_MAXLEN - 1);
  node->tpgt = tpgt;
  node->port = port;
}

static gint
iscsi_perform_login_action (UDisksDaemon             *daemon,
                            libiscsi_login_action       action,
                            struct libiscsi_node       *node,
                            struct libiscsi_auth_info  *auth_info,
                            gchar                     **errorstr)
{
  struct libiscsi_context *ctx;
  gint err;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), 1);

  /* Get a libiscsi context. */
  ctx = iscsi_get_libiscsi_context (daemon);

  if (action == ACTION_LOGIN &&
      auth_info && auth_info->method == libiscsi_auth_chap)
    {
      libiscsi_node_set_auth (ctx, node, auth_info);
    }

  /* Login or Logout */
  err = action == ACTION_LOGIN ?
        libiscsi_node_login  (ctx, node) :
        libiscsi_node_logout (ctx, node);

  if (errorstr && err != 0)
    *errorstr = g_strdup (libiscsi_get_error_string (ctx));

  return err;
}

static gint
iscsi_node_set_parameters (struct libiscsi_context *ctx,
                           struct libiscsi_node    *node,
                           GVariant                *params)
{
  GVariantIter  iter;
  GVariant     *value;
  gchar        *key;
  gchar        *param_value;
  gint          err = 0;

  g_return_val_if_fail (ctx, ISCSI_ERR_INVAL);
  g_return_val_if_fail (node, ISCSI_ERR_INVAL);
  g_return_val_if_fail (params, ISCSI_ERR_INVAL);

  g_variant_iter_init (&iter, params);
  while (err == 0 && g_variant_iter_next (&iter, "{sv}", &key, &value))
    {
      g_variant_get (value, "&s", &param_value);

      /* Update the node parameter value. */
      err = libiscsi_node_set_parameter (ctx, node, key, param_value);

      g_variant_unref (value);
      g_free ((gpointer) key);
    }

  return 0;
}

static void
iscsi_params_get_chap_data (GVariant      *params,
                            const gchar  **username,
                            const gchar  **password,
                            const gchar  **reverse_username,
                            const gchar  **reverse_password)
{
  g_return_if_fail (params);

  g_variant_lookup (params, "username", "&s", username);
  g_variant_lookup (params, "password", "&s", password);
  g_variant_lookup (params, "reverse-username", "&s", reverse_username);
  g_variant_lookup (params, "reverse-password", "&s", reverse_password);
}

static GVariant *
iscsi_params_pop_chap_data (GVariant      *params,
                            const gchar  **username,
                            const gchar  **password,
                            const gchar  **reverse_username,
                            const gchar  **reverse_password)
{
  GVariantDict dict;

  g_return_val_if_fail (params, NULL);

  /* Pop CHAP parameters */
  g_variant_dict_init (&dict, params);
  g_variant_dict_lookup (&dict, "username", "&s", username);
  g_variant_dict_lookup (&dict, "password", "&s", password);
  g_variant_dict_lookup (&dict, "reverse-username", "&s", reverse_username);
  g_variant_dict_lookup (&dict, "reverse-password", "&s", reverse_password);

  if (username)
    g_variant_dict_remove (&dict, "username");
  if (password)
    g_variant_dict_remove (&dict, "password");
  if (reverse_username)
    g_variant_dict_remove (&dict, "reverse-username");
  if (reverse_password)
    g_variant_dict_remove (&dict, "reverse-password");

  /* Update the params, so that it doesn't contain CHAP parameters. */
  return g_variant_dict_end (&dict);
}

gint
iscsi_login (UDisksDaemon  *daemon,
             const gchar   *name,
             const gint     tpgt,
             const gchar   *address,
             const gint     port,
             const gchar   *iface,
             GVariant      *params,
             gchar        **errorstr)
{
  struct libiscsi_context *ctx;
  struct libiscsi_auth_info auth_info;
  struct libiscsi_node node;
  GVariant *params_without_chap;
  const gchar *username = NULL;
  const gchar *password = NULL;
  const gchar *reverse_username = NULL;
  const gchar *reverse_password = NULL;
  gint err;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), 1);

  /* Optional data for CHAP authentication. We pop these parameters from the
   * dictionary; it then contains only iSCSI node parameters. */
  params_without_chap = iscsi_params_pop_chap_data (params,
                                                    &username,
                                                    &password,
                                                    &reverse_username,
                                                    &reverse_password);

  /* Prepare authentication data */
  iscsi_make_auth_info (&auth_info,
                        username,
                        password,
                        reverse_username,
                        reverse_password);

  /* Create iscsi node. */
  iscsi_make_node (&node, name, tpgt, address, port, iface);

  /* Get iscsi context. */
  ctx = iscsi_get_libiscsi_context (daemon);

  /* Login */
  err = iscsi_perform_login_action (daemon,
                                    ACTION_LOGIN,
                                    &node,
                                    &auth_info,
                                    errorstr);

  if (err == 0 && params)
    {
      /* Update node parameters. */
      err = iscsi_node_set_parameters (ctx, &node, params_without_chap);
    }

  g_variant_unref (params_without_chap);

  return err;
}

gint
iscsi_logout (UDisksDaemon  *daemon,
              const gchar   *name,
              const gint     tpgt,
              const gchar   *address,
              const gint     port,
              const gchar   *iface,
              GVariant      *params,
              gchar        **errorstr)
{
  struct libiscsi_context *ctx;
  struct libiscsi_node node;
  gint err;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), 1);

  /* Create iscsi node. */
  iscsi_make_node (&node, name, tpgt, address, port, iface);

  /* Get iscsi context. */
  ctx = iscsi_get_libiscsi_context (daemon);

  /* Logout */
  err = iscsi_perform_login_action (daemon,
                                    ACTION_LOGOUT,
                                    &node,
                                    NULL,
                                    errorstr);

  if (err == 0 && params)
    {
      /* Update node parameters. */
      err = iscsi_node_set_parameters (ctx, &node, params);

    }

  return err;
}

gint
iscsi_discover_send_targets (UDisksDaemon   *daemon,
                             const gchar    *address,
                             const guint16   port,
                             GVariant       *params,
                             GVariant      **nodes,
                             gint           *nodes_cnt,
                             gchar         **errorstr)
{
  struct libiscsi_context *ctx;
  struct libiscsi_auth_info auth_info;
  struct libiscsi_node *found_nodes;
  const gchar *username = NULL;
  const gchar *password = NULL;
  const gchar *reverse_username = NULL;
  const gchar *reverse_password = NULL;
  gint err;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), 1);

  ctx = iscsi_get_libiscsi_context (daemon);

  /* Optional data for CHAP authentication. */
  iscsi_params_get_chap_data (params,
                              &username,
                              &password,
                              &reverse_username,
                              &reverse_password);

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

UDisksError
iscsi_error_to_udisks_error (const gint err)
{
  switch (err)
    {
      case ISCSI_ERR_TRANS: /* 4 */
        return UDISKS_ERROR_ISCSI_TRANSPORT_FAILED;
      case ISCSI_ERR_LOGIN: /* 5 */
        return UDISKS_ERROR_ISCSI_LOGIN_FAILED;
      case ISCSI_ERR_IDBM: /* 6 */
        return UDISKS_ERROR_ISCSI_IDMB;
      case ISCSI_ERR_LOGOUT: /* 10 */
        return UDISKS_ERROR_ISCSI_LOGOUT_FAILED;
      case ISCSI_ERR_ISCSID_COMM_ERR: /* 18 */
        return UDISKS_ERROR_ISCSI_DAEMON_TRANSPORT_FAILED;
      case ISCSI_ERR_FATAL_LOGIN: /* 19 */
        return UDISKS_ERROR_ISCSI_LOGIN_FATAL;
      case ISCSI_ERR_ISCSID_NOTCONN: /* 20 */
        return UDISKS_ERROR_ISCSI_NOT_CONNECTED;
      case ISCSI_ERR_NO_OBJS_FOUND: /* 21 */
        return UDISKS_ERROR_ISCSI_NO_OBJECTS_FOUND;
      case ISCSI_ERR_HOST_NOT_FOUND: /* 23 */
        return UDISKS_ERROR_ISCSI_HOST_NOT_FOUND;
      case ISCSI_ERR_LOGIN_AUTH_FAILED: /* 24 */
        return UDISKS_ERROR_ISCSI_LOGIN_AUTH_FAILED;
      case ISCSI_ERR_UNKNOWN_DISCOVERY_TYPE: /* 30 */
        return UDISKS_ERROR_ISCSI_UNKNOWN_DISCOVERY_TYPE;

      default:
        return UDISKS_ERROR_FAILED;
    }
}

UDisksObject *
wait_for_iscsi_object (UDisksDaemon *daemon,
                       gpointer      user_data)
{
  const gchar *device_iqn = user_data;
  UDisksObject *ret = NULL;
  GList *objects, *l;
  const gchar *const *symlinks = NULL;

  objects = udisks_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block != NULL)
        {
          symlinks = udisks_block_get_symlinks (UDISKS_BLOCK (block));
          if (symlinks != NULL)
            for (guint n = 0; symlinks[n] != NULL; n++)
              if (g_str_has_prefix (symlinks[n], "/dev/disk/by-path/") &&
                  strstr (symlinks[n], device_iqn) != NULL)
                {
                  ret = g_object_ref (object);
                  goto out;
                }
            }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

UDisksObject *
wait_for_iscsi_session_object (UDisksDaemon *daemon,
                               gpointer      user_data)
{
  const gchar *device_iqn = user_data;
  UDisksObject *ret = NULL;
  GList *objects, *l;

  objects = udisks_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksISCSISession *session;

      session = udisks_object_peek_iscsi_session (object);
      if (session != NULL)
        {
          if (g_strcmp0 (udisks_iscsi_session_get_target_name (session), device_iqn) == 0)
            {
              ret = g_object_ref (object);
              goto out;
            }
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}
