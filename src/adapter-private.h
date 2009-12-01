/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2009 David Zeuthen <david@fubar.dk>
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

#ifndef __ADAPTER_PRIVATE_H__
#define __ADAPTER_PRIVATE_H__

#include <dbus/dbus-glib.h>
#include <gudev/gudev.h>
#include <atasmart.h>

#include "types.h"

G_BEGIN_DECLS

struct AdapterPrivate
{
  DBusGConnection *system_bus_connection;
  Daemon *daemon;
  GUdevDevice *d;

  gchar *object_path;
  gchar *native_path;
  gboolean removed;

  /* if non-zero, the id of the idle for emitting a 'change' signal */
  guint emit_changed_idle_id;

  /**************/
  /* Properties */
  /**************/

  gchar *vendor;
  gchar *model;
  gchar *driver;
  guint num_ports;
  gchar *fabric;
};

/* property setters */

void adapter_set_vendor (Adapter *adapter, const gchar *value);
void adapter_set_model (Adapter *adapter, const gchar *value);
void adapter_set_driver (Adapter *adapter, const gchar *value);
void adapter_set_num_ports (Adapter *adapter, guint value);
void adapter_set_fabric (Adapter *adapter, const gchar *value);

G_END_DECLS

#endif /* __ADAPTER_PRIVATE_H__ */
