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

#ifndef __ADAPTER_H__
#define __ADAPTER_H__

#include <dbus/dbus-glib.h>
#include <gudev/gudev.h>
#include <sys/types.h>

#include "types.h"

G_BEGIN_DECLS

#define TYPE_ADAPTER         (adapter_get_type ())
#define ADAPTER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_ADAPTER, Adapter))
#define ADAPTER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_ADAPTER, AdapterClass))
#define IS_ADAPTER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_ADAPTER))
#define IS_ADAPTER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_ADAPTER))
#define ADAPTER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_ADAPTER, AdapterClass))

typedef struct AdapterClass AdapterClass;
typedef struct AdapterPrivate AdapterPrivate;

struct Adapter
{
  GObject parent;
  AdapterPrivate *priv;
};

struct AdapterClass
{
  GObjectClass parent_class;
};

GType adapter_get_type (void) G_GNUC_CONST;

Adapter *adapter_new (Daemon *daemon,
                      GUdevDevice *d);

gboolean adapter_changed (Adapter *adapter,
                          GUdevDevice *d,
                          gboolean synthesized);

void adapter_removed (Adapter *adapter);

/* local methods */

const char *adapter_local_get_object_path (Adapter *adapter);
const char *adapter_local_get_native_path (Adapter *adapter);
const char *adapter_local_get_driver (Adapter *adapter);
const char *adapter_local_get_fabric (Adapter *adapter);

G_END_DECLS

#endif /* __ADAPTER_H__ */
