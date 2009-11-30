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

#ifndef __DEVKIT_DISKS_EXPANDER_H__
#define __DEVKIT_DISKS_EXPANDER_H__

#include <dbus/dbus-glib.h>
#include <gudev/gudev.h>
#include <sys/types.h>

#include "devkit-disks-types.h"

G_BEGIN_DECLS

#define DEVKIT_DISKS_TYPE_EXPANDER         (devkit_disks_expander_get_type ())
#define DEVKIT_DISKS_EXPANDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), DEVKIT_DISKS_TYPE_EXPANDER, DevkitDisksExpander))
#define DEVKIT_DISKS_EXPANDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), DEVKIT_DISKS_TYPE_EXPANDER, DevkitDisksExpanderClass))
#define DEVKIT_DISKS_IS_EXPANDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), DEVKIT_DISKS_TYPE_EXPANDER))
#define DEVKIT_DISKS_IS_EXPANDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), DEVKIT_DISKS_TYPE_EXPANDER))
#define DEVKIT_DISKS_EXPANDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DEVKIT_DISKS_TYPE_EXPANDER, DevkitDisksExpanderClass))

typedef struct DevkitDisksExpanderClass   DevkitDisksExpanderClass;
typedef struct DevkitDisksExpanderPrivate DevkitDisksExpanderPrivate;

struct DevkitDisksExpander
{
        GObject                    parent;
        DevkitDisksExpanderPrivate *priv;
};

struct DevkitDisksExpanderClass
{
        GObjectClass parent_class;
};

GType              devkit_disks_expander_get_type              (void) G_GNUC_CONST;

DevkitDisksExpander *devkit_disks_expander_new                 (DevkitDisksDaemon     *daemon,
                                                                GUdevDevice           *d);

gboolean           devkit_disks_expander_changed               (DevkitDisksExpander   *expander,
                                                                GUdevDevice           *d,
                                                                gboolean               synthesized);

void               devkit_disks_expander_removed               (DevkitDisksExpander   *expander);

/* local methods */

const char        *devkit_disks_expander_local_get_object_path (DevkitDisksExpander   *expander);
const char        *devkit_disks_expander_local_get_native_path (DevkitDisksExpander   *expander);

gboolean           devkit_disks_local_expander_encloses_native_path (DevkitDisksExpander   *expander,
                                                                     const gchar           *native_path);
G_END_DECLS

#endif /* __DEVKIT_DISKS_EXPANDER_H__ */
