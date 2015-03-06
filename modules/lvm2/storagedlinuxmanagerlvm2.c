/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014 Tomas Bzatek <tbzatek@redhat.com>
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
 *
 */

#include "config.h"

#include <sys/types.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <glib/gi18n-lib.h>

#include <src/storagedlogging.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlinuxblockobject.h>

#include "storagedlvm2types.h"
#include "storagedlinuxmanagerlvm2.h"
#include "storagedlvm2daemonutil.h"
#include "module-lvm2-generated.h"

/**
 * SECTION:storagedlinuxmanagerlvm2
 * @title: StoragedLinuxManagerLVM2
 * @short_description: Linux implementation of #StoragedLinuxManagerLVM2
 *
 * This type provides an implementation of the #StoragedLinuxManagerLVM2
 * interface on Linux.
 */

typedef struct _StoragedLinuxManagerLVM2Class   StoragedLinuxManagerLVM2Class;

/**
 * StoragedLinuxManagerLVM2:
 *
 * The #StoragedLinuxManagerLVM2 structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxManagerLVM2
{
  StoragedManagerLVM2Skeleton parent_instance;

  StoragedDaemon *daemon;
};

struct _StoragedLinuxManagerLVM2Class
{
  StoragedManagerLVM2SkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON
};

static void storaged_linux_manager_lvm2_iface_init (StoragedManagerLVM2Iface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxManagerLVM2, storaged_linux_manager_lvm2, STORAGED_TYPE_MANAGER_LVM2_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MANAGER_LVM2, storaged_linux_manager_lvm2_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_manager_lvm2_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  StoragedLinuxManagerLVM2 *manager = STORAGED_LINUX_MANAGER_LVM2 (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_manager_lvm2_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_lvm2_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  StoragedLinuxManagerLVM2 *manager = STORAGED_LINUX_MANAGER_LVM2 (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (manager->daemon == NULL);
      /* we don't take a reference to the daemon */
      manager->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_manager_lvm2_init (StoragedLinuxManagerLVM2 *manager)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_manager_lvm2_class_init (StoragedLinuxManagerLVM2Class *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = storaged_linux_manager_lvm2_set_property;
  gobject_class->get_property = storaged_linux_manager_lvm2_get_property;

  /**
   * StoragedLinuxManager:daemon:
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

/**
 * storaged_linux_manager_lvm2_new:
 * @daemon: A #StoragedDaemon.
 *
 * Creates a new #StoragedLinuxManagerLVM2 instance.
 *
 * Returns: A new #StoragedLinuxManagerLVM2. Free with g_object_unref().
 */
StoragedLinuxManagerLVM2 *
storaged_linux_manager_lvm2_new (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_LINUX_MANAGER_LVM2 (g_object_new (STORAGED_TYPE_LINUX_MANAGER_LVM2,
                                                    "daemon", daemon,
                                                    NULL));
}

/**
 * storaged_linux_manager_lvm2_get_daemon:
 * @manager: A #StoragedLinuxManagerLVM2.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @manager.
 */
StoragedDaemon *
storaged_linux_manager_lvm2_get_daemon (StoragedLinuxManagerLVM2 *manager)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MANAGER_LVM2 (manager), NULL);
  return manager->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

static StoragedObject *
wait_for_volume_group_object (StoragedDaemon *daemon,
                              gpointer        user_data)
{
  const gchar *name = user_data;

  return STORAGED_OBJECT (storaged_daemon_util_lvm2_find_volume_group_object (daemon, name));
}

