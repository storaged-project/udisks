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
#include "udiskslinuxpartitiontable.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxpartitiontable
 * @title: UDisksLinuxPartitionTable
 * @short_description: Linux implementation of #UDisksPartitionTable
 *
 * This type provides an implementation of the #UDisksPartitionTable
 * interface on Linux.
 */

typedef struct _UDisksLinuxPartitionTableClass   UDisksLinuxPartitionTableClass;

/**
 * UDisksLinuxPartitionTable:
 *
 * The #UDisksLinuxPartitionTable structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxPartitionTable
{
  UDisksPartitionTableSkeleton parent_instance;
};

struct _UDisksLinuxPartitionTableClass
{
  UDisksPartitionTableSkeletonClass parent_class;
};

static void partition_table_iface_init (UDisksPartitionTableIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxPartitionTable, udisks_linux_partition_table, UDISKS_TYPE_PARTITION_TABLE_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_PARTITION_TABLE, partition_table_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_partition_table_init (UDisksLinuxPartitionTable *partition_table)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (partition_table),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_partition_table_class_init (UDisksLinuxPartitionTableClass *klass)
{
}

/**
 * udisks_linux_partition_table_new:
 *
 * Creates a new #UDisksLinuxPartitionTable instance.
 *
 * Returns: A new #UDisksLinuxPartitionTable. Free with g_object_unref().
 */
UDisksPartitionTable *
udisks_linux_partition_table_new (void)
{
  return UDISKS_PARTITION_TABLE (g_object_new (UDISKS_TYPE_LINUX_PARTITION_TABLE,
                                               NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_partition_table_update:
 * @table: A #UDisksLinuxPartitionTable.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_partition_table_update (UDisksLinuxPartitionTable  *table,
                                     UDisksLinuxBlockObject     *object)
{
  const gchar *type = NULL;
  GUdevDevice *device = NULL;;

  device = udisks_linux_block_object_get_device (object);
  if (device != NULL)
    type = g_udev_device_get_property (device, "ID_PART_TABLE_TYPE");

  udisks_partition_table_set_type_ (UDISKS_PARTITION_TABLE (table), type);

  g_clear_object (&device);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
partition_table_iface_init (UDisksPartitionTableIface *iface)
{
}
