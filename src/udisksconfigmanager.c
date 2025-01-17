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
#include <stdio.h>
#if defined (HAVE_LIBECONF) && defined (USE_VENDORDIR)
#include <libeconf.h>
#endif

#include "udiskslogging.h"
#include "udisksdaemontypes.h"
#include "udisksconfigmanager.h"
#include "udisksdaemonutil.h"

struct _UDisksConfigManager {
  GObject parent_instance;

  gboolean uninstalled;

  UDisksModuleLoadPreference load_preference;

  const gchar *encryption;
  gchar *config_dir;
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

#define MODULES_ALL_ARG "*"

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

static void
set_module_list (GList **out_modules, gchar **modules)
{
  gchar **modules_tmp = modules;

  for (gchar * module_i = *modules_tmp; module_i; module_i = *++modules_tmp)
    {
      g_strstrip (module_i);
      if (! udisks_module_validate_name (module_i) && !g_str_equal (module_i, MODULES_ALL_ARG))
        {
          g_warning ("Invalid module name '%s' specified in the config file.",
                     module_i);
          continue;
        }
      *out_modules = g_list_append (*out_modules, g_strdup (module_i));
    }
}

static void
set_load_preference (UDisksModuleLoadPreference *out_load_preference, const gchar *load_preference)
{
  /* Check the key value */
  if (g_ascii_strcasecmp (load_preference, "ondemand") == 0)
    {
      *out_load_preference = UDISKS_MODULE_LOAD_ONDEMAND;
    }
  else if (g_ascii_strcasecmp (load_preference, "onstartup") == 0)
    {
      *out_load_preference = UDISKS_MODULE_LOAD_ONSTARTUP;
    }
  else
    {
      udisks_warning ("Unknown value used for 'modules_load_preference': %s; defaulting to 'ondemand'",
                      load_preference);
    }
}

static const gchar *
get_encryption_config (const gchar *encryption)
{
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
      udisks_warning ("Unknown value used for 'encryption': %s; defaulting to '%s'",
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
      manager->encryption = get_encryption_config (g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

#if defined (HAVE_LIBECONF) && defined (USE_VENDORDIR)

static void
parse_config_file (UDisksConfigManager         *manager,
                   UDisksModuleLoadPreference  *out_load_preference,
                   const gchar                **out_encryption,
                   GList                      **out_modules)
{
  gchar *conf_dir;
  gchar *conf_filename;
  gchar *load_preference;
  gchar *encryption;
  gchar **modules;
  econf_err econf_ret = ECONF_SUCCESS;
  econf_file *key_file = NULL;
  gchar *val;

  /* Get modules and means of loading */
  conf_dir = g_build_path (G_DIR_SEPARATOR_S,
                           PACKAGE_SYSCONF_DIR,
                           PROJECT_SYSCONF_DIR,
                           NULL);

  if (manager->uninstalled || !g_str_equal(conf_dir,manager->config_dir))
    {
      /* Taking this file only and not parsing e.g. vendor files */
      conf_filename = g_build_filename (G_DIR_SEPARATOR_S,
                                        manager->config_dir,
                                        PACKAGE_NAME_UDISKS2 ".conf",
                                        NULL);
      udisks_debug ("Loading configuration file: %s", conf_filename);
      if ((econf_ret = econf_readFile (&key_file, conf_filename, "=", "#")))
        {
          udisks_warning ("Error cannot read file %s: %s", conf_filename, econf_errString(econf_ret));
        }
      g_free (conf_filename);
    }
  else
    {
      /* Parsing vendor, run and syscconf dir */
      udisks_debug ("Loading configuration files (%s.conf)", PACKAGE_NAME_UDISKS2);

      if (econf_ret == ECONF_SUCCESS)
        {
          econf_ret = econf_readConfig(&key_file,
                                       PROJECT_SYSCONF_DIR,
                                       _PATH_VENDORDIR,
                                       PACKAGE_NAME_UDISKS2,
                                       ".conf",
                                       "=",
                                       "#");
        }
      else
        {
          udisks_warning ("Error cannot read file %s.conf: %s",
                          PACKAGE_NAME_UDISKS2, econf_errString(econf_ret));
        }
    }

  if (econf_ret != ECONF_SUCCESS)
    return;

  if (out_modules != NULL)
    {
      /* Read the list of modules to load. */
      econf_ret = econf_getStringValue (key_file, MODULES_GROUP_NAME, MODULES_KEY, &val);
      if (econf_ret != ECONF_SUCCESS)
        {
          if (econf_ret != ECONF_NOKEY) {
            udisks_warning ("Error cannot read value %s/%s: %s",
                            MODULES_GROUP_NAME, MODULES_KEY, econf_errString(econf_ret));
          }
        }
      else
        {
          modules = g_strsplit (val, ",", -1);
          if (modules)
            {
              set_module_list (out_modules, modules);
              g_strfreev (modules);
            }
          g_free(val);
        }
    }

  if (out_load_preference != NULL)
    {
      /* Read the load preference configuration option. */
      econf_ret = econf_getStringValue (key_file, MODULES_GROUP_NAME, MODULES_LOAD_PREFERENCE_KEY,
                                        &load_preference);
      if (econf_ret != ECONF_SUCCESS) {
        if (econf_ret != ECONF_NOKEY)
          udisks_warning ("Error cannot read value%s/%s: %s",
                          MODULES_GROUP_NAME, MODULES_LOAD_PREFERENCE_KEY, econf_errString(econf_ret));
	}
      else
        {
          if (load_preference)
            {
              set_load_preference (out_load_preference, load_preference);
              g_free (load_preference);
            }
        }
    }

  if (out_encryption != NULL)
    {
      /* Read the encryption option. */
      econf_ret = econf_getStringValue (key_file, DEFAULTS_GROUP_NAME, DEFAULTS_ENCRYPTION_KEY,
                                        &encryption);
      if (econf_ret != ECONF_SUCCESS) {
        if (econf_ret != ECONF_NOKEY)
          udisks_warning ("Error cannot read value %s/%s: %s",
                          DEFAULTS_GROUP_NAME, DEFAULTS_ENCRYPTION_KEY, econf_errString(econf_ret));
	}
      else
        {
          if (encryption)
            {
              *out_encryption = get_encryption_config (encryption);
              g_free (encryption);
            }
        }
    }

  econf_free (key_file);
}

#else /* using vendordir and libeconf */

static void
parse_config_file (UDisksConfigManager         *manager,
                   UDisksModuleLoadPreference  *out_load_preference,
                   const gchar                **out_encryption,
                   GList                      **out_modules)
{
  GKeyFile *config_file;
  gchar *conf_filename;
  gchar *load_preference;
  gchar *encryption;
  gchar **modules;
  GError *l_error = NULL;

  /* Get modules and means of loading */
  conf_filename = g_build_filename (G_DIR_SEPARATOR_S,
                                    manager->config_dir,
                                    PACKAGE_NAME_UDISKS2 ".conf",
                                    NULL);

  udisks_debug ("Loading configuration file: %s", conf_filename);

  /* Load config */
  config_file = g_key_file_new ();
  g_key_file_set_list_separator (config_file, ',');
  if (g_key_file_load_from_file (config_file, conf_filename, G_KEY_FILE_NONE, &l_error))
    {
      if (out_modules != NULL)
        {
          modules = g_key_file_get_string_list (config_file, MODULES_GROUP_NAME, MODULES_KEY, NULL, NULL);
          /* Read the list of modules to load. */
          if (modules)
            {
              set_module_list (out_modules, modules);
              g_strfreev (modules);
            }
        }

      if (out_load_preference != NULL)
        {
          /* Read the load preference configuration option. */
          load_preference = g_key_file_get_string (config_file, MODULES_GROUP_NAME, MODULES_LOAD_PREFERENCE_KEY, NULL);
          if (load_preference)
            {
              set_load_preference (out_load_preference, load_preference);
              g_free (load_preference);
            }
        }

      if (out_encryption != NULL)
        {
          /* Read the encryption option. */
          encryption = g_key_file_get_string (config_file, DEFAULTS_GROUP_NAME, DEFAULTS_ENCRYPTION_KEY, NULL);
          if (encryption)
            {
              *out_encryption = get_encryption_config (encryption);
              g_free (encryption);
            }
        }
    }
  else
    {
      if (l_error != NULL)
        {
          udisks_warning ("Can't load configuration file %s: %s", conf_filename, l_error->message);
          g_error_free (l_error);
        }
      else
        {
          udisks_warning ("Can't load configuration file %s", conf_filename);
        }

    }

  g_key_file_free (config_file);
  g_free (conf_filename);
}
#endif

static void
udisks_config_manager_constructed (GObject *object)
{
  UDisksConfigManager *manager = UDISKS_CONFIG_MANAGER (object);

  /* Build a path to the config directory */
  manager->config_dir = g_build_path (G_DIR_SEPARATOR_S,
                                      manager->uninstalled ? BUILD_DIR : PACKAGE_SYSCONF_DIR,
                                      manager->uninstalled ? "udisks" : PROJECT_SYSCONF_DIR,
                                      NULL);

  /* Make sure the config dir exists, UDisksLinuxDrive may store some data there */
  if (g_mkdir_with_parents (manager->config_dir, 0755) != 0)
    {
      /* don't abort the daemon, the config dir may point to a readonly filesystem */
      udisks_warning ("Error creating directory %s: %m", manager->config_dir);
    }

  parse_config_file (manager, &manager->load_preference, &manager->encryption, NULL);

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

  g_free (manager->config_dir);

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
  manager->load_preference = UDISKS_MODULE_LOAD_ONDEMAND;
  manager->encryption = UDISKS_ENCRYPTION_DEFAULT;
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

/**
 * udisks_config_manager_get_modules:
 * @manager: A #UDisksConfigManager.
 *
 * Reads the udisks2.conf file and retrieves a list of module names to load.
 * A special '*' placeholder may be present as a first item as specified
 * in the config file.
 *
 * Returns: (transfer full) (nullable) (element-type gchar*): A list of strings
 *          or %NULL if no specific configuration has been found in the config file.
 *          Free the elements with g_free().
 */
GList *
udisks_config_manager_get_modules (UDisksConfigManager *manager)
{
  GList *modules = NULL;

  g_return_val_if_fail (UDISKS_IS_CONFIG_MANAGER (manager), NULL);

  parse_config_file (manager, NULL, NULL, &modules);
  return modules;
}

/**
 * udisks_config_manager_get_modules_all:
 * @manager: A #UDisksConfigManager.
 *
 * Reads the udisks2.conf file and returns whether to load all modules or not.
 * This corresponds to a special '*' placeholder in the config file.
 *
 * Returns: %TRUE when the daemon runs from a source tree, %FALSE otherwise.
 */
gboolean
udisks_config_manager_get_modules_all (UDisksConfigManager *manager)
{
  GList *modules = NULL;
  gboolean ret;

  g_return_val_if_fail (UDISKS_IS_CONFIG_MANAGER (manager), FALSE);

  parse_config_file (manager, NULL, NULL, &modules);

  ret = !modules || (g_strcmp0 (modules->data, MODULES_ALL_ARG) == 0 && g_list_length (modules) == 1);

  g_list_free_full (modules, (GDestroyNotify) g_free);

  return ret;
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

/**
 * udisks_config_manager_get_config_dir:
 * @manager: A #UDisksConfigManager.
 *
 * Gets path to the actual directory where global UDisks configuration files are
 * stored. Takes in account the flag whether the UDisks daemon is running from
 * a source code tree ("uninstalled") or whether it is a properly installed package.
 *
 * Returns: (transfer none): path to the global UDisks configuration directory.
 */
const gchar *
udisks_config_manager_get_config_dir (UDisksConfigManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_CONFIG_MANAGER (manager), NULL);
  g_warn_if_fail (manager->config_dir != NULL);
  return manager->config_dir;
}

/**
 * udisks_config_manager_get_supported_encryption_types:
 * @manager: A #UDisksConfigManager.
 *
 * Returns: (transfer none) (array zero-terminated=1): a %NULL terminated list of supported encryption types.
 *          Do not free or modify.
 */
const gchar * const *
udisks_config_manager_get_supported_encryption_types (UDisksConfigManager *manager)
{
  static const gchar *_encryption_types[] =
    {
      UDISKS_ENCRYPTION_LUKS1,
      UDISKS_ENCRYPTION_LUKS2,
      NULL
    };

  return _encryption_types;
}
