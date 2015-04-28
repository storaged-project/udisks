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

#include <config.h>

#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <libiscsi.h>

#include <src/storageddaemon.h>
#include <src/storagedlogging.h>

#include "storaged-iscsi-generated.h"
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
  struct libiscsi_context *iscsi_context;
  GMutex initiator_config_mutex;  /* We use separate mutex for configuration
                                     file because libiscsi doesn't provide us
                                     any API for this. */
  GMutex libiscsi_mutex;
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
const gchar *iscsi_nodes_fmt = "a(sisis)";
const gchar *iscsi_node_fmt = "(sisis)";

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
    }
}

static void
storaged_linux_manager_iscsi_initiator_dispose (GObject *object)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);

  if (manager->iscsi_context)
    {
      libiscsi_cleanup (manager->iscsi_context);
      manager->iscsi_context = NULL;
    }

  G_OBJECT_CLASS (storaged_linux_manager_iscsi_initiator_parent_class)->dispose (object);
}

static void
storaged_linux_manager_iscsi_initiator_finalize (GObject *object)
{
  G_OBJECT_CLASS (storaged_linux_manager_iscsi_initiator_parent_class)->finalize (object);
}

static void
storaged_linux_manager_iscsi_initiator_class_init (StoragedLinuxManagerISCSIInitiatorClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = storaged_linux_manager_iscsi_initiator_get_property;
  gobject_class->set_property = storaged_linux_manager_iscsi_initiator_set_property;
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
  manager->iscsi_context = NULL;

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

static struct libiscsi_context *
storaged_linux_manager_iscsi_initiator_get_iscsi_context (StoragedLinuxManagerISCSIInitiator *manager)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER_ISCSI_INITIATOR (manager), NULL);
  if (!manager->iscsi_context)
    manager->iscsi_context = libiscsi_init ();
  return manager->iscsi_context;
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

  /* Enter a critical section */
  g_mutex_lock (&manager->initiator_config_mutex);

  initiator_name_fd = open (initiator_filename, O_RDONLY);
  if (initiator_name_fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error opening %s: %s",
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
                                             "Error reading %s: %s",
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
  /* Leave the critical section */
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
                           const gchar                    *arg_name)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);
  int initiator_name_fd = -1;
  GString *content;

  if (!arg_name || strlen (arg_name) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Empty initiator name");
      return TRUE;
    }

  /* Enter a critical section */
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
                                             "Error opening %s: %s",
                                             initiator_filename,
                                             strerror (errno));

      goto out;
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
                                             "Error writing to %s: %s",
                                             initiator_filename,
                                             strerror (errno));

      goto out;
    }

  /* Finish with no error */
  storaged_manager_iscsi_initiator_complete_set_initiator_name (object,
                                                                invocation);

out:
  /* Leave the critical section */
  g_mutex_unlock (&manager->initiator_config_mutex);

  /* Release the resources */
  g_string_free (content, TRUE);
  if (initiator_name_fd != -1)
    close (initiator_name_fd);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static GVariant *
libiscsi_nodes_to_gvariant (const struct libiscsi_node *nodes,
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

static void
libiscsi_nodes_free (const struct libiscsi_node *nodes)
{
  g_free ((gpointer) nodes);
}

static gint
discover_send_targets (StoragedManagerISCSIInitiator  *object,
                       const gchar                    *address,
                       const guint16                   port,
                       struct libiscsi_auth_info      *auth_info,
                       GVariant                      **nodes,
                       gint                           *nodes_cnt)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);

  gint rval;
  struct libiscsi_context *ctx;
  struct libiscsi_node *found_nodes;

  /* Enter a critical section */
  g_mutex_lock (&manager->libiscsi_mutex);

  /* Discovery */
  ctx = storaged_linux_manager_iscsi_initiator_get_iscsi_context (manager);
  rval = libiscsi_discover_sendtargets (ctx,
                                        address,
                                        port,
                                        auth_info,
                                        nodes_cnt,
                                        &found_nodes);

  if (rval == 0)
    *nodes = libiscsi_nodes_to_gvariant (found_nodes,
                                         *nodes_cnt);

  /* Leave the critical section */
  g_mutex_unlock (&manager->libiscsi_mutex);

  /* Release the resources */
  libiscsi_nodes_free (found_nodes);

  return rval;
}

