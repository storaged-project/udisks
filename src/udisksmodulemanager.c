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
#include <glib/gstdio.h>
#include <gmodule.h>

#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksmodulemanager.h"
#include "udisksconfigmanager.h"
#include "udisksprivate.h"
#include "udiskslogging.h"
#include "udisksmodule.h"
#include "udisksstate.h"

/**
 * SECTION:UDisksModuleManager
 * @title: UDisksModuleManager
 * @short_description: Manage daemon modules
 *
 * ## UDisks modular approach # {#udisks-modular-design}
 *
 * UDisks functionality can be extended by modules. It's not that traditional
 * fully pluggable system as we all know it, modules are essentially just
 * carved-out parts of the daemon code and are free to access whatever internal
 * daemon objects they need. There's no universal module API other than a couple
 * of module initialization functions and a stateful module object. Out-of-tree
 * modules are not supported either and no ABI guarantee exists at all.
 *
 * This fact allows us to stay code-wise simple and transparent. It's also easier
 * to adapt modules for any change done to the core daemon. As a design decision
 * and leading from #GType system limitation modules can't be unloaded once
 * initialized. This may be a subject to change in the future, though unlikely.
 *
 * The primary motivation for introducing the modular system was to keep the daemon
 * low on resource footprint for basic usage (typically desktop environments) and
 * activating extended functionality only as needed (e.g. enterprise storage
 * applications). As the extra information comes in form of additional D-Bus
 * objects and interfaces, no difference should be observed by ordinary clients.
 *
 * ## Modules activation # {#udisks-modular-activation}
 *
 * The UDisks daemon constructs a #UDisksModuleManager singleton acting as
 * a module manager. This object tracks module usage and takes care of their
 * activation.
 *
 * By default #UDisksModuleManager is constructed on daemon startup with module
 * loading delayed until requested. This can be overridden by the
 * <literal>--force-load-modules</literal> and <literal>--disable-modules</literal>
 * commandline switches that makes modules loaded right on startup or never loaded
 * respectively.
 *
 * Clients are supposed to call the <link linkend="gdbus-method-org-freedesktop-UDisks2-Manager.EnableModule">org.freedesktop.UDisks2.Manager.EnableModule()</link>
 * D-Bus method as a <emphasis>"greeter"</emphasis> call for each module requested.
 * Proper error is reported should the module initialization fail or the module
 * is not available. Clients are supposed to act accordingly and make sure that all
 * requested modules are available and loaded prior to using any of the extra API.
 *
 * Upon successful activation, a <literal>modules-activated</literal> signal is
 * emitted internally on the #UDisksModuleManager object. Any daemon objects
 * connected to this signal are responsible for performing <emphasis>"coldplug"</emphasis>
 * on exported objects to assure modules would pick up the devices they're
 * interested in.
 *
 * ## D-Bus interface extensibility # {#udisks-modular-design-dbus}
 *
 * The modular approach is fairly simple, there are basically three primary ways
 * of extending the D-Bus API:
 *  * by attaching custom interfaces to existing block and drive objects - see
 *    udisks_module_new_block_object_interface() and
 *    udisks_module_new_drive_object_interface().
 *  * by exporting objects of its own type (socalled <emphasis>"module objects"</emphasis>)
 *    directly on the object manager root - see udisks_module_new_object().
 *  * attaching a common manager interface on the master <filename>/org/freedesktop/UDisks2/Manager</filename>
 *    object - see udisks_module_new_manager().
 *
 * All these ways of extensibility are implemented as #UDisksModule methods and
 * it is a #UDisksModuleManager task to provide interconnection between #UDisksModule
 * instances and daemon objects representing drives and block devices.
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

  UDisksDaemon *daemon;

  GList *modules;
  GMutex modules_lock;

  gboolean uninstalled;
};

typedef struct _UDisksModuleManagerClass UDisksModuleManagerClass;

struct _UDisksModuleManagerClass
{
  GObjectClass parent_class;

  /* Signals */
  void (*modules_activated) (UDisksModuleManager *manager);
};

