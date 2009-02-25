/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#ifndef __DEVKIT_DISKS_INHIBITOR_H__
#define __DEVKIT_DISKS_INHIBITOR_H__

#include <dbus/dbus-glib.h>

#include "devkit-disks-types.h"

G_BEGIN_DECLS

#define DEVKIT_TYPE_DISKS_INHIBITOR         (devkit_disks_inhibitor_get_type ())
#define DEVKIT_DISKS_INHIBITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), DEVKIT_TYPE_DISKS_INHIBITOR, DevkitDisksInhibitor))
#define DEVKIT_DISKS_INHIBITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), DEVKIT_TYPE_DISKS_INHIBITOR, DevkitDisksInhibitorClass))
#define DEVKIT_IS_DISKS_INHIBITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), DEVKIT_TYPE_DISKS_INHIBITOR))
#define DEVKIT_IS_DISKS_INHIBITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), DEVKIT_TYPE_DISKS_INHIBITOR))
#define DEVKIT_DISKS_INHIBITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DEVKIT_TYPE_DISKS_INHIBITOR, DevkitDisksInhibitorClass))

typedef struct DevkitDisksInhibitorClass   DevkitDisksInhibitorClass;
typedef struct DevkitDisksInhibitorPrivate DevkitDisksInhibitorPrivate;

struct DevkitDisksInhibitor
{
        GObject        parent;
        DevkitDisksInhibitorPrivate *priv;
};

struct DevkitDisksInhibitorClass
{
        GObjectClass   parent_class;
};

GType                 devkit_disks_inhibitor_get_type             (void) G_GNUC_CONST;
DevkitDisksInhibitor *devkit_disks_inhibitor_new                  (DBusGMethodInvocation *context);
const gchar          *devkit_disks_inhibitor_get_unique_dbus_name (DevkitDisksInhibitor *inhibitor);
const gchar          *devkit_disks_inhibitor_get_cookie           (DevkitDisksInhibitor *inhibitor);

G_END_DECLS

#endif /* __DEVKIT_DISKS_INHIBITOR_H__ */
