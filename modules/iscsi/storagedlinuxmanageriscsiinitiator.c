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

#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <glib/gi18n-lib.h>

#include <libiscsi.h>

#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlogging.h>
#include <src/storagedmodulemanager.h>

#include "storaged-iscsi-generated.h"
#include "storagediscsistate.h"
#include "storagediscsiutil.h"
#include "storagedlinuxmanageriscsiinitiator.h"

/**
 * SECTION:storagedlinuxmanageriscsiinitiator
 * @title: StoragedLinuxManagerISCSIInitiator
 * @short_description: Linux implementation of
 * #StoragedLinuxManagerISCSIInitiator
 *
 * This type provides an implementation of the
 * #StoragedLinuxManagerISCSIInitiator interface on Linux.
 */

/**
 * StoragedLinuxManagerISCSIInitiator:
 *
 * The #StoragedLinuxManagerISCSIInitiator structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _StoragedLinuxManagerISCSIInitiator{
  StoragedManagerISCSIInitiatorSkeleton parent_instance;

  StoragedDaemon *daemon;
  StoragedISCSIState *state;
  GMutex initiator_config_mutex;  /* We use separate mutex for configuration
                                     file because libiscsi doesn't provide us
                                     any API for this. */
};

struct _StoragedLinuxManagerISCSIInitiatorClass {
  StoragedManagerISCSIInitiatorSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON
};

static void storaged_linux_manager_iscsi_initiator_iface_init (StoragedManagerISCSIInitiatorIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxManagerISCSIInitiator, storaged_linux_manager_iscsi_initiator,
                         STORAGED_TYPE_MANAGER_ISCSI_INITIATOR_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MANAGER_ISCSI_INITIATOR,
                                                storaged_linux_manager_iscsi_initiator_iface_init));

const gchar *initiator_filename = "/etc/iscsi/initiatorname.iscsi";
const gchar *initiator_name_prefix = "InitiatorName=";

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_manager_iscsi_initiator_get_property (GObject *object, guint property_id,
                                                     GValue *value, GParamSpec *pspec)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_manager_iscsi_initiator_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_iscsi_initiator_set_property (GObject *object, guint property_id,
                                                     const GValue *value, GParamSpec *pspec)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_assert (manager->daemon == NULL);
      /* We don't take a reference to the daemon */
      manager->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_iscsi_initiator_constructed (GObject *object)
{
  StoragedLinuxManagerISCSIInitiator *manager;
  StoragedModuleManager *module_manager;

  /* Store state pointer for later usage; libiscsi_context. */
  manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);
  module_manager = storaged_daemon_get_module_manager (manager->daemon);
  manager->state = storaged_module_manager_get_module_state_pointer (module_manager,
                                                                     ISCSI_MODULE_NAME);
}

static void
storaged_linux_manager_iscsi_initiator_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_iscsi_initiator_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_iscsi_initiator_parent_class)->dispose (object);
}

static void
storaged_linux_manager_iscsi_initiator_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_manager_iscsi_initiator_parent_class))
    G_OBJECT_CLASS (storaged_linux_manager_iscsi_initiator_parent_class)->finalize (object);
}

static void
storaged_linux_manager_iscsi_initiator_class_init (StoragedLinuxManagerISCSIInitiatorClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = storaged_linux_manager_iscsi_initiator_get_property;
  gobject_class->set_property = storaged_linux_manager_iscsi_initiator_set_property;
  gobject_class->constructed = storaged_linux_manager_iscsi_initiator_constructed;
  gobject_class->dispose = storaged_linux_manager_iscsi_initiator_dispose;
  gobject_class->finalize = storaged_linux_manager_iscsi_initiator_finalize;

  /** StoragedLinuxManager:daemon
   *
   * The #StoragedDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
storaged_linux_manager_iscsi_initiator_init (StoragedLinuxManagerISCSIInitiator *manager)
{
  manager->daemon = NULL;

  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * storaged_linux_manager_iscsi_initiator_new:
 * @daemon: A #StoragedDaemon.
 *
 * Creates a new #StoragedLinuxManagerISCSIInitiator instance.
 *
 * Returns: A new #StoragedLinuxManagerISCSIInitiator. Free with g_object_unref().
 */
StoragedLinuxManagerISCSIInitiator *
storaged_linux_manager_iscsi_initiator_new (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (g_object_new (STORAGED_TYPE_LINUX_MANAGER_ISCSI_INITIATOR,
                                                               "daemon", daemon,
                                                               NULL));
}

/**
 * storaged_linux_manager_iscsi_initiator_get_daemon:
 * @manager: A #StoragedLinuxManagerISCSIInitiator.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @manager.
 */
StoragedDaemon *
storaged_linux_manager_iscsi_initiator_get_daemon (StoragedLinuxManagerISCSIInitiator *manager)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER_ISCSI_INITIATOR (manager), NULL);
  return manager->daemon;
}

