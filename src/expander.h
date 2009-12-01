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

#ifndef __EXPANDER_H__
#define __EXPANDER_H__

#include <dbus/dbus-glib.h>
#include <gudev/gudev.h>
#include <sys/types.h>

#include "types.h"

G_BEGIN_DECLS

#define TYPE_EXPANDER         (expander_get_type ())
#define EXPANDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_EXPANDER, Expander))
#define EXPANDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_EXPANDER, ExpanderClass))
#define IS_EXPANDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_EXPANDER))
#define IS_EXPANDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_EXPANDER))
#define EXPANDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_EXPANDER, ExpanderClass))

typedef struct ExpanderClass ExpanderClass;
typedef struct ExpanderPrivate ExpanderPrivate;

struct Expander
{
  GObject parent;
  ExpanderPrivate *priv;
};

struct ExpanderClass
{
  GObjectClass parent_class;
};

GType expander_get_type (void) G_GNUC_CONST;

Expander *expander_new (Daemon *daemon, GUdevDevice *d);

gboolean expander_changed (Expander *expander, GUdevDevice *d, gboolean synthesized);

void expander_removed (Expander *expander);

/* local methods */

const char *expander_local_get_object_path (Expander *expander);
const char *expander_local_get_native_path (Expander *expander);

gboolean local_expander_encloses_native_path (Expander *expander, const gchar *native_path);

G_END_DECLS

#endif /* __EXPANDER_H__ */
