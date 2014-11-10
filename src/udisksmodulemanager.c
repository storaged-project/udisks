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

#include "config.h"
#include <glib/gi18n-lib.h>

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>

#include "udisksmodulemanager.h"
#include "udisksprivate.h"
#include "udiskslogging.h"
#include <modules/udisksmoduleifacetypes.h>



#define MODULE_DIR PACKAGE_LIB_DIR "/udisks2/modules"


/**
 * SECTION:udisksmodulemanager
 * @title: UDisksModuleManager
 * @short_description: Manages plugins
 *
 * This type is used for managing daemon plugins.
 */

/**
 * UDisksModuleManager:
 *
 * The #UDisksModuleManager structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksModuleManager
{
  GObject parent_instance;

  GList *modules;
  GList *block_object_interface_infos;
  GList *drive_object_interface_infos;
  GList *module_object_new_funcs;

  gboolean modules_ready;
};

typedef struct _UDisksModuleManagerClass UDisksModuleManagerClass;

struct _UDisksModuleManagerClass
{
  GObjectClass parent_class;

  void (*modules_ready)  (UDisksModuleManager *manager);
};

typedef struct
{
  GModule *handle;
} ModuleData;


/*--------------------------------------------------------------------------------------------------------------*/

enum
{
  PROP_0,
  PROP_MODULES_READY,
};

G_DEFINE_TYPE (UDisksModuleManager, udisks_module_manager, G_TYPE_OBJECT)

static void
free_module_data (ModuleData *data)
{
  g_module_close (data->handle);
  free (data);
}