/**
 * storaged_linux_manager_iscsi_initiator_get_state:
 * @manager: A #StoragedLinuxManagerISCSIInitiator.
 *
 * Gets the state pointer for iSCSI module.
 *
 * Returns: A #StoragedISCSIState. Do not free, the structure is owned by
 * #StoragedModuleManager.
 */
static StoragedISCSIState *
storaged_linux_manager_iscsi_initiator_get_state (StoragedLinuxManagerISCSIInitiator *manager)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER_ISCSI_INITIATOR (manager), NULL);
  return manager->state;
}

static struct libiscsi_context *
storaged_linux_manager_iscsi_initiator_get_iscsi_context (StoragedLinuxManagerISCSIInitiator *manager)
{
  StoragedModuleManager *module_manager;
  StoragedISCSIState *state;

  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER_ISCSI_INITIATOR (manager), NULL);

  module_manager = storaged_daemon_get_module_manager (manager->daemon);
  state = storaged_module_manager_get_module_state_pointer (module_manager,
                                                            ISCSI_MODULE_NAME);

  return storaged_iscsi_state_get_libiscsi_context (state);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_get_initiator_name (StoragedManagerISCSIInitiator  *object,
                           GDBusMethodInvocation          *invocation)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);

  int initiator_name_fd = -1;
  int nbytes;
  gchar buf[64]; /* Do we need more? */
  gchar *initiator_name;
  size_t len;
  GString *content = NULL;

  /* Enter a critical section. */
  g_mutex_lock (&manager->initiator_config_mutex);

  initiator_name_fd = open (initiator_filename, O_RDONLY);
  if (initiator_name_fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             N_("Error opening %s: %s"),
                                             initiator_filename,
                                             strerror (errno));

      goto out;
    }

  /* Read the initiator name. */
  content = g_string_new (NULL);
  while ((nbytes = read (initiator_name_fd, buf, sizeof (buf) - 1)) > 0)
    {
      buf[nbytes] = '\0';
      content = g_string_append (content, buf);
    }

  if (nbytes < 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             N_("Error reading %s: %s"),
                                             initiator_filename,
                                             strerror (errno));

      goto out;
    }

  /* We don't want to create a scanner for this iscsi initiator name grammar.
   * So for simplicity, we just search for "InitiatorName=" prefix and if
   * found, we just shift the name pointer.  The string may contain whitespace
   * at the end; it's removed as well. */
  len = strlen (initiator_name_prefix);
  initiator_name = content->str;
  if (strncmp (content->str, initiator_name_prefix, len) == 0)
    {
      /* Shift the pointer by prefix length further. */
      initiator_name = &content->str[len];
    }
  /* Trim the whitespace at the end of the string. */
  while (g_ascii_isspace (content->str[content->len - 1]))
    {
      content->str[content->len - 1] = '\0';
      content->len--;
    }

  /* Return the initiator name */
  storaged_manager_iscsi_initiator_complete_get_initiator_name (object,
                                                                invocation,
                                                                initiator_name);

out:
  /* Leave the critical section. */
  g_mutex_unlock (&manager->initiator_config_mutex);

  /* Release the resources */
  g_string_free (content, TRUE);
  if (initiator_name_fd != -1)
    close (initiator_name_fd);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_set_initiator_name (StoragedManagerISCSIInitiator  *object,
                           GDBusMethodInvocation          *invocation,
                           const gchar                    *arg_name,
                           GVariant                       *arg_options)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);
  int initiator_name_fd = -1;
  GString *content = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (manager->daemon,
                                       NULL,
                                       iscsi_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required change iSCSI initiator name"),
                                       invocation);

  if (!arg_name || strlen (arg_name) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             N_("Empty initiator name"));
      return TRUE;
    }

  /* Enter a critical section. */
  g_mutex_lock (&manager->initiator_config_mutex);

  initiator_name_fd = open (initiator_filename,
                            O_WRONLY |
                            O_TRUNC |
                            S_IRUSR |
                            S_IWUSR |
                            S_IRGRP |
                            S_IROTH);

  if (initiator_name_fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             N_("Error opening %s: %s"),
                                             initiator_filename,
                                             strerror (errno));

      goto mutex_out;
    }

  /* Make a new initiator name */
  content = g_string_new (initiator_name_prefix);
  g_string_append_printf (content, "%s\n", arg_name);

  /* Write the new initiator name */
  if (write (initiator_name_fd, content->str, content->len) != (ssize_t) content->len)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             N_("Error writing to %s: %s"),
                                             initiator_filename,
                                             strerror (errno));

      goto mutex_out;
    }

  /* Finish with no error */
  storaged_manager_iscsi_initiator_complete_set_initiator_name (object,
                                                                invocation);

