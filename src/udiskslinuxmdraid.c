/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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
#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <mntent.h>

#include <glib/gstdio.h>

#include "udiskslogging.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxmdraidobject.h"
#include "udiskslinuxmdraid.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxmdraid
 * @title: UDisksLinuxMDRaid
 * @short_description: Linux implementation of #UDisksMDRaid
 *
 * This type provides an implementation of the #UDisksMDRaid interface
 * on Linux.
 */

typedef struct _UDisksLinuxMDRaidClass   UDisksLinuxMDRaidClass;

/**
 * UDisksLinuxMDRaid:
 *
 * The #UDisksLinuxMDRaid structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxMDRaid
{
  UDisksMDRaidSkeleton parent_instance;

};

struct _UDisksLinuxMDRaidClass
{
  UDisksMDRaidSkeletonClass parent_class;
};

static void mdraid_iface_init (UDisksMDRaidIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxMDRaid, udisks_linux_mdraid, UDISKS_TYPE_MDRAID_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MDRAID, mdraid_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_mdraid_finalize (GObject *object)
{
  /* UDisksLinuxMDRaid *mdraid = UDISKS_LINUX_MDRAID (object); */

  if (G_OBJECT_CLASS (udisks_linux_mdraid_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_mdraid_parent_class)->finalize (object);
}

static void
udisks_linux_mdraid_init (UDisksLinuxMDRaid *mdraid)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (mdraid),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_mdraid_class_init (UDisksLinuxMDRaidClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_mdraid_finalize;
}

/**
 * udisks_linux_mdraid_new:
 *
 * Creates a new #UDisksLinuxMDRaid instance.
 *
 * Returns: A new #UDisksLinuxMDRaid. Free with g_object_unref().
 */
