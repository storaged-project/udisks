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

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslogging.h>
#include <blockdev/btrfs.h>

#include "udiskslinuxfilesystembtrfs.h"
#include "udisks-btrfs-generated.h"
#include "udisksbtrfsutil.h"

/**
 * SECTION:udiskslinuxfilesystembtrfs
 * @title: UDisksLinuxFilesystemBTRFS
 * @short_description: Linux implementation of #UDisksFilesystemBTRFS
 *
 * This type provides an implementation of #UDisksFilesystemBTRFS interface
 * on Linux.
 */

/**
 * UDisksLinuxFilesystemBTRFS:
 *
 * The #UDisksLinuxFilesystemBTRFS structure contains only private data and
 * should be only accessed using provided API.
 */
struct _UDisksLinuxFilesystemBTRFS{
  UDisksFilesystemBTRFSSkeleton parent_instance;

  UDisksDaemon *daemon;
};

struct _UDisksLinuxFilesystemBTRFSClass {
  UDisksFilesystemBTRFSSkeletonClass parent_class;
};

typedef gboolean (*btrfs_subvolume_func)(const gchar *mount_point, const gchar *name, const BDExtraArg **extra, GError **error);
typedef gboolean (*btrfs_device_func)(const gchar *mountpoint, const gchar *device, const BDExtraArg **extra, GError **error);

enum
{
  PROP_0,
  PROP_DAEMON,
  N_PROPERTIES
};

static void udisks_linux_filesystem_btrfs_iface_init (UDisksFilesystemBTRFSIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxFilesystemBTRFS, udisks_linux_filesystem_btrfs,
                         UDISKS_TYPE_FILESYSTEM_BTRFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_FILESYSTEM_BTRFS,
                         udisks_linux_filesystem_btrfs_iface_init));

static void
udisks_linux_filesystem_btrfs_get_property (GObject    *object,
                                            guint       property_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  UDisksLinuxFilesystemBTRFS *l_fs_btrfs = UDISKS_LINUX_FILESYSTEM_BTRFS (object);

  switch (property_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_filesystem_btrfs_get_daemon (l_fs_btrfs));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_filesystem_btrfs_set_property (GObject      *object,
                                            guint         property_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
  UDisksLinuxFilesystemBTRFS *l_fs_btrfs = UDISKS_LINUX_FILESYSTEM_BTRFS (object);

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
udisks_linux_filesystem_btrfs_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_filesystem_btrfs_parent_class))
    G_OBJECT_CLASS (udisks_linux_filesystem_btrfs_parent_class)->dispose (object);
}

static void
udisks_linux_filesystem_btrfs_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_filesystem_btrfs_parent_class))
    G_OBJECT_CLASS (udisks_linux_filesystem_btrfs_parent_class)->finalize (object);
}

