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
#include "udiskslinuxloop.h"
#include "udiskslinuxblock.h"
#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxloop
 * @title: UDisksLinuxLoop
 * @short_description: Loop devices on Linux
 *
 * This type provides an implementation of the #UDisksLoop
 * interface on Linux.
 */

typedef struct _UDisksLinuxLoopClass   UDisksLinuxLoopClass;

/**
 * UDisksLinuxLoop:
 *
 * The #UDisksLinuxLoop structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxLoop
{
  UDisksLoopSkeleton parent_instance;
};

struct _UDisksLinuxLoopClass
{
  UDisksLoopSkeletonClass parent_class;
};

static void loop_iface_init (UDisksLoopIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxLoop, udisks_linux_loop, UDISKS_TYPE_LOOP_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_LOOP, loop_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_loop_init (UDisksLinuxLoop *loop)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (loop),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_loop_class_init (UDisksLinuxLoopClass *klass)
{
}

/**
 * udisks_linux_loop_new:
 *
 * Creates a new #UDisksLinuxLoop instance.
 *
 * Returns: A new #UDisksLinuxLoop. Free with g_object_unref().
 */
UDisksLoop *
udisks_linux_loop_new (void)
{
  return UDISKS_LOOP (g_object_new (UDISKS_TYPE_LINUX_LOOP,
                                    NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_delete (UDisksLoop             *loop,
               GDBusMethodInvocation  *invocation,
               GVariant               *options)
{
  UDisksObject *object;
  UDisksBlockDevice *block;
  UDisksDaemon *daemon;
  gchar *error_message;
  gchar *escaped_device;

  object = NULL;
  daemon = NULL;
  error_message = NULL;
  escaped_device = NULL;

  object = g_object_ref (UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (loop))));
  block = udisks_object_peek_block_device (object);
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    "org.freedesktop.udisks2.manage-loop-devices",
                                                    options,
                                                    N_("Authentication is required to delete the loop device $(udisks2.device)"),
                                                    invocation))
    goto out;

  escaped_device = g_strescape (udisks_block_device_get_device (block), NULL);

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              NULL,  /* GCancellable */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "losetup -d \"%s\"",
                                              escaped_device))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error deleting %s: %s",
                                             udisks_block_device_get_device (block),
                                             error_message);
      goto out;
    }

  udisks_notice ("Deleted loop device %s (was backed by %s)",
                 udisks_block_device_get_device (block),
                 udisks_loop_get_backing_file (loop));

  udisks_loop_complete_delete (loop, invocation);

 out:
  g_free (escaped_device);
  g_free (error_message);
  g_object_unref (object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}


/* ---------------------------------------------------------------------------------------------------- */

static void
loop_iface_init (UDisksLoopIface *iface)
{
  iface->handle_delete = handle_delete;
}
