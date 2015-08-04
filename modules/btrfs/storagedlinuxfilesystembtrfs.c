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

#include <src/storageddaemonutil.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlogging.h>
#include <blockdev/btrfs.h>

#include "storagedlinuxfilesystembtrfs.h"
#include "storaged-btrfs-generated.h"

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
  gpointer priv;
};

struct _StoragedLinuxFilesystemBTRFSClass {
  StoragedFilesystemBTRFSSkeletonClass parent_class;
};

static void storaged_linux_filesystem_btrfs_iface_init (StoragedFilesystemBTRFSIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxFilesystemBTRFS, storaged_linux_filesystem_btrfs,
                         STORAGED_TYPE_FILESYSTEM_BTRFS_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_FILESYSTEM_BTRFS,
                         storaged_linux_filesystem_btrfs_iface_init));

static void
storaged_linux_filesystem_btrfs_get_property (GObject *object, guint property_id,
                                              GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
storaged_linux_filesystem_btrfs_set_property (GObject *object, guint property_id,
                                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
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
}

static void
storaged_linux_filesystem_btrfs_init (StoragedLinuxFilesystemBTRFS *filesystem)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (filesystem),
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
  return g_object_new (STORAGED_TYPE_LINUX_FILESYSTEM_BTRFS, NULL);
}

/**
 * storaged_linux_filesystem_btrfs_update:
 * @filesystem: A #StoragedLinuxFilesystemBTRFS.
 * @object: The enclosing #StoragedLlinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if the configuration has changed, %FALSE otherwise.
 */
gboolean
storaged_linux_filesystem_btrfs_update (StoragedLinuxFilesystemBTRFS *filesystem,
                                        StoragedLinuxBlockObject     *object)
{
  StoragedFilesystemBTRFS *iface = STORAGED_FILESYSTEM_BTRFS (filesystem);
  StoragedLinuxDevice *device = NULL;
  BDBtrfsFilesystemInfo *btrfs_info = NULL;
  GError *error = NULL;
  gchar *dev_file = NULL;
  gboolean rval = FALSE;

  g_return_val_if_fail (STORAGED_IS_LINUX_FILESYSTEM_BTRFS (filesystem), FALSE);
  g_return_val_if_fail (STORAGED_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  device = storaged_linux_block_object_get_device (STORAGED_LINUX_BLOCK_OBJECT (object));
  dev_file = g_strdup (g_udev_device_get_device_file (device->udev_device));

  btrfs_info = bd_btrfs_filesystem_info (dev_file, &error);

  if (! btrfs_info)
    {
      storaged_error ("Can't get BTRFS filesystem info for %s", dev_file);
      rval = FALSE;
      goto out;
    }

  /* Update the interface */
  storaged_filesystem_btrfs_set_label (iface, btrfs_info->label);
  storaged_filesystem_btrfs_set_uuid (iface, btrfs_info->uuid);
  storaged_filesystem_btrfs_set_num_devices (iface, btrfs_info->num_devices);
  storaged_filesystem_btrfs_set_used (iface, btrfs_info->used);

out:
  if (btrfs_info)
    bd_btrfs_filesystem_info_free (btrfs_info);
  if (error)
    g_error_free (error);
  g_free (dev_file);

  return rval;
}

static gboolean
handle_set_label (StoragedFilesystemBTRFS  *filesystem_,
                  GDBusMethodInvocation    *invocation,
                  const gchar              *label_)
{
  StoragedLinuxFilesystemBTRFS *filesystem = STORAGED_LINUX_FILESYSTEM_BTRFS (filesystem_);
  StoragedObject *object = NULL;
  StoragedLinuxDevice *device = NULL;
  GError *error = NULL;
  gchar *dev_file = NULL;
  gchar *label = NULL;

  /* Get the enclosing (exported) object for this interface. */
  object = storaged_daemon_util_dup_object (filesystem, &error);
  if (! object)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  label = g_strdup (label_);

  /* Get the device filename (eg.: /dev/sda1) */
  device = storaged_linux_block_object_get_device (STORAGED_LINUX_BLOCK_OBJECT (object));
  dev_file = g_strdup (g_udev_device_get_device_file (device->udev_device));

  /* Change the label. */
  if (! bd_btrfs_change_label (dev_file, label, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Complete DBus call. */
  storaged_filesystem_btrfs_complete_set_label (filesystem_,
                                                invocation);
out:
  g_free (dev_file);
  g_free (label);
  g_clear_object (&object);
  return TRUE;
}

static void
storaged_linux_filesystem_btrfs_iface_init (StoragedFilesystemBTRFSIface *iface)
{
  iface->handle_set_label = handle_set_label;
}
