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

#include <src/udiskslogging.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxdevice.h>

#include "udiskslinuxblocklvm2.h"
#include "udisks-lvm2-generated.h"

/**
 * SECTION:udiskslinuxblocklvm2
 * @title: UDisksLinuxBlockLVM2
 * @short_description: Linux implementation of #UDisksBlockLVM2
 *
 * This type provides an implementation of the #UDisksBlockLVM2
 * interface on Linux.
 */

typedef struct _UDisksLinuxBlockLVM2Class   UDisksLinuxBlockLVM2Class;

/**
 * UDisksLinuxBlockLVM2:
 *
 * The #UDisksLinuxBlockLVM2 structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxBlockLVM2
{
  UDisksBlockLVM2Skeleton parent_instance;
};

struct _UDisksLinuxBlockLVM2Class
{
  UDisksBlockLVM2SkeletonClass parent_class;
};

static void udisks_linux_block_lvm2_iface_init (UDisksBlockLVM2Iface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxBlockLVM2, udisks_linux_block_lvm2, UDISKS_TYPE_BLOCK_LVM2_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_BLOCK_LVM2, udisks_linux_block_lvm2_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_block_lvm2_init (UDisksLinuxBlockLVM2 *block)
{
}

static void
udisks_linux_block_lvm2_class_init (UDisksLinuxBlockLVM2Class *klass)
{
}

/**
 * udisks_linux_block_lvm2_new:
 *
 * Creates a new #UDisksLinuxBlockLVM2 instance.
 *
 * Returns: A new #UDisksLinuxBlockLVM2. Free with g_object_unref().
 */
UDisksBlockLVM2 *
udisks_linux_block_lvm2_new (void)
{
  return UDISKS_BLOCK_LVM2 (g_object_new (UDISKS_TYPE_LINUX_BLOCK_LVM2, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_block_lvm2_update:
 * @block_lvm2: A #UDisksLinuxBlockLVM2.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_block_lvm2_update (UDisksLinuxBlockLVM2   *block_lvm2,
                                UDisksLinuxBlockObject *object)
{
  /* do something */

  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_block_lvm2_iface_init (UDisksBlockLVM2Iface *iface)
{
}