static gboolean
handle_volume_group_create (StoragedManagerLVM2     *_object,
                            GDBusMethodInvocation   *invocation,
                            const gchar             *arg_name,
                            const gchar *const      *arg_blocks,
                            GVariant                *arg_options)
{
  StoragedLinuxManagerLVM2 *manager = STORAGED_LINUX_MANAGER_LVM2(_object);
  uid_t caller_uid;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;
  GList *blocks = NULL;
  GList *l;
  guint n;
  gchar *escaped_name = NULL;
  GString *str = NULL;
  gint status;
  gchar *error_message = NULL;
  StoragedObject *group_object = NULL;

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  message = N_("Authentication is required to create a volume group");
  action_id = "org.storaged.Storaged.lvm2.manage-lvm";
  if (!storaged_daemon_util_check_authorization_sync (manager->daemon,
                                                      NULL,
                                                      action_id,
                                                      arg_options,
                                                      message,
                                                      invocation))
    goto out;

  /* Collect and validate block objects
   *
   * Also, check we can open the block devices at the same time - this
   * is to avoid start deleting half the block devices while the other
   * half is already in use.
   */
  for (n = 0; arg_blocks != NULL && arg_blocks[n] != NULL; n++)
    {
      StoragedObject *object = NULL;
      StoragedBlock *block = NULL;

      object = storaged_daemon_find_object (manager->daemon, arg_blocks[n]);
      if (object == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Invalid object path %s at index %d",
                                                 arg_blocks[n], n);
          goto out;
        }

      block = storaged_object_get_block (object);
      if (block == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Object path %s for index %d is not a block device",
                                                 arg_blocks[n], n);
          goto out;
        }

      if (!storaged_daemon_util_lvm2_block_is_unused (block, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }

      blocks = g_list_prepend (blocks, block); /* adopts ownership */
      g_object_unref (object);
    }
  blocks = g_list_reverse (blocks);

  /* wipe existing devices */
  for (l = blocks; l != NULL; l = l->next)
    {
      if (!storaged_daemon_util_lvm2_wipe_block (manager->daemon, STORAGED_BLOCK (l->data), &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
    }

  /* Create the volume group... */
  escaped_name = storaged_daemon_util_escape_and_quote (arg_name);
  str = g_string_new ("vgcreate");
  g_string_append_printf (str, " %s", escaped_name);
  for (l = blocks; l != NULL; l = l->next)
    {
      StoragedBlock *block = STORAGED_BLOCK (l->data);
      gchar *escaped_device;
      escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (block));
      g_string_append_printf (str, " %s", escaped_device);
      g_free (escaped_device);
    }

  if (!storaged_daemon_launch_spawned_job_sync (manager->daemon,
                                                NULL,
                                                "lvm-vg-create", caller_uid,
                                                NULL, /* cancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                &status,
                                                &error_message,
                                                NULL, /* input_string */
                                                "%s",
                                                str->str))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error creating volume group: %s",
                                             error_message);
      g_free (error_message);
      goto out;
    }

  for (l = blocks; l != NULL; l = l->next)
    {
      StoragedBlock *block = STORAGED_BLOCK (l->data);
      StoragedObject *object_for_block;
      object_for_block = storaged_daemon_util_dup_object (block, &error);
      if (object_for_block != NULL)
        storaged_linux_block_object_trigger_uevent (STORAGED_LINUX_BLOCK_OBJECT (object_for_block));
      g_object_unref (object_for_block);
    }

  /* ... then, sit and wait for the object to show up */
  group_object = storaged_daemon_wait_for_object_sync (manager->daemon,
                                                       wait_for_volume_group_object,
                                                       (gpointer) arg_name,
                                                       NULL,
                                                       10, /* timeout_seconds */
                                                       &error);
  if (group_object == NULL)
    {
      g_prefix_error (&error,
                      "Error waiting for volume group object for %s",
                      arg_name);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  storaged_manager_lvm2_complete_volume_group_create (_object,
                                                      invocation,
                                                      g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object)));

 out:
  if (str != NULL)
    g_string_free (str, TRUE);
  g_list_free_full (blocks, g_object_unref);
  g_free (escaped_name);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_manager_lvm2_iface_init (StoragedManagerLVM2Iface *iface)
{
  iface->handle_volume_group_create = handle_volume_group_create;
}
