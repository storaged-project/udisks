/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014 Tomas Bzatek <tbzatek@redhat.com>
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

#ifndef __UDISKS_MODULE_OBJECT_H__
#define __UDISKS_MODULE_OBJECT_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <src/udisksdaemontypes.h>

G_BEGIN_DECLS

#define UDISKS_TYPE_MODULE_OBJECT            (udisks_module_object_get_type ())
#define UDISKS_MODULE_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), UDISKS_TYPE_MODULE_OBJECT, UDisksModuleObject))
#define UDISKS_IS_MODULE_OBJECT(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), UDISKS_TYPE_MODULE_OBJECT))
#define UDISKS_MODULE_OBJECT_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), UDISKS_TYPE_MODULE_OBJECT, UDisksModuleObjectIface))

/**
 * UDisksModuleObject:
 *
 * The #UDisksModuleObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksModuleObject;
typedef struct _UDisksModuleObject         UDisksModuleObject;
typedef struct _UDisksModuleObjectIface    UDisksModuleObjectIface;

struct _UDisksModuleObjectIface
{
  GTypeInterface parent_iface;

  gboolean (*process_uevent) (UDisksModuleObject  *object,
                              const gchar         *action,
                              UDisksLinuxDevice   *device);

  gboolean (*housekeeping) (UDisksModuleObject  *object,
                            guint                secs_since_last,
                            GCancellable        *cancellable,
                            GError             **error);
};

GType udisks_module_object_get_type (void) G_GNUC_CONST;

gboolean udisks_module_object_process_uevent (UDisksModuleObject  *object,
                                              const gchar         *action,
                                              UDisksLinuxDevice   *device);
gboolean udisks_module_object_housekeeping   (UDisksModuleObject  *object,
                                              guint                secs_since_last,
                                              GCancellable        *cancellable,
                                              GError             **error);

G_END_DECLS

#endif /* __UDISKS_MODULE_OBJECT_H__ */
