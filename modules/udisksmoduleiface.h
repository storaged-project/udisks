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

#ifndef __UDISKS_MODULE_IFACE_H__
#define __UDISKS_MODULE_IFACE_H__

#include <gio/gio.h>
#include <udisks/udisks.h>
#include <gudev/gudev.h>

#include <sys/types.h>

#include "udisksmoduleifacetypes.h"


G_MODULE_EXPORT UDisksModuleInterfaceInfo **udisks_module_get_block_object_iface_setup_entries (void);
G_MODULE_EXPORT UDisksModuleInterfaceInfo **udisks_module_get_drive_object_iface_setup_entries (void);


#endif /* __UDISKS_MODULE_IFACE_H__ */
