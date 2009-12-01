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

#ifndef __PORT_H__
#define __PORT_H__

#include <dbus/dbus-glib.h>
#include <gudev/gudev.h>
#include <sys/types.h>

#include "types.h"

G_BEGIN_DECLS

#define TYPE_PORT         (port_get_type ())
#define PORT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_PORT, Port))
#define PORT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_PORT, PortClass))
#define IS_PORT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_PORT))
#define IS_PORT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_PORT))
#define PORT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_PORT, PortClass))

typedef struct PortClass PortClass;
typedef struct PortPrivate PortPrivate;

struct Port
{
  GObject parent;
  PortPrivate *priv;
};

struct PortClass
{
  GObjectClass parent_class;
};

GType    port_get_type (void) G_GNUC_CONST;

Port    *port_new (Daemon *daemon,
                   GUdevDevice *d);

gboolean port_changed (Port *port,
                       GUdevDevice *d,
                       gboolean synthesized);

void     port_removed (Port *port);

/* local methods */

const char *port_local_get_object_path      (Port *port);
const char *port_local_get_native_path      (Port *port);
gboolean    local_port_encloses_native_path (Port *port,
                                             const gchar *native_path);

G_END_DECLS

#endif /* __PORT_H__ */
