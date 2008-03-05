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

#ifndef __DEVKIT_DISKS_DAEMON_H__
#define __DEVKIT_DISKS_DAEMON_H__

#include <glib-object.h>
#include <polkit-dbus/polkit-dbus.h>

G_BEGIN_DECLS

#define DEVKIT_TYPE_DISKS_DAEMON         (devkit_disks_daemon_get_type ())
#define DEVKIT_DISKS_DAEMON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), DEVKIT_TYPE_DISKS_DAEMON, DevkitDisksDaemon))
#define DEVKIT_DISKS_DAEMON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), DEVKIT_TYPE_DISKS_DAEMON, DevkitDisksDaemonClass))
#define DEVKIT_IS_DISKS_DAEMON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), DEVKIT_TYPE_DISKS_DAEMON))
#define DEVKIT_IS_DISKS_DAEMON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), DEVKIT_TYPE_DISKS_DAEMON))
#define DEVKIT_DISKS_DAEMON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DEVKIT_TYPE_DISKS_DAEMON, DevkitDisksDaemonClass))

typedef struct DevkitDisksDaemonPrivate DevkitDisksDaemonPrivate;

typedef struct
{
        GObject        parent;
        DevkitDisksDaemonPrivate *priv;
} DevkitDisksDaemon;

typedef struct
{
        GObjectClass   parent_class;
} DevkitDisksDaemonClass;

typedef enum
{
        DEVKIT_DISKS_DAEMON_ERROR_GENERAL,
        DEVKIT_DISKS_DAEMON_ERROR_NOT_AUTHORIZED,
        DEVKIT_DISKS_DAEMON_NUM_ERRORS
} DevkitDisksDaemonError;

#define DEVKIT_DISKS_DAEMON_ERROR devkit_disks_daemon_error_quark ()

GType devkit_disks_daemon_error_get_type (void);
#define DEVKIT_DISKS_DAEMON_TYPE_ERROR (devkit_disks_daemon_error_get_type ())

GQuark             devkit_disks_daemon_error_quark         (void);
GType              devkit_disks_daemon_get_type            (void);
DevkitDisksDaemon *devkit_disks_daemon_new                 (gboolean no_exit);

void               devkit_disks_daemon_inhibit_killtimer   (DevkitDisksDaemon *daemon);
void               devkit_disks_daemon_uninhibit_killtimer (DevkitDisksDaemon *daemon);
void               devkit_disks_daemon_reset_killtimer     (DevkitDisksDaemon *daemon);

/* exported methods */

gboolean devkit_disks_daemon_inhibit_shutdown (DevkitDisksDaemon     *daemon,
                                               DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_uninhibit_shutdown (DevkitDisksDaemon     *daemon,
                                                 const char            *cookie,
                                                 DBusGMethodInvocation *context);

gboolean devkit_disks_daemon_enumerate_devices (DevkitDisksDaemon     *daemon,
                                                DBusGMethodInvocation *context);

G_END_DECLS

#endif /* __DEVKIT_DISKS_DAEMON_H__ */