static void
udisks_linux_filesystem_btrfs_class_init (UDisksLinuxFilesystemBTRFSClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_filesystem_btrfs_get_property;
  gobject_class->set_property = udisks_linux_filesystem_btrfs_set_property;
  gobject_class->dispose = udisks_linux_filesystem_btrfs_dispose;
  gobject_class->finalize = udisks_linux_filesystem_btrfs_finalize;

  /**
   * UDisksLinuxManager:daemon
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

static void
udisks_linux_filesystem_btrfs_init (UDisksLinuxFilesystemBTRFS *l_fs_btrfs)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (l_fs_btrfs),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

/**
 * udisks_linux_filesystem_btrfs_new:
 *
 * Creates a new #UDisksLinuxFilesystemBTRFS instance.
 *
 * Returns: A new #UDisksLinuxFilesystemBTRFS. Free with g_object_unref().
 */
UDisksLinuxFilesystemBTRFS *
udisks_linux_filesystem_btrfs_new (void)
{
  return UDISKS_LINUX_FILESYSTEM_BTRFS (g_object_new (UDISKS_TYPE_LINUX_FILESYSTEM_BTRFS,
                                                      NULL));
}

/**
 * udisks_linux_filesystem_btrfs_get_daemon:
 * @fs: A #UDisksLinuxFilesystemBTRFS.
 *
 * Gets the daemon used by @l_fs_btrfs.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @l_fs_btrfs.
 */
UDisksDaemon *
udisks_linux_filesystem_btrfs_get_daemon (UDisksLinuxFilesystemBTRFS *l_fs_btrfs)
{
  GError *error = NULL;
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon = NULL;

  g_return_val_if_fail (UDISKS_IS_LINUX_FILESYSTEM_BTRFS (l_fs_btrfs), NULL);

  object = udisks_daemon_util_dup_object (l_fs_btrfs, &error);
  if (object)
    {
      daemon = udisks_linux_block_object_get_daemon (object);
      g_clear_object (&object);
    }
  else
    {
      udisks_critical ("%s", error->message);
      g_clear_error (&error);
    }

  return daemon;
}

/**
 * udisks_linux_filesystem_btrfs_update:
 * @l_fs_btrfs: A #UDisksLinuxFilesystemBTRFS.
 * @object: The enclosing #UDisksLlinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if the configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_filesystem_btrfs_update (UDisksLinuxFilesystemBTRFS *l_fs_btrfs,
                                      UDisksLinuxBlockObject     *object)
{
  UDisksFilesystemBTRFS *fs_btrfs = UDISKS_FILESYSTEM_BTRFS (l_fs_btrfs);
  BDBtrfsFilesystemInfo *btrfs_info = NULL;
  GError *error = NULL;
  gchar *dev_file = NULL;
  gboolean rval = FALSE;

  g_return_val_if_fail (UDISKS_IS_LINUX_FILESYSTEM_BTRFS (fs_btrfs), FALSE);
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  dev_file = udisks_linux_block_object_get_device_file (object);
  if (! dev_file)
    {
      rval = FALSE;
      goto out;
    }

  btrfs_info = bd_btrfs_filesystem_info (dev_file, &error);

  if (! btrfs_info)
    {
      udisks_critical ("Can't get BTRFS filesystem info for %s", dev_file);
      rval = FALSE;
      goto out;
    }

  /* Update the interface */
  udisks_filesystem_btrfs_set_label (fs_btrfs, btrfs_info->label);
  udisks_filesystem_btrfs_set_uuid (fs_btrfs, btrfs_info->uuid);
  udisks_filesystem_btrfs_set_num_devices (fs_btrfs, btrfs_info->num_devices);
  udisks_filesystem_btrfs_set_used (fs_btrfs, btrfs_info->used);

out:
  if (btrfs_info)
    bd_btrfs_filesystem_info_free (btrfs_info);
  if (error)
    g_clear_error (&error);
  g_free ((gpointer) dev_file);

  return rval;
}

static const gchar *const *
udisks_filesystem_btrfs_get_mount_points (UDisksFilesystemBTRFS  *fs_btrfs,
                                          GError                **error)
{
  UDisksObject *object = NULL;
  UDisksFilesystem *fs = NULL;
  const gchar *const *mount_points = NULL;

  g_return_val_if_fail (UDISKS_IS_FILESYSTEM_BTRFS (fs_btrfs), NULL);
  g_return_val_if_fail (error, NULL);

  /* Get enclosing object for this interface. */
  object = udisks_daemon_util_dup_object (fs_btrfs, error);
  g_return_val_if_fail (object, NULL);

  /* Get UDisksFilesystem. */
  fs = udisks_object_get_filesystem (object);
  mount_points = udisks_filesystem_get_mount_points (fs);

  if (! mount_points || ! *mount_points)
    {
      *error = g_error_new (UDISKS_ERROR,
                            UDISKS_ERROR_NOT_MOUNTED,
                            "Volume not mounted");
      return NULL;
    }

  return mount_points;
}

/**
 * udisks_filesystem_btrfs_get_first_mount_point:
 *
 * @fs_btrfs: A #UDisksFilesystemBTRFS.
 *
 * Returns: the first mount_point for the given BTRFS volume. Free with g_free().
 */
