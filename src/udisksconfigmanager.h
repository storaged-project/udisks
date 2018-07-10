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

#ifndef __UDISKS_CONFIG_MANAGER_H__
#define __UDISKS_CONFIG_MANAGER_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_CONFIG_MANAGER            (udisks_config_manager_get_type ())
#define UDISKS_CONFIG_MANAGER(o)              (G_TYPE_CHECK_INSTANCE_CAST  ((o), UDISKS_TYPE_CONFIG_MANAGER, UDisksConfigManager))
#define UDISKS_IS_CONFIG_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_TYPE  ((o), UDISKS_TYPE_CONFIG_MANAGER))
#define UDISKS_CONFIG_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), UDISKS_TYPE_CONFIG_MANAGER, UDisksConfigManagerClass))
#define UDISKS_IS_CONFIG_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), UDISKS_TYPE_CONFIG_MANAGER))
#define UDISKS_CONFIG_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), UDISKS_TYPE_CONFIG_MANAGER, UDisksConfigManagerClass))

/**
 * UDisksModuleLoadPreference:
 * @UDISKS_MODULE_LOAD_ONDEMAND
 * @UDISKS_MODULE_LOAD_ONSTARTUP
 *
 * Enumeration used to specify when to load additional modules.
 */
typedef enum
{
 UDISKS_MODULE_LOAD_ONDEMAND,
 UDISKS_MODULE_LOAD_ONSTARTUP
} UDisksModuleLoadPreference;

#define UDISKS_ENCRYPTION_LUKS1 "luks1"
#define UDISKS_ENCRYPTION_LUKS2 "luks2"
#define UDISKS_ENCRYPTION_DEFAULT UDISKS_ENCRYPTION_LUKS1

GType                 udisks_config_manager_get_type        (void) G_GNUC_CONST;
UDisksConfigManager  *udisks_config_manager_new             (void);
UDisksConfigManager  *udisks_config_manager_new_uninstalled (void);

gboolean              udisks_config_manager_get_uninstalled (UDisksConfigManager *manager);

const GList          *udisks_config_manager_get_modules     (UDisksConfigManager *manager);
gboolean              udisks_config_manager_get_modules_all (UDisksConfigManager *manager);
UDisksModuleLoadPreference
                      udisks_config_manager_get_load_preference (UDisksConfigManager *manager);
const gchar          *udisks_config_manager_get_encryption (UDisksConfigManager *manager);

G_END_DECLS

#endif /* __UDISKS_CONFIG_MANAGER_H__ */