mutex_out:
  /* Leave the critical section. */
  g_mutex_unlock (&manager->initiator_config_mutex);

out:
  /* Release the resources */
  if (content)
    g_string_free (content, TRUE);
  if (initiator_name_fd != -1)
    close (initiator_name_fd);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

/**
 * discover_firmware:
 * @object: A #StoragedManagerISCSIInitiator
 * @nodes: A #GVariant containing an array with discovery results
 * @nodes_cnt: The count of discovered nodes
 * @errorstr: An error string pointer; may be NULL. Free with g_free().
 *
 * Performs firmware discovery (ppc or ibft).
 */
static gint

discover_firmware (StoragedManagerISCSIInitiator  *object,
                   GVariant                      **nodes,
                   gint                           *nodes_cnt,
                   gchar                         **errorstr)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);
  StoragedISCSIState *state = storaged_linux_manager_iscsi_initiator_get_state (manager);

  gint rval;
  struct libiscsi_context *ctx;
  struct libiscsi_node *found_nodes;

  /* Enter a critical section. */
  storaged_iscsi_state_lock_libiscsi_context (state);

  /* Discovery */
  ctx = storaged_linux_manager_iscsi_initiator_get_iscsi_context (manager);
  rval = libiscsi_discover_firmware (ctx,
                                     nodes_cnt,
                                     &found_nodes);

  if (rval == 0)
    *nodes = iscsi_libiscsi_nodes_to_gvariant (found_nodes, *nodes_cnt);
  else if (errorstr)
    *errorstr = g_strdup (libiscsi_get_error_string (ctx));

  /* Leave the critical section. */
  storaged_iscsi_state_unlock_libiscsi_context (state);

  /* Release the resources */
  iscsi_libiscsi_nodes_free (found_nodes);

  return rval;
}

static gboolean
handle_discover_send_targets (StoragedManagerISCSIInitiator  *object,
                              GDBusMethodInvocation          *invocation,
                              const gchar                    *arg_address,
                              const guint16                   arg_port,
                              GVariant                       *arg_options)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);
  StoragedISCSIState *state = storaged_linux_manager_iscsi_initiator_get_state (manager);
  GVariant *nodes = NULL;
  gchar *errorstr = NULL;
  const gchar *username = NULL;
  const gchar *password = NULL;
  const gchar *reverse_username = NULL;
  const gchar *reverse_password = NULL;
  gint err = 0;
  gint nodes_cnt = 0;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (manager->daemon,
                                       NULL,
                                       iscsi_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to discover targets"),
                                       invocation);

  /* Optional data for CHAP authentication */
  g_variant_lookup (arg_options, "username", "&s", &username);
  g_variant_lookup (arg_options, "password", "&s", &password);
  g_variant_lookup (arg_options, "reverse-username", "&s", reverse_username);
  g_variant_lookup (arg_options, "reverse-password", "&s", reverse_password);

  /* Enter a critical section. */
  storaged_iscsi_state_lock_libiscsi_context (state);

  /* Perform the discovery. */
  err = iscsi_discover_send_targets (manager->daemon,
                                     arg_address,
                                     arg_port,
                                     username,
                                     password,
                                     reverse_username,
                                     reverse_password,
                                     &nodes,
                                     &nodes_cnt,
                                     &errorstr);

  /* Leave the critical section. */
  storaged_iscsi_state_unlock_libiscsi_context (state);

  if (err != 0)
    {
      /* Discovery failed. */
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             iscsi_error_to_storaged_error (err),
                                             N_("Discovery failed: %s"),
                                             errorstr);
      goto out;
    }

  /* Return discovered portals. */
  storaged_manager_iscsi_initiator_complete_discover_send_targets (object,
                                                                   invocation,
                                                                   nodes,
                                                                   nodes_cnt);

out:
  g_free ((gpointer) errorstr);

  /* Indicate that we handled the method invocation. */
  return TRUE;
}

