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

#include <glib/gstdio.h>

#include "udiskslogging.h"
#include "udiskslinuxpartition.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxpartition
 * @title: UDisksLinuxPartition
 * @short_description: Linux implementation of #UDisksPartition
 *
 * This type provides an implementation of the #UDisksPartition
 * interface on Linux.
 */

typedef struct _UDisksLinuxPartitionClass   UDisksLinuxPartitionClass;

/**
 * UDisksLinuxPartition:
 *
 * The #UDisksLinuxPartition structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxPartition
{
  UDisksPartitionSkeleton parent_instance;
};

struct _UDisksLinuxPartitionClass
{
  UDisksPartitionSkeletonClass parent_class;
};

static void partition_iface_init (UDisksPartitionIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxPartition, udisks_linux_partition, UDISKS_TYPE_PARTITION_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_PARTITION, partition_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_partition_init (UDisksLinuxPartition *partition)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (partition),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_partition_class_init (UDisksLinuxPartitionClass *klass)
{
}

/**
 * udisks_linux_partition_new:
 *
 * Creates a new #UDisksLinuxPartition instance.
 *
 * Returns: A new #UDisksLinuxPartition. Free with g_object_unref().
 */
UDisksPartition *
udisks_linux_partition_new (void)
{
  return UDISKS_PARTITION (g_object_new (UDISKS_TYPE_LINUX_PARTITION,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_partition_update:
 * @partition: A #UDisksLinuxPartition.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_partition_update (UDisksLinuxPartition  *partition,
                               UDisksLinuxBlockObject     *object)
{
  UDisksObject *disk_block_object = NULL;
  GUdevDevice *device = NULL;
  guint number = 0;
  const gchar *type = NULL;
  guint64 offset = 0;
  guint64 size = 0;
  gchar *name = NULL;
  const gchar *uuid = NULL;
  const gchar *flags = NULL;
  const gchar *table_object_path = "/";
  gboolean is_container = FALSE;
  gboolean is_contained = FALSE;

  device = udisks_linux_block_object_get_device (object);
  if (g_udev_device_has_property (device, "ID_PART_ENTRY_TYPE"))
    {
      const gchar *disk_string;
      number = g_udev_device_get_property_as_int (device, "ID_PART_ENTRY_NUMBER");
      type = g_udev_device_get_property (device, "ID_PART_ENTRY_TYPE");
      offset = g_udev_device_get_property_as_uint64 (device, "ID_PART_ENTRY_OFFSET") * G_GUINT64_CONSTANT (512);
      size = g_udev_device_get_property_as_uint64 (device, "ID_PART_ENTRY_SIZE") * G_GUINT64_CONSTANT (512);
      name = udisks_decode_udev_string (g_udev_device_get_property (device, "ID_PART_ENTRY_NAME"));
      uuid = g_udev_device_get_property (device, "ID_PART_ENTRY_UUID");
      flags = g_udev_device_get_property (device, "ID_PART_ENTRY_FLAGS");

      disk_string = g_udev_device_get_property (device, "ID_PART_ENTRY_DISK");
      if (disk_string != NULL)
        {
          gint disk_major, disk_minor;
          if (sscanf (disk_string, "%d:%d", &disk_major, &disk_minor) == 2)
            {
              disk_block_object = udisks_daemon_find_block (udisks_linux_block_object_get_daemon (object),
                                                            makedev (disk_major, disk_minor));
            }
        }

      if (g_strcmp0 (g_udev_device_get_property (device, "ID_PART_ENTRY_SCHEME"), "dos") == 0)
        {
          if (number <= 4)
            {
              gint type_as_int = strtol (type, NULL, 0);
              if (type_as_int == 0x05 || type_as_int == 0x0f || type_as_int == 0x85)
                {
                  is_container = TRUE;
                }
            }
          else if (number >= 5)
            {
              is_contained = TRUE;
            }
        }
    }
  else
    {
      GUdevDevice *parent_device;
      number = g_udev_device_get_sysfs_attr_as_int (device, "partition");
      offset = g_udev_device_get_sysfs_attr_as_uint64 (device, "start") * G_GUINT64_CONSTANT (512);
      size = g_udev_device_get_sysfs_attr_as_uint64 (device, "size") * G_GUINT64_CONSTANT (512);
      parent_device = g_udev_device_get_parent_with_subsystem (device, "block", "disk");
      if (parent_device != NULL)
        {
          disk_block_object = udisks_daemon_find_block (udisks_linux_block_object_get_daemon (object),
                                                        g_udev_device_get_device_number (parent_device));
          g_object_unref (parent_device);
        }
    }

  if (disk_block_object != NULL)
    table_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (disk_block_object));

  udisks_partition_set_number (UDISKS_PARTITION (partition), number);
  udisks_partition_set_type_ (UDISKS_PARTITION (partition), type);
  udisks_partition_set_flags (UDISKS_PARTITION (partition), flags);
  udisks_partition_set_offset (UDISKS_PARTITION (partition), offset);
  udisks_partition_set_size (UDISKS_PARTITION (partition), size);
  udisks_partition_set_name (UDISKS_PARTITION (partition), name);
  udisks_partition_set_uuid (UDISKS_PARTITION (partition), uuid);
  udisks_partition_set_table (UDISKS_PARTITION (partition), table_object_path);
  udisks_partition_set_is_container (UDISKS_PARTITION (partition), is_container);
  udisks_partition_set_is_contained (UDISKS_PARTITION (partition), is_contained);

  g_free (name);
  g_clear_object (&device);
  g_clear_object (&disk_block_object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
partition_iface_init (UDisksPartitionIface *iface)
{
}