static gint
discover_firmware (StoragedManagerISCSIInitiator  *object,
                   GVariant                      **nodes,
                   gint                           *nodes_cnt)
{
  StoragedLinuxManagerISCSIInitiator *manager = STORAGED_LINUX_MANAGER_ISCSI_INITIATOR (object);

  gint rval;
  struct libiscsi_context *ctx;
  struct libiscsi_node *found_nodes;

  /* Enter a critical section */
  g_mutex_lock (&manager->libiscsi_mutex);

  /* Discovery */
  ctx = storaged_linux_manager_iscsi_initiator_get_iscsi_context (manager);
  rval = libiscsi_discover_firmware (ctx,
                                     nodes_cnt,
                                     &found_nodes);

  if (rval == 0)
    *nodes = libiscsi_nodes_to_gvariant (found_nodes, *nodes_cnt);

  /* Leave the critical section */
  g_mutex_unlock (&manager->libiscsi_mutex);

  /* Release the resources */
  libiscsi_nodes_free (found_nodes);

  return rval;
}

static gboolean
handle_discover_send_targets_no_auth (StoragedManagerISCSIInitiator  *object,
                                      GDBusMethodInvocation          *invocation,
                                      const gchar                    *arg_address,
                                      const guint16                   arg_port)
{
  gint err;
  gint nodes_cnt = 0;
  struct libiscsi_auth_info auth_info = { libiscsi_auth_none };
  GVariant *nodes = NULL;

  /* Perform the discovery. */
  err = discover_send_targets (object,
                               arg_address,
                               arg_port,
                               &auth_info,
                               &nodes,
                               &nodes_cnt);

  if (err == 0)
    {
      /* Return discovered portals. */
      storaged_manager_iscsi_initiator_complete_discover_send_targets_no_auth (object,
                                                                               invocation,
                                                                               nodes,
                                                                               nodes_cnt);
    }
  else
    {
      /* Discovery failed. */
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Discovery failed: %s",
                                             strerror (err));
    }

  /* Indicate that we handled the method invocation. */
  return TRUE;
}

static gboolean
handle_discover_send_targets_chap (StoragedManagerISCSIInitiator  *object,
                                   GDBusMethodInvocation          *invocation,
                                   const gchar                    *arg_address,
                                   guint16                         arg_port,
                                   const gchar                    *arg_username,
                                   const gchar                    *arg_password,
                                   const gchar                    *arg_reverse_username,
                                   const gchar                    *arg_reverse_password)
{
  gint err;
  gint nodes_cnt = 0;
  struct libiscsi_auth_info auth_info;
  GVariant *nodes = NULL;

  /* Fill in authentication information */
  auth_info.method = libiscsi_auth_chap;

  /* Username */
  if (strlen (arg_username) > LIBISCSI_VALUE_MAXLEN)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Username too long");
      goto out;
    }
  strcpy(auth_info.chap.username, arg_username);

  /* Password */
  if (strlen (arg_username) > LIBISCSI_VALUE_MAXLEN)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Password too long");
      goto out;
    }
  strcpy(auth_info.chap.password, arg_password);

  /* Reverse username */
  if (strlen (arg_reverse_username) > LIBISCSI_VALUE_MAXLEN)
    {
      g_dbus_method_invocation_return_error (invocation,
                                            STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Reverse username too long");
      goto out;
    }
  strcpy(auth_info.chap.reverse_username, arg_reverse_username);

  /* Reverse password */
  if (strlen (arg_reverse_password) > LIBISCSI_VALUE_MAXLEN)
    {
      g_dbus_method_invocation_return_error (invocation,
                                            STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Reverse password too long");
      goto out;
    }
  strcpy(auth_info.chap.reverse_password, arg_reverse_password);

  /* Perform the discovery. */
  err = discover_send_targets (object,
                               arg_address,
                               arg_port,
                               &auth_info,
                               &nodes,
                               &nodes_cnt);

  if (err != 0)
    {
      /* Discovery failed. */
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Discovery failed: %s",
                                             strerror (err));

      goto out;
    }

  /* Return discovered portals. */
  storaged_manager_iscsi_initiator_complete_discover_send_targets_no_auth (object,
                                                                           invocation,
                                                                           nodes,
                                                                           nodes_cnt);
out:
  /* Indicate that we handled the method invocation. */
  return TRUE;
}

static gboolean
handle_discover_firmware (StoragedManagerISCSIInitiator  *object,
                          GDBusMethodInvocation          *invocation)
{
  gint err;
  gint nodes_cnt = 0;
  GVariant *nodes = NULL;

  /* Perform the discovery. */
  err = discover_firmware (object,
                           &nodes,
                           &nodes_cnt);

  if (err != 0)
    {
      /* Discovery failed. */
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Discovery failed: %s",
                                             strerror (err));

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

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_manager_iscsi_initiator_iface_init (StoragedManagerISCSIInitiatorIface *iface)
{
  iface->handle_get_initiator_name = handle_get_initiator_name;
  iface->handle_set_initiator_name = handle_set_initiator_name;
  iface->handle_discover_send_targets_no_auth = handle_discover_send_targets_no_auth;
  iface->handle_discover_send_targets_chap = handle_discover_send_targets_chap;
  iface->handle_discover_firmware = handle_discover_firmware;
}
