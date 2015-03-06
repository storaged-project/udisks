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
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>

#include "dummytypes.h"
#include "dummylinuxmanager.h"
#include "dummy-generated.h"

/**
 * SECTION:dummylinuxmanager
 * @title: DummyLinuxManager
 * @short_description: Linux implementation of #DummyLinuxManager
 *
 * This type provides an implementation of the #DummyLinuxManager
 * interface on Linux.
 */

typedef struct _DummyLinuxManagerClass   DummyLinuxManagerClass;

/**
 * DummyLinuxManager:
 *
 * The #DummyLinuxManager structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _DummyLinuxManager
{
  DummyDummyManagerSkeleton parent_instance;
};

struct _DummyLinuxManagerClass
{
  DummyDummyManagerSkeletonClass parent_class;
};

static void dummy_linux_manager_iface_init (DummyDummyManagerIface *iface);

G_DEFINE_TYPE_WITH_CODE (DummyLinuxManager, dummy_linux_manager, DUMMY_TYPE_DUMMY_MANAGER_SKELETON,
                         G_IMPLEMENT_INTERFACE (DUMMY_TYPE_DUMMY_MANAGER, dummy_linux_manager_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
dummy_linux_manager_init (DummyLinuxManager *manager)
{
}

static void
dummy_linux_manager_class_init (DummyLinuxManagerClass *klass)
{
}

/**
 * dummy_linux_manager_new:
 *
 * Creates a new #DummyLinuxManager instance.
 *
 * Returns: A new #DummyLinuxManager. Free with g_object_unref().
 */
DummyLinuxManager *
dummy_linux_manager_new (void)
{
  return DUMMY_LINUX_MANAGER (g_object_new (DUMMY_TYPE_LINUX_MANAGER, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_create_loop_pool (DummyDummyManager *object,
                         GDBusMethodInvocation *invocation,
                         const gchar *arg_name)
{
  storaged_notice ("Dummy plugin: called org.storaged.Storaged.DummyManager.CreateLoopPool(name=\"%s\")", arg_name);

  dummy_dummy_manager_complete_create_loop_pool (object, invocation);

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
dummy_linux_manager_iface_init (DummyDummyManagerIface *iface)
{
  iface->handle_create_loop_pool = handle_create_loop_pool;
}
