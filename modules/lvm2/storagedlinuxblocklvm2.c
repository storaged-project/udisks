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

#include <src/storagedlogging.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>

#include "storagedlinuxblocklvm2.h"
#include "storaged-lvm2-generated.h"

/**
 * SECTION:storagedlinuxblocklvm2
 * @title: StoragedLinuxBlockLVM2
 * @short_description: Linux implementation of #StoragedBlockLVM2
 *
 * This type provides an implementation of the #StoragedBlockLVM2
 * interface on Linux.
 */

typedef struct _StoragedLinuxBlockLVM2Class   StoragedLinuxBlockLVM2Class;

/**
 * StoragedLinuxBlockLVM2:
 *
 * The #StoragedLinuxBlockLVM2 structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxBlockLVM2
{
  StoragedBlockLVM2Skeleton parent_instance;
};

struct _StoragedLinuxBlockLVM2Class
{
  StoragedBlockLVM2SkeletonClass parent_class;
};

static void storaged_linux_block_lvm2_iface_init (StoragedBlockLVM2Iface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxBlockLVM2, storaged_linux_block_lvm2, STORAGED_TYPE_BLOCK_LVM2_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_BLOCK_LVM2, storaged_linux_block_lvm2_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_block_lvm2_init (StoragedLinuxBlockLVM2 *block)
{
}

static void
storaged_linux_block_lvm2_class_init (StoragedLinuxBlockLVM2Class *klass)
{
}

/**
 * storaged_linux_block_lvm2_new:
 *
 * Creates a new #StoragedLinuxBlockLVM2 instance.
 *
 * Returns: A new #StoragedLinuxBlockLVM2. Free with g_object_unref().
 */
StoragedBlockLVM2 *
storaged_linux_block_lvm2_new (void)
{
  return STORAGED_BLOCK_LVM2 (g_object_new (STORAGED_TYPE_LINUX_BLOCK_LVM2, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_block_lvm2_update:
 * @block_lvm2: A #StoragedLinuxBlockLVM2.
 * @object: The enclosing #StoragedLinuxBlockObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
storaged_linux_block_lvm2_update (StoragedLinuxBlockLVM2   *block_lvm2,
                                  StoragedLinuxBlockObject *object)
{
  /* do something */

  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_block_lvm2_iface_init (StoragedBlockLVM2Iface *iface)
{
}
