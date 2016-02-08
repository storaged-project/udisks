/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
 * Copyright (C)      2016 Peter Hatina <phatina@redhat.com>
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

#include "storagedlogging.h"
#include "storageddaemon.h"
#include "storagedprovider.h"
#include "storagedlinuxprovider.h"
#include "storagedmountmonitor.h"
#include "storagedmount.h"
#include "storagedbasejob.h"
#include "storagedspawnedjob.h"
#include "storagedthreadedjob.h"
#include "storagedsimplejob.h"
#include "storagedstate.h"
#include "storagedfstabmonitor.h"
#include "storagedfstabentry.h"
#include "storagedcrypttabmonitor.h"
#include "storagedcrypttabentry.h"
#include "storagedlinuxblockobject.h"
#include "storagedlinuxdevice.h"
#include "storagedmodulemanager.h"
#include "storagedconfigmanager.h"

/**
 * SECTION:storageddaemon
 * @title: StoragedDaemon
 * @short_description: Main daemon object
 *
 * Object holding all global state.
 */

typedef struct _StoragedDaemonClass   StoragedDaemonClass;

/**
 * StoragedDaemon:
 *
 * The #StoragedDaemon structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedDaemon
{
  GObject parent_instance;
  GDBusConnection *connection;
  GDBusObjectManagerServer *object_manager;

  StoragedMountMonitor *mount_monitor;

  StoragedLinuxProvider *linux_provider;

  /* may be NULL if polkit is masked */
  PolkitAuthority *authority;

  StoragedState *state;

  StoragedFstabMonitor *fstab_monitor;

  StoragedCrypttabMonitor *crypttab_monitor;

  StoragedModuleManager *module_manager;

  StoragedConfigManager *config_manager;

  gboolean disable_modules;
  gboolean force_load_modules;
  gboolean uninstalled;
};

struct _StoragedDaemonClass
{
  GObjectClass parent_class;
};

enum
{
  PROP_0,
  PROP_CONNECTION,
  PROP_OBJECT_MANAGER,
  PROP_MOUNT_MONITOR,
  PROP_FSTAB_MONITOR,
  PROP_CRYPTTAB_MONITOR,
  PROP_MODULE_MANAGER,
  PROP_CONFIG_MANAGER,
  PROP_DISABLE_MODULES,
  PROP_FORCE_LOAD_MODULES,
  PROP_UNINSTALLED,
};

G_DEFINE_TYPE (StoragedDaemon, storaged_daemon, G_TYPE_OBJECT);

