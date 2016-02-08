/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2016 Peter Hatina <phatina@redhat.com>
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

#ifndef __STORAGED_CONFIG_MANAGER_H__
#define __STORAGED_CONFIG_MANAGER_H__

#include "storageddaemontypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_CONFIG_MANAGER            (storaged_config_manager_get_type ())
#define STORAGED_CONFIG_MANAGER(o)              (G_TYPE_CHECK_INSTANCE_CAST  ((o), STORAGED_TYPE_CONFIG_MANAGER, StoragedConfigManager))
#define STORAGED_IS_CONFIG_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_TYPE  ((o), STORAGED_TYPE_CONFIG_MANAGER))
#define STORAGED_CONFIG_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), STORAGED_TYPE_CONFIG_MANAGER, StoragedConfigManagerClass))
#define STORAGED_IS_CONFIG_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), STORAGED_TYPE_CONFIG_MANAGER))
#define STORAGED_CONFIG_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), STORAGED_TYPE_CONFIG_MANAGER, StoragedConfigManagerClass))

/**
 * StoragedModuleLoadPreference:
 * @STORAGED_MODULE_LOAD_ONDEMAND
 * @STORAGED_MODULE_LOAD_ONSTARTUP
 *
 * Enumeration used to specify when to load additional modules.
 */
typedef enum
{
 STORAGED_MODULE_LOAD_ONDEMAND,
 STORAGED_MODULE_LOAD_ONSTARTUP
} StoragedModuleLoadPreference;

GType                   storaged_config_manager_get_type        (void) G_GNUC_CONST;
StoragedConfigManager  *storaged_config_manager_new             (void);
StoragedConfigManager  *storaged_config_manager_new_uninstalled (void);

gboolean                storaged_config_manager_get_uninstalled (StoragedConfigManager *manager);

const GList            *storaged_config_manager_get_modules     (StoragedConfigManager *manager);
gboolean                storaged_config_manager_get_modules_all (StoragedConfigManager *manager);
StoragedModuleLoadPreference
                        storaged_config_manager_get_load_preference (StoragedConfigManager *manager);

G_END_DECLS

#endif /* __STORAGED_CONFIG_MANAGER_H__ */
