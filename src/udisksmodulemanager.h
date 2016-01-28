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

#ifndef __UDISKS_MODULE_MANAGER_H__
#define __UDISKS_MODULE_MANAGER_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_MODULE_MANAGER  (udisks_module_manager_get_type ())
#define UDISKS_MODULE_MANAGER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_MODULE_MANAGER, UDisksModuleManager))
#define UDISKS_IS_MODULE_MANAGER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_MODULE_MANAGER))

GType                   udisks_module_manager_get_type        (void) G_GNUC_CONST;
UDisksModuleManager    *udisks_module_manager_new             (UDisksDaemon *daemon);
UDisksModuleManager    *udisks_module_manager_new_uninstalled (UDisksDaemon *daemon);

UDisksDaemon           *udisks_module_manager_get_daemon            (UDisksModuleManager *manager);
gboolean                udisks_module_manager_get_modules_available (UDisksModuleManager *manager);
gboolean                udisks_module_manager_get_uninstalled       (UDisksModuleManager *manager);
void                    udisks_module_manager_load_modules          (UDisksModuleManager *manager);
void                    udisks_module_manager_unload_modules        (UDisksModuleManager *manager);

GList                  *udisks_module_manager_get_block_object_iface_infos (UDisksModuleManager  *manager);
GList                  *udisks_module_manager_get_drive_object_iface_infos (UDisksModuleManager  *manager);
GList                  *udisks_module_manager_get_module_object_new_funcs  (UDisksModuleManager  *manager);
GList                  *udisks_module_manager_get_new_manager_iface_funcs  (UDisksModuleManager  *manager);
GList                  *udisks_module_manager_get_track_parent_funcs       (UDisksModuleManager  *manager);

void                    udisks_module_manager_set_module_state_pointer (UDisksModuleManager  *manager,
                                                                        const gchar          *module_name,
                                                                        gpointer              state);
gpointer                udisks_module_manager_get_module_state_pointer (UDisksModuleManager  *manager,
                                                                        const gchar          *module_name);

G_END_DECLS

#endif /* __UDISKS_MODULE_MANAGER_H__ */