static gboolean
handle_discover_firmware (StoragedManagerISCSIInitiator  *object,
                          GDBusMethodInvocation          *invocation,
                          GVariant                       *arg_options)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);
  GVariant *nodes = NULL;
  gint err = 0;
  gint nodes_cnt = 0;
  gchar *errorstr = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (manager->daemon,
                                       NULL,
                                       iscsi_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to discover firmware targets"),
                                       invocation);

  /* Perform the discovery. */
  err = discover_firmware (object,
                           &nodes,
                           &nodes_cnt,
                           &errorstr);

  if (err != 0)
    {
      /* Discovery failed. */
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             iscsi_error_to_storaged_error (err),
                                             N_("Discovery failed: %s"),
                                             errorstr);
      g_free ((gpointer) errorstr);
      goto out;
    }

  /* Return discovered portals. */
  storaged_manager_iscsi_initiator_complete_discover_firmware (object,
                                                               invocation,
                                                               nodes,
                                                               nodes_cnt);

out:
  /* Indicate that we handled the method invocation. */
  return TRUE;
}

static gboolean
handle_login (StoragedManagerISCSIInitiator  *object,
              GDBusMethodInvocation          *invocation,
              const gchar                    *arg_name,
              gint                            arg_tpgt,
              const gchar                    *arg_address,
              gint                            arg_port,
              const gchar                    *arg_iface,
              GVariant                       *arg_options)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);
  StoragedISCSIState *state = storaged_linux_manager_iscsi_initiator_get_state (manager);
  gint err = 0;
  const gchar *username = NULL;
  const gchar *password = NULL;
  const gchar *reverse_username = NULL;
  const gchar *reverse_password = NULL;
  gchar *errorstr = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (manager->daemon,
                                       NULL,
                                       iscsi_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to perform iSCSI login"),
                                       invocation);

  /* Optional data for CHAP authentication */
  g_variant_lookup (arg_options, "username", "&s", &username);
  g_variant_lookup (arg_options, "password", "&s", &password);
  g_variant_lookup (arg_options, "reverse-username", "&s", reverse_username);
  g_variant_lookup (arg_options, "reverse-password", "&s", reverse_password);

  /* Enter a critical section. */
  storaged_iscsi_state_lock_libiscsi_context (state);

  /* Login */
  err = iscsi_login (manager->daemon,
                     arg_name,
                     arg_tpgt,
                     arg_address,
                     arg_port,
                     arg_iface,
                     username,
                     password,
                     reverse_username,
                     reverse_password,
                     &errorstr);

  /* Leave the critical section. */
  storaged_iscsi_state_unlock_libiscsi_context (state);

  if (err != 0)
    {
      /* Login failed. */
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             iscsi_error_to_storaged_error (err),
                                             N_("Login failed: %s"),
                                             errorstr);
      goto out;
    }

  /* Complete DBus call. */
  storaged_manager_iscsi_initiator_complete_login (object,
                                                   invocation);

out:
  g_free ((gpointer) errorstr);

  /* Indicate that we handled the method invocation. */
  return TRUE;
}

static gboolean
handle_logout(StoragedManagerISCSIInitiator  *object,
              GDBusMethodInvocation          *invocation,
              const gchar                    *arg_name,
              gint                            arg_tpgt,
              const gchar                    *arg_address,
              gint                            arg_port,
              const gchar                    *arg_iface,
              GVariant                       *arg_options)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);
  StoragedISCSIState *state = storaged_linux_manager_iscsi_initiator_get_state (manager);
  gint err = 0;
  gchar *errorstr = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (manager->daemon,
                                       NULL,
                                       iscsi_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to perform iSCSI logout"),
                                       invocation);

  /* Enter a critical section. */
  storaged_iscsi_state_lock_libiscsi_context (state);

  /* Logout */
  err = iscsi_logout (manager->daemon,
                      arg_name,
                      arg_tpgt,
                      arg_address,
                      arg_port,
                      arg_iface,
                      &errorstr);

  /* Leave the critical section. */
  storaged_iscsi_state_unlock_libiscsi_context (state);

  if (err != 0)
    {
      /* Logout failed. */
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             iscsi_error_to_storaged_error (err),
                                             N_("Logout failed: %s"),
                                             errorstr);
      goto out;
    }

  /* Complete DBus call. */
  storaged_manager_iscsi_initiator_complete_logout (object,
                                                    invocation);

out:
  g_free ((gpointer) errorstr);

  /* Indicate that we handled the method invocation. */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_manager_iscsi_initiator_iface_init (StoragedManagerISCSIInitiatorIface *iface)
{
  iface->handle_get_initiator_name = handle_get_initiator_name;
  iface->handle_set_initiator_name = handle_set_initiator_name;
  iface->handle_discover_send_targets = handle_discover_send_targets;
  iface->handle_discover_firmware = handle_discover_firmware;
  iface->handle_login = handle_login;
  iface->handle_logout = handle_logout;
}
