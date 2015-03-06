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

#ifndef __STORAGED_MODULE_MANAGER_H__
#define __STORAGED_MODULE_MANAGER_H__

#include "storageddaemontypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_MODULE_MANAGER  (storaged_module_manager_get_type ())
#define STORAGED_MODULE_MANAGER(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_MODULE_MANAGER, StoragedModuleManager))
#define STORAGED_IS_MODULE_MANAGER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_MODULE_MANAGER))

GType                     storaged_module_manager_get_type    (void) G_GNUC_CONST;
StoragedModuleManager    *storaged_module_manager_new         (void);

gboolean                  storaged_module_manager_get_modules_available (StoragedModuleManager *manager);
void                      storaged_module_manager_load_modules          (StoragedModuleManager *manager);

GList                    *storaged_module_manager_get_block_object_iface_infos (StoragedModuleManager  *manager);
GList                    *storaged_module_manager_get_drive_object_iface_infos (StoragedModuleManager  *manager);
GList                    *storaged_module_manager_get_module_object_new_funcs  (StoragedModuleManager  *manager);
GList                    *storaged_module_manager_get_new_manager_iface_funcs  (StoragedModuleManager  *manager);

void                      storaged_module_manager_set_module_state_pointer (StoragedModuleManager  *manager,
                                                                            const gchar            *module_name,
                                                                            gpointer                state);
gpointer                  storaged_module_manager_get_module_state_pointer (StoragedModuleManager  *manager,
                                                                            const gchar            *module_name);

G_END_DECLS

#endif /* __STORAGED_MODULE_MANAGER_H__ */
