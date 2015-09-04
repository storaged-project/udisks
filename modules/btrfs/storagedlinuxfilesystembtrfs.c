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

#include <glib/gi18n.h>

#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlogging.h>
#include <blockdev/btrfs.h>

#include "storagedlinuxfilesystembtrfs.h"
#include "storaged-btrfs-generated.h"
#include "storagedbtrfsutil.h"

/**
 * SECTION:storagedlinuxfilesystembtrfs
 * @title: StoragedLinuxFilesystemBTRFS
 * @short_description: Linux implementation of #StoragedFilesystemBTRFS
 *
 * This type provides an implementation of #StoragedFilesystemBTRFS interface
 * on Linux.
 */

/**
 * StoragedLinuxFilesystemBTRFS:
 *
 * The #StoragedLinuxFilesystemBTRFS structure contains only private data and
 * should be only accessed using provided API.
 */
struct _StoragedLinuxFilesystemBTRFS{
  StoragedFilesystemBTRFSSkeleton parent_instance;

  StoragedDaemon *daemon;
};

struct _StoragedLinuxFilesystemBTRFSClass {
  StoragedFilesystemBTRFSSkeletonClass parent_class;
};

typedef gboolean (*btrfs_subvolume_func)(gchar *mount_point, gchar *name, GError **error);
typedef gboolean (*btrfs_device_func)(gchar *mountpoint, gchar *device, GError **error);

enum
{
  PROP_0,
  PROP_DAEMON,
  N_PROPERTIES
};

static void storaged_linux_filesystem_btrfs_iface_init (StoragedFilesystemBTRFSIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxFilesystemBTRFS, storaged_linux_filesystem_btrfs,
                         STORAGED_TYPE_FILESYSTEM_BTRFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_FILESYSTEM_BTRFS,
                         storaged_linux_filesystem_btrfs_iface_init));

static void
storaged_linux_filesystem_btrfs_get_property (GObject    *object,
                                              guint       property_id,
                                              GValue     *value,
                                              GParamSpec *pspec)
{
  StoragedLinuxFilesystemBTRFS *l_fs_btrfs = STORAGED_LINUX_FILESYSTEM_BTRFS (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_filesystem_btrfs_get_daemon (l_fs_btrfs));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_filesystem_btrfs_set_property (GObject      *object,
                                              guint         property_id,
                                              const GValue *value,
                                              GParamSpec   *pspec)
{
  StoragedLinuxFilesystemBTRFS *l_fs_btrfs = STORAGED_LINUX_FILESYSTEM_BTRFS (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_assert (l_fs_btrfs->daemon == NULL);
      /* We don't take a reference to the daemon. */
      l_fs_btrfs->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_filesystem_btrfs_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_filesystem_btrfs_parent_class))
    G_OBJECT_CLASS (storaged_linux_filesystem_btrfs_parent_class)->dispose (object);
}

static void
storaged_linux_filesystem_btrfs_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_filesystem_btrfs_parent_class))
    G_OBJECT_CLASS (storaged_linux_filesystem_btrfs_parent_class)->finalize (object);
}