/*--------------------------------------------------------------------------------------------------------------*/

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_UNINSTALLED,
};

enum
{
  MODULES_ACTIVATED_SIGNAL,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (UDisksModuleManager, udisks_module_manager, G_TYPE_OBJECT)

static void
udisks_module_manager_finalize (GObject *object)
{
  UDisksModuleManager *manager = UDISKS_MODULE_MANAGER (object);

  g_mutex_clear (&manager->modules_lock);

  if (G_OBJECT_CLASS (udisks_module_manager_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_module_manager_parent_class)->finalize (object);
}


static void
udisks_module_manager_init (UDisksModuleManager *manager)
{
  g_return_if_fail (UDISKS_IS_MODULE_MANAGER (manager));

  g_mutex_init (&manager->modules_lock);
}

static gchar *
get_module_sopath_for_name (UDisksModuleManager *manager,
                            const gchar         *module_name)
{
  gchar *module_dir;
  gchar *module_path;
  gchar *lib_filename;

  g_return_val_if_fail (UDISKS_IS_MODULE_MANAGER (manager), NULL);

  if (! udisks_module_manager_get_uninstalled (manager))
    module_dir = g_build_path (G_DIR_SEPARATOR_S, UDISKS_MODULE_DIR, NULL);
  else
    module_dir = g_build_path (G_DIR_SEPARATOR_S, BUILD_DIR, "modules", NULL);

  lib_filename = g_strdup_printf ("lib" PACKAGE_NAME_UDISKS2 "_%s.so", module_name);
  module_path = g_build_filename (G_DIR_SEPARATOR_S,
                                  module_dir,
                                  lib_filename,
                                  NULL);
  g_free (lib_filename);
  g_free (module_dir);

  return module_path;
}

static GList *
get_modules_list (UDisksModuleManager *manager)
{
  UDisksConfigManager *config_manager;
  GDir *dir;
  GError *error = NULL;
  GList *modules_list = NULL;
  const gchar *dent;
  gchar *module_dir;
  gchar *pth;

  g_return_val_if_fail (UDISKS_IS_MODULE_MANAGER (manager), NULL);

  /* Open a directory with modules. */
  if (! udisks_module_manager_get_uninstalled (manager))
    module_dir = g_build_path (G_DIR_SEPARATOR_S, UDISKS_MODULE_DIR, NULL);
  else
    module_dir = g_build_path (G_DIR_SEPARATOR_S, BUILD_DIR, "modules", NULL);
  dir = g_dir_open (module_dir, 0, &error);
  if (! dir)
    {
      udisks_warning ("Error loading modules: %s", error->message);
      g_clear_error (&error);
      g_free (module_dir);
      return NULL;
    }

  config_manager = udisks_daemon_get_config_manager (manager->daemon);
  if (udisks_config_manager_get_modules_all (config_manager))
    {
      /* Load all the modules from modules directory. */
      while ((dent = g_dir_read_name (dir)))
        {
          if (!g_str_has_suffix (dent, ".so"))
            continue;

          pth = g_build_filename (G_DIR_SEPARATOR_S, module_dir, dent, NULL);
          modules_list = g_list_append (modules_list, pth);
        }
    }
  else
    {
      GList *configured_modules;
      GList *modules_i;

      /* Load only those modules which are specified in config file. */
      configured_modules = udisks_config_manager_get_modules (config_manager);
      for (modules_i = configured_modules; modules_i; modules_i = modules_i->next)
        {
          pth = get_module_sopath_for_name (manager, modules_i->data);
          modules_list = g_list_append (modules_list, pth);
        }

      g_list_free_full (configured_modules, (GDestroyNotify) g_free);
    }

  g_dir_close (dir);
  g_free (module_dir);

  return modules_list;
}

static gboolean
have_module (UDisksModuleManager *manager,
             const gchar         *module_name)
{
  GList *l;

  for (l = manager->modules; l != NULL; l = g_list_next (l))
    {
      UDisksModule *module = l->data;

      if (g_strcmp0 (udisks_module_get_name (module), module_name) == 0)
        return TRUE;
    }

  return FALSE;
}

static gboolean
load_single_module_unlocked (UDisksModuleManager *manager,
                             const gchar         *sopath,
                             gboolean            *do_notify,
                             GError             **error)
{
  UDisksState *state;
  GModule *handle;
  gchar *module_id;
  gchar *module_new_func_name;
  UDisksModuleIDFunc module_id_func;
  UDisksModuleNewFunc module_new_func;
  UDisksModule *module;

  /* Unfortunately error reporting from dlopen() is done through dlerror() which
   * only returns a string - no errno set (or at least not officially documented).
   * Thus perform this extra check in a slightly racy way.
   */
  if (g_access (sopath, R_OK) != 0)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_NOT_SUPPORTED,
                   "Module not available: %s", sopath);
      return FALSE;
    }

