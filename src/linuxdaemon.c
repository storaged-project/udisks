/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
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

#include <gudev/gudev.h>

#include "linuxdaemon.h"

struct _LinuxDaemonPrivate
{
  GUdevClient *udev_client;
};

static void daemon_iface_init (DaemonIface *iface);
G_DEFINE_TYPE_WITH_CODE (LinuxDaemon, linux_daemon, TYPE_DAEMON_STUB,
                         G_IMPLEMENT_INTERFACE (TYPE_DAEMON, daemon_iface_init));


static void
linux_daemon_finalize (GObject *object)
{
  LinuxDaemon *daemon = LINUX_DAEMON (object);

  g_object_unref (daemon->priv->udev_client);

  if (G_OBJECT_CLASS (linux_daemon_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (linux_daemon_parent_class)->finalize (object);
}

static void
linux_daemon_init (LinuxDaemon *daemon)
{
  const gchar *subsystems[] = {"block", NULL};

  daemon->priv = G_TYPE_INSTANCE_GET_PRIVATE (daemon, TYPE_LINUX_DAEMON, LinuxDaemonPrivate);

  daemon->priv->udev_client = g_udev_client_new (subsystems);
}

static void
linux_daemon_class_init (LinuxDaemonClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = linux_daemon_finalize;

  g_type_class_add_private (klass, sizeof (LinuxDaemonPrivate));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_enumerate_device_files (Daemon                *_daemon,
                               GDBusMethodInvocation *invocation)
{
  /* LinuxDaemon *daemon = LINUX_DAEMON (_daemon); */
  const gchar *ret[] = {"/dev/sda", "/dev/sda1", NULL};

  daemon_complete_enumerate_device_files (_daemon, invocation, ret);

  return TRUE;
}

static void
daemon_iface_init (DaemonIface *iface)
{
  iface->handle_enumerate_device_files = handle_enumerate_device_files;
}

/* ---------------------------------------------------------------------------------------------------- */