static gchar *
udisks_filesystem_btrfs_get_first_mount_point (UDisksFilesystemBTRFS  *fs_btrfs,
                                               GError                **error)
{
  const gchar *const *mount_points;

  g_return_val_if_fail (UDISKS_IS_FILESYSTEM_BTRFS (fs_btrfs), NULL);

  mount_points = udisks_filesystem_btrfs_get_mount_points (fs_btrfs, error);
  if (! mount_points)
    return NULL;

  return g_strdup (*mount_points);
}

static gboolean
handle_set_label (UDisksFilesystemBTRFS *fs_btrfs,
                  GDBusMethodInvocation *invocation,
                  const gchar           *arg_label,
                  GVariant              *arg_options)
{
  UDisksLinuxFilesystemBTRFS *l_fs_btrfs = UDISKS_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  UDisksLinuxBlockObject *object = NULL;
  GError *error = NULL;
  gchar *dev_file = NULL;
  gchar *label = NULL;

  object = udisks_daemon_util_dup_object (l_fs_btrfs, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                     UDISKS_OBJECT (object),
                                     btrfs_policy_action_id,
                                     arg_options,
                                     N_("Authentication is required to change label for BTRFS volume"),
                                     invocation);

  /* Allow any arbitrary label. */
  label = g_strdup (arg_label);

  /* Get the device filename (eg.: /dev/sda1) */
  dev_file = udisks_linux_block_object_get_device_file (object);
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
  udisks_filesystem_btrfs_complete_set_label (fs_btrfs,
                                              invocation);
out:
  g_clear_object (&object);
  g_free ((gpointer) dev_file);
  g_free ((gpointer) label);
  return TRUE;
}