  handle = g_module_open (sopath, 0);
  if (handle == NULL)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "%s",
                   g_module_error ());
      return FALSE;
    }

  if (! g_module_symbol (handle, "udisks_module_id", (gpointer *) &module_id_func))
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "%s: %s", sopath, g_module_error ());
      g_module_close (handle);
      return FALSE;
    }

  module_id = module_id_func ();
  if (have_module (manager, module_id))
    {
      /* module with the same name already loaded, skip */
      udisks_debug ("Module '%s' already loaded, skipping", module_id);
      g_free (module_id);
      g_module_close (handle);
      return TRUE;
    }

  udisks_notice ("Loading module %s ...", module_id);

  module_new_func_name = g_strdup_printf ("udisks_module_%s_new", module_id);
  if (! g_module_symbol (handle, module_new_func_name, (gpointer *) &module_new_func))
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "%s", g_module_error ());
      g_module_close (handle);
      g_free (module_new_func_name);
      g_free (module_id);
      return FALSE;
    }
  g_free (module_new_func_name);

  /* The following calls will initialize new GType's from the module,
   * making it uneligible for unload.
   */
  g_module_make_resident (handle);

  module = module_new_func (manager->daemon,
                            NULL /* cancellable */,
                            error);
  if (module == NULL)
    {
      /* Workaround for broken modules to avoid segfault */
      if (error == NULL)
        {
          g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                               "unknown fatal error");
        }
      g_free (module_id);
      g_module_close (handle);
      return FALSE;
    }

  manager->modules = g_list_append (manager->modules, module);

  state = udisks_daemon_get_state (manager->daemon);
  udisks_state_add_module (state, module_id);

  g_free (module_id);

  *do_notify = TRUE;
  return TRUE;
}

/**
 * udisks_module_manager_load_single_module:
 * @manager: A #UDisksModuleManager instance.
 * @name: Module name.
 * @error: Return location for error or %NULL.
 *
 * Loads single module and emits the <literal>modules-activated</literal> signal
 * in case the module activation was successful. Already active module is not
 * being reinitialized on subsequent calls to this method and %TRUE is returned
 * immediately.
 *
 * Returns: %TRUE if module was activated successfully, %FALSE otherwise with @error being set.
 */
