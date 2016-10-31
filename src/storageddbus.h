/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2016 Red Hat Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Stef Walter <stefw@redhat.com>
 */

#ifndef __STORAGED_DBUS_H__
#define __STORAGED_DBUS_H__

#include <systemd/sd-bus.h>
#include <gio/gio.h>

gboolean                storaged_dbus_initialize                 (sd_bus **system,
                                                                  GDBusConnection **connection);

/*
 * These GDBus related functions are unsupported due to the sd-bus to GDBus
 * bridge. In particular the GDBus code will believe that it has a different
 * unique name than actually present on the bus.
 */

#define g_dbus_connection_get_unique_name unsupported__dbus_connection_get_unique_name

#endif /* __STORAGED_DBUS_H__ */

