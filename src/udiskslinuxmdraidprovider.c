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

#include "udiskslinuxmdraidprovider.h"
#include "udiskslinuxmdraidobject.h"
#include "udiskslinuxdevice.h"
#include "udisksdaemon.h"
#include "udiskslogging.h"

/* called with lock held */

static void
maybe_remove_mdraid_object (UDisksDaemon              *daemon,
                            UDisksLinuxMDRaidProvider *provider,
                            UDisksLinuxMDRaidObject   *object)
{
  gchar *object_uuid = NULL;

  /* remove the object only if there are no devices left */
  if (udisks_linux_mdraid_object_have_devices (object))
    goto out;

  object_uuid = g_strdup (udisks_linux_mdraid_object_get_uuid (object));
  g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (daemon),
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  g_warn_if_fail (g_hash_table_remove (provider->uuid_to_mdraid, object_uuid));

 out:
  g_free (object_uuid);
}

static void
handle_block_uevent_for_mdraid_with_uuid (UDisksDaemon              *daemon,
                                          UDisksLinuxMDRaidProvider *provider,
                                          const gchar               *action,
                                          UDisksLinuxDevice         *device,
                                          const gchar               *uuid,
                                          gboolean                   is_member)
{
  UDisksLinuxMDRaidObject *object;
  const gchar *sysfs_path;

  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  /* if uuid is NULL or bogus, consider it a remove event */
  if (uuid == NULL || g_strcmp0 (uuid, "00000000:00000000:00000000:00000000") == 0)
    action = "remove";
  else
    {
      /* sometimes the bogus UUID looks legit, but it is still bogus. */
      if (!is_member)
        {
          UDisksLinuxMDRaidObject *candidate = g_hash_table_lookup (provider->sysfs_path_to_mdraid, sysfs_path);
          if (candidate != NULL &&
              g_strcmp0 (uuid, udisks_linux_mdraid_object_get_uuid (candidate)) != 0)
            {
              udisks_debug ("UUID of %s became bogus (changed from %s to %s)",
                            sysfs_path, udisks_linux_mdraid_object_get_uuid (candidate), uuid);
              action = "remove";
            }
        }
    }

  if (g_strcmp0 (action, "remove") == 0)
    {
      /* first check if this device was a member */
      object = g_hash_table_lookup (provider->sysfs_path_to_mdraid_members, sysfs_path);
      if (object != NULL)
        {
          udisks_linux_mdraid_object_uevent (object, action, device, TRUE /* is_member */);
          g_warn_if_fail (g_hash_table_remove (provider->sysfs_path_to_mdraid_members, sysfs_path));
          maybe_remove_mdraid_object (daemon, provider, object);
        }

      /* then check if the device was the raid device */
      object = g_hash_table_lookup (provider->sysfs_path_to_mdraid, sysfs_path);
      if (object != NULL)
        {
          udisks_linux_mdraid_object_uevent (object, action, device, FALSE /* is_member */);
          g_warn_if_fail (g_hash_table_remove (provider->sysfs_path_to_mdraid, sysfs_path));
          maybe_remove_mdraid_object (daemon, provider, object);
        }
    }
  else
    {
      if (uuid == NULL)
        goto out;

      object = g_hash_table_lookup (provider->uuid_to_mdraid, uuid);
      if (object != NULL)
        {
          if (is_member)
            {
              if (g_hash_table_lookup (provider->sysfs_path_to_mdraid_members, sysfs_path) == NULL)
                g_hash_table_insert (provider->sysfs_path_to_mdraid_members, g_strdup (sysfs_path), object);
            }
          else
            {
              if (g_hash_table_lookup (provider->sysfs_path_to_mdraid, sysfs_path) == NULL)
                g_hash_table_insert (provider->sysfs_path_to_mdraid, g_strdup (sysfs_path), object);
            }
          udisks_linux_mdraid_object_uevent (object, action, device, is_member);
        }
      else
        {
          object = udisks_linux_mdraid_object_new (daemon, uuid);
          udisks_linux_mdraid_object_uevent (object, action, device, is_member);
          g_dbus_object_manager_server_export_uniquely (udisks_daemon_get_object_manager (daemon),
                                                        G_DBUS_OBJECT_SKELETON (object));
          g_hash_table_insert (provider->uuid_to_mdraid, g_strdup (uuid), object);
          if (is_member)
            g_hash_table_insert (provider->sysfs_path_to_mdraid_members, g_strdup (sysfs_path), object);
          else
            g_hash_table_insert (provider->sysfs_path_to_mdraid, g_strdup (sysfs_path), object);
        }
    }

 out:
  ;
}

void
handle_block_uevent_for_mdraid (UDisksDaemon              *daemon,
                                UDisksLinuxMDRaidProvider *provider,
                                const gchar               *action,
                                UDisksLinuxDevice         *device)
{
  const gchar *uuid;
  const gchar *member_uuid;

  /* For nested RAID levels, a device can be both a member of one
   * array and the RAID device for another. Therefore we need to
   * consider both UUIDs.
   *
   * For removal, we also need to consider the case where there is no
   * UUID.
   */
  uuid = g_udev_device_get_property (device->udev_device, "UDISKS_MD_UUID");
  if (! uuid)
    uuid = g_udev_device_get_property (device->udev_device, "STORAGED_MD_UUID");

  member_uuid = g_udev_device_get_property (device->udev_device, "UDISKS_MD_MEMBER_UUID");
  if (! member_uuid)
    member_uuid = g_udev_device_get_property (device->udev_device, "STORAGED_MD_MEMBER_UUID");

  if (uuid != NULL)
    handle_block_uevent_for_mdraid_with_uuid (daemon, provider, action, device, uuid, FALSE);

  if (member_uuid != NULL)
    handle_block_uevent_for_mdraid_with_uuid (daemon, provider, action, device, member_uuid, TRUE);

  if (uuid == NULL && member_uuid == NULL)
    handle_block_uevent_for_mdraid_with_uuid (daemon, provider, action, device, NULL, FALSE);
}