UDisksMDRaid *
udisks_linux_mdraid_new (void)
{
  return UDISKS_MDRAID (g_object_new (UDISKS_TYPE_LINUX_MDRAID,
                                          NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
read_sysfs_attr (GUdevDevice *device,
                 const gchar *attr)
{
  gchar *ret = NULL;
  gchar *path = NULL;
  GError *error = NULL;

  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), NULL);

  path = g_strdup_printf ("%s/%s", g_udev_device_get_sysfs_path (device), attr);
  if (!g_file_get_contents (path, &ret, NULL /* size */, &error))
    {
      udisks_warning ("Error reading sysfs attr `%s': %s (%s, %d)",
                      path, error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      goto out;
    }

 out:
  g_free (path);
  return ret;
}

static gint
read_sysfs_attr_as_int (GUdevDevice *device,
                        const gchar *attr)
{
  gint ret = 0;
  gchar *str = NULL;

  str = read_sysfs_attr (device, attr);
  if (str == NULL)
    goto out;

  ret = atoi (str);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_mdraid_update:
 * @mdraid: A #UDisksLinuxMDRaid.
 * @object: The enclosing #UDisksLinuxMDRaidObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_mdraid_update (UDisksLinuxMDRaid       *mdraid,
                            UDisksLinuxMDRaidObject *object)
{
  UDisksMDRaid *iface = UDISKS_MDRAID (mdraid);
  gboolean ret = FALSE;
  guint num_devices = 0;
  guint64 size = 0;
  GUdevDevice *raid_device = NULL;
  GList *member_devices = NULL;
  GUdevDevice *member_device = NULL;
  const gchar *level = NULL;
  gchar *sync_action = NULL;
  guint degraded = 0;

  member_devices = udisks_linux_mdraid_object_get_members (object);
  if (member_devices == NULL)
    goto out;

  raid_device = udisks_linux_mdraid_object_get_device (object);
  member_device = G_UDEV_DEVICE (member_devices->data);

  num_devices = g_udev_device_get_property_as_int (member_device, "MD_DEVICES");
  level = g_udev_device_get_property (member_device, "MD_LEVEL");

  /* figure out size */
  if (raid_device != NULL)
    {
      size = 512 * g_udev_device_get_sysfs_attr_as_uint64 (raid_device, "size");
    }
  else
    {
      /* TODO: need MD_ARRAY_SIZE, see https://bugs.freedesktop.org/show_bug.cgi?id=53239#c5 */
    }

  udisks_mdraid_set_uuid (iface, g_udev_device_get_property (member_device, "MD_UUID"));
  udisks_mdraid_set_name (iface, g_udev_device_get_property (member_device, "MD_NAME"));
  udisks_mdraid_set_level (iface, level);
  udisks_mdraid_set_num_devices (iface, num_devices);
  udisks_mdraid_set_size (iface, size);

  if (raid_device != NULL)
    {
      /* Can't use GUdevDevice methods as they cache the result and these variables vary */
      degraded = read_sysfs_attr_as_int (raid_device, "md/degraded");
      sync_action = read_sysfs_attr (raid_device, "md/sync_action");
    }
  udisks_mdraid_set_degraded (iface, degraded);
  udisks_mdraid_set_sync_action (iface, sync_action);

  /* TODO: set other stuff */

 out:
  g_free (sync_action);
  g_list_free_full (member_devices, g_object_unref);
  g_clear_object (&raid_device);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_start (UDisksMDRaid           *_mdraid,
              GDBusMethodInvocation  *invocation,
              GVariant               *options)
{
  UDisksLinuxMDRaid *mdraid = UDISKS_LINUX_MDRAID (_mdraid);
  UDisksDaemon *daemon;
  UDisksLinuxMDRaidObject *object;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  GUdevDevice *raid_device = NULL;
  gchar *uuid = NULL;
  gchar *escaped_uuid = NULL;
  GError *error = NULL;
  gchar *error_message = NULL;

  object = udisks_daemon_util_dup_object (mdraid, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_mdraid_object_get_daemon (object);

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &caller_gid,
                                               NULL,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  raid_device = udisks_linux_mdraid_object_get_device (object);
  if (raid_device != NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "RAID Array is already running");
      goto out;
    }

  /* Translators: Shown in authentication dialog when the attempts.
   * to stop a RAID ARRAY.
   */
  /* TODO: variables */
  message = N_("Authentication is required to start a RAID array");
  action_id = "org.freedesktop.udisks2.manage-md-raid";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  uuid = udisks_mdraid_dup_uuid (_mdraid);
  escaped_uuid = udisks_daemon_util_escape_and_quote (uuid);

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
                                              "md-raid-start", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "mdadm --assemble --scan --uuid %s",
                                              escaped_uuid))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error starting RAID array with UUID %s: %s",
                                             uuid,
                                             error_message);
      goto out;
    }

  /* TODO: wait for array to actually show up? */

  udisks_mdraid_complete_stop (_mdraid, invocation);

 out:
  g_free (error_message);
  g_free (uuid);
  g_free (escaped_uuid);
  g_clear_object (&raid_device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_stop (UDisksMDRaid           *_mdraid,
             GDBusMethodInvocation  *invocation,
             GVariant               *options)
{
  UDisksLinuxMDRaid *mdraid = UDISKS_LINUX_MDRAID (_mdraid);
  UDisksDaemon *daemon;
  UDisksLinuxMDRaidObject *object;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  GUdevDevice *raid_device = NULL;
  const gchar *device_file = NULL;
  gchar *escaped_device_file = NULL;
  GError *error = NULL;
  gchar *error_message = NULL;

  object = udisks_daemon_util_dup_object (mdraid, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_mdraid_object_get_daemon (object);

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &caller_gid,
                                               NULL,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  raid_device = udisks_linux_mdraid_object_get_device (object);
  if (raid_device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "RAID Array is not running");
      goto out;
    }

  /* Translators: Shown in authentication dialog when the attempts.
   * to stop a RAID ARRAY.
   */
  /* TODO: variables */
  message = N_("Authentication is required to stop a RAID array");
  action_id = "org.freedesktop.udisks2.manage-md-raid";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  device_file = g_udev_device_get_device_file (raid_device);
  escaped_device_file = udisks_daemon_util_escape_and_quote (g_udev_device_get_device_file (raid_device));

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
                                              "md-raid-stop", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "mdadm --stop %s",
                                              escaped_device_file))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error stopping RAID array %s: %s",
                                             device_file,
                                             error_message);
      goto out;
    }

  udisks_mdraid_complete_stop (_mdraid, invocation);

 out:
  g_free (error_message);
  g_free (escaped_device_file);
  g_clear_object (&raid_device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mdraid_iface_init (UDisksMDRaidIface *iface)
{
  iface->handle_start = handle_start;
  iface->handle_stop = handle_stop;
}
