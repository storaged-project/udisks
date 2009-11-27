/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#ifndef __DEVKIT_DISKS_CONTROLLER_PRIVATE_H__
#define __DEVKIT_DISKS_CONTROLLER_PRIVATE_H__

#include <dbus/dbus-glib.h>
#include <gudev/gudev.h>
#include <atasmart.h>

#include "devkit-disks-types.h"

G_BEGIN_DECLS

struct DevkitDisksControllerPrivate
{
        DBusGConnection *system_bus_connection;
        DevkitDisksDaemon *daemon;
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
};

/* property setters */

void devkit_disks_controller_set_vendor (DevkitDisksController *controller, const gchar *value);
void devkit_disks_controller_set_model (DevkitDisksController *controller, const gchar *value);
void devkit_disks_controller_set_driver (DevkitDisksController *controller, const gchar *value);

G_END_DECLS

#endif /* __DEVKIT_DISKS_CONTROLLER_PRIVATE_H__ */
