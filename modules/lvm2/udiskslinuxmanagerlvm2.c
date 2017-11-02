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
#include <alloca.h>

#include <glib/gi18n-lib.h>

#include <src/udiskslogging.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslinuxblockobject.h>

#include "udiskslvm2types.h"
#include "udiskslinuxmanagerlvm2.h"
#include "udiskslvm2daemonutil.h"
#include "udiskslvm2util.h"
#include "udisks-lvm2-generated.h"
#include "jobhelpers.h"

/**
 * SECTION:udiskslinuxmanagerlvm2
 * @title: UDisksLinuxManagerLVM2
 * @short_description: Linux implementation of #UDisksLinuxManagerLVM2
 *
 * This type provides an implementation of the #UDisksLinuxManagerLVM2
 * interface on Linux.
 */

typedef struct _UDisksLinuxManagerLVM2Class   UDisksLinuxManagerLVM2Class;

/**
 * UDisksLinuxManagerLVM2:
 *
 * The #UDisksLinuxManagerLVM2 structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxManagerLVM2
{
  UDisksManagerLVM2Skeleton parent_instance;

  UDisksDaemon *daemon;
};

struct _UDisksLinuxManagerLVM2Class
{
  UDisksManagerLVM2SkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON
};

static void udisks_linux_manager_lvm2_iface_init (UDisksManagerLVM2Iface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerLVM2, udisks_linux_manager_lvm2, UDISKS_TYPE_MANAGER_LVM2_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_LVM2, udisks_linux_manager_lvm2_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_manager_lvm2_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  UDisksLinuxManagerLVM2 *manager = UDISKS_LINUX_MANAGER_LVM2 (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_manager_lvm2_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_lvm2_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  UDisksLinuxManagerLVM2 *manager = UDISKS_LINUX_MANAGER_LVM2 (object);

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
udisks_linux_manager_lvm2_init (UDisksLinuxManagerLVM2 *manager)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_manager_lvm2_class_init (UDisksLinuxManagerLVM2Class *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = udisks_linux_manager_lvm2_set_property;
  gobject_class->get_property = udisks_linux_manager_lvm2_get_property;

  /**
   * UDisksLinuxManager:daemon:
   *
   * The #UDisksDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_linux_manager_lvm2_new:
 * @daemon: A #UDisksDaemon.
 *
 * Creates a new #UDisksLinuxManagerLVM2 instance.
 *
 * Returns: A new #UDisksLinuxManagerLVM2. Free with g_object_unref().
 */
UDisksLinuxManagerLVM2 *
udisks_linux_manager_lvm2_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_LINUX_MANAGER_LVM2 (g_object_new (UDISKS_TYPE_LINUX_MANAGER_LVM2,
                                                  "daemon", daemon,
                                                  NULL));
}

/**
 * udisks_linux_manager_lvm2_get_daemon:
 * @manager: A #UDisksLinuxManagerLVM2.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @manager.
 */
UDisksDaemon *
udisks_linux_manager_lvm2_get_daemon (UDisksLinuxManagerLVM2 *manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER_LVM2 (manager), NULL);
  return manager->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksObject *
wait_for_volume_group_object (UDisksDaemon *daemon,
                              gpointer      user_data)
{
  const gchar *name = user_data;

  return UDISKS_OBJECT (udisks_daemon_util_lvm2_find_volume_group_object (daemon, name));
}