static void
udisks_module_manager_finalize (GObject *object)
{
  UDisksModuleManager *manager = UDISKS_MODULE_MANAGER (object);

  if (manager->block_object_interface_infos != NULL)
    {
      g_list_foreach (manager->block_object_interface_infos, (GFunc) g_free, NULL);
      g_list_free (manager->block_object_interface_infos);
    }

  if (manager->drive_object_interface_infos != NULL)
    {
      g_list_foreach (manager->drive_object_interface_infos, (GFunc) g_free, NULL);
      g_list_free (manager->drive_object_interface_infos);
    }

  if (manager->module_object_new_funcs != NULL)
    {
      g_list_free (manager->module_object_new_funcs);
    }

  if (manager->modules != NULL)
    {
      g_list_foreach (manager->modules, (GFunc) free_module_data, NULL);
      g_list_free (manager->modules);
    }

  if (G_OBJECT_CLASS (udisks_module_manager_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_module_manager_parent_class)->finalize (object);
}


static void
udisks_module_manager_init (UDisksModuleManager *monitor)
{
}

static void
load_modules (UDisksModuleManager *manager)
{
  GError *error;
  GDir *dir;
  const gchar *dent;
  GModule *module;
  ModuleData *module_data;
  gchar *pth;
  UDisksModuleIfaceSetupFunc block_object_iface_setup_func;
  UDisksModuleIfaceSetupFunc drive_object_iface_setup_func;
  UDisksModuleObjectNewSetupFunc module_object_new_setup_func;
  UDisksModuleInterfaceInfo **infos, **infos_i;
  UDisksModuleObjectNewFunc *module_object_new_funcs, *module_object_new_funcs_i;


  error = NULL;
  dir = g_dir_open (MODULE_DIR, 0, &error);
  if (! dir)
    {
      udisks_warning ("Error loading modules: %s", error->message);
      g_error_free (error);
      return;
    }

  while ((dent = g_dir_read_name (dir)))
    {
      pth = g_build_filename (MODULE_DIR, dent, NULL);
      module = g_module_open (pth, /* G_MODULE_BIND_LOCAL */ 0);

      if (module != NULL)
        {
          module_data = g_new0 (ModuleData, 1);
          module_data->handle = module;
          udisks_notice ("Loading module %s...", dent);
          if (! g_module_symbol (module_data->handle, "udisks_module_get_block_object_iface_setup_entries", (gpointer *) &block_object_iface_setup_func) ||
              ! g_module_symbol (module_data->handle, "udisks_module_get_drive_object_iface_setup_entries", (gpointer *) &drive_object_iface_setup_func) ||
              ! g_module_symbol (module_data->handle, "udisks_module_get_object_new_funcs", (gpointer *) &module_object_new_setup_func))
            {
              udisks_warning ("  Error importing required symbols from module '%s'", pth);
              free_module_data (module_data);
            }
          else
            {
              infos = block_object_iface_setup_func ();
              for (infos_i = infos; infos_i && *infos_i; infos_i++)
                manager->block_object_interface_infos = g_list_append (manager->block_object_interface_infos, *infos_i);
              g_free (infos);

              infos = drive_object_iface_setup_func ();
              for (infos_i = infos; infos_i && *infos_i; infos_i++)
                manager->drive_object_interface_infos = g_list_append (manager->drive_object_interface_infos, *infos_i);
              g_free (infos);

              module_object_new_funcs = module_object_new_setup_func ();
              for (module_object_new_funcs_i = module_object_new_funcs; module_object_new_funcs_i && *module_object_new_funcs_i; module_object_new_funcs_i++)
                manager->module_object_new_funcs = g_list_append (manager->module_object_new_funcs, *module_object_new_funcs_i);
              g_free (module_object_new_funcs);

              manager->modules = g_list_append (manager->modules, module_data);
            }
        }
      g_free (pth);
    }
  g_dir_close (dir);

  manager->modules_ready = TRUE;
  g_object_notify (G_OBJECT (manager), "modules-ready");
}

static void
udisks_module_manager_constructed (GObject *object)
{
  UDisksModuleManager *manager = UDISKS_MODULE_MANAGER (object);

  manager->modules_ready = FALSE;

  if (! g_module_supported ())
    {
      udisks_warning ("Modules are unsupported on the current platform");
      return;
    }

  load_modules (manager);

  if (G_OBJECT_CLASS (udisks_module_manager_parent_class)->constructed != NULL)
    (*G_OBJECT_CLASS (udisks_module_manager_parent_class)->constructed) (object);
}

static void
udisks_module_manager_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  UDisksModuleManager *manager = UDISKS_MODULE_MANAGER (object);

  switch (prop_id)
    {
    case PROP_MODULES_READY:
      g_value_set_boolean (value, manager->modules_ready);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_module_manager_class_init (UDisksModuleManagerClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize     = udisks_module_manager_finalize;
  gobject_class->constructed  = udisks_module_manager_constructed;
  gobject_class->get_property = udisks_module_manager_get_property;

  /**
   * UDisksModuleManager:modules-ready
   *
   * Indicates whether modules have been loaded.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MODULES_READY,
                                   g_param_spec_boolean ("modules-ready",
                                                         "Modules ready",
                                                         "Indicates whether the modules have been loaded",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_module_manager_new:
 *
 * Creates a new #UDisksModuleManager object.
 *
 * Returns: A #UDisksModuleManager. Free with g_object_unref().
 */
UDisksModuleManager *
udisks_module_manager_new (void)
{
  return UDISKS_MODULE_MANAGER (g_object_new (UDISKS_TYPE_MODULE_MANAGER, NULL));
}


/* ---------------------------------------------------------------------------------------------------- */


/**
 * udisks_module_manager_get_block_object_iface_infos:
 * @manager: A #UDisksModuleManager.
 *
 * Gets all block object interface info structs that can be plugged in #UDisksLinuxBlockObject instances.
 *
 * Returns: (transfer full) (element-type #UDisksModuleIfaceSetupFunc): A list of #UDisksModuleIfaceSetupFunc structs that belongs to the manager and must not be freed.
 */
GList *
udisks_module_manager_get_block_object_iface_infos (UDisksModuleManager *manager)
{
  if (! manager->modules_ready)
    return NULL;
  return manager->block_object_interface_infos;
}

/**
 * udisks_module_manager_get_drive_object_iface_infos:
 * @manager: A #UDisksModuleManager.
 *
 * Gets all drive object interface info structs that can be plugged in #UDisksLinuxDriveObject instances.
 *
 * Returns: (transfer full) (element-type #UDisksModuleIfaceSetupFunc): A list of #UDisksModuleIfaceSetupFunc structs that belongs to the manager and must not be freed.
 */
GList *
udisks_module_manager_get_drive_object_iface_infos (UDisksModuleManager *manager)
{
  if (! manager->modules_ready)
    return NULL;
  return manager->drive_object_interface_infos;
}

/**
 * udisks_module_manager_get_module_object_new_funcs:
 * @manager: A #UDisksModuleManager.
 *
 * Gets all module object new functions that can be used to create new objects that are exported under the /org/freedesktop/UDisks2 path.
 *
 * Returns: (transfer full) (element-type #UDisksModuleObjectNewFunc): A list of #UDisksModuleObjectNewFunc function pointers that belongs to the manager and must not be freed.
 */
GList *
udisks_module_manager_get_module_object_new_funcs (UDisksModuleManager *manager)
{
  if (! manager->modules_ready)
    return NULL;
  return manager->module_object_new_funcs;
}
