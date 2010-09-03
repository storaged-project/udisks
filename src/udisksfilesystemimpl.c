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

#include "udisksfilesystemimpl.h"

typedef struct _UDisksFilesystemImplClass   UDisksFilesystemImplClass;

struct _UDisksFilesystemImpl
{
  UDisksFilesystemStub parent_instance;
};

struct _UDisksFilesystemImplClass
{
  UDisksFilesystemStubClass parent_class;
  gpointer padding[8];
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

UDisksFilesystem *
udisks_filesystem_impl_new (void)
{
  return UDISKS_FILESYSTEM (g_object_new (UDISKS_TYPE_FILESYSTEM_IMPL, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_mount (UDisksFilesystem       *object,
              GDBusMethodInvocation  *invocation,
              const gchar            *filesystem_type,
              const gchar* const     *options)
{
  //UDisksFilesystemImpl *filesystem = UDISKS_FILESYSTEM_IMPL (object);
  g_dbus_method_invocation_return_dbus_error (invocation, "org.foo.error.mount", "no, not yet implemented");
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