static void
storaged_daemon_finalize (GObject *object)
{
  StoragedDaemon *daemon = STORAGED_DAEMON (object);

  storaged_state_stop_cleanup (daemon->state);
  g_object_unref (daemon->state);

  g_clear_object (&daemon->authority);
  g_object_unref (daemon->object_manager);
  g_object_unref (daemon->linux_provider);
  g_object_unref (daemon->mount_monitor);
  g_object_unref (daemon->connection);
  g_object_unref (daemon->fstab_monitor);
  g_object_unref (daemon->crypttab_monitor);

  storaged_module_manager_unload_modules (daemon->module_manager);
  g_clear_object (&daemon->module_manager);

  g_clear_object (&daemon->config_manager);

  if (G_OBJECT_CLASS (storaged_daemon_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_daemon_parent_class)->finalize (object);
}

static void
storaged_daemon_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  StoragedDaemon *daemon = STORAGED_DAEMON (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, storaged_daemon_get_connection (daemon));
      break;

    case PROP_OBJECT_MANAGER:
      g_value_set_object (value, storaged_daemon_get_object_manager (daemon));
      break;

    case PROP_MOUNT_MONITOR:
      g_value_set_object (value, storaged_daemon_get_mount_monitor (daemon));
      break;

    case PROP_FSTAB_MONITOR:
      g_value_set_object (value, storaged_daemon_get_fstab_monitor (daemon));
      break;

    case PROP_CRYPTTAB_MONITOR:
      g_value_set_object (value, storaged_daemon_get_crypttab_monitor (daemon));
      break;

    case PROP_MODULE_MANAGER:
      g_value_set_object (value, storaged_daemon_get_module_manager (daemon));
      break;

    case PROP_CONFIG_MANAGER:
      g_value_set_object (value, storaged_daemon_get_config_manager (daemon));
      break;

    case PROP_DISABLE_MODULES:
      g_value_set_boolean (value, storaged_daemon_get_disable_modules (daemon));
      break;

    case PROP_FORCE_LOAD_MODULES:
      g_value_set_boolean (value, storaged_daemon_get_force_load_modules (daemon));
      break;

    case PROP_UNINSTALLED:
      g_value_set_boolean (value, storaged_daemon_get_uninstalled (daemon));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_daemon_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  StoragedDaemon *daemon = STORAGED_DAEMON (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      g_assert (daemon->connection == NULL);
      daemon->connection = g_value_dup_object (value);
      break;

    case PROP_DISABLE_MODULES:
      daemon->disable_modules = g_value_get_boolean (value);
      break;

    case PROP_FORCE_LOAD_MODULES:
      daemon->force_load_modules = g_value_get_boolean (value);
      break;

    case PROP_UNINSTALLED:
      daemon->uninstalled = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_daemon_init (StoragedDaemon *daemon)
{
}

static void
mount_monitor_on_mount_removed (StoragedMountMonitor *monitor,
                                StoragedMount        *mount,
                                gpointer              user_data)
{
  StoragedDaemon *daemon = STORAGED_DAEMON (user_data);
  storaged_state_check (daemon->state);
}

static void
storaged_daemon_constructed (GObject *object)
{
  StoragedDaemon *daemon = STORAGED_DAEMON (object);
  GError *error;

  error = NULL;
  daemon->authority = polkit_authority_get_sync (NULL, &error);
  if (daemon->authority == NULL)
    {
      storaged_error ("Error initializing polkit authority: %s (%s, %d)",
                    error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
    }

  daemon->object_manager = g_dbus_object_manager_server_new ("/org/storaged/Storaged");

  if (!g_file_test ("/run/storaged", G_FILE_TEST_IS_DIR))
    {
      if (g_mkdir_with_parents ("/run/storaged", 0700) != 0)
        {
          storaged_error ("Error creating directory %s: %m", "/run/storaged");
        }
    }

  if (!g_file_test (PACKAGE_LOCALSTATE_DIR "/lib/storaged", G_FILE_TEST_IS_DIR))
    {
      if (g_mkdir_with_parents (PACKAGE_LOCALSTATE_DIR "/lib/storaged", 0700) != 0)
        {
          storaged_error ("Error creating directory %s: %m", PACKAGE_LOCALSTATE_DIR "/lib/storaged");
        }
    }


  if (! daemon->uninstalled)
    {
      daemon->config_manager = storaged_config_manager_new ();
      daemon->module_manager = storaged_module_manager_new (daemon);
    }
  else
    {
      daemon->config_manager = storaged_config_manager_new_uninstalled ();
      daemon->module_manager = storaged_module_manager_new_uninstalled (daemon);
    }

  daemon->mount_monitor = storaged_mount_monitor_new ();

  daemon->state = storaged_state_new (daemon);

  g_signal_connect (daemon->mount_monitor,
                    "mount-removed",
                    G_CALLBACK (mount_monitor_on_mount_removed),
                    daemon);

  daemon->fstab_monitor = storaged_fstab_monitor_new ();
  daemon->crypttab_monitor = storaged_crypttab_monitor_new ();

  /* now add providers */
  daemon->linux_provider = storaged_linux_provider_new (daemon);

  if (daemon->force_load_modules
      || (storaged_config_manager_get_load_preference (daemon->config_manager)
          == STORAGED_MODULE_LOAD_ONSTARTUP))
    {
      storaged_module_manager_load_modules (daemon->module_manager);
    }

  storaged_provider_start (STORAGED_PROVIDER (daemon->linux_provider));

  /* Export the ObjectManager */
  g_dbus_object_manager_server_set_connection (daemon->object_manager, daemon->connection);

  /* Start cleaning up */
  storaged_state_start_cleanup (daemon->state);
  storaged_state_check (daemon->state);

  if (G_OBJECT_CLASS (storaged_daemon_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (storaged_daemon_parent_class)->constructed (object);
}


static void
storaged_daemon_class_init (StoragedDaemonClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_daemon_finalize;
  gobject_class->constructed  = storaged_daemon_constructed;
  gobject_class->set_property = storaged_daemon_set_property;
  gobject_class->get_property = storaged_daemon_get_property;

  /**
   * StoragedDaemon:connection:
   *
   * The #GDBusConnection the daemon is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CONNECTION,
                                   g_param_spec_object ("connection",
                                                        "Connection",
                                                        "The D-Bus connection the daemon is for",
                                                        G_TYPE_DBUS_CONNECTION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedDaemon:object-manager:
   *
   * The #GDBusObjectManager used by the daemon
   */
  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_MANAGER,
                                   g_param_spec_object ("object-manager",
                                                        "Object Manager",
                                                        "The D-Bus Object Manager server used by the daemon",
                                                        G_TYPE_DBUS_OBJECT_MANAGER_SERVER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedDaemon:mount-monitor:
   *
   * The #StoragedMountMonitor used by the daemon
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MOUNT_MONITOR,
                                   g_param_spec_object ("mount-monitor",
                                                        "Mount Monitor",
                                                        "The mount monitor",
                                                        STORAGED_TYPE_MOUNT_MONITOR,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedDaemon:disable-modules:
   *
   * Whether modules should be disabled
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DISABLE_MODULES,
                                   g_param_spec_boolean ("disable-modules",
                                                         "Disable modules",
                                                         "Whether modules should be disabled",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT_ONLY));

  /**
   * StoragedDaemon:force-load-modules:
   *
   * Whether modules should be activated upon startup
   */
  g_object_class_install_property (gobject_class,
                                   PROP_FORCE_LOAD_MODULES,
                                   g_param_spec_boolean ("force-load-modules",
                                                         "Force load modules",
                                                         "Whether modules should be activated upon startup",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT_ONLY));

  /**
   * StoragedDaemon:uninstalled:
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
 * storaged_daemon_new:
 * @connection: A #GDBusConnection.
 * @disable_modules: Indicates whether modules should never be activated.
 * @force_load_modules: Activate modules on startup (for debugging purposes).
 * @uninstalled: Loads modules from the build directory (for debugging purposes).
 *
 * Create a new daemon object for exporting objects on @connection.
 *
 * Returns: A #StoragedDaemon object. Free with g_object_unref().
 */
StoragedDaemon *
storaged_daemon_new (GDBusConnection *connection,
                     gboolean         disable_modules,
                     gboolean         force_load_modules,
                     gboolean         uninstalled)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  return STORAGED_DAEMON (g_object_new (STORAGED_TYPE_DAEMON,
                                      "connection", connection,
                                      "disable-modules", disable_modules,
                                      "force-load-modules", force_load_modules,
                                      "uninstalled", uninstalled,
                                      NULL));
}

/**
 * storaged_daemon_get_connection:
 * @daemon: A #StoragedDaemon.
 *
 * Gets the D-Bus connection used by @daemon.
 *
 * Returns: A #GDBusConnection. Do not free, the object is owned by @daemon.
 */
GDBusConnection *
storaged_daemon_get_connection (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->connection;
}

/**
 * storaged_daemon_get_object_manager:
 * @daemon: A #StoragedDaemon.
 *
 * Gets the D-Bus object manager used by @daemon.
 *
 * Returns: A #GDBusObjectManagerServer. Do not free, the object is owned by @daemon.
 */
GDBusObjectManagerServer *
storaged_daemon_get_object_manager (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->object_manager;
}

/**
 * storaged_daemon_get_mount_monitor:
 * @daemon: A #StoragedDaemon
 *
 * Gets the mount monitor used by @daemon.
 *
 * Returns: A #StoragedMountMonitor. Do not free, the object is owned by @daemon.
 */
StoragedMountMonitor *
storaged_daemon_get_mount_monitor (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->mount_monitor;
}

/**
 * storaged_daemon_get_fstab_monitor:
 * @daemon: A #StoragedDaemon
 *
 * Gets the fstab monitor used by @daemon.
 *
 * Returns: A #StoragedFstabMonitor. Do not free, the object is owned by @daemon.
 */
StoragedFstabMonitor *
storaged_daemon_get_fstab_monitor (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->fstab_monitor;
}

/**
 * storaged_daemon_get_crypttab_monitor:
 * @daemon: A #StoragedDaemon
 *
 * Gets the crypttab monitor used by @daemon.
 *
 * Returns: A #StoragedCrypttabMonitor. Do not free, the object is owned by @daemon.
 */
StoragedCrypttabMonitor *
storaged_daemon_get_crypttab_monitor (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->crypttab_monitor;
}

/**
 * storaged_daemon_get_linux_provider:
 * @daemon: A #StoragedDaemon.
 *
 * Gets the Linux Provider, if any.
 *
 * Returns: A #StoragedLinuxProvider or %NULL. Do not free, the object is owned by @daemon.
 */
StoragedLinuxProvider *
storaged_daemon_get_linux_provider (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->linux_provider;
}

/**
 * storaged_daemon_get_authority:
 * @daemon: A #StoragedDaemon.
 *
 * Gets the PolicyKit authority used by @daemon.
 *
 * Returns: A #PolkitAuthority instance or %NULL if the polkit
 * authority is not available. Do not free, the object is owned by
 * @daemon.
 */
PolkitAuthority *
storaged_daemon_get_authority (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->authority;
}

/**
 * storaged_daemon_get_state:
 * @daemon: A #StoragedDaemon.
 *
 * Gets the state object used by @daemon.
 *
 * Returns: A #StoragedState instance. Do not free, the object is owned by @daemon.
 */
StoragedState *
storaged_daemon_get_state (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->state;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_job_completed (StoragedJob    *job,
                  gboolean        success,
                  const gchar    *message,
                  gpointer        user_data)
{
  StoragedDaemon *daemon = STORAGED_DAEMON (user_data);
  StoragedObjectSkeleton *object;

  object = STORAGED_OBJECT_SKELETON (g_dbus_interface_get_object (G_DBUS_INTERFACE (job)));
  g_assert (object != NULL);

  /* Unexport job */
  g_dbus_object_manager_server_unexport (daemon->object_manager,
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  g_object_unref (object);

  /* free the allocated job object */
  g_object_unref (job);

  /* returns the reference we took when connecting to the
   * StoragedJob::completed signal in storaged_daemon_launch_{spawned,threaded}_job()
   * below
   */
  g_object_unref (daemon);
}

/* ---------------------------------------------------------------------------------------------------- */

static guint job_id = 0;

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_daemon_launch_simple_job:
 * @daemon: A #StoragedDaemon.
 * @object: (allow-none): A #StoragedObject to add to the job or %NULL.
 * @job_operation: The operation for the job.
 * @job_started_by_uid: The user who started the job.
 * @cancellable: A #GCancellable or %NULL.
 *
 * Launches a new simple job.
 *
 * The job is started immediately - When the job is done, call
 * storaged_simple_job_complete() on the returned object. Long-running
 * jobs should periodically check @cancellable to see if they have
 * been cancelled.
 *
 * The returned object will be exported on the bus until the
 * #StoragedJob::completed signal is emitted on the object. It is not
 * valid to use the returned object after this signal fires.
 *
 * Returns: A #StoragedSimpleJob object. Do not free, the object
 * belongs to @manager.
 */
StoragedBaseJob *
storaged_daemon_launch_simple_job (StoragedDaemon    *daemon,
                                   StoragedObject    *object,
                                   const gchar       *job_operation,
                                   uid_t              job_started_by_uid,
                                   GCancellable      *cancellable)
{
  StoragedSimpleJob *job;
  StoragedObjectSkeleton *job_object;
  gchar *job_object_path;

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);

  job = storaged_simple_job_new (daemon, cancellable);
  if (object != NULL)
    storaged_base_job_add_object (STORAGED_BASE_JOB (job), object);

  /* TODO: protect job_id by a mutex */
  job_object_path = g_strdup_printf ("/org/storaged/Storaged/jobs/%u", job_id++);
  job_object = storaged_object_skeleton_new (job_object_path);
  storaged_object_skeleton_set_job (job_object, STORAGED_JOB (job));
  g_free (job_object_path);

  storaged_job_set_cancelable (STORAGED_JOB (job), TRUE);
  storaged_job_set_operation (STORAGED_JOB (job), job_operation);
  storaged_job_set_started_by_uid (STORAGED_JOB (job), job_started_by_uid);

  g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (job_object));
  g_signal_connect_after (job,
                          "completed",
                          G_CALLBACK (on_job_completed),
                          g_object_ref (daemon));

  return STORAGED_BASE_JOB (job);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_daemon_launch_threaded_job:
 * @daemon: A #StoragedDaemon.
 * @object: (allow-none): A #StoragedObject to add to the job or %NULL.
 * @job_operation: The operation for the job.
 * @job_started_by_uid: The user who started the job.
 * @job_func: The function to run in another thread.
 * @user_data: User data to pass to @job_func.
 * @user_data_free_func: Function to free @user_data with or %NULL.
 * @cancellable: A #GCancellable or %NULL.
 *
 * Launches a new job by running @job_func in a new dedicated thread.
 *
 * The job is started immediately - connect to the
 * #StoragedThreadedJob::threaded-job-completed or #StoragedJob::completed
 * signals to get notified when the job is done.
 *
 * Long-running jobs should periodically check @cancellable to see if
 * they have been cancelled.
 *
 * The returned object will be exported on the bus until the
 * #StoragedJob::completed signal is emitted on the object. It is not
 * valid to use the returned object after this signal fires.
 *
 * Returns: A #StoragedThreadedJob object. Do not free, the object
 * belongs to @manager.
 */
StoragedBaseJob *
storaged_daemon_launch_threaded_job  (StoragedDaemon         *daemon,
                                      StoragedObject         *object,
                                      const gchar            *job_operation,
                                      uid_t                   job_started_by_uid,
                                      StoragedThreadedJobFunc job_func,
                                      gpointer                user_data,
                                      GDestroyNotify          user_data_free_func,
                                      GCancellable           *cancellable)
{
  StoragedThreadedJob *job;
  StoragedObjectSkeleton *job_object;
  gchar *job_object_path;

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (job_func != NULL, NULL);

  job = storaged_threaded_job_new (job_func,
                                   user_data,
                                   user_data_free_func,
                                   daemon,
                                   cancellable);
  if (object != NULL)
    storaged_base_job_add_object (STORAGED_BASE_JOB (job), object);

  /* TODO: protect job_id by a mutex */
  job_object_path = g_strdup_printf ("/org/storaged/Storaged/jobs/%u", job_id++);
  job_object = storaged_object_skeleton_new (job_object_path);
  storaged_object_skeleton_set_job (job_object, STORAGED_JOB (job));
  g_free (job_object_path);

  storaged_job_set_cancelable (STORAGED_JOB (job), TRUE);
  storaged_job_set_operation (STORAGED_JOB (job), job_operation);
  storaged_job_set_started_by_uid (STORAGED_JOB (job), job_started_by_uid);

  g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (job_object));
  g_signal_connect_after (job,
                          "completed",
                          G_CALLBACK (on_job_completed),
                          g_object_ref (daemon));

  return STORAGED_BASE_JOB (job);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_daemon_launch_spawned_job:
 * @daemon: A #StoragedDaemon.
 * @object: (allow-none): A #StoragedObject to add to the job or %NULL.
 * @job_operation: The operation for the job.
 * @job_started_by_uid: The user who started the job.
 * @cancellable: A #GCancellable or %NULL.
 * @run_as_uid: The #uid_t to run the command as.
 * @run_as_euid: The effective #uid_t to run the command as.
 * @input_string: A string to write to stdin of the spawned program or %NULL.
 * @command_line_format: printf()-style format for the command line to spawn.
 * @...: Arguments for @command_line_format.
 *
 * Launches a new job for @command_line_format.
 *
 * The job is started immediately - connect to the
 * #StoragedSpawnedJob::spawned-job-completed or #StoragedJob::completed
 * signals to get notified when the job is done.
 *
 * The returned object will be exported on the bus until the
 * #StoragedJob::completed signal is emitted on the object. It is not
 * valid to use the returned object after this signal fires.
 *
 * Returns: A #StoragedSpawnedJob object. Do not free, the object
 * belongs to @manager.
 */
StoragedBaseJob *
storaged_daemon_launch_spawned_job (StoragedDaemon    *daemon,
                                    StoragedObject    *object,
                                    const gchar       *job_operation,
                                    uid_t              job_started_by_uid,
                                    GCancellable      *cancellable,
                                    uid_t              run_as_uid,
                                    uid_t              run_as_euid,
                                    const gchar       *input_string,
                                    const gchar       *command_line_format,
                                    ...)
{
  va_list var_args;
  gchar *command_line;
  StoragedSpawnedJob *job;
  StoragedObjectSkeleton *job_object;
  gchar *job_object_path;

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (command_line_format != NULL, NULL);

  va_start (var_args, command_line_format);
  command_line = g_strdup_vprintf (command_line_format, var_args);
  va_end (var_args);
  job = storaged_spawned_job_new (command_line, input_string, run_as_uid, run_as_euid, daemon, cancellable);
  g_free (command_line);

  if (object != NULL)
    storaged_base_job_add_object (STORAGED_BASE_JOB (job), object);

  /* TODO: protect job_id by a mutex */
  job_object_path = g_strdup_printf ("/org/storaged/Storaged/jobs/%u", job_id++);
  job_object = storaged_object_skeleton_new (job_object_path);
  storaged_object_skeleton_set_job (job_object, STORAGED_JOB (job));
  g_free (job_object_path);

  storaged_job_set_cancelable (STORAGED_JOB (job), TRUE);
  storaged_job_set_operation (STORAGED_JOB (job), job_operation);
  storaged_job_set_started_by_uid (STORAGED_JOB (job), job_started_by_uid);

  g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (job_object));
  g_signal_connect_after (job,
                          "completed",
                          G_CALLBACK (on_job_completed),
                          g_object_ref (daemon));

  return STORAGED_BASE_JOB (job);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GMainContext *context;
  GMainLoop *loop;
  gboolean success;
  gint status;
  gchar *message;
} SpawnedJobSyncData;

static gboolean
spawned_job_sync_on_spawned_job_completed (StoragedSpawnedJob *job,
                                           GError           *error,
                                           gint              status,
                                           GString          *standard_output,
                                           GString          *standard_error,
                                           gpointer          user_data)
{
  SpawnedJobSyncData *data = user_data;
  data->status = status;
  return FALSE; /* let other handlers run */
}

static void
spawned_job_sync_on_completed (StoragedJob    *job,
                               gboolean      success,
                               const gchar  *message,
                               gpointer      user_data)
{
  SpawnedJobSyncData *data = user_data;
  data->success = success;
  data->message = g_strdup (message);
  g_main_loop_quit (data->loop);
}

/**
 * storaged_daemon_launch_spawned_job_sync:
 * @daemon: A #StoragedDaemon.
 * @object: (allow-none): A #StoragedObject to add to the job or %NULL.
 * @job_operation: The operation for the job.
 * @job_started_by_uid: The user who started the job.
 * @cancellable: A #GCancellable or %NULL.
 * @run_as_uid: The #uid_t to run the command as.
 * @run_as_euid: The effective #uid_t to run the command as.
 * @input_string: A string to write to stdin of the spawned program or %NULL.
 * @out_status: Return location for the @status parameter of the #StoragedSpawnedJob::spawned-job-completed signal.
 * @out_message: Return location for the @message parameter of the #StoragedJob::completed signal.
 * @command_line_format: printf()-style format for the command line to spawn.
 * @...: Arguments for @command_line_format.
 *
 * Like storaged_daemon_launch_spawned_job() but blocks the calling
 * thread until the job completes.
 *
 * Returns: The @success parameter of the #StoragedJob::completed signal.
 */
gboolean
storaged_daemon_launch_spawned_job_sync (StoragedDaemon    *daemon,
                                         StoragedObject    *object,
                                         const gchar       *job_operation,
                                         uid_t              job_started_by_uid,
                                         GCancellable      *cancellable,
                                         uid_t              run_as_uid,
                                         uid_t              run_as_euid,
                                         gint              *out_status,
                                         gchar            **out_message,
                                         const gchar       *input_string,
                                         const gchar       *command_line_format,
                                         ...)
{
  va_list var_args;
  gchar *command_line;
  StoragedBaseJob *job;
  SpawnedJobSyncData data;

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (command_line_format != NULL, FALSE);

  data.context = g_main_context_new ();
  g_main_context_push_thread_default (data.context);
  data.loop = g_main_loop_new (data.context, FALSE);
  data.success = FALSE;
  data.status = 0;
  data.message = NULL;

  va_start (var_args, command_line_format);
  command_line = g_strdup_vprintf (command_line_format, var_args);
  va_end (var_args);
  job = storaged_daemon_launch_spawned_job (daemon,
                                          object,
                                          job_operation,
                                          job_started_by_uid,
                                          cancellable,
                                          run_as_uid,
                                          run_as_euid,
                                          input_string,
                                          "%s",
                                          command_line);
  g_signal_connect (job,
                    "spawned-job-completed",
                    G_CALLBACK (spawned_job_sync_on_spawned_job_completed),
                    &data);
  g_signal_connect_after (job,
                          "completed",
                          G_CALLBACK (spawned_job_sync_on_completed),
                          &data);

  g_main_loop_run (data.loop);

  if (out_status != NULL)
    *out_status = data.status;

  if (out_message != NULL)
    *out_message = data.message;
  else
    g_free (data.message);

  g_free (command_line);
  g_main_loop_unref (data.loop);
  g_main_context_pop_thread_default (data.context);
  g_main_context_unref (data.context);

  /* note: the job object is freed in the ::completed handler */

  return data.success;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_daemon_wait_for_object_sync:
 * @daemon: A #StoragedDaemon.
 * @wait_func: Function to check for desired object.
 * @user_data: User data to pass to @wait_func.
 * @user_data_free_func: (allow-none): Function to free @user_data or %NULL.
 * @timeout_seconds: Maximum time to wait for the object (in seconds) or 0 to never wait.
 * @error: (allow-none): Return location for error or %NULL.
 *
 * Blocks the calling thread until an object picked by @wait_func is
 * available or until @timeout_seconds has passed (in which case the
 * function fails with %STORAGED_ERROR_TIMED_OUT).
 *
 * Note that @wait_func will be called from time to time - for example
 * if there is a device event.
 *
 * Returns: (transfer full): The object picked by @wait_func or %NULL if @error is set.
 */

typedef struct {
  GMainContext *context;
  GMainLoop *loop;
  gboolean timed_out;
} WaitData;

static gboolean
wait_on_timed_out (gpointer user_data)
{
  WaitData *data = user_data;
  data->timed_out = TRUE;
  g_main_loop_quit (data->loop);
  return FALSE; /* remove the source */
}

static gboolean
wait_on_recheck (gpointer user_data)
{
  WaitData *data = user_data;
  g_main_loop_quit (data->loop);
  return FALSE; /* remove the source */
}

StoragedObject *
storaged_daemon_wait_for_object_sync (StoragedDaemon         *daemon,
                                      StoragedDaemonWaitFunc  wait_func,
                                      gpointer                user_data,
                                      GDestroyNotify          user_data_free_func,
                                      guint                   timeout_seconds,
                                      GError                **error)
{
  StoragedObject *ret;
  WaitData data;

  /* TODO: support GCancellable */

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (wait_func != NULL, NULL);

  ret = NULL;

  memset (&data, '\0', sizeof (data));
  data.context = NULL;
  data.loop = NULL;

  g_object_ref (daemon);

 again:
  ret = wait_func (daemon, user_data);

  if (ret == NULL && timeout_seconds > 0)
    {
      GSource *source;

      /* sit and wait for up to @timeout_seconds if the object isn't there already */
      if (data.context == NULL)
        {
          /* TODO: this will deadlock if we are calling from the main thread... */
          data.context = g_main_context_new ();
          data.loop = g_main_loop_new (data.context, FALSE);

          source = g_timeout_source_new_seconds (timeout_seconds);
          g_source_set_priority (source, G_PRIORITY_DEFAULT);
          g_source_set_callback (source, wait_on_timed_out, &data, NULL);
          g_source_attach (source, data.context);
          g_source_unref (source);
        }

      /* TODO: do something a bit more elegant than checking every 250ms ... it's
       *       probably going to involve having each StoragedProvider emit a "changed"
       *       signal when it's time to recheck... for now this works.
       */
      source = g_timeout_source_new (250);
      g_source_set_priority (source, G_PRIORITY_DEFAULT);
      g_source_set_callback (source, wait_on_recheck, &data, NULL);
      g_source_attach (source, data.context);
      g_source_unref (source);

      g_main_loop_run (data.loop);

      if (data.timed_out)
        {
          g_set_error (error,
                       STORAGED_ERROR, STORAGED_ERROR_FAILED,
                       "Timed out waiting for object");
        }
      else
        {
          goto again;
        }
    }

  if (user_data_free_func != NULL)
    user_data_free_func (user_data);

  g_object_unref (daemon);

  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  if (data.context != NULL)
    g_main_context_unref (data.context);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_daemon_find_block:
 * @daemon: A #StoragedDaemon.
 * @block_device_number: A #dev_t with the device number to find.
 *
 * Finds a block device with the number given by @block_device_number.
 *
 * Returns: (transfer full): A #StoragedObject or %NULL if not found. Free with g_object_unref().
 */
StoragedObject *
storaged_daemon_find_block (StoragedDaemon *daemon,
                            dev_t           block_device_number)
{
  StoragedObject *ret = NULL;
  GList *objects, *l;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (daemon->object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;

      block = storaged_object_peek_block (object);
      if (block == NULL)
        continue;

      if (storaged_block_get_device_number (block) == block_device_number)
        {
          ret = g_object_ref (object);
          goto out;
        }
    }
 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_daemon_find_block_by_device_file:
 * @daemon: A #StoragedDaemon.
 * @device_file: A device file.
 *
 * Finds a block device with device file given by @device_file.
 *
 * Returns: (transfer full): A #StoragedObject or %NULL if not found. Free with g_object_unref().
 */
StoragedObject *
storaged_daemon_find_block_by_device_file (StoragedDaemon *daemon,
                                           const gchar    *device_file)
{
  StoragedObject *ret = NULL;
  GList *objects, *l;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (daemon->object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;

      block = storaged_object_peek_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (storaged_block_get_device (block), device_file) == 0)
        {
          ret = g_object_ref (object);
          goto out;
        }
    }
 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_daemon_find_block_by_sysfs_path:
 * @daemon: A #StoragedDaemon.
 * @sysfs_path: A sysfs path.
 *
 * Finds a block device with a sysfs path given by @sysfs_path.
 *
 * Returns: (transfer full): A #StoragedObject or %NULL if not found. Free with g_object_unref().
 */
StoragedObject *
storaged_daemon_find_block_by_sysfs_path (StoragedDaemon *daemon,
                                          const gchar    *sysfs_path)
{
  StoragedObject *ret = NULL;
  GList *objects, *l;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (daemon->object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedLinuxDevice *device;

      if (!STORAGED_IS_LINUX_BLOCK_OBJECT (object))
        continue;

      device = storaged_linux_block_object_get_device (STORAGED_LINUX_BLOCK_OBJECT (object));
      if (device == NULL)
        continue;

      if (g_strcmp0 (g_udev_device_get_sysfs_path (device->udev_device), sysfs_path) == 0)
        {
          g_object_unref (device);
          ret = g_object_ref (object);
          goto out;
        }
      g_object_unref (device);
    }
 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_daemon_find_object:
 * @daemon: A #StoragedDaemon.
 * @object_path: A #dev_t with the device number to find.
 *
 * Finds an exported object with the object path given by @object_path.
 *
 * Returns: (transfer full): A #StoragedObject or %NULL if not found. Free with g_object_unref().
 */
StoragedObject *
storaged_daemon_find_object (StoragedDaemon *daemon,
                             const gchar    *object_path)
{
  return (StoragedObject *) g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (daemon->object_manager),
                                                              object_path);
}

/**
 * storaged_daemon_get_objects:
 * @daemon: A #StoragedDaemon.
 *
 * Gets all D-Bus objects exported by @daemon.
 *
 * Returns: (transfer full) (element-type StoragedObject): A list of #StoragedObject instaces. The returned list should be freed with g_list_free() after each element has been freed with g_object_unref().
 */
GList *
storaged_daemon_get_objects (StoragedDaemon *daemon)
{
  return g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (daemon->object_manager));
}

/**
 * storaged_daemon_get_module_manager:
 * @daemon: A #StoragedDaemon.
 *
 * Gets the module manager used by @daemon.
 *
 * Returns: A #StoragedModuleManager. Do not free, the object is owned by @daemon.
 */
StoragedModuleManager *
storaged_daemon_get_module_manager (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->module_manager;
}

/**
 * storage_daemon_get_config_manager:
 * @daemon: A #StoragedDaemon.
 *
 * Gets the config manager used by @daemon.
 *
 * Returns: A #ConfigModuleManager. Do not free, the object is owned by @daemon.
 */
StoragedConfigManager *
storaged_daemon_get_config_manager (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return daemon->config_manager;
}

/**
 * storaged_daemon_get_disable_modules:
 * @daemon: A #StoragedDaemon.
 *
 * Gets @daemon setting whether modules should never be loaded.
 *
 * Returns: %TRUE if --disable-modules commandline switch has been specified.
 */
gboolean
storaged_daemon_get_disable_modules (StoragedDaemon*daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), FALSE);
  return daemon->disable_modules;
}

/**
 * storaged_daemon_get_force_load_modules:
 * @daemon: A #StoragedDaemon.
 *
 * Gets @daemon setting whether modules should be activated upon start.
 *
 * Returns: %TRUE if --force-load-modules commandline switch has been specified.
 */
gboolean
storaged_daemon_get_force_load_modules (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), FALSE);
  return daemon->force_load_modules;
}

/**
 * storaged_daemon_get_uninstalled:
 * @daemon: A #StoragedDaemon.
 *
 * Gets @daemon setting whether the modules should be loaded from the build
 * directory.
 *
 * Returns: %TRUE if --uninstalled commandline switch has been specified.
 */
gboolean
storaged_daemon_get_uninstalled (StoragedDaemon *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), FALSE);
  return daemon->uninstalled;
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
storaged_daemon_get_parent_for_tracking (StoragedDaemon *daemon,
                                         const gchar    *path,
                                         gchar         **uuid_ret)
{
  const gchar *parent_path = NULL;
  const gchar *parent_uuid = NULL;

  StoragedObject *object = NULL;
  StoragedObject *crypto_object = NULL;
  StoragedObject *mdraid_object = NULL;
  StoragedObject *table_object = NULL;
  GList *track_parent_funcs;

  StoragedBlock *block;
  StoragedBlock *crypto_block;
  StoragedMDRaid *mdraid;
  StoragedPartition *partition;
  StoragedBlock *table_block;

  object = storaged_daemon_find_object (daemon, path);
  if (object == NULL)
    goto out;

  block = storaged_object_peek_block (object);
  if (block)
    {
      crypto_object = storaged_daemon_find_object (daemon, storaged_block_get_crypto_backing_device (block));
      if (crypto_object)
        {
          crypto_block = storaged_object_peek_block (crypto_object);
          if (crypto_block)
            {
              parent_uuid = storaged_block_get_id_uuid (crypto_block);
              parent_path = storaged_block_get_crypto_backing_device (block);
              goto out;
            }
        }

      mdraid_object = storaged_daemon_find_object (daemon, storaged_block_get_mdraid (block));
      if (mdraid_object)
        {
          mdraid = storaged_object_peek_mdraid (mdraid_object);
          if (mdraid)
            {
              parent_uuid = storaged_mdraid_get_uuid (mdraid);
              parent_path = storaged_block_get_mdraid (block);
              goto out;
            }
        }

      partition = storaged_object_peek_partition (object);
      if (partition)
        {
          table_object = storaged_daemon_find_object (daemon, storaged_partition_get_table (partition));
          if (table_object)
            {
              table_block = storaged_object_peek_block (table_object);
              if (table_block)
                {
                  /* We don't want to track partition tables because
                     they can't be 'closed' in a way that their
                     children temporarily invisible.
                  */
                  parent_uuid = NULL;
                  parent_path = storaged_partition_get_table (partition);
                  goto out;
                }
            }
        }
    }

 out:
  g_clear_object (&object);
  g_clear_object (&crypto_object);
  g_clear_object (&mdraid_object);
  g_clear_object (&table_object);

  if (parent_path)
    {
      if (uuid_ret)
        *uuid_ret = g_strdup (parent_uuid);
      return g_strdup (parent_path);
    }

  track_parent_funcs = storaged_module_manager_get_track_parent_funcs (daemon->module_manager);
  while (track_parent_funcs)
    {
      StoragedTrackParentFunc func = track_parent_funcs->data;
      gchar *path_ret = func (daemon, path, uuid_ret);
      if (path_ret)
        return path_ret;

      track_parent_funcs = track_parent_funcs->next;
    }

  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */
