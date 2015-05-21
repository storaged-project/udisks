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

#include "storageddaemon.h"
#include "storagedmodulemanager.h"
#include "storagedprivate.h"
#include "storagedlogging.h"
#include <modules/storagedmoduleifacetypes.h>


/**
 * SECTION:StoragedModuleManager
 * @title: StoragedModuleManager
 * @short_description: Manages daemon modules
 *
 * ## Storaged modular approach # {#storaged-modular-design}
 *
 * Storaged functionality can be extended by modules. It's not a fully
 * pluggable system as we know it, in this case modules are almost integral
 * parts of the source tree. Meaning that modules are free to use whatever
 * internal objects they need as there is no universal module API (or a
 * translation layer).
 *
 * This fact allows us to stay code-wise simple and transparent. It also means
 * that there's no support for out-of-the-tree modules and care must be taken
 * when changing Storaged internals. As a design decision, for sake of simplicity,
 * once modules are loaded they stay active until the daemon exits (this may be
 * a subject to change in the future).
 *
 * The primary motivation for this was to keep the daemon low on resource
 * footprint for basic usage (typically desktop environments) and only
 * activating the extended functionality when needed (e.g. enterprise storage
 * applications). As the extra information comes in form of additional D-Bus
 * objects and interfaces, no difference should be observed by legacy clients.
 *
 * ## D-Bus interface extensibility # {#storaged-modular-design-dbus}
 *
 * The modular approach is fairly simple, there are basically two primary ways
 * of extending the D-Bus API:
 *  * by attaching custom interfaces to existing objects (limited to block and
 *    drive objects for the moment)
 *  * by exporting objects of its own type directly in the object manager root
 *
 * Besides that there are several other ways of extensibility such as attaching
 * custom interfaces on the master /org/storaged/Storaged/Manager object.
 *
 * ## Modules activation # {#storaged-modular-activation}
 *
 * The Storaged daemon constructs a #StoragedModuleManager singleton acting as
 * a manager. This object tracks module usage and takes care of its activation.
 *
 * By default, module manager is constructed on daemon startup but module
 * loading is delayed until requested. This can be overriden by the
 * --force-load-modules and --disable-modules commandline switches that makes
 * modules loaded right on startup or never loaded respectively.
 *
 * Upon successful activation, the "modules-ready" property on the #StoragedModuleManager
 * instance is set to %TRUE. Any daemon objects watching this property are
 * responsible for performing "coldplug" on their exported objects to assure
 * modules would pick up the devices they're interested in. See e.g.
 * StoragedModuleObjectNewFunc() to see how device binding works for
 * #StoragedModuleObject.
 *
 * Modules are in fact separate shared objects (.so) that are loaded from the
 * "$(libdir)/storaged/modules" path (usually "/usr/lib/storaged/modules"). No
 * extra or service files are needed, the directory is enumerated and all files
 * are attempted to be loaded.
 *
 * Clients are supposed to call the org.storaged.Storaged.Manager.EnableModules()
 * D-Bus method as a "greeter" call. Please note that from asynchronous nature
 * of uevents and the way modules are processing them the extra D-Bus interfaces
 * may not be available right after this method call returns.
 *
 * ## Module API # {#storaged-modular-api}
 *
 * The (strictly internal) module API is simple - only a couple of functions
 * are needed. The following text contains brief description of individual
 * parts of the module API with further links to detailed description within
 * this API reference book.
 *
 * The #StoragedModuleManager first loads all module entry functions, i.e.
 * symbols defined in the public facing header "storagedmoduleiface.h". Only
 * those symbols should be exported from each module. The header file is only
 * meant to be compiled in modules, not the daemon. If any of the symbols is
 * missing in the module library, the whole module is skipped.
 *
 * Once module symbols are resolved, module manager activates each module by
 * calling storaged_module_init() on it. The returned so-called "state" pointer
 * is stored in the #StoragedModuleManager and can be later retrieved by calling
 * the storaged_module_manager_get_module_state_pointer() method. This is typically
 * used further in the module code to retrieve and store module-specific runtime
 * data.
 *
 * Every one of the "storagedmoduleiface.h" header file symbols has its counterpart
 * defined in the "storagedmoduleifacetypes.h" header file in form of function
 * pointers. Those are used internally for symbol resolving purposes. However,
 * they also carry detailed documentation. For illustration purposes, let's
 * call these symbol pairs the "module setup entry functions". See
 * #StoragedModuleIfaceSetupFunc, #StoragedModuleObjectNewSetupFunc and
 * #StoragedModuleNewManagerIfaceSetupFunc for reference. These however are
 * essentially auxiliary symbols only described for demonstrating the big
 * picture; for the useful part of the module API please read on.
 *
 * Every module setup entry function (besides the very simple storaged_module_init())
 * returns an array of setup structures or functions, containing either none
 * (NULL result), one or more elements. The result is then mixed by
 * #StoragedModuleManager from all modules and separate lists are created for
 * each kind of Storaged way of extension. Such lists are then used in the daemon
 * code at appropriate places, sequentially calling elements from the lists to
 * obtain data or objects that are then typically exported on D-Bus.
 *
 * In short, have a look at the #StoragedModuleInterfaceInfo,
 * #StoragedModuleObjectNewFunc and #StoragedModuleNewManagerIfaceFunc definitions
 * to learn more about particular ways of extending Storaged.
 */


