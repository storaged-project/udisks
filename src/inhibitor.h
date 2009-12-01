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

#ifndef __INHIBITOR_H__
#define __INHIBITOR_H__

#include <dbus/dbus-glib.h>

#include "types.h"

G_BEGIN_DECLS

#define TYPE_INHIBITOR         (inhibitor_get_type ())
#define INHIBITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_INHIBITOR, Inhibitor))
#define INHIBITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_INHIBITOR, InhibitorClass))
#define IS_INHIBITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_INHIBITOR))
#define IS_INHIBITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_INHIBITOR))
#define INHIBITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_INHIBITOR, InhibitorClass))

typedef struct InhibitorClass InhibitorClass;
typedef struct InhibitorPrivate InhibitorPrivate;

struct Inhibitor
{
  GObject parent;
  InhibitorPrivate *priv;
};

struct InhibitorClass
{
  GObjectClass parent_class;
};

GType inhibitor_get_type (void) G_GNUC_CONST;
Inhibitor *inhibitor_new (DBusGMethodInvocation *context);
const gchar *inhibitor_get_unique_dbus_name (Inhibitor *inhibitor);
const gchar *inhibitor_get_cookie (Inhibitor *inhibitor);

G_END_DECLS

#endif /* __INHIBITOR_H__ */