static gboolean
handle_volume_group_create (UDisksManagerLVM2     *_object,
                            GDBusMethodInvocation *invocation,
                            const gchar           *arg_name,
                            const gchar *const    *arg_blocks,
                            GVariant              *arg_options)
{
  UDisksLinuxManagerLVM2 *manager = UDISKS_LINUX_MANAGER_LVM2(_object);
  uid_t caller_uid;
  GError *error = NULL;
  GList *blocks = NULL;
  GList *l;
  guint n;
  UDisksObject *group_object = NULL;
  const gchar **pvs = NULL;
  VGJobData data;

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_manager_lvm2_get_daemon (manager),
                                     NULL,
                                     lvm2_policy_action_id,
                                     arg_options,
                                     N_("Authentication is required to create a volume group"),
                                     invocation);

  if (arg_blocks == NULL || *arg_blocks == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "List of block devices is empty.");
      goto out;
    }

  /* Collect and validate block objects
   *
   * Also, check we can open the block devices at the same time - this
   * is to avoid start deleting half the block devices while the other
   * half is already in use.
   */
  for (n = 0; arg_blocks != NULL && arg_blocks[n] != NULL; n++)
    {
      UDisksObject *object = NULL;
      UDisksBlock *block = NULL;

      object = udisks_daemon_find_object (manager->daemon, arg_blocks[n]);
      if (object == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Invalid object path %s at index %u",
                                                 arg_blocks[n], n);
          goto out;
        }

      block = udisks_object_get_block (object);
      if (block == NULL)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Object path %s for index %u is not a block device",
                                                 arg_blocks[n], n);
          goto out;
        }

      if (!udisks_daemon_util_lvm2_block_is_unused (block, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }

      blocks = g_list_prepend (blocks, block); /* adopts ownership */
      g_object_unref (object);
    }
  blocks = g_list_reverse (blocks);

  /* XXX: is it okay to allocate memory on stack here? */
  pvs = (const gchar**) alloca (sizeof(gchar*) * n);

  /* wipe existing devices */
  for (l = blocks; l != NULL; l = l->next)
    {
      if (!udisks_daemon_util_lvm2_wipe_block (manager->daemon, UDISKS_BLOCK (l->data), &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
    }

  /* Create the PVs... */
  for (n = 0, l = blocks; l != NULL; l = l->next, n++)
    {
      UDisksBlock *block = UDISKS_BLOCK (l->data);
      PVJobData pv_data;
      pvs[n] = udisks_block_get_device (block);
      pv_data.path = pvs[n];
      if (!udisks_daemon_launch_threaded_job_sync (manager->daemon,
                                                   NULL,
                                                   "lvm-pv-create",
                                                   caller_uid,
                                                   pvcreate_job_func,
                                                   &pv_data,
                                                   NULL, /* user_data_free_func */
                                                   NULL, /* GCancellable */
                                                   &error))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error creating a physical volume: %s",
                                                 error->message);
          goto out;
        }
    }
  pvs[n] = NULL;

  /* Create the volume group... */
  data.vg_name = arg_name;
  data.pvs = pvs;

  if (!udisks_daemon_launch_threaded_job_sync (manager->daemon,
                                               NULL,
                                               "lvm-vg-create",
                                               caller_uid,
                                               vgcreate_job_func,
                                               &data,
                                               NULL, /* user_data_free_func */
                                               NULL, /* GCancellable */
                                               &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error creating volume group: %s",
                                             error->message);
      goto out;
    }

  for (l = blocks; l != NULL; l = l->next)
    {
      UDisksBlock *block = UDISKS_BLOCK (l->data);
      UDisksObject *object_for_block;
      object_for_block = udisks_daemon_util_dup_object (block, &error);
      if (object_for_block != NULL)
        udisks_linux_block_object_trigger_uevent (UDISKS_LINUX_BLOCK_OBJECT (object_for_block));
      g_object_unref (object_for_block);
    }

  /* ... then, sit and wait for the object to show up */
  group_object = udisks_daemon_wait_for_object_sync (manager->daemon,
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

  udisks_manager_lvm2_complete_volume_group_create (_object,
                                                    invocation,
                                                    g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object)));
 out:
  g_list_free_full (blocks, g_object_unref);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_manager_lvm2_iface_init (UDisksManagerLVM2Iface *iface)
{
  iface->handle_volume_group_create = handle_volume_group_create;
}