/**
 * StoragedModuleManager:
 *
 * The #StoragedModuleManager structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _StoragedModuleManager
{
  GObject parent_instance;

  StoragedDaemon *daemon;

  GList *modules;
  GList *block_object_interface_infos;
  GList *drive_object_interface_infos;
  GList *module_object_new_funcs;
  GList *new_manager_iface_funcs;
  GList *module_track_parent_funcs;

  GMutex modules_ready_lock;
  gboolean modules_ready;
  gboolean uninstalled;

  GHashTable *state_pointers;
};

typedef struct _StoragedModuleManagerClass StoragedModuleManagerClass;

struct _StoragedModuleManagerClass
{
  GObjectClass parent_class;
};

typedef struct
{
  GModule *handle;
} ModuleData;


/*--------------------------------------------------------------------------------------------------------------*/

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_MODULES_READY,
  PROP_UNINSTALLED,
};

G_DEFINE_TYPE (StoragedModuleManager, storaged_module_manager, G_TYPE_OBJECT)

static void
free_module_data (ModuleData *data)
{
  g_module_close (data->handle);
  free (data);
}

static void
storaged_module_manager_finalize (GObject *object)
{
  StoragedModuleManager *manager = STORAGED_MODULE_MANAGER (object);

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

  if (manager->new_manager_iface_funcs != NULL)
    {
      g_list_free (manager->new_manager_iface_funcs);
    }

  if (manager->module_track_parent_funcs != NULL)
    {
      g_list_free (manager->module_track_parent_funcs);
    }

  g_hash_table_destroy (manager->state_pointers);

  if (manager->modules != NULL)
    {
      g_list_foreach (manager->modules, (GFunc) free_module_data, NULL);
      g_list_free (manager->modules);
    }

  g_mutex_clear (&manager->modules_ready_lock);

  if (G_OBJECT_CLASS (storaged_module_manager_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_module_manager_parent_class)->finalize (object);
}


static void
storaged_module_manager_init (StoragedModuleManager *manager)
{
  g_mutex_init (&manager->modules_ready_lock);

  manager->state_pointers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * storaged_module_manager_load_modules:
 * @manager: A #StoragedModuleManager instance.
 *
 * Loads all modules at a time and emits the "modules-ready" signal.
 * Does nothing when called multiple times.
 */
void
storaged_module_manager_load_modules (StoragedModuleManager *manager)
{
  GError *error;
  GDir *dir;
  const gchar *dent;
  GModule *module;
  ModuleData *module_data;
  gchar *module_dir;
  gchar *pth;

  StoragedModuleIfaceSetupFunc block_object_iface_setup_func;
  StoragedModuleIfaceSetupFunc drive_object_iface_setup_func;
  StoragedModuleObjectNewSetupFunc module_object_new_setup_func;
  StoragedModuleNewManagerIfaceSetupFunc module_new_manager_iface_setup_func;
  StoragedModuleIDFunc module_id_func;
  StoragedModuleInitFunc module_init_func;

  gchar *module_id;
  gpointer module_state_pointer;
  StoragedModuleInterfaceInfo **infos, **infos_i;
  StoragedModuleObjectNewFunc *module_object_new_funcs, *module_object_new_funcs_i;
  StoragedModuleNewManagerIfaceFunc *module_new_manager_iface_funcs, *module_new_manager_iface_funcs_i;
  gpointer track_parent_func;

  g_return_if_fail (STORAGED_IS_MODULE_MANAGER (manager));

  g_mutex_lock (&manager->modules_ready_lock);
  if (manager->modules_ready)
    {
      g_mutex_unlock (&manager->modules_ready_lock);
      return;
    }

  error = NULL;
  module_dir = g_strdup(STORAGED_MODULE_DIR);
  dir = g_dir_open (module_dir, 0, &error);
  if (! dir)
    {
      /* We don't want to exit prematurely.  Check build directory first. */
      if (manager->uninstalled)
        {
          g_error_free (error);
          g_free (module_dir);
          error = NULL;
          module_dir = g_strdup (BUILDDIR "/modules");
          dir = g_dir_open (module_dir, 0, &error);
        }

      if (!dir)
        {
          storaged_warning ("Error loading modules: %s", error->message);
          g_error_free (error);
          g_free (module_dir);
          g_mutex_unlock (&manager->modules_ready_lock);
          return;
        }
    }

  while ((dent = g_dir_read_name (dir)))
    {
      if (!g_str_has_suffix (dent, ".so"))
        continue;

      pth = g_build_filename (module_dir, dent, NULL);
      module = g_module_open (pth, /* G_MODULE_BIND_LOCAL */ 0);

      if (module != NULL)
        {
          module_data = g_new0 (ModuleData, 1);
          module_data->handle = module;
          storaged_notice ("Loading module %s...", dent);
          if (! g_module_symbol (module_data->handle, "storaged_module_id", (gpointer *) &module_id_func) ||
              ! g_module_symbol (module_data->handle, "storaged_module_init", (gpointer *) &module_init_func) ||
              ! g_module_symbol (module_data->handle, "storaged_module_get_block_object_iface_setup_entries", (gpointer *) &block_object_iface_setup_func) ||
              ! g_module_symbol (module_data->handle, "storaged_module_get_drive_object_iface_setup_entries", (gpointer *) &drive_object_iface_setup_func) ||
              ! g_module_symbol (module_data->handle, "storaged_module_get_object_new_funcs", (gpointer *) &module_object_new_setup_func) ||
              ! g_module_symbol (module_data->handle, "storaged_module_get_new_manager_iface_funcs", (gpointer *) &module_new_manager_iface_setup_func))
            {
              storaged_warning ("  Error importing required symbols from module '%s'", pth);
              free_module_data (module_data);
            }
          else
            {
              /* Module name */
              module_id = module_id_func ();

              /* Initialize the module and store its state pointer. */
              module_state_pointer = module_init_func (storaged_module_manager_get_daemon (manager));

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

              module_new_manager_iface_funcs = module_new_manager_iface_setup_func ();
              for (module_new_manager_iface_funcs_i = module_new_manager_iface_funcs; module_new_manager_iface_funcs_i && *module_new_manager_iface_funcs_i; module_new_manager_iface_funcs_i++)
                manager->new_manager_iface_funcs = g_list_append (manager->new_manager_iface_funcs, *module_new_manager_iface_funcs_i);
              g_free (module_new_manager_iface_funcs);

              if (g_module_symbol (module_data->handle, "storaged_module_track_parent", &track_parent_func))
                {
                  storaged_debug("ADDING TRACK");
                  manager->module_track_parent_funcs = g_list_append (manager->module_track_parent_funcs,
                                                                      track_parent_func);
                }

              manager->modules = g_list_append (manager->modules, module_data);
              if (module_state_pointer != NULL && module_id != NULL)
                storaged_module_manager_set_module_state_pointer (manager, module_id, module_state_pointer);
              g_free (module_id);
            }
        }
      else
        {
          storaged_error ("Module loading failed: %s", g_module_error ());
        }

      g_free (pth);
    }
  g_dir_close (dir);

  g_free (module_dir);

  manager->modules_ready = TRUE;
  g_mutex_unlock (&manager->modules_ready_lock);

  /* Ensured to fire only once */
  g_object_notify (G_OBJECT (manager), "modules-ready");
}

static void
storaged_module_manager_constructed (GObject *object)
{
  StoragedModuleManager *manager = STORAGED_MODULE_MANAGER (object);

  manager->modules_ready = FALSE;

  if (! g_module_supported ())
    {
      storaged_warning ("Modules are unsupported on the current platform");
      return;
    }

  if (G_OBJECT_CLASS (storaged_module_manager_parent_class)->constructed != NULL)
    (*G_OBJECT_CLASS (storaged_module_manager_parent_class)->constructed) (object);
}

static void
storaged_module_manager_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  StoragedModuleManager *manager = STORAGED_MODULE_MANAGER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_module_manager_get_daemon (manager));
      break;

    case PROP_MODULES_READY:
      g_value_set_boolean (value, storaged_module_manager_get_modules_available (manager));
      break;

    case PROP_UNINSTALLED:
      g_value_set_boolean (value, manager->uninstalled);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_module_manager_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  StoragedModuleManager *manager = STORAGED_MODULE_MANAGER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (manager->daemon == NULL);
      /* We don't take a reference to the daemon */
      manager->daemon = g_value_get_object (value);
      break;

    case PROP_UNINSTALLED:
      manager->uninstalled = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_module_manager_class_init (StoragedModuleManagerClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize     = storaged_module_manager_finalize;
  gobject_class->constructed  = storaged_module_manager_constructed;
  gobject_class->get_property = storaged_module_manager_get_property;
  gobject_class->set_property = storaged_module_manager_set_property;

  /**
   * StoragedModuleManager:modules-ready:
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
  /**
   * StoragedModuleManager:daemon:
   *
   * The #StoragedDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedModuleManager:uninstalled:
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
                                                         G_PARAM_CONSTRUCT_ONLY));
}

/**
 * storaged_module_manager_new:
 * @daemon: A #StoragedDaemon instance.
 *
 * Creates a new #StoragedModuleManager object.
 *
 * Returns: A #StoragedModuleManager. Free with g_object_unref().
 */
StoragedModuleManager *
storaged_module_manager_new (StoragedDaemon *daemon)
{
  return STORAGED_MODULE_MANAGER (g_object_new (STORAGED_TYPE_MODULE_MANAGER,
                                                "daemon", daemon, NULL));
}

/**
 * storaged_module_manager_new_uninstalled:
 * @daemon: A #StoragedDaemon instance.
 *
 * Creates a new #StoragedModuleManager object.
 *
 * Returns: A #StoragedModuleManager. Free with g_object_notify().
 */
StoragedModuleManager *
storaged_module_manager_new_uninstalled (StoragedDaemon *daemon)
{
  return STORAGED_MODULE_MANAGER (g_object_new (STORAGED_TYPE_MODULE_MANAGER,
                                                "daemon", daemon,
                                                "uninstalled", TRUE, NULL));
}

/**
 * storaged_module_manager_get_daemon:
 * @manager: A #StoragedModuleManager.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @manager.
 */
StoragedDaemon *
storaged_module_manager_get_daemon (StoragedModuleManager *manager)
{
  g_return_val_if_fail (STORAGED_IS_MODULE_MANAGER (manager), NULL);
  return manager->daemon;
}

/**
 * storaged_module_manager_get_modules_available:
 * @manager: A #StoragedModuleManager instance.
 *
 * Indicates whether modules have been loaded.
 *
 * Returns: %TRUE if modules have been loaded, %FALSE otherwise.
 */
gboolean
storaged_module_manager_get_modules_available (StoragedModuleManager *manager)
{
  gboolean ret;

  g_return_val_if_fail (STORAGED_IS_MODULE_MANAGER (manager), FALSE);

  g_mutex_lock (&manager->modules_ready_lock);
  ret = manager->modules_ready;
  g_mutex_unlock (&manager->modules_ready_lock);

  return ret;
}

gboolean
storaged_module_manager_get_uninstalled(StoragedModuleManager *manager)
{
  g_return_val_if_fail (STORAGED_IS_MODULE_MANAGER (manager), FALSE);
  return manager->uninstalled;
}

/* ---------------------------------------------------------------------------------------------------- */


/**
 * storaged_module_manager_get_block_object_iface_infos:
 * @manager: A #StoragedModuleManager instance.
 *
 * Returns a list of block object interface info structs that can be plugged in #StoragedLinuxBlockObject instances. See #StoragedModuleIfaceSetupFunc for details.
 *
 * Returns: (element-type StoragedModuleIfaceSetupFunc) (transfer full): A list of #StoragedModuleIfaceSetupFunc structs that belongs to the manager and must not be freed.
 */
GList *
storaged_module_manager_get_block_object_iface_infos (StoragedModuleManager *manager)
{
  g_return_val_if_fail (STORAGED_IS_MODULE_MANAGER (manager), NULL);
  if (! manager->modules_ready)
    return NULL;
  return manager->block_object_interface_infos;
}

/**
 * storaged_module_manager_get_drive_object_iface_infos:
 * @manager: A #StoragedModuleManager instance.
 *
 * Returns a list of drive object interface info structs that can be plugged in #StoragedLinuxDriveObject instances. See #StoragedModuleIfaceSetupFunc for details.
 *
 * Returns: (element-type StoragedModuleIfaceSetupFunc) (transfer full): A list of #StoragedModuleIfaceSetupFunc structs that belongs to the manager and must not be freed.
 */
GList *
storaged_module_manager_get_drive_object_iface_infos (StoragedModuleManager *manager)
{
  g_return_val_if_fail (STORAGED_IS_MODULE_MANAGER (manager), NULL);
  if (! manager->modules_ready)
    return NULL;
  return manager->drive_object_interface_infos;
}

/**
 * storaged_module_manager_get_module_object_new_funcs:
 * @manager: A #StoragedModuleManager instance.
 *
 * Returns a list of all module object new functions. See #StoragedModuleObjectNewFunc for details.
 *
 * Returns: (element-type StoragedModuleObjectNewFunc) (transfer full): A list of #StoragedModuleObjectNewFunc function pointers that belongs to the manager and must not be freed.
 */
GList *
storaged_module_manager_get_module_object_new_funcs (StoragedModuleManager *manager)
{
  g_return_val_if_fail (STORAGED_IS_MODULE_MANAGER (manager), NULL);
  if (! manager->modules_ready)
    return NULL;
  return manager->module_object_new_funcs;
}

/**
 * storaged_module_manager_get_new_manager_iface_funcs:
 * @manager: A #StoragedModuleManager instance.
 *
 * Returns a list of all module new manager interface functions. See #StoragedModuleNewManagerIfaceFunc for details.
 *
 * Returns: (element-type StoragedModuleNewManagerIfaceFunc) (transfer full): A list of #StoragedModuleNewManagerIfaceFunc function pointers that belongs to the manager and must not be freed.
 */
GList *
storaged_module_manager_get_new_manager_iface_funcs (StoragedModuleManager *manager)
{
  g_return_val_if_fail (STORAGED_IS_MODULE_MANAGER (manager), NULL);
  if (! manager->modules_ready)
    return NULL;
  return manager->new_manager_iface_funcs;
}

/**
 * storaged_module_manager_get_parent_tracking_funcs:
 * @manager: A #StoragedModuleManager instance.
 *
 * Returns a list of all module parent tracking functions. See
 * #StoragedTrackParentFunc for details.
 *
 * Returns: (element-type StoragedTrackParentFunc) (transfer none): A
 * list of #StoragedTrackParentFunc function pointers that belongs to
 * the manager and must not be freed.
 */
GList *
storaged_module_manager_get_track_parent_funcs (StoragedModuleManager *manager)
{
  g_return_val_if_fail (STORAGED_IS_MODULE_MANAGER (manager), NULL);
  if (! manager->modules_ready)
    return NULL;
  return manager->module_track_parent_funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_module_manager_set_module_state_pointer:
 * @manager: A #StoragedModuleManager instance.
 * @module_name: A module name.
 * @state: Pointer to a private data.
 *
 * Stores the @state pointer for the given @module_name.
 */
void
storaged_module_manager_set_module_state_pointer (StoragedModuleManager *manager,
                                                const gchar           *module_name,
                                                gpointer               state)
{
  g_return_if_fail (STORAGED_IS_MODULE_MANAGER (manager));

  g_hash_table_insert (manager->state_pointers, g_strdup (module_name), state);
}

/**
 * storaged_module_manager_get_module_state_pointer:
 * @manager: A #StoragedModuleManager instance.
 * @module_name: A module name.
 *
 * Retrieves the stored module state pointer for the given @module_name.
 *
 * Returns: A stored pointer to the private data or %NULL if there is no state pointer for the given @module_name.
 */
gpointer
storaged_module_manager_get_module_state_pointer (StoragedModuleManager *manager,
                                                  const gchar           *module_name)
{
  g_return_val_if_fail (STORAGED_IS_MODULE_MANAGER (manager), NULL);

  return g_hash_table_lookup (manager->state_pointers, module_name);
}

/* ---------------------------------------------------------------------------------------------------- */
