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

#ifndef __DEVKIT_DISKS_PORT_PRIVATE_H__
#define __DEVKIT_DISKS_PORT_PRIVATE_H__

#include <dbus/dbus-glib.h>
#include <gudev/gudev.h>
#include <atasmart.h>

#include "devkit-disks-types.h"

G_BEGIN_DECLS

typedef enum {
        PORT_TYPE_ATA,
        PORT_TYPE_SAS
} PortType;

struct DevkitDisksPortPrivate
{
        DBusGConnection *system_bus_connection;
        DevkitDisksDaemon *daemon;
        GUdevDevice *d;

        gchar *object_path;
        gchar *native_path;
        gboolean removed;

        /* if non-zero, the id of the idle for emitting a 'change' signal */
        guint emit_changed_idle_id;

        /* used for internal bookkeeping */
        PortType port_type;
        gchar *native_path_for_device_prefix;

        /**************/
        /* Properties */
        /**************/

        gchar *adapter;
        gchar *parent;
        gint number;
};

/* property setters */

void devkit_disks_port_set_adapter (DevkitDisksPort *port, const gchar *value);
void devkit_disks_port_set_parent (DevkitDisksPort *port, const gchar *value);
void devkit_disks_port_set_number (DevkitDisksPort *port, gint value);

G_END_DECLS

#endif /* __DEVKIT_DISKS_PORT_PRIVATE_H__ */
