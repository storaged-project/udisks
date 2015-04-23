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
  GMutex iscsi_mutex;
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
storaged_linux_manager_iscsi_initiator_class_init (StoragedLinuxManagerISCSIInitiatorClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = storaged_linux_manager_iscsi_initiator_get_property;
  gobject_class->set_property = storaged_linux_manager_iscsi_initiator_set_property;

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

/* ---------------------------------------------------------------------------------------------------- */

static gboolean handle_get_initiator_name (StoragedManagerISCSIInitiator  *object,
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
  g_mutex_lock (&manager->iscsi_mutex);

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
  g_mutex_unlock (&manager->iscsi_mutex);

  /* Release the resources */
  g_string_free (content, TRUE);
  if (initiator_name_fd != -1)
    close (initiator_name_fd);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean handle_set_initiator_name(StoragedManagerISCSIInitiator  *object,
                                          GDBusMethodInvocation          *invocation,
                                          const gchar                     *arg_name)
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
  g_mutex_lock (&manager->iscsi_mutex);

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
  g_mutex_unlock (&manager->iscsi_mutex);

  /* Release the resources */
  g_string_free (content, TRUE);
  if (initiator_name_fd != -1)
    close (initiator_name_fd);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_manager_iscsi_initiator_iface_init (StoragedManagerISCSIInitiatorIface *iface)
{
  iface->handle_get_initiator_name = handle_get_initiator_name;
  iface->handle_set_initiator_name = handle_set_initiator_name;
}