static gboolean
btrfs_subvolume_perform_action (UDisksFilesystemBTRFS *fs_btrfs,
                                GDBusMethodInvocation *invocation,
                                btrfs_subvolume_func   subvolume_action,
                                const gchar           *arg_name,
                                GVariant              *arg_options,
                                const gchar           *polkit_message)
{
  UDisksLinuxFilesystemBTRFS *l_fs_btrfs = UDISKS_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  UDisksLinuxBlockObject *object = NULL;
  GError *error = NULL;
  gchar *name = NULL;
  gchar *mount_point = NULL;

  object = udisks_daemon_util_dup_object (l_fs_btrfs, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                     UDISKS_OBJECT (object),
                                     btrfs_policy_action_id,
                                     arg_options,
                                     N_(polkit_message),
                                     invocation);

  /* Do we have a valid subvolume name? */
  if (! *arg_name)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Invalid subvolume name");
      goto out;
    }
  name = g_strdup (arg_name);

  /* Get the mount point for this volume. */
  mount_point = udisks_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
  if (! mount_point)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Add/remove the subvolume. */
  if (! subvolume_action (mount_point, name, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  udisks_filesystem_btrfs_complete_set_label (fs_btrfs,
                                              invocation);

out:
  /* Release the resources */
  g_clear_object (&object);
  g_free ((gpointer) name);
  g_free ((gpointer) mount_point);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
btrfs_device_perform_action (UDisksFilesystemBTRFS *fs_btrfs,
                             GDBusMethodInvocation *invocation,
                             btrfs_device_func      device_action,
                             const gchar           *arg_device,
                             GVariant              *arg_options)
{
  UDisksLinuxFilesystemBTRFS *l_fs_btrfs = UDISKS_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  UDisksLinuxBlockObject *object = NULL;
  GError *error = NULL;
  gchar *mount_point = NULL;
  gchar *device = NULL;
  UDisksDaemon *daemon = NULL;
  UDisksObject *new_device_object = NULL;
  UDisksBlock *new_device_block = NULL;

  object = udisks_daemon_util_dup_object (fs_btrfs, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_filesystem_btrfs_get_daemon (l_fs_btrfs);

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (daemon,
                                     UDISKS_OBJECT (object),
                                     btrfs_policy_action_id,
                                     arg_options,
                                     N_("Authentication is required to add "
                                        "the device to the volume"),
                                     invocation);

  /* Get the mount point for this volume. */
  mount_point = udisks_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
  if (! mount_point)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  new_device_object = udisks_daemon_find_object (daemon, arg_device);
  if (new_device_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Invalid object path %s",
                                             arg_device);
      goto out;
    }

  new_device_block = udisks_object_get_block (new_device_object);
  if (new_device_block == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Object path %s is not a block device",
                                             arg_device);
      goto out;
    }

  device = udisks_block_dup_device (new_device_block);

  /* Add/remove the device to/from the volume. */
  if (! device_action (mount_point, device, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Update the interface. */
  udisks_linux_filesystem_btrfs_update (l_fs_btrfs, object);

  /* Complete DBus call. */
  udisks_filesystem_btrfs_complete_add_device (fs_btrfs, invocation);

out:
  /* Release the resources */
  g_clear_object (&object);
  g_clear_object (&new_device_object);
  g_clear_object (&new_device_block);
  g_free ((gpointer) mount_point);
  g_free ((gpointer) device);

  /* Indicate that we handled the method invocation. */
  return TRUE;
}

static gboolean
handle_add_device (UDisksFilesystemBTRFS *fs_btrfs,
                   GDBusMethodInvocation *invocation,
                   const gchar           *arg_device,
                   GVariant              *arg_options)
{
  return btrfs_device_perform_action (fs_btrfs,
                                      invocation,
                                      bd_btrfs_add_device,
                                      arg_device,
                                      arg_options);
}

static gboolean
handle_remove_device (UDisksFilesystemBTRFS *fs_btrfs,
                      GDBusMethodInvocation *invocation,
                      const gchar           *arg_device,
                      GVariant              *arg_options)
{
  return btrfs_device_perform_action (fs_btrfs,
                                      invocation,
                                      bd_btrfs_remove_device,
                                      arg_device,
                                      arg_options);
}

static gboolean
handle_create_subvolume (UDisksFilesystemBTRFS *fs_btrfs,
                         GDBusMethodInvocation *invocation,
                         const gchar           *arg_name,
                         GVariant              *arg_options)
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
handle_remove_subvolume (UDisksFilesystemBTRFS *fs_btrfs,
                         GDBusMethodInvocation *invocation,
                         const gchar           *arg_name,
                         GVariant              *arg_options)
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
handle_get_subvolumes (UDisksFilesystemBTRFS *fs_btrfs,
                       GDBusMethodInvocation *invocation,
                       gboolean               arg_snapshots_only,
                       GVariant              *arg_options)
{
  UDisksLinuxFilesystemBTRFS *l_fs_btrfs = UDISKS_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  UDisksLinuxBlockObject *object = NULL;
  BDBtrfsSubvolumeInfo **subvolumes_info = NULL;
  GVariant *subvolumes = NULL;
  GError *error = NULL;
  gchar *mount_point = NULL;
  gint subvolumes_cnt = 0;

  object = udisks_daemon_util_dup_object (fs_btrfs, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                     UDISKS_OBJECT (object),
                                     btrfs_policy_action_id,
                                     arg_options,
                                     N_("Authentication is required to change label "
                                        "for BTRFS volume"),
                                     invocation);

  /* Get the mount point for this volume. */
  mount_point = udisks_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
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
  udisks_filesystem_btrfs_complete_get_subvolumes (fs_btrfs,
                                                   invocation,
                                                   subvolumes,
                                                   subvolumes_cnt);

out:
  /* Release the resources */
  g_clear_object (&object);
  btrfs_free_subvolumes_info (subvolumes_info);
  g_free ((gpointer) mount_point);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_create_snapshot(UDisksFilesystemBTRFS  *fs_btrfs,
                       GDBusMethodInvocation  *invocation,
                       const gchar            *arg_source,
                       const gchar            *arg_dest,
                       gboolean                arg_ro,
                       GVariant               *arg_options)
{
  UDisksLinuxFilesystemBTRFS *l_fs_btrfs = UDISKS_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  UDisksLinuxBlockObject *object = NULL;
  GError *error = NULL;
  gchar *source = NULL;
  gchar *dest = NULL;
  gchar *mount_point = NULL;

  object = udisks_daemon_util_dup_object (fs_btrfs, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                     UDISKS_OBJECT (object),
                                     btrfs_policy_action_id,
                                     arg_options,
                                     N_("Authentication is required to create a new snapshot"),
                                     invocation);

  /* Prefix source and destination directories with mount point; so that user
   * doesn't need to always type in a full path.
   */
  mount_point = udisks_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
  if (! mount_point)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  source = g_build_path (G_DIR_SEPARATOR_S, mount_point, arg_source, NULL);
  dest = g_build_path (G_DIR_SEPARATOR_S, mount_point, arg_dest, NULL);

  /* Create the snapshot. */
  if (! bd_btrfs_create_snapshot (source, dest, arg_ro, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  udisks_filesystem_btrfs_complete_create_snapshot (fs_btrfs,
                                                    invocation);

out:
  /* Release the resources */
  g_clear_object (&object);
  g_free ((gpointer) source);
  g_free ((gpointer) dest);
  g_free ((gpointer) mount_point);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_repair (UDisksFilesystemBTRFS *fs_btrfs,
               GDBusMethodInvocation *invocation,
               GVariant              *arg_options)
{
  UDisksLinuxFilesystemBTRFS *l_fs_btrfs = UDISKS_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  UDisksLinuxBlockObject *object = NULL;
  GError *error = NULL;
  gchar *dev_file = NULL;

  object = udisks_daemon_util_dup_object (l_fs_btrfs, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                     UDISKS_OBJECT (object),
                                     btrfs_policy_action_id,
                                     arg_options,
                                     N_("Authentication is required to check and repair the volume"),
                                     invocation);

  /* Get the device filename (eg.: /dev/sda1) */
  dev_file = udisks_linux_block_object_get_device_file (object);
  if (! dev_file)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Check and repair. */
  if (! bd_btrfs_repair (dev_file, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  udisks_filesystem_btrfs_complete_repair (fs_btrfs,
                                           invocation);

out:
  g_clear_object (&object);
  g_free ((gpointer) dev_file);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static gboolean
handle_resize (UDisksFilesystemBTRFS *fs_btrfs,
               GDBusMethodInvocation *invocation,
               guint64                arg_size,
               GVariant              *arg_options)
{
  UDisksLinuxFilesystemBTRFS *l_fs_btrfs = UDISKS_LINUX_FILESYSTEM_BTRFS (fs_btrfs);
  UDisksLinuxBlockObject *object = NULL;
  GError *error = NULL;
  gchar *mount_point = NULL;

  object = udisks_daemon_util_dup_object (l_fs_btrfs, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Policy check. */
  UDISKS_DAEMON_CHECK_AUTHORIZATION (udisks_linux_filesystem_btrfs_get_daemon (l_fs_btrfs),
                                     UDISKS_OBJECT (object),
                                     btrfs_policy_action_id,
                                     arg_options,
                                     N_("Authentication is required to resize the volume"),
                                     invocation);

  /* Get the mount point for this volume. */
  mount_point = udisks_filesystem_btrfs_get_first_mount_point (fs_btrfs, &error);
  if (! mount_point)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Resize the volume. */
  if (! bd_btrfs_resize (mount_point, arg_size, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  udisks_filesystem_btrfs_complete_resize (fs_btrfs,
                                           invocation);

out:
  g_clear_object (&object);
  g_free ((gpointer) mount_point);

  /* Indicate that we handled the method invocation */
  return TRUE;
}

static void
udisks_linux_filesystem_btrfs_iface_init (UDisksFilesystemBTRFSIface *iface)
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
