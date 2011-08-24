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

#include <atasmart.h>

#include "udiskslogging.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxdriveata.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxdriveata
 * @title: UDisksLinuxDriveAta
 * @short_description: Linux implementation of #UDisksDriveAta
 *
 * This type provides an implementation of the #UDisksDriveAta
 * interface on Linux.
 */

typedef struct _UDisksLinuxDriveAtaClass   UDisksLinuxDriveAtaClass;

/**
 * UDisksLinuxDriveAta:
 *
 * The #UDisksLinuxDriveAta structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxDriveAta
{
  UDisksDriveAtaSkeleton parent_instance;

  guint64  smart_updated;
  gboolean smart_failing;
  gdouble  smart_temperature;
  guint64  smart_power_on_seconds;
};

struct _UDisksLinuxDriveAtaClass
{
  UDisksDriveAtaSkeletonClass parent_class;
};

static void drive_ata_iface_init (UDisksDriveAtaIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxDriveAta, udisks_linux_drive_ata, UDISKS_TYPE_DRIVE_ATA_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_DRIVE_ATA, drive_ata_iface_init));

G_LOCK_DEFINE_STATIC (object_lock);

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_drive_ata_init (UDisksLinuxDriveAta *drive_ata)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (drive_ata),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_drive_ata_class_init (UDisksLinuxDriveAtaClass *klass)
{
}

/**
 * udisks_linux_drive_ata_new:
 *
 * Creates a new #UDisksLinuxDriveAta instance.
 *
 * Returns: A new #UDisksLinuxDriveAta. Free with g_object_unref().
 */
