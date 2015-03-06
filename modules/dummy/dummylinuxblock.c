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
#include <src/storagedlinuxdriveobject.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedbasejob.h>
#include <src/storagedsimplejob.h>
#include <src/storagedthreadedjob.h>
#include <src/storagedlinuxdevice.h>

#include "dummytypes.h"
#include "dummylinuxblock.h"
#include "dummy-generated.h"

/**
 * SECTION:storagedlinuxdriveata
 * @title: DummyLinuxBlock
 * @short_description: Linux implementation of #DummyDummyBlock
 *
 * This type provides an implementation of the #DummyDummyBlock
 * interface on Linux.
 */

typedef struct _DummyLinuxBlockClass   DummyLinuxBlockClass;

/**
 * DummyLinuxBlock:
 *
 * The #DummyLinuxBlock structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _DummyLinuxBlock
{
  DummyDummyBlockSkeleton parent_instance;

  StoragedThreadedJob *selftest_job;
};

struct _DummyLinuxBlockClass
{
  DummyDummyBlockSkeletonClass parent_class;
};

static void dummy_linux_block_iface_init (DummyDummyBlockIface *iface);

G_DEFINE_TYPE_WITH_CODE (DummyLinuxBlock, dummy_linux_block, DUMMY_TYPE_DUMMY_BLOCK_SKELETON,
                         G_IMPLEMENT_INTERFACE (DUMMY_TYPE_DUMMY_BLOCK, dummy_linux_block_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
dummy_linux_block_finalize (GObject *object)
{
  DummyLinuxBlock *block = DUMMY_LINUX_BLOCK (object);

  if (G_OBJECT_CLASS (dummy_linux_block_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (dummy_linux_block_parent_class)->finalize (object);
}


static void
dummy_linux_block_init (DummyLinuxBlock *block)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (block),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);

  dummy_dummy_block_set_have_nonsense (DUMMY_DUMMY_BLOCK (block), FALSE);
}

static void
dummy_linux_block_class_init (DummyLinuxBlockClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = dummy_linux_block_finalize;
}

/**
 * dummy_linux_block_new:
 *
 * Creates a new #DummyLinuxBlock instance.
 *
 * Returns: A new #DummyLinuxBlock. Free with g_object_unref().
 */
DummyDummyBlock *
dummy_linux_block_new (void)
{
  return DUMMY_DUMMY_BLOCK (g_object_new (DUMMY_TYPE_LINUX_BLOCK,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * dummy_linux_block_update:
 * @drive: A #DummyLinuxBlock.
 * @object: The enclosing #StoragedLinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
dummy_linux_block_update (DummyLinuxBlock          *block,
                          StoragedLinuxBlockObject *object)
{
  /* do something */

  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_create_nonsense (DummyDummyBlock *object,
                        GDBusMethodInvocation *invocation,
                        const gchar *arg_name)
{
  dummy_dummy_block_set_have_nonsense (object, TRUE);
  dummy_dummy_block_emit_nonsense_created (object, TRUE, arg_name);

  dummy_dummy_block_complete_create_nonsense (object, invocation);

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
dummy_linux_block_iface_init (DummyDummyBlockIface *iface)
{
  iface->handle_create_nonsense = handle_create_nonsense;
}
