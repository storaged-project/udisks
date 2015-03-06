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

#ifndef __STORAGED_MODULE_IFACE_H__
#define __STORAGED_MODULE_IFACE_H__

#include <gio/gio.h>
#include <storaged/storaged.h>
#include <gudev/gudev.h>

#include <sys/types.h>

#include "storagedmoduleifacetypes.h"

G_BEGIN_DECLS


/* Corresponds with the StoragedModuleInitFunc type */
G_MODULE_EXPORT gpointer storaged_module_init (gchar **module_id);


/**
 * Storaged module setup entry functions:
 *   Functions below are module entry functions that return an array of setup
 *   functions (or structures containing group of setup functions). See
 *   storagedmoduleifacetypes.h for detailed description.
 *
 *   Setup functions are queried by StoragedModuleManager only once (typically on
 *   startup or on demand). Modules are never unloaded for safety reasons.
 */

/* Corresponds with the StoragedModuleIfaceSetupFunc type */
G_MODULE_EXPORT StoragedModuleInterfaceInfo **storaged_module_get_block_object_iface_setup_entries (void);
G_MODULE_EXPORT StoragedModuleInterfaceInfo **storaged_module_get_drive_object_iface_setup_entries (void);

/* Corresponds with the StoragedModuleObjectNewSetupFunc type */
G_MODULE_EXPORT StoragedModuleObjectNewFunc  *storaged_module_get_object_new_funcs (void);

/* Corresponds with the StoragedModuleNewManagerIfaceSetupFunc type */
G_MODULE_EXPORT StoragedModuleNewManagerIfaceFunc *storaged_module_get_new_manager_iface_funcs (void);


G_END_DECLS

#endif /* __STORAGED_MODULE_IFACE_H__ */