UDisksDriveAta *
udisks_linux_drive_ata_new (void)
{
  return UDISKS_DRIVE_ATA (g_object_new (UDISKS_TYPE_LINUX_DRIVE_ATA,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/* may be called from *any* thread when the SMART data has been updated */
static void
update_smart (UDisksLinuxDriveAta *drive,
              GUdevDevice         *device)
{
  gboolean supported;
  gboolean enabled;
  guint64 updated;
  gboolean failing;
  gdouble temperature;
  guint64 power_on_seconds;

  supported = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_SMART");
  enabled = g_udev_device_get_property_as_boolean (device, "ID_ATA_FEATURE_SET_SMART_ENABLED");
  updated = 0;
  failing = FALSE;
  temperature = 0.0;
  power_on_seconds = 0;

  G_LOCK (object_lock);
  if (drive->smart_updated > 0)
    {
      updated = drive->smart_updated;
      failing = drive->smart_failing;
      temperature = drive->smart_temperature;
      power_on_seconds = drive->smart_power_on_seconds;
    }
  G_UNLOCK (object_lock);

  g_object_freeze_notify (G_OBJECT (drive));
  udisks_drive_ata_set_smart_supported (UDISKS_DRIVE_ATA (drive), supported);
  udisks_drive_ata_set_smart_enabled (UDISKS_DRIVE_ATA (drive), enabled);
  udisks_drive_ata_set_smart_updated (UDISKS_DRIVE_ATA (drive), updated);
  udisks_drive_ata_set_smart_failing (UDISKS_DRIVE_ATA (drive), failing);
  udisks_drive_ata_set_smart_temperature (UDISKS_DRIVE_ATA (drive), temperature);
  udisks_drive_ata_set_smart_power_on_seconds (UDISKS_DRIVE_ATA (drive), power_on_seconds);
  g_object_thaw_notify (G_OBJECT (drive));
}

/**
 * udisks_linux_drive_ata_update:
 * @drive: A #UDisksLinuxDriveAta.
 * @object: The enclosing #UDisksLinuxDriveObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_drive_ata_update (UDisksLinuxDriveAta    *drive,
                               UDisksLinuxDriveObject *object)
{
  GUdevDevice *device;
  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    goto out;
  update_smart (drive, device);
 out:
  if (device != NULL)
    g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_drive_ata_refresh_smart_sync:
 * @drive: The #UDisksLinuxDriveAta to refresh.
 * @nowakeup: If %TRUE, will not wake up the disk if asleep.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error.
 *
 * Synchronously refreshes ATA S.M.A.R.T. data on @drive using one of
 * the physical drives associated with it. The calling thread is
 * blocked until the data has been obtained.
 *
 * If @nowake is %TRUE and the disk is in a sleep state this fails
 * with %UDISKS_ERROR_WOULD_WAKEUP.
 *
 * This may only be called if @drive has been associated with a
 * #UDisksLinuxDriveObject instance.
 *
 * This method may be called from any thread.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
udisks_linux_drive_ata_refresh_smart_sync (UDisksLinuxDriveAta  *drive,
                                           gboolean              nowakeup,
                                           GCancellable         *cancellable,
                                           GError              **error)
{
  UDisksLinuxDriveObject  *object;
  GUdevDevice *device;
  gboolean ret;
  SkDisk *d;
  SkBool awake;
  SkBool good;
  uint64_t temp_mkelvin;
  uint64_t power_on_msec;

  object = UDISKS_LINUX_DRIVE_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (drive)));
  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  g_assert (device != NULL);

  /* TODO: use cancellable */

  d = NULL;
  ret = FALSE;

  if (sk_disk_open (g_udev_device_get_device_file (device), &d) != 0)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "sk_disk_open: %m");
      goto out;
    }

  if (sk_disk_check_sleep_mode (d, &awake) != 0)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "sk_disk_check_sleep_mode: %m");
      goto out;
    }

  /* don't wake up disk unless specically asked to */
  if (nowakeup && !awake)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_WOULD_WAKEUP,
                   "Disk is in sleep mode and the nowakeup option was passed");
      goto out;
    }

  if (sk_disk_smart_read_data (d) != 0)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "sk_disk_smart_read_data: %m");
      goto out;
    }

  if (sk_disk_smart_status (d, &good) != 0)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "sk_disk_smart_status: %m");
      goto out;
    }

  /* don't care if these are failing or not */
  temp_mkelvin = 0;
  sk_disk_smart_get_temperature (d, &temp_mkelvin);
  power_on_msec = 0;
  sk_disk_smart_get_power_on (d, &power_on_msec);

  G_LOCK (object_lock);
  drive->smart_updated = time (NULL);
  drive->smart_failing = !good;
  drive->smart_temperature = temp_mkelvin / 1000.0;
  drive->smart_power_on_seconds = power_on_msec / 1000.0;
  G_UNLOCK (object_lock);

  update_smart (drive, device);

  ret = TRUE;

 out:
  g_object_unref (device);
  if (d != NULL)
    sk_disk_free (d);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_smart_update (UDisksDriveAta        *_drive,
                     GDBusMethodInvocation *invocation,
                     GVariant              *options)
{
  UDisksLinuxDriveAta *drive = UDISKS_LINUX_DRIVE_ATA (_drive);
  UDisksLinuxDriveObject *object;
  UDisksLinuxBlockObject *block_object;
  UDisksBlock *block;
  UDisksDaemon *daemon;
  const gchar *action_id;
  gboolean nowakeup;
  GError *error;

  daemon = NULL;
  block = NULL;

  object = UDISKS_LINUX_DRIVE_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (drive)));
  daemon = udisks_linux_drive_object_get_daemon (object);
  block_object = udisks_linux_drive_object_get_block (object, TRUE);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unable to find physical block device for drive");
      goto out;
    }
  block = udisks_object_peek_block (UDISKS_OBJECT (block_object));

  g_variant_lookup (options,
                    "nowakeup",
                    "b",
                    &nowakeup);

  /* TODO: is it a good idea to overload modify-device? */
  action_id = "org.freedesktop.udisks2.modify-device";
  if (udisks_block_get_hint_system (block))
    action_id = "org.freedesktop.udisks2.modify-device-system";

  /* Check that the user is actually authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (block_object),
                                                    action_id,
                                                    options,
                                                    N_("Authentication is required to update S.M.A.R.T. data from $(udisks2.device)"),
                                                    invocation))
    goto out;

  if (!udisks_drive_ata_get_smart_supported (UDISKS_DRIVE_ATA (drive)))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "SMART is not supported");
      goto out;
    }

  if (!udisks_drive_ata_get_smart_enabled (UDISKS_DRIVE_ATA (drive)))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "SMART is not enabled");
      goto out;
    }

  error = NULL;
  if (!udisks_linux_drive_ata_refresh_smart_sync (drive,
                                                  nowakeup,
                                                  NULL /* cancellable */,
                                                  &error))
    {
      udisks_warning ("Error updating ATA smart for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (drive)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_drive_ata_complete_smart_update (UDISKS_DRIVE_ATA (drive), invocation);

 out:
  if (block_object != NULL)
    g_object_unref (block_object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
drive_ata_iface_init (UDisksDriveAtaIface *iface)
{
  iface->handle_smart_update = handle_smart_update;
}
