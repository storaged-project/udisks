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

#include "udisksdaemon.h"
#include "udiskslinuxblock.h"

#include "udisksfilesystemimpl.h"

/**
 * SECTION:udisksfilesystemimpl
 * @title: UDisksFilesystemImpl
 * @short_description: Filesystem Implementation
 *
 * This type provides an implementation of the #UDisksFilesystem
 * interface that uses the <command>mount</command> and
 * <command>umount</command> commands.
 *
 * TODO: mention other impl details like
 * <filename>/var/lib/udisks/mtab</filename>, how mount options work,
 * what role <filename>/etc/fstab</filename> plays and so on.
 */


typedef struct _UDisksFilesystemImplClass   UDisksFilesystemImplClass;

/**
 * UDisksFilesystemImpl:
 *
 * The #UDisksFilesystemImpl structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksFilesystemImpl
{
  UDisksFilesystemStub parent_instance;
};

struct _UDisksFilesystemImplClass
{
  UDisksFilesystemStubClass parent_class;
};

static void filesystem_iface_init (UDisksFilesystemIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksFilesystemImpl, udisks_filesystem_impl, UDISKS_TYPE_FILESYSTEM_STUB,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_FILESYSTEM, filesystem_iface_init));

static void
udisks_filesystem_impl_init (UDisksFilesystemImpl *filesystem)
{
}

static void
udisks_filesystem_impl_class_init (UDisksFilesystemImplClass *klass)
{
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mount_on_job_completed (UDisksJob    *job,
                        gboolean      success,
                        const gchar  *message,
                        gpointer      user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (user_data);
  UDisksFilesystem *interface;

  interface = UDISKS_FILESYSTEM (g_object_get_data (G_OBJECT (job), "filesystem-interface"));

  if (!success)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Mounting the device failed: %s",
                                             message);
    }
  else
    {
      /* TODO: determine mount point */
      udisks_filesystem_complete_mount (interface, invocation, "/foobar");
    }
}

static gboolean
handle_mount (UDisksFilesystem       *interface,
              GDBusMethodInvocation  *invocation,
              const gchar            *filesystem_type,
              const gchar* const     *options)
{
  GDBusObject *object;
  UDisksBlockDevice *block;
  UDisksDaemon *daemon;
  UDisksJob *job;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (interface));
  block = UDISKS_BLOCK_DEVICE (g_dbus_object_lookup_interface (object, "org.freedesktop.UDisks.BlockDevice"));
  daemon = udisks_linux_block_get_daemon (UDISKS_LINUX_BLOCK (object));

  job = UDISKS_JOB (udisks_daemon_launch_spawned_job (daemon,
                                                      NULL, /* GCancellable */
                                                      NULL, /* input string */
                                                      "/bin/false"));
  /* this blows a little bit - would be nice to have an easier way to
   * get back to the object from the job
   */
  g_object_set_data_full (G_OBJECT (job),
                          "filesystem-interface",
                          g_object_ref (interface),
                          (GDestroyNotify) g_object_unref);
  g_signal_connect (job,
                    "completed",
                    G_CALLBACK (mount_on_job_completed),
                    invocation);

  g_object_unref (block);
  g_object_unref (object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_unmount (UDisksFilesystem       *object,
                GDBusMethodInvocation  *invocation,
                const gchar* const     *options)
{
  //UDisksFilesystemImpl *filesystem = UDISKS_FILESYSTEM_IMPL (object);
  g_dbus_method_invocation_return_dbus_error (invocation, "org.foo.error.unmount", "no, not yet implemented");
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
filesystem_iface_init (UDisksFilesystemIface *iface)
{
  iface->handle_mount   = handle_mount;
  iface->handle_unmount = handle_unmount;
}

/* ---------------------------------------------------------------------------------------------------- */