static void
storaged_linux_filesystem_btrfs_class_init (StoragedLinuxFilesystemBTRFSClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = storaged_linux_filesystem_btrfs_get_property;
  gobject_class->set_property = storaged_linux_filesystem_btrfs_set_property;
  gobject_class->dispose = storaged_linux_filesystem_btrfs_dispose;
  gobject_class->finalize = storaged_linux_filesystem_btrfs_finalize;

  /**
   * StoragedLinuxManager:daemon
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
storaged_linux_filesystem_btrfs_init (StoragedLinuxFilesystemBTRFS *l_fs_btrfs)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (l_fs_btrfs),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * storaged_linux_filesystem_btrfs_new:
 *
 * Creates a new #StoragedLinuxFilesystemBTRFS instance.
 *
 * Returns: A new #StoragedLinuxFilesystemBTRFS. Free with g_object_unref().
 */
StoragedLinuxFilesystemBTRFS *
storaged_linux_filesystem_btrfs_new (void)
{
  return STORAGED_LINUX_FILESYSTEM_BTRFS (g_object_new (STORAGED_TYPE_LINUX_FILESYSTEM_BTRFS,
                                                        NULL));
}

/**
 * storaged_linux_filesystem_btrfs_get_daemon:
 * @fs: A #StoragedLinuxFilesystemBTRFS.
 *
 * Gets the daemon used by @l_fs_btrfs.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @l_fs_btrfs.
 */
StoragedDaemon *
storaged_linux_filesystem_btrfs_get_daemon (StoragedLinuxFilesystemBTRFS *l_fs_btrfs)
{
  GError *error = NULL;
  StoragedLinuxBlockObject *object;
  StoragedDaemon *daemon = NULL;

  g_return_val_if_fail (STORAGED_IS_LINUX_FILESYSTEM_BTRFS (l_fs_btrfs), NULL);

  object = storaged_daemon_util_dup_object (l_fs_btrfs, &error);
  if (object)
    {
      daemon = storaged_linux_block_object_get_daemon (object);
      g_clear_object (&object);
    }
  else
    {
      storaged_error ("%s", error->message);
      g_error_free (error);
    }

  return daemon;
}

/**
 * storaged_linux_filesystem_btrfs_update:
 * @l_fs_btrfs: A #StoragedLinuxFilesystemBTRFS.
 * @object: The enclosing #StoragedLlinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if the configuration has changed, %FALSE otherwise.
 */
gboolean
storaged_linux_filesystem_btrfs_update (StoragedLinuxFilesystemBTRFS *l_fs_btrfs,
                                        StoragedLinuxBlockObject     *object)
{
  StoragedFilesystemBTRFS *fs_btrfs = STORAGED_FILESYSTEM_BTRFS (l_fs_btrfs);
  BDBtrfsFilesystemInfo *btrfs_info = NULL;
  GError *error = NULL;
  gchar *dev_file = NULL;
  gboolean rval = FALSE;

  g_return_val_if_fail (STORAGED_IS_LINUX_FILESYSTEM_BTRFS (fs_btrfs), FALSE);
  g_return_val_if_fail (STORAGED_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  dev_file = storaged_linux_block_object_get_device_file (object);
  if (! dev_file)
    {
      rval = FALSE;
      goto out;
    }

  btrfs_info = bd_btrfs_filesystem_info (dev_file, &error);

  if (! btrfs_info)
    {
      storaged_error ("Can't get BTRFS filesystem info for %s", dev_file);
      rval = FALSE;
      goto out;
    }

  /* Update the interface */
  storaged_filesystem_btrfs_set_label (fs_btrfs, btrfs_info->label);
  storaged_filesystem_btrfs_set_uuid (fs_btrfs, btrfs_info->uuid);
  storaged_filesystem_btrfs_set_num_devices (fs_btrfs, btrfs_info->num_devices);
  storaged_filesystem_btrfs_set_used (fs_btrfs, btrfs_info->used);

out:
  if (btrfs_info)
    bd_btrfs_filesystem_info_free (btrfs_info);
  if (error)
    g_error_free (error);
  g_free ((gpointer) dev_file);

  return rval;
}

static const gchar *const *
storaged_filesystem_btrfs_get_mount_points (StoragedFilesystemBTRFS  *fs_btrfs,
                                            GError                  **error)
{
  StoragedObject *object = NULL;
  StoragedFilesystem *fs = NULL;
  const gchar *const *mount_points = NULL;

  g_return_val_if_fail (STORAGED_IS_FILESYSTEM_BTRFS (fs_btrfs), NULL);
  g_return_val_if_fail (error, NULL);

  /* Get enclosing object for this interface. */
  object = storaged_daemon_util_dup_object (fs_btrfs, error);
  g_return_val_if_fail (object, NULL);

  /* Get StoragedFilesystem. */
  fs = storaged_object_get_filesystem (object);
  mount_points = storaged_filesystem_get_mount_points (fs);

  if (! mount_points || ! *mount_points)
    {
      *error = g_error_new (STORAGED_ERROR,
                            STORAGED_ERROR_NOT_MOUNTED,
                            "Volume not mounted");
      return NULL;
    }

  return mount_points;
}

/**
 * storaged_filesystem_btrfs_get_first_mount_point:
 *
 * @fs_btrfs: A #StoragedFilesystemBTRFS.
 *
 * Returns: the first mount_point for the given BTRFS volume. Free with g_free().
 */
static gchar *
storaged_filesystem_btrfs_get_first_mount_point (StoragedFilesystemBTRFS  *fs_btrfs,
                                                 GError                  **error)
{
  const gchar *const *mount_points;

  g_return_val_if_fail (STORAGED_IS_FILESYSTEM_BTRFS (fs_btrfs), NULL);

  mount_points = storaged_filesystem_btrfs_get_mount_points (fs_btrfs, error);
  if (! mount_points)
    return NULL;

  return g_strdup (*mount_points);
}

static gboolean
handle_set_label (StoragedFilesystemBTRFS  *fs_btrfs,
                  GDBusMethodInvocation    *invocation,
                  const gchar              *arg_label,
                  GVariant                 *arg_options)
{
  StoragedLinuxFilesystemBTRFS *l_fs_btrfs = STORAGED_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  GError *error = NULL;
  gchar *dev_file = NULL;
  gchar *label = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                       NULL,
                                       btrfs_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to change label for BTRFS volume"),
                                       invocation);

  /* Allow any arbitrary label. */
  label = g_strdup (arg_label);

  /* Get the device filename (eg.: /dev/sda1) */
  dev_file = storaged_linux_block_object_get_device_file (object);
  if (! dev_file)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Change the label. */
  if (! bd_btrfs_change_label (dev_file, label, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  storaged_filesystem_btrfs_complete_set_label (fs_btrfs,
                                                invocation);
out:
  g_free ((gpointer) dev_file);
  g_free ((gpointer) label);
  return TRUE;
}

static gboolean
btrfs_subvolume_perform_action (StoragedFilesystemBTRFS  *fs_btrfs,
                                GDBusMethodInvocation    *invocation,
                                btrfs_subvolume_func      subvolume_action,
                                const gchar              *arg_name,
                                GVariant                 *arg_options,
                                const gchar              *polkit_message)
{
  StoragedLinuxFilesystemBTRFS *l_fs_btrfs = STORAGED_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  GError *error = NULL;
  gchar *name = NULL;
  gchar *mount_point = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                       NULL,
                                       btrfs_policy_action_id,
                                       arg_options,
                                       N_(polkit_message),
                                       invocation);

  /* Do we have a valid subvolume name? */
  if (! *arg_name)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Invalid subvolume name");
      goto out;
    }
  name = g_strdup (arg_name);

  /* Get the mount point for this volume. */
  mount_point = storaged_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
  if (! mount_point)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Add/remove the subvolume. */
  if (! subvolume_action (mount_point, name, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  storaged_filesystem_btrfs_complete_set_label (fs_btrfs,
                                                invocation);

out:
  /* Release the resources */
  g_free ((gpointer) name);
  g_free ((gpointer) mount_point);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
btrfs_device_perform_action (StoragedFilesystemBTRFS  *fs_btrfs,
                             GDBusMethodInvocation    *invocation,
                             btrfs_device_func         device_action,
                             const gchar              *arg_device,
                             GVariant                 *arg_options)
{
  StoragedLinuxFilesystemBTRFS *l_fs_btrfs = STORAGED_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  StoragedLinuxBlockObject *object = NULL;
  GError *error = NULL;
  gchar *mount_point = NULL;
  gchar *device = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                       NULL,
                                       btrfs_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to add "
                                          "the device to the volume"),
                                       invocation);

  /* Get the mount point for this volume. */
  mount_point = storaged_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
  if (! mount_point)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  device = g_strdup (arg_device);

  /* Add/remove the device to/from the volume. */
  if (! device_action (mount_point, device, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Update the interface. */
  object = storaged_daemon_util_dup_object (fs_btrfs, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  storaged_linux_filesystem_btrfs_update (l_fs_btrfs, object);

  /* Complete DBus call. */
  storaged_filesystem_btrfs_complete_add_device (fs_btrfs, invocation);

out:
  /* Release the resources */
  g_free ((gpointer) mount_point);
  g_free ((gpointer) device);
  g_clear_object (&object);

  /* Indicate that we handled the method invocation. */
  return TRUE;
}

static gboolean
handle_add_device (StoragedFilesystemBTRFS  *fs_btrfs,
                   GDBusMethodInvocation    *invocation,
                   const gchar              *arg_device,
                   GVariant                 *arg_options)
{
  return btrfs_device_perform_action (fs_btrfs,
                                      invocation,
                                      bd_btrfs_add_device,
                                      arg_device,
                                      arg_options);
}

static gboolean
handle_remove_device (StoragedFilesystemBTRFS  *fs_btrfs,
                      GDBusMethodInvocation    *invocation,
                      const gchar              *arg_device,
                      GVariant                 *arg_options)
{
  return btrfs_device_perform_action (fs_btrfs,
                                      invocation,
                                      bd_btrfs_remove_device,
                                      arg_device,
                                      arg_options);
}

static gboolean
handle_create_subvolume (StoragedFilesystemBTRFS  *fs_btrfs,
                         GDBusMethodInvocation    *invocation,
                         const gchar              *arg_name,
                         GVariant                 *arg_options)
{
  return btrfs_subvolume_perform_action (fs_btrfs,
                                         invocation,
                                         bd_btrfs_create_subvolume,
                                         arg_name,
                                         arg_options,
                                         "Authentication is required to add "
                                         "a new subvolume for the given BTRFS volume");
}

static gboolean
handle_remove_subvolume (StoragedFilesystemBTRFS  *fs_btrfs,
                         GDBusMethodInvocation    *invocation,
                         const gchar              *arg_name,
                         GVariant                 *arg_options)
{
  return btrfs_subvolume_perform_action (fs_btrfs,
                                         invocation,
                                         bd_btrfs_delete_subvolume,
                                         arg_name,
                                         arg_options,
                                         "Authentication is required to remove "
                                         "the subvolume for the given BTRFS volume");
}

static gboolean
handle_get_subvolumes (StoragedFilesystemBTRFS  *fs_btrfs,
                       GDBusMethodInvocation    *invocation,
                       gboolean                  arg_snapshots_only,
                       GVariant                 *arg_options)
{
  StoragedLinuxFilesystemBTRFS *l_fs_btrfs = STORAGED_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  BDBtrfsSubvolumeInfo **subvolumes_info = NULL;
  GVariant *subvolumes = NULL;
  GError *error = NULL;
  gchar *mount_point = NULL;
  gint subvolumes_cnt = 0;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                       NULL,
                                       btrfs_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to change label "
                                          "for BTRFS volume"),
                                       invocation);

  /* Get the mount point for this volume. */
  mount_point = storaged_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
  if (! mount_point)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Get subvolume infos. */
  subvolumes_info = bd_btrfs_list_subvolumes (mount_point,
                                              arg_snapshots_only,
                                              &error);

  if (! subvolumes_info && error)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  subvolumes = btrfs_subvolumes_to_gvariant (subvolumes_info, &subvolumes_cnt);

  /* Complete DBus call. */
  storaged_filesystem_btrfs_complete_get_subvolumes (fs_btrfs,
                                                     invocation,
                                                     subvolumes,
                                                     subvolumes_cnt);

out:
  /* Release the resources */
  btrfs_free_subvolumes_info (subvolumes_info);
  g_free ((gpointer) mount_point);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_create_snapshot(StoragedFilesystemBTRFS  *fs_btrfs,
                       GDBusMethodInvocation    *invocation,
                       const gchar              *arg_source,
                       const gchar              *arg_dest,
                       gboolean                  arg_ro,
                       GVariant                 *arg_options)
{
  StoragedLinuxFilesystemBTRFS *l_fs_btrfs = STORAGED_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  GError *error = NULL;
  gchar *source = NULL;
  gchar *dest = NULL;
  gchar *mount_point = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                       NULL,
                                       btrfs_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to create a new snapshot"),
                                       invocation);

  /* Prefix source and destination directories with mount point; so that user
   * doesn't need to always type in a full path.
   */
  mount_point = storaged_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
  if (! mount_point)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  source = g_build_path (G_DIR_SEPARATOR_S, mount_point, arg_source, NULL);
  dest = g_build_path (G_DIR_SEPARATOR_S, mount_point, arg_dest, NULL);

  /* Create the snapshot. */
  if (! bd_btrfs_create_snapshot (source, dest, arg_ro, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  storaged_filesystem_btrfs_complete_create_snapshot (fs_btrfs,
                                                      invocation);

out:
  /* Release the resources */
  g_free ((gpointer) source);
  g_free ((gpointer) dest);
  g_free ((gpointer) mount_point);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_repair (StoragedFilesystemBTRFS  *fs_btrfs,
               GDBusMethodInvocation    *invocation,
               GVariant                 *arg_options)
{
  StoragedLinuxFilesystemBTRFS *l_fs_btrfs = STORAGED_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  GError *error = NULL;
  gchar *dev_file = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                       NULL,
                                       btrfs_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to check and repair the volume"),
                                       invocation);

  /* Get the device filename (eg.: /dev/sda1) */
  dev_file = storaged_linux_block_object_get_device_file (object);
  if (! dev_file)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Check and repair. */
  if (! bd_btrfs_repair (dev_file, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  storaged_filesystem_btrfs_complete_repair (fs_btrfs,
                                             invocation);

out:
  g_free ((gpointer) dev_file);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_resize (StoragedFilesystemBTRFS  *fs_btrfs,
               GDBusMethodInvocation    *invocation,
               guint64                   arg_size,
               GVariant                 *arg_options)
{
  StoragedLinuxFilesystemBTRFS *l_fs_btrfs = STORAGED_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  GError *error = NULL;
  gchar *mount_point = NULL;

  /* Policy check. */
  STORAGED_DAEMON_CHECK_AUTHORIZATION (storaged_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                       NULL,
                                       btrfs_policy_action_id,
                                       arg_options,
                                       N_("Authentication is required to resize the volume"),
                                       invocation);

  /* Get the mount point for this volume. */
  mount_point = storaged_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
  if (! mount_point)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Resize the volume. */
  if (! bd_btrfs_resize (mount_point, arg_size, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  storaged_filesystem_btrfs_complete_resize (fs_btrfs,
                                             invocation);

out:
  g_free ((gpointer) mount_point);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static void
storaged_linux_filesystem_btrfs_iface_init (StoragedFilesystemBTRFSIface *iface)
{
  iface->handle_set_label = handle_set_label;
  iface->handle_add_device = handle_add_device;
  iface->handle_remove_device = handle_remove_device;
  iface->handle_create_subvolume = handle_create_subvolume;
  iface->handle_remove_subvolume = handle_remove_subvolume;
  iface->handle_get_subvolumes = handle_get_subvolumes;
  iface->handle_create_snapshot = handle_create_snapshot;
  iface->handle_repair = handle_repair;
  iface->handle_resize = handle_resize;
}