gboolean
udisks_module_manager_load_single_module (UDisksModuleManager *manager,
                                          const gchar         *name,
                                          GError             **error)
{
  gchar *module_path;
  gboolean do_notify = FALSE;
  gboolean ret;

  g_return_val_if_fail (UDISKS_IS_MODULE_MANAGER (manager), FALSE);

  module_path = get_module_sopath_for_name (manager, name);
  if (module_path == NULL)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Cannot determine module path for '%s'",
                   name);
      return FALSE;
    }

  g_mutex_lock (&manager->modules_lock);
  ret = load_single_module_unlocked (manager, module_path, &do_notify, error);
  g_mutex_unlock (&manager->modules_lock);

  g_free (module_path);

  if (do_notify)
    {
      /* This will run connected signal handlers synchronously, i.e.
       * performs coldplug on all existing objects within #UDisksLinuxProvider.
       */
      g_signal_emit (manager, signals[MODULES_ACTIVATED_SIGNAL], 0);
    }

  return ret;
}

/**
 * udisks_module_manager_load_modules:
 * @manager: A #UDisksModuleManager instance.
 *
 * Loads all modules at a time and emits the <literal>modules-activated</literal>
 * signal in case any new module has been activated. Modules that are already loaded
 * are skipped on subsequent calls to this method.
 */
void
udisks_module_manager_load_modules (UDisksModuleManager *manager)
{
  GList *modules_to_load;
  GList *modules_to_load_tmp;
  GError *error = NULL;
  gboolean do_notify = FALSE;

  g_return_if_fail (UDISKS_IS_MODULE_MANAGER (manager));

  g_mutex_lock (&manager->modules_lock);

  /* Load the modules */
  modules_to_load = get_modules_list (manager);
  for (modules_to_load_tmp = modules_to_load;
       modules_to_load_tmp;
       modules_to_load_tmp = modules_to_load_tmp->next)
    {

      if (! load_single_module_unlocked (manager,
                                         modules_to_load_tmp->data,
                                         &do_notify,
                                         &error))
        {
          if (! g_error_matches (error, UDISKS_ERROR, UDISKS_ERROR_NOT_SUPPORTED))
            udisks_critical ("Error loading module: %s",
                             error->message);
          g_clear_error (&error);
          continue;
        }
    }

  g_mutex_unlock (&manager->modules_lock);

  g_list_free_full (modules_to_load, (GDestroyNotify) g_free);

  /* Emit 'modules-activated' in case new modules have been loaded. */
  if (do_notify)
    g_signal_emit (manager, signals[MODULES_ACTIVATED_SIGNAL], 0);
}

/**
 * udisks_module_manager_unload_modules:
 * @manager: A #UDisksModuleManager instance.
 *
 * Unloads all modules at a time. A <literal>modules-activated</literal> signal
 * is emitted if there are any modules staged for unload to give listeners room
 * to unexport all module interfaces and objects. The udisks_module_manager_get_modules()
 * would return %NULL at that time. Note that proper module unload is not fully
 * supported, this is just a convenience call for cleanup.
 */
void
udisks_module_manager_unload_modules (UDisksModuleManager *manager)
{
  UDisksState *state;
  GList *l;

  g_return_if_fail (UDISKS_IS_MODULE_MANAGER (manager));

  g_mutex_lock (&manager->modules_lock);

  l = g_steal_pointer (&manager->modules);
  if (l)
    {
      /* notify listeners that the list of active modules has changed */
      g_signal_emit (manager, signals[MODULES_ACTIVATED_SIGNAL], 0);
    }
  /* only unref module objects after all listeners have performed cleanup */
  g_list_free_full (l, g_object_unref);

  /* clear the state file */
  state = udisks_daemon_get_state (manager->daemon);
  udisks_state_clear_modules (state);

  g_mutex_unlock (&manager->modules_lock);
}

