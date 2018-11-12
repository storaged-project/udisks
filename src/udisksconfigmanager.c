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

#include "config.h"

#include <string.h>
#include <ctype.h>

#include "udiskslogging.h"
#include "udisksdaemontypes.h"
#include "udisksconfigmanager.h"

struct _UDisksConfigManager {
  GObject parent_instance;

  gboolean uninstalled;

  UDisksModuleLoadPreference load_preference;
  GList *modules;

  const gchar *encryption;
};

struct _UDisksConfigManagerClass {
  GObjectClass parent_class;
};

G_DEFINE_TYPE (UDisksConfigManager, udisks_config_manager, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_UNINSTALLED,
  PROP_PREFERENCE,
  PROP_ENCRYPTION,
  PROP_N
};

#define MODULES_GROUP_NAME  PACKAGE_NAME_UDISKS2
#define MODULES_KEY "modules"
#define MODULES_LOAD_PREFERENCE_KEY "modules_load_preference"

#define DEFAULTS_GROUP_NAME "defaults"
#define DEFAULTS_ENCRYPTION_KEY "encryption"

static void
udisks_config_manager_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  UDisksConfigManager *manager = UDISKS_CONFIG_MANAGER (object);

  switch (property_id)
    {
    case PROP_UNINSTALLED:
      g_value_set_boolean (value, udisks_config_manager_get_uninstalled (manager));
      break;

    case PROP_PREFERENCE:
      g_value_set_int (value, udisks_config_manager_get_load_preference (manager));
      break;

    case PROP_ENCRYPTION:
      g_value_set_string (value, udisks_config_manager_get_encryption (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static const gchar *
get_encryption_config (const GValue *value)
{
  const gchar *encryption = g_value_get_string (value);

  if (g_strcmp0 (encryption, UDISKS_ENCRYPTION_LUKS1) == 0)
    {
      return UDISKS_ENCRYPTION_LUKS1;
    }
  else if (g_strcmp0 (encryption, UDISKS_ENCRYPTION_LUKS2) == 0)
    {
      return UDISKS_ENCRYPTION_LUKS2;
    }
  else
    {
      udisks_warning ("Unknown value used for 'encryption': %s"
                      "; defaulting to '%s'",
                      encryption, UDISKS_ENCRYPTION_DEFAULT);
      return UDISKS_ENCRYPTION_DEFAULT;
    }
}

static void
udisks_config_manager_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  UDisksConfigManager *manager = UDISKS_CONFIG_MANAGER (object);

  switch (property_id)
    {
    case PROP_UNINSTALLED:
      manager->uninstalled = g_value_get_boolean (value);
      break;

    case PROP_PREFERENCE:
      manager->load_preference = g_value_get_int (value);
      break;

    case PROP_ENCRYPTION:
      manager->encryption = get_encryption_config (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_config_manager_constructed (GObject *object)
{
  UDisksConfigManager *manager = UDISKS_CONFIG_MANAGER (object);
  GKeyFile *config_file;
  gchar *conf_filename;
  gchar *load_preference;
  gchar *encryption;
  gchar *module_i;
  gchar **modules;
  gchar **modules_tmp;

  config_file = g_key_file_new ();
  g_key_file_set_list_separator (config_file, ',');

  /* Get modules and means of loading */
  conf_filename = g_build_filename (G_DIR_SEPARATOR_S,
                                    udisks_config_manager_get_uninstalled (manager) ?
                                      BUILD_DIR :
                                      PACKAGE_SYSCONF_DIR,
                                    udisks_config_manager_get_uninstalled (manager) ?
                                      "udisks" :
                                      PROJECT_SYSCONF_DIR,
                                    PACKAGE_NAME_UDISKS2 ".conf",
                                    NULL);

  udisks_debug ("Loading configuration file: %s", conf_filename);

  if (manager->modules)
    {
      g_list_free_full (manager->modules, (GDestroyNotify) g_free);
      manager->modules = NULL;  /* NULL == '*' */
    }
  manager->load_preference = UDISKS_MODULE_LOAD_ONDEMAND;
  manager->encryption = UDISKS_ENCRYPTION_DEFAULT;

  /* Load config */
  if (g_key_file_load_from_file (config_file,
                                 conf_filename,
                                 G_KEY_FILE_NONE,
                                 NULL))
    {
      modules = g_key_file_get_string_list (config_file,
                                            MODULES_GROUP_NAME,
                                            MODULES_KEY,
                                            NULL,
                                            NULL);
      /* Read the list of modules to load. */
      if (modules)
        {
          modules_tmp = modules;
          for (module_i = *modules_tmp; module_i; module_i = *++modules_tmp)
              manager->modules = g_list_append (manager->modules,
                                                g_strdup (g_strstrip (module_i)));
          g_strfreev (modules);
        }
      else
        {
          udisks_debug ("No 'modules' found in configuration file");
        }

      /* Read the load preference configuration option. */
      load_preference = g_key_file_get_string (config_file,
                                               MODULES_GROUP_NAME,
                                               MODULES_LOAD_PREFERENCE_KEY,
                                               NULL);
      if (load_preference)
        {
          /* Check the key value */
          if (g_ascii_strcasecmp (load_preference, "ondemand") == 0)
            {
              manager->load_preference = UDISKS_MODULE_LOAD_ONDEMAND;
            }
          else if (g_ascii_strcasecmp (load_preference, "onstartup") == 0)
            {
              manager->load_preference = UDISKS_MODULE_LOAD_ONSTARTUP;
            }
          else
            {
              udisks_warning ("Unknown value used for 'modules_load_preference': %s"
                              "; defaulting to 'ondemand'",
                              load_preference);
            }

          g_free (load_preference);
        }
      else
        {
          udisks_debug ("No 'modules_load_preference' found in configuration file");
        }

      /* Read the load preference configuration option. */
      encryption = g_key_file_get_string (config_file,
                                          DEFAULTS_GROUP_NAME,
                                          DEFAULTS_ENCRYPTION_KEY,
                                          NULL);
      if (encryption)
        {
          /* Check the key value */
          if (g_ascii_strcasecmp (encryption, UDISKS_ENCRYPTION_LUKS1) == 0)
            {
              manager->encryption = UDISKS_ENCRYPTION_LUKS1;
            }
          else if (g_ascii_strcasecmp (encryption, UDISKS_ENCRYPTION_LUKS2) == 0)
            {
              manager->encryption = UDISKS_ENCRYPTION_LUKS2;
            }
          else
            {
              udisks_warning ("Unknown value used for 'encryption': %s"
                              "; defaulting to '%s'",
                              encryption, manager->encryption);
            }

          g_free (encryption);
        }
      else
        {
          udisks_debug ("No 'encryption' found in configuration file");
        }

    }
  else
    {
      udisks_warning ("Can't load configuration file %s", conf_filename);
    }


  g_key_file_free (config_file);
  g_free (conf_filename);

  if (G_OBJECT_CLASS (udisks_config_manager_parent_class))
    G_OBJECT_CLASS (udisks_config_manager_parent_class)->constructed (object);
}

static void
udisks_config_manager_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_config_manager_parent_class))
    G_OBJECT_CLASS (udisks_config_manager_parent_class)->dispose (object);
}

static void
udisks_config_manager_finalize (GObject *object)
{
  UDisksConfigManager *manager = UDISKS_CONFIG_MANAGER (object);

  if (manager->modules)
    {
      g_list_free_full (manager->modules, (GDestroyNotify) g_free);
      manager->modules = NULL;
    }

  if (G_OBJECT_CLASS (udisks_config_manager_parent_class))
    G_OBJECT_CLASS (udisks_config_manager_parent_class)->finalize (object);
}

static void
udisks_config_manager_class_init (UDisksConfigManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed  = udisks_config_manager_constructed;
  gobject_class->get_property = udisks_config_manager_get_property;
  gobject_class->set_property = udisks_config_manager_set_property;
  gobject_class->dispose = udisks_config_manager_dispose;
  gobject_class->finalize = udisks_config_manager_finalize;

  /**
   * UDisksConfigManager:uninstalled:
   *
   * Loads modules from the build directory.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_UNINSTALLED,
                                   g_param_spec_boolean ("uninstalled",
                                                         "Load modules from the build directory",
                                                         "Whether the modules should be loaded from the build directory",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_STATIC_STRINGS |
                                                         G_PARAM_CONSTRUCT_ONLY));

  /**
   * UDisksConfigManager:preference:
   *
   * Module load preference.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_PREFERENCE,
                                   g_param_spec_int ("preference",
                                                     "Module load preference",
                                                     "When to load the additional modules",
                                                     UDISKS_MODULE_LOAD_ONDEMAND,
                                                     UDISKS_MODULE_LOAD_ONSTARTUP,
                                                     UDISKS_MODULE_LOAD_ONDEMAND,
                                                     G_PARAM_READABLE |
                                                     G_PARAM_WRITABLE |
                                                     G_PARAM_STATIC_STRINGS |
                                                     G_PARAM_CONSTRUCT_ONLY));

  /**
   * UDisksConfigManager:encryption:
   *
   * Default encryption technolog.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_ENCRYPTION,
                                   g_param_spec_string ("encryption",
                                                        "Default encryption technology",
                                                        "Encryption technology used when creating encrypted filesystems",
                                                        UDISKS_ENCRYPTION_DEFAULT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_STATIC_STRINGS |
                                                        G_PARAM_CONSTRUCT_ONLY));

}

static void
udisks_config_manager_init (UDisksConfigManager *manager)
{
}

UDisksConfigManager *
udisks_config_manager_new (void)
{
  return UDISKS_CONFIG_MANAGER (g_object_new (UDISKS_TYPE_CONFIG_MANAGER,
                                "uninstalled", FALSE,
                                NULL));
}

UDisksConfigManager *
udisks_config_manager_new_uninstalled (void)
{
  return UDISKS_CONFIG_MANAGER (g_object_new (UDISKS_TYPE_CONFIG_MANAGER,
                                "uninstalled", TRUE,
                                NULL));
}

gboolean
udisks_config_manager_get_uninstalled (UDisksConfigManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_CONFIG_MANAGER (manager), FALSE);
  return manager->uninstalled;
}

const GList *
udisks_config_manager_get_modules (UDisksConfigManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_CONFIG_MANAGER (manager), NULL);
  return manager->modules;
}

gboolean
udisks_config_manager_get_modules_all (UDisksConfigManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_CONFIG_MANAGER (manager), FALSE);
  return ! manager->modules
         || (g_strcmp0 (manager->modules->data, "*") == 0
             && g_list_length (manager->modules) == 1);
}

UDisksModuleLoadPreference
udisks_config_manager_get_load_preference (UDisksConfigManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_CONFIG_MANAGER (manager),
                        UDISKS_MODULE_LOAD_ONDEMAND);
  return manager->load_preference;
}

const gchar *
udisks_config_manager_get_encryption (UDisksConfigManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_CONFIG_MANAGER (manager),
                        UDISKS_ENCRYPTION_DEFAULT);
  return manager->encryption;
}
