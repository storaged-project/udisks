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

#ifndef __LINUX_DAEMON_H__
#define __LINUX_DAEMON_H__

#include "types.h"

G_BEGIN_DECLS

#define TYPE_LINUX_DAEMON         (linux_daemon_get_type ())
#define LINUX_DAEMON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_LINUX_DAEMON, LinuxDaemon))
#define LINUX_DAEMON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_DAEMON, LinuxDaemonClass))
#define IS_LINUX_DAEMON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_LINUX_DAEMON))
#define IS_LINUX_DAEMON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_LINUX_DAEMON))
#define LINUX_DAEMON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_LINUX_DAEMON, LinuxDaemonClass))

typedef struct _LinuxDaemonClass   LinuxDaemonClass;
typedef struct _LinuxDaemonPrivate LinuxDaemonPrivate;

struct _LinuxDaemon
{
  DaemonStub parent;
  LinuxDaemonPrivate *priv;
};

struct _LinuxDaemonClass
{
  DaemonStubClass parent_class;
};

GType linux_daemon_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __LINUX_DAEMON_H__ */
