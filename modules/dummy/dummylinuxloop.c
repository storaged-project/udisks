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
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxdevice.h>

#include "dummytypes.h"
#include "dummyloopobject.h"
#include "dummylinuxloop.h"
#include "dummy-generated.h"

/**
 * SECTION:dummylinuxloop
 * @title: DummyLinuxLoop
 * @short_description: Linux implementation of #DummyDriveDummy
 *
 * This type provides an implementation of the #DummyDriveDummy
 * interface on Linux.
 */

typedef struct _DummyLinuxLoopClass   DummyLinuxLoopClass;

/**
 * DummyLinuxLoop:
 *
 * The #DummyLinuxLoop structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _DummyLinuxLoop
{
  DummyDummyLoopSkeleton parent_instance;
};

struct _DummyLinuxLoopClass
{
  DummyDummyLoopSkeletonClass parent_class;
};

static void dummy_linux_loop_iface_init (DummyDummyLoopIface *iface);

G_DEFINE_TYPE_WITH_CODE (DummyLinuxLoop, dummy_linux_loop, DUMMY_TYPE_DUMMY_LOOP_SKELETON,
                         G_IMPLEMENT_INTERFACE (DUMMY_TYPE_DUMMY_LOOP, dummy_linux_loop_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
dummy_linux_loop_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (dummy_linux_loop_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (dummy_linux_loop_parent_class)->finalize (object);
}


static void
dummy_linux_loop_init (DummyLinuxLoop *loop)
{
  dummy_dummy_loop_set_num_devices (DUMMY_DUMMY_LOOP (loop), 1);
}

static void
dummy_linux_loop_class_init (DummyLinuxLoopClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = dummy_linux_loop_finalize;
}

/**
 * dummy_linux_loop_new:
 *
 * Creates a new #DummyLinuxLoop instance.
 *
 * Returns: A new #DummyLinuxLoop. Free with g_object_unref().
 */
DummyLinuxLoop *
dummy_linux_loop_new (void)
{
  return DUMMY_LINUX_LOOP (g_object_new (DUMMY_TYPE_LINUX_LOOP, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * dummy_linux_loop_update:
 * @drive: A #DummyLinuxLoop.
 * @object: The enclosing #DummyLoopObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
dummy_linux_loop_update (DummyLinuxLoop  *loop,
                         DummyLoopObject *object)
{
  GList *devices;

  devices = dummy_loop_object_get_devices (object);
  dummy_dummy_loop_set_num_devices (DUMMY_DUMMY_LOOP (loop), g_list_length (devices));

  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
dummy_linux_loop_iface_init (DummyDummyLoopIface *iface)
{
}
