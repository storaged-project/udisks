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

#ifndef __UDISKS_MODULE_IFACE_TYPES_H__
#define __UDISKS_MODULE_IFACE_TYPES_H__

#include <gio/gio.h>
#include <udisks/udisks.h>
#include <gudev/gudev.h>

#include <sys/types.h>

#include <src/udisksdaemontypes.h>

G_BEGIN_DECLS


typedef struct
{
  UDisksObjectHasInterfaceFunc has_func;
  UDisksObjectConnectInterfaceFunc connect_func;
  UDisksObjectUpdateInterfaceFunc update_func;
  GType skeleton_type;
} UDisksModuleInterfaceInfo;

/**
 * UDisksModuleObjectNewFunc:
 *
 * A function prototype that creates new #GDBusObjectSkeleton instance that
 * implements the #UDisksModuleObject interface.
 *
 * Returns: A new #GDBusObjectSkeleton or NULL when the plugin doesn't handle
 *          the device specified. Free with g_object_unref().
 */
typedef GDBusObjectSkeleton* (*UDisksModuleObjectNewFunc) (UDisksDaemon      *daemon,
                                                           UDisksLinuxDevice *device);

/**
 * UDisksModuleNewManagerIfaceFunc:
 *
 * A function prototype that creates new #GDBusInterfaceSkeleton instance
 * carrying an interface to be exported on the UDisks manager object.
 *
 * Returns: A new #GDBusInterfaceSkeleton. Free with g_object_unref().
 */
typedef GDBusInterfaceSkeleton* (*UDisksModuleNewManagerIfaceFunc) (UDisksDaemon *daemon);


/* Module setup functions */
typedef UDisksModuleInterfaceInfo ** (*UDisksModuleIfaceSetupFunc) (void);
typedef UDisksModuleObjectNewFunc *  (*UDisksModuleObjectNewSetupFunc) (void);
typedef UDisksModuleNewManagerIfaceFunc * (*UDisksModuleNewManagerIfaceSetupFunc) (void);



G_END_DECLS

#endif /* __UDISKS_MODULE_IFACE_TYPES_H__ */
