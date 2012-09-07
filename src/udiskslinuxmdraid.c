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
  guint num_devices;
  const gchar *level;
  guint64 size = 0;
  GUdevDevice *raid_device;
  GList *member_devices;
  GUdevDevice *member_device;

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

  /* TODO: set other stuff */

 out:
  g_list_free_full (member_devices, g_object_unref);
  g_clear_object (&raid_device);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mdraid_iface_init (UDisksMDRaidIface *iface)
{
}