static void
udisks_module_manager_constructed (GObject *object)
{
  if (! g_module_supported ())
    {
      udisks_warning ("Modules are unsupported on the current platform");
      return;
    }

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
    case PROP_DAEMON:
      g_value_set_object (value, udisks_module_manager_get_daemon (manager));
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
udisks_module_manager_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  UDisksModuleManager *manager = UDISKS_MODULE_MANAGER (object);

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
udisks_module_manager_class_init (UDisksModuleManagerClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize     = udisks_module_manager_finalize;
  gobject_class->constructed  = udisks_module_manager_constructed;
  gobject_class->get_property = udisks_module_manager_get_property;
  gobject_class->set_property = udisks_module_manager_set_property;

  /**
   * UDisksModuleManager:daemon:
   *
   * The #UDisksDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksModuleManager:uninstalled:
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

  /**
   * UDisksModuleManager:modules-activated:
   * @manager: A #UDisksModuleManager.
   *
   * Emitted after new modules have been activated.
   *
   * This signal is emitted in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread that @manager was created in.
   */
  signals[MODULES_ACTIVATED_SIGNAL] = g_signal_new ("modules-activated",
                                                    G_TYPE_FROM_CLASS (klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    G_STRUCT_OFFSET (UDisksModuleManagerClass, modules_activated),
                                                    NULL,
                                                    NULL,
                                                    g_cclosure_marshal_generic,
                                                    G_TYPE_NONE,
                                                    0);
}

/**
 * udisks_module_manager_new:
 * @daemon: A #UDisksDaemon instance.
 *
 * Creates a new #UDisksModuleManager object.
 *
 * Returns: A #UDisksModuleManager. Free with g_object_unref().
 */
UDisksModuleManager *
udisks_module_manager_new (UDisksDaemon *daemon)
{
  return UDISKS_MODULE_MANAGER (g_object_new (UDISKS_TYPE_MODULE_MANAGER,
                                              "daemon", daemon, NULL));
}

/**
 * udisks_module_manager_new_uninstalled:
 * @daemon: A #UDisksDaemon instance.
 *
 * Creates a new #UDisksModuleManager object with indication that
 * the daemon runs from a source tree.
 *
 * Returns: A #UDisksModuleManager. Free with g_object_notify().
 */
UDisksModuleManager *
udisks_module_manager_new_uninstalled (UDisksDaemon *daemon)
{
  return UDISKS_MODULE_MANAGER (g_object_new (UDISKS_TYPE_MODULE_MANAGER,
                                              "daemon", daemon,
                                              "uninstalled", TRUE, NULL));
}

/**
 * udisks_module_manager_get_daemon:
 * @manager: A #UDisksModuleManager.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @manager.
 */
UDisksDaemon *
udisks_module_manager_get_daemon (UDisksModuleManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_MODULE_MANAGER (manager), NULL);
  return manager->daemon;
}

/**
 * udisks_module_manager_get_uninstalled:
 * @manager: A #UDisksModuleManager.
 *
 * Indicates whether the udisks daemon runs from a source tree
 * rather than being a regular system instance.
 *
 * Returns: %TRUE when the daemon runs from a source tree, %FALSE otherwise.
 */
gboolean
udisks_module_manager_get_uninstalled (UDisksModuleManager *manager)
{
  g_return_val_if_fail (UDISKS_IS_MODULE_MANAGER (manager), FALSE);
  return manager->uninstalled;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_module_manager_get_modules:
 * @manager: A #UDisksModuleManager instance.
 *
 * Gets list of active modules. Can be called from different threads.
 *
 * Returns: (transfer full) (nullable) (element-type UDisksModule): A list of #UDisksModule
 *          or %NULL if no modules are presently loaded.  Free the elements
 *          with g_object_unref().
 */
GList *
udisks_module_manager_get_modules (UDisksModuleManager *manager)
{
  GList *l;

  g_return_val_if_fail (UDISKS_IS_MODULE_MANAGER (manager), NULL);

  /* Return fast to avoid bottleneck over locking, expecting
   * a simple pointer check would be atomic.
   */
  if (manager->modules == NULL)
    return NULL;

  g_mutex_lock (&manager->modules_lock);
  l = g_list_copy_deep (manager->modules, (GCopyFunc) udisks_g_object_ref_copy, NULL);
  g_mutex_unlock (&manager->modules_lock);

  return l;
}
