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

#ifndef __STORAGED_MODULE_OBJECT_H__
#define __STORAGED_MODULE_OBJECT_H__

#include <glib-object.h>
#include <gio/gio.h>

#include <src/storageddaemontypes.h>

G_BEGIN_DECLS

#define STORAGED_TYPE_MODULE_OBJECT            (storaged_module_object_get_type ())
#define STORAGED_MODULE_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), STORAGED_TYPE_MODULE_OBJECT, StoragedModuleObject))
#define STORAGED_IS_MODULE_OBJECT(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), STORAGED_TYPE_MODULE_OBJECT))
#define STORAGED_MODULE_OBJECT_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), STORAGED_TYPE_MODULE_OBJECT, StoragedModuleObjectIface))

/**
 * StoragedModuleObject:
 *
 * The #StoragedModuleObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _StoragedModuleObject;
typedef struct _StoragedModuleObject         StoragedModuleObject;
typedef struct _StoragedModuleObjectIface    StoragedModuleObjectIface;

struct _StoragedModuleObjectIface
{
  GTypeInterface parent_iface;

  gboolean (*process_uevent) (StoragedModuleObject  *object,
                              const gchar           *action,
                              StoragedLinuxDevice   *device);

  gboolean (*housekeeping) (StoragedModuleObject  *object,
                            guint                  secs_since_last,
                            GCancellable          *cancellable,
                            GError               **error);
};

GType storaged_module_object_get_type (void) G_GNUC_CONST;

gboolean storaged_module_object_process_uevent (StoragedModuleObject  *object,
                                                const gchar           *action,
                                                StoragedLinuxDevice   *device);
gboolean storaged_module_object_housekeeping   (StoragedModuleObject  *object,
                                                guint                  secs_since_last,
                                                GCancellable          *cancellable,
                                                GError               **error);

G_END_DECLS

#endif /* __STORAGED_MODULE_OBJECT_H__ */
