/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#include "udiskslogging.h"
#include "udisksdaemon.h"
#include "udisksprovider.h"
#include "udiskslinuxprovider.h"
#include "udisksmountmonitor.h"
#include "udisksmount.h"
#include "udiskspersistentstore.h"
#include "udisksbasejob.h"
#include "udisksspawnedjob.h"
#include "udisksthreadedjob.h"
#include "udiskssimplejob.h"
#include "udiskscleanup.h"
#include "udisksfstabmonitor.h"
#include "udisksfstabentry.h"
#include "udiskscrypttabmonitor.h"
#include "udiskscrypttabentry.h"
#include "udiskslinuxblockobject.h"

/**
 * SECTION:udisksdaemon
 * @title: UDisksDaemon
 * @short_description: Main daemon object
 *
 * Object holding all global state.
 */

typedef struct _UDisksDaemonClass   UDisksDaemonClass;

/**
 * UDisksDaemon:
 *
 * The #UDisksDaemon structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksDaemon
{
  GObject parent_instance;
  GDBusConnection *connection;
  GDBusObjectManagerServer *object_manager;

  UDisksMountMonitor *mount_monitor;

  UDisksPersistentStore *persistent_store;

  UDisksLinuxProvider *linux_provider;

  /* may be NULL if polkit is masked */
  PolkitAuthority *authority;

  UDisksCleanup *cleanup;

  UDisksFstabMonitor *fstab_monitor;

  UDisksCrypttabMonitor *crypttab_monitor;
};

struct _UDisksDaemonClass
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
};

G_DEFINE_TYPE (UDisksDaemon, udisks_daemon, G_TYPE_OBJECT);

static void
udisks_daemon_finalize (GObject *object)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);

  udisks_cleanup_stop (daemon->cleanup);
  g_object_unref (daemon->cleanup);

  g_clear_object (&daemon->authority);
  g_object_unref (daemon->persistent_store);
  g_object_unref (daemon->object_manager);
  g_object_unref (daemon->linux_provider);
  g_object_unref (daemon->mount_monitor);
  g_object_unref (daemon->connection);
  g_object_unref (daemon->fstab_monitor);
  g_object_unref (daemon->crypttab_monitor);

  if (G_OBJECT_CLASS (udisks_daemon_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_daemon_parent_class)->finalize (object);
}

static void
udisks_daemon_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, udisks_daemon_get_connection (daemon));
      break;

    case PROP_OBJECT_MANAGER:
      g_value_set_object (value, udisks_daemon_get_object_manager (daemon));
      break;

    case PROP_MOUNT_MONITOR:
      g_value_set_object (value, udisks_daemon_get_mount_monitor (daemon));
      break;

    case PROP_FSTAB_MONITOR:
      g_value_set_object (value, udisks_daemon_get_fstab_monitor (daemon));
      break;

    case PROP_CRYPTTAB_MONITOR:
      g_value_set_object (value, udisks_daemon_get_crypttab_monitor (daemon));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_daemon_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      g_assert (daemon->connection == NULL);
      daemon->connection = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_daemon_init (UDisksDaemon *daemon)
{
}

static void
mount_monitor_on_mount_removed (UDisksMountMonitor *monitor,
                                UDisksMount        *mount,
                                gpointer            user_data)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (user_data);
  udisks_cleanup_check (daemon->cleanup);
}

static void
udisks_daemon_constructed (GObject *object)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);
  GError *error;

  error = NULL;
  daemon->authority = polkit_authority_get_sync (NULL, &error);
  if (daemon->authority == NULL)
    {
      udisks_error ("Error initializing polkit authority: %s (%s, %d)",
                    error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
    }

  daemon->object_manager = g_dbus_object_manager_server_new ("/org/freedesktop/UDisks2");

  if (!g_file_test ("/run/udisks2", G_FILE_TEST_IS_DIR))
    {
      if (g_mkdir_with_parents ("/run/udisks2", 0700) != 0)
        {
          udisks_error ("Error creating directory %s: %m", "/run/udisks2");
        }
    }

  if (!g_file_test (PACKAGE_LOCALSTATE_DIR "/lib/udisks2", G_FILE_TEST_IS_DIR))
    {
      if (g_mkdir_with_parents (PACKAGE_LOCALSTATE_DIR "/lib/udisks2", 0700) != 0)
        {
          udisks_error ("Error creating directory %s: %m", PACKAGE_LOCALSTATE_DIR "/lib/udisks2");
        }
    }

  daemon->mount_monitor = udisks_mount_monitor_new ();

  daemon->persistent_store = udisks_persistent_store_new (PACKAGE_LOCALSTATE_DIR "/lib/udisks2",
                                                          "/run/udisks2");

  g_signal_connect (daemon->mount_monitor,
                    "mount-removed",
                    G_CALLBACK (mount_monitor_on_mount_removed),
                    daemon);

  daemon->fstab_monitor = udisks_fstab_monitor_new ();
  daemon->crypttab_monitor = udisks_crypttab_monitor_new ();

  /* now add providers */
  daemon->linux_provider = udisks_linux_provider_new (daemon);

  udisks_provider_start (UDISKS_PROVIDER (daemon->linux_provider));

  /* Export the ObjectManager */
  g_dbus_object_manager_server_set_connection (daemon->object_manager, daemon->connection);

  /* Start cleaning up */
  daemon->cleanup = udisks_cleanup_new (daemon);
  udisks_cleanup_start (daemon->cleanup);
  udisks_cleanup_check (daemon->cleanup);

  if (G_OBJECT_CLASS (udisks_daemon_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_daemon_parent_class)->constructed (object);
}


static void
udisks_daemon_class_init (UDisksDaemonClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_daemon_finalize;
  gobject_class->constructed  = udisks_daemon_constructed;
  gobject_class->set_property = udisks_daemon_set_property;
  gobject_class->get_property = udisks_daemon_get_property;

  /**
   * UDisksDaemon:connection:
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
   * UDisksDaemon:object-manager:
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
   * UDisksDaemon:mount-monitor:
   *
   * The #UDisksMountMonitor used by the daemon
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MOUNT_MONITOR,
                                   g_param_spec_object ("mount-monitor",
                                                        "Mount Monitor",
                                                        "The mount monitor",
                                                        UDISKS_TYPE_MOUNT_MONITOR,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_daemon_new:
 * @connection: A #GDBusConnection.
 *
 * Create a new daemon object for exporting objects on @connection.
 *
 * Returns: A #UDisksDaemon object. Free with g_object_unref().
 */
UDisksDaemon *
udisks_daemon_new (GDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  return UDISKS_DAEMON (g_object_new (UDISKS_TYPE_DAEMON,
                                      "connection", connection,
                                      NULL));
}

/**
 * udisks_daemon_get_connection:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the D-Bus connection used by @daemon.
 *
 * Returns: A #GDBusConnection. Do not free, the object is owned by @daemon.
 */
GDBusConnection *
udisks_daemon_get_connection (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->connection;
}

/**
 * udisks_daemon_get_object_manager:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the D-Bus object manager used by @daemon.
 *
 * Returns: A #GDBusObjectManagerServer. Do not free, the object is owned by @daemon.
 */
GDBusObjectManagerServer *
udisks_daemon_get_object_manager (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->object_manager;
}

/**
 * udisks_daemon_get_mount_monitor:
 * @daemon: A #UDisksDaemon
 *
 * Gets the mount monitor used by @daemon.
 *
 * Returns: A #UDisksMountMonitor. Do not free, the object is owned by @daemon.
 */
UDisksMountMonitor *
udisks_daemon_get_mount_monitor (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->mount_monitor;
}

/**
 * udisks_daemon_get_fstab_monitor:
 * @daemon: A #UDisksDaemon
 *
 * Gets the fstab monitor used by @daemon.
 *
 * Returns: A #UDisksFstabMonitor. Do not free, the object is owned by @daemon.
 */
UDisksFstabMonitor *
udisks_daemon_get_fstab_monitor (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->fstab_monitor;
}

/**
 * udisks_daemon_get_crypttab_monitor:
 * @daemon: A #UDisksDaemon
 *
 * Gets the crypttab monitor used by @daemon.
 *
 * Returns: A #UDisksCrypttabMonitor. Do not free, the object is owned by @daemon.
 */
UDisksCrypttabMonitor *
udisks_daemon_get_crypttab_monitor (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->crypttab_monitor;
}

/**
 * udisks_daemon_get_linux_provider:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the Linux Provider, if any.
 *
 * Returns: A #UDisksLinuxProvider or %NULL. Do not free, the object is owned by @daemon.
 */
UDisksLinuxProvider *
udisks_daemon_get_linux_provider (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->linux_provider;
}

/**
 * udisks_daemon_get_persistent_store:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the object used to store persistent data
 *
 * Returns: A #UDisksPersistentStore. Do not free, the object is owned by @daemon.
 */
UDisksPersistentStore *
udisks_daemon_get_persistent_store (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->persistent_store;
}

/**
 * udisks_daemon_get_authority:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the PolicyKit authority used by @daemon.
 *
 * Returns: A #PolkitAuthority instance or %NULL if the polkit
 * authority is not available. Do not free, the object is owned by
 * @daemon.
 */
PolkitAuthority *
udisks_daemon_get_authority (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->authority;
}

/**
 * udisks_daemon_get_cleanup:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the cleanup object used by @daemon.
 *
 * Returns: A #UDisksCleanup instance. Do not free, the object is owned by @daemon.
 */
UDisksCleanup *
udisks_daemon_get_cleanup (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->cleanup;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_job_completed (UDisksJob    *job,
                  gboolean      success,
                  const gchar  *message,
                  gpointer      user_data)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (user_data);
  UDisksObjectSkeleton *object;

  object = UDISKS_OBJECT_SKELETON (g_dbus_interface_get_object (G_DBUS_INTERFACE (job)));
  g_assert (object != NULL);

  /* Unexport job */
  g_dbus_object_manager_server_unexport (daemon->object_manager,
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  g_object_unref (object);

  /* free the allocated job object */
  g_object_unref (job);

  /* returns the reference we took when connecting to the
   * UDisksJob::completed signal in udisks_daemon_launch_{spawned,threaded}_job()
   * below
   */
  g_object_unref (daemon);
}

/* ---------------------------------------------------------------------------------------------------- */

static guint job_id = 0;

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_daemon_launch_simple_job:
 * @daemon: A #UDisksDaemon.
 * @object: (allow-none): A #UDisksObject to add to the job or %NULL.
 * @cancellable: A #GCancellable or %NULL.
 *
 * Launches a new simple job.
 *
 * The job is started immediately - When the job is done, call
 * udisks_simple_job_complete() on the returned object. Long-running
 * jobs should periodically check @cancellable to see if they have
 * been cancelled.
 *
 * The returned object will be exported on the bus until the
 * #UDisksJob::completed signal is emitted on the object. It is not
 * valid to use the returned object after this signal fires.
 *
 * Returns: A #UDisksSimpleJob object. Do not free, the object
 * belongs to @manager.
 */
UDisksBaseJob *
udisks_daemon_launch_simple_job (UDisksDaemon    *daemon,
                                 UDisksObject    *object,
                                 GCancellable    *cancellable)
{
  UDisksSimpleJob *job;
  UDisksObjectSkeleton *job_object;
  gchar *job_object_path;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);

  job = udisks_simple_job_new (cancellable);
  if (object != NULL)
    udisks_base_job_add_object (UDISKS_BASE_JOB (job), object);

  /* TODO: protect job_id by a mutex */
  job_object_path = g_strdup_printf ("/org/freedesktop/UDisks2/jobs/%d", job_id++);
  job_object = udisks_object_skeleton_new (job_object_path);
  udisks_object_skeleton_set_job (job_object, UDISKS_JOB (job));
  g_free (job_object_path);

  g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (job_object));
  g_signal_connect_after (job,
                          "completed",
                          G_CALLBACK (on_job_completed),
                          g_object_ref (daemon));

  return UDISKS_BASE_JOB (job);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_daemon_launch_threaded_job:
 * @daemon: A #UDisksDaemon.
 * @object: (allow-none): A #UDisksObject to add to the job or %NULL.
 * @job_func: The function to run in another thread.
 * @user_data: User data to pass to @job_func.
 * @user_data_free_func: Function to free @user_data with or %NULL.
 * @cancellable: A #GCancellable or %NULL.
 *
 * Launches a new job by running @job_func in a new dedicated thread.
 *
 * The job is started immediately - connect to the
 * #UDisksThreadedJob::threaded-job-completed or #UDisksJob::completed
 * signals to get notified when the job is done.
 *
 * Long-running jobs should periodically check @cancellable to see if
 * they have been cancelled.
 *
 * The returned object will be exported on the bus until the
 * #UDisksJob::completed signal is emitted on the object. It is not
 * valid to use the returned object after this signal fires.
 *
 * Returns: A #UDisksThreadedJob object. Do not free, the object
 * belongs to @manager.
 */
UDisksBaseJob *
udisks_daemon_launch_threaded_job  (UDisksDaemon    *daemon,
                                    UDisksObject    *object,
                                    UDisksThreadedJobFunc job_func,
                                    gpointer         user_data,
                                    GDestroyNotify   user_data_free_func,
                                    GCancellable    *cancellable)
{
  UDisksThreadedJob *job;
  UDisksObjectSkeleton *job_object;
  gchar *job_object_path;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (job_func != NULL, NULL);

  job = udisks_threaded_job_new (job_func,
                                 user_data,
                                 user_data_free_func,
                                 cancellable);
  if (object != NULL)
    udisks_base_job_add_object (UDISKS_BASE_JOB (job), object);

  /* TODO: protect job_id by a mutex */
  job_object_path = g_strdup_printf ("/org/freedesktop/UDisks2/jobs/%d", job_id++);
  job_object = udisks_object_skeleton_new (job_object_path);
  udisks_object_skeleton_set_job (job_object, UDISKS_JOB (job));
  g_free (job_object_path);

  g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (job_object));
  g_signal_connect_after (job,
                          "completed",
                          G_CALLBACK (on_job_completed),
                          g_object_ref (daemon));

  return UDISKS_BASE_JOB (job);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_daemon_launch_spawned_job:
 * @daemon: A #UDisksDaemon.
 * @object: (allow-none): A #UDisksObject to add to the job or %NULL.
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
 * #UDisksSpawnedJob::spawned-job-completed or #UDisksJob::completed
 * signals to get notified when the job is done.
 *
 * The returned object will be exported on the bus until the
 * #UDisksJob::completed signal is emitted on the object. It is not
 * valid to use the returned object after this signal fires.
 *
 * Returns: A #UDisksSpawnedJob object. Do not free, the object
 * belongs to @manager.
 */
UDisksBaseJob *
udisks_daemon_launch_spawned_job (UDisksDaemon    *daemon,
                                  UDisksObject    *object,
                                  GCancellable    *cancellable,
                                  uid_t            run_as_uid,
                                  uid_t            run_as_euid,
                                  const gchar     *input_string,
                                  const gchar     *command_line_format,
                                  ...)
{
  va_list var_args;
  gchar *command_line;
  UDisksSpawnedJob *job;
  UDisksObjectSkeleton *job_object;
  gchar *job_object_path;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (command_line_format != NULL, NULL);

  va_start (var_args, command_line_format);
  command_line = g_strdup_vprintf (command_line_format, var_args);
  va_end (var_args);
  job = udisks_spawned_job_new (command_line, input_string, run_as_uid, run_as_euid, cancellable);
  g_free (command_line);

  if (object != NULL)
    udisks_base_job_add_object (UDISKS_BASE_JOB (job), object);

  /* TODO: protect job_id by a mutex */
  job_object_path = g_strdup_printf ("/org/freedesktop/UDisks2/jobs/%d", job_id++);
  job_object = udisks_object_skeleton_new (job_object_path);
  udisks_object_skeleton_set_job (job_object, UDISKS_JOB (job));
  g_free (job_object_path);

  g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (job_object));
  g_signal_connect_after (job,
                          "completed",
                          G_CALLBACK (on_job_completed),
                          g_object_ref (daemon));

  return UDISKS_BASE_JOB (job);
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
spawned_job_sync_on_spawned_job_completed (UDisksSpawnedJob *job,
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
spawned_job_sync_on_completed (UDisksJob    *job,
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
 * udisks_daemon_launch_spawned_job_sync:
 * @daemon: A #UDisksDaemon.
 * @object: (allow-none): A #UDisksObject to add to the job or %NULL.
 * @cancellable: A #GCancellable or %NULL.
 * @run_as_uid: The #uid_t to run the command as.
 * @run_as_euid: The effective #uid_t to run the command as.
 * @input_string: A string to write to stdin of the spawned program or %NULL.
 * @out_status: Return location for the @status parameter of the #UDisksSpawnedJob::spawned-job-completed signal.
 * @out_message: Return location for the @message parameter of the #UDisksJob::completed signal.
 * @command_line_format: printf()-style format for the command line to spawn.
 * @...: Arguments for @command_line_format.
 *
 * Like udisks_daemon_launch_spawned_job() but blocks the calling
 * thread until the job completes.
 *
 * Returns: The @success parameter of the #UDisksJob::completed signal.
 */
gboolean
udisks_daemon_launch_spawned_job_sync (UDisksDaemon    *daemon,
                                       UDisksObject    *object,
                                       GCancellable    *cancellable,
                                       uid_t            run_as_uid,
                                       uid_t            run_as_euid,
                                       gint            *out_status,
                                       gchar          **out_message,
                                       const gchar     *input_string,
                                       const gchar     *command_line_format,
                                       ...)
{
  va_list var_args;
  gchar *command_line;
  UDisksBaseJob *job;
  SpawnedJobSyncData data;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), FALSE);
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
  job = udisks_daemon_launch_spawned_job (daemon,
                                          object,
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
 * udisks_daemon_wait_for_object_sync:
 * @daemon: A #UDisksDaemon.
 * @wait_func: Function to check for desired object.
 * @user_data: User data to pass to @wait_func.
 * @user_data_free_func: (allow-none): Function to free @user_data or %NULL.
 * @timeout_seconds: Maximum time to wait for the object (in seconds) or 0 to never wait.
 * @error: (allow-none): Return location for error or %NULL.
 *
 * Blocks the calling thread until an object picked by @wait_func is
 * available or until @timeout_seconds has passed (in which case the
 * function fails with %UDISKS_ERROR_TIMED_OUT).
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

UDisksObject *
udisks_daemon_wait_for_object_sync (UDisksDaemon         *daemon,
                                    UDisksDaemonWaitFunc  wait_func,
                                    gpointer              user_data,
                                    GDestroyNotify        user_data_free_func,
                                    guint                 timeout_seconds,
                                    GError              **error)
{
  UDisksObject *ret;
  WaitData data;

  /* TODO: support GCancellable */

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
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
       *       probably going to involve having each UDisksProvider emit a "changed"
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
                       UDISKS_ERROR, UDISKS_ERROR_FAILED,
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
 * udisks_daemon_find_block:
 * @daemon: A #UDisksDaemon.
 * @block_device_number: A #dev_t with the device number to find.
 *
 * Finds a block device with the number given by @block_device_number.
 *
 * Returns: (transfer full): A #UDisksObject or %NULL if not found. Free with g_object_unref().
 */
UDisksObject *
udisks_daemon_find_block (UDisksDaemon *daemon,
                          dev_t         block_device_number)
{
  UDisksObject *ret = NULL;
  GList *objects, *l;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (daemon->object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (udisks_block_get_device_number (block) == block_device_number)
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
 * udisks_daemon_find_block_by_device_file:
 * @daemon: A #UDisksDaemon.
 * @device_file: A device file.
 *
 * Finds a block device with device file given by @device_file.
 *
 * Returns: (transfer full): A #UDisksObject or %NULL if not found. Free with g_object_unref().
 */
UDisksObject *
udisks_daemon_find_block_by_device_file (UDisksDaemon *daemon,
                                         const gchar  *device_file)
{
  UDisksObject *ret = NULL;
  GList *objects, *l;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (daemon->object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_device (block), device_file) == 0)
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
 * udisks_daemon_find_block_by_sysfs_path:
 * @daemon: A #UDisksDaemon.
 * @sysfs_path: A sysfs path.
 *
 * Finds a block device with a sysfs path given by @sysfs_path.
 *
 * Returns: (transfer full): A #UDisksObject or %NULL if not found. Free with g_object_unref().
 */
UDisksObject *
udisks_daemon_find_block_by_sysfs_path (UDisksDaemon *daemon,
                                        const gchar  *sysfs_path)
{
  UDisksObject *ret = NULL;
  GList *objects, *l;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (daemon->object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      GUdevDevice *device;

      if (!UDISKS_IS_LINUX_BLOCK_OBJECT (object))
        continue;

      device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));
      if (device == NULL)
        continue;

      if (g_strcmp0 (g_udev_device_get_sysfs_path (device), sysfs_path) == 0)
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
 * udisks_daemon_find_object:
 * @daemon: A #UDisksDaemon.
 * @object_path: A #dev_t with the device number to find.
 *
 * Finds an exported object with the object path given by @object_path.
 *
 * Returns: (transfer full): A #UDisksObject or %NULL if not found. Free with g_object_unref().
 */
UDisksObject *
udisks_daemon_find_object (UDisksDaemon         *daemon,
                           const gchar          *object_path)
{
  return (UDisksObject *) g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (daemon->object_manager),
                                                            object_path);
}

/**
 * udisks_daemon_get_objects:
 * @daemon: A #UDisksDaemon.
 *
 * Gets all D-Bus objects exported by @daemon.
 *
 * Returns: (transfer full) (element-type UDisksObject): A list of #UDisksObject instaces. The returned list should be freed with g_list_free() after each element has been freed with g_object_unref().
 */
GList *
udisks_daemon_get_objects (UDisksDaemon *daemon)
{
  return g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (daemon->object_manager));
}


/* ---------------------------------------------------------------------------------------------------- */

#if 0

/* ---------------------------------------------------------------------------------------------------- */

/*
 * The 'mounted-fs' persistent value has type a{sa{sv}} and is a dict
 * from mount_point (e.g. /media/smallfs) into a set of details. Known
 * details include
 *
 *   block-device-file: a byte-string with the block device that is mounted at the given location
 *   mounted-by-uid:    an uint32 with the uid of the user who mounted the device
 */

static gboolean
mounted_fs_entry_is_valid (UDisksDaemon   *daemon,
                           GVariant       *value)
{
  const gchar *mount_point;
  GVariant *details = NULL;
  GVariant *block_device_file_value = NULL;
  const gchar *block_device_file;
  gboolean ret;
  gchar *s;
  struct stat statbuf;
  GList *mounts;
  GList *l;
  gboolean found_mount;

  //udisks_daemon_log (daemon->daemon, UDISKS_LOG_LEVEL_DEBUG, "TODO: validate %s", g_variant_print (value, TRUE));

  ret = FALSE;
  g_variant_get (value,
                 "{&s@a{sv}}",
                 &mount_point,
                 &details);

  /* Don't consider entries being unmounted right now */
  if (g_hash_table_lookup (daemon->currently_unmounting, mount_point) != NULL)
    {
      ret = TRUE;
      goto out;
    }

  block_device_file_value = lookup_asv (details, "block-device-file");
  if (block_device_file_value == NULL)
    {
      s = g_variant_print (value, TRUE);
      udisks_info ("mounted-fs entry %s is invalid: no block-device-file key/value pair", s);
      g_free (s);
      goto out;
    }

  block_device_file = g_variant_get_bytestring (block_device_file_value);
  if (stat (block_device_file, &statbuf) != 0)
    {
      s = g_variant_print (value, TRUE);
      udisks_info ("mounted-fs entry %s is invalid: error statting block-device-file %s: %m",
                   s, block_device_file);
      g_free (s);
      goto out;
    }
  if (!S_ISBLK (statbuf.st_mode))
    {
      s = g_variant_print (value, TRUE);
      udisks_info ("mounted-fs entry %s is invalid: block-device-file %s is not a block device",
                   s, block_device_file);
      g_free (s);
      goto out;
    }

  found_mount = FALSE;
  mounts = udisks_mount_monitor_get_mounts_for_dev (daemon->mount_monitor, statbuf.st_rdev);
  for (l = mounts; l != NULL; l = l->next)
    {
      UDisksMount *mount = UDISKS_MOUNT (l->data);
      if (udisks_mount_get_mount_type (mount) == UDISKS_MOUNT_TYPE_FILESYSTEM &&
          g_strcmp0 (udisks_mount_get_mount_path (mount), mount_point) == 0)
        {
          found_mount = TRUE;
          break;
        }
    }
  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);

  if (!found_mount)
    {
      s = g_variant_print (value, TRUE);
      udisks_info ("mounted-fs entry %s is invalid: block-device-file %s is not mounted at %s",
                   s,
                   block_device_file,
                   mount_point);
      g_free (s);
      goto out;
    }

  /* OK, entry is valid */
  ret = TRUE;

 out:
  /* TODO:unmount (if still mounted) and clean up mount point if entry was invalid */
  if (!ret)
    {
      g_assert (g_str_has_prefix (mount_point, "/media"));
      /* but only if the directory actually exists (user might have manually cleaned it up etc.) */
      if (g_file_test (mount_point, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
        {
          if (g_rmdir (mount_point) != 0)
            {
              udisks_error ("Error cleaning up mount point %s: Error removing directory: %m",
                            mount_point);
            }
          else
            {
              udisks_notice ("Cleaned up mount point %s since %s is no longer mounted on it",
                             mount_point, block_device_file);
            }
        }
    }

  if (block_device_file_value != NULL)
    g_variant_unref (block_device_file_value);
  if (details != NULL)
    g_variant_unref (details);
  return ret;
}

/**
 * udisks_daemon_mounted_fs_cleanup:
 * @daemon: A #UDisksDaemon.
 *
 * Cleans up stale entries and mount points.
 */
void
udisks_daemon_mounted_fs_cleanup (UDisksDaemon *daemon)
{
  gboolean changed;
  GVariant *value;
  GVariant *new_value;
  GVariantBuilder builder;
  GError *error;

  udisks_debug ("Ensuring the mounted-fs file has no stale entries");

  changed = FALSE;

  /* load existing entries */
  error = NULL;
  value = udisks_persistent_store_get (daemon->persistent_store,
                                       UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
                                       "mounted-fs",
                                       G_VARIANT_TYPE ("a{sa{sv}}"),
                                       &error);
  if (error != NULL)
    {
      udisks_warning ("Error getting mounted-fs: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  /* include valid entries */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
  if (value != NULL)
    {
      GVariantIter iter;
      GVariant *child;
      g_variant_iter_init (&iter, value);
      while ((child = g_variant_iter_next_value (&iter)) != NULL)
        {
          if (mounted_fs_entry_is_valid (daemon, child))
            g_variant_builder_add_value (&builder, child);
          else
            changed = TRUE;
          g_variant_unref (child);
        }
      g_variant_unref (value);
    }

  new_value = g_variant_builder_end (&builder);

  /* save new entries */
  if (changed)
    {
      error = NULL;
      if (!udisks_persistent_store_set (daemon->persistent_store,
                                        UDISKS_PERSISTENT_FLAGS_NORMAL_STORE,
                                        "mounted-fs",
                                        G_VARIANT_TYPE ("a{sa{sv}}"),
                                        new_value, /* consumes new_value */
                                        &error))
        {
          udisks_warning ("Error setting mounted-fs: %s (%s, %d)",
                          error->message,
                          g_quark_to_string (error->domain),
                          error->code);
          g_error_free (error);
          goto out;
        }
    }
  else
    {
      g_variant_unref (new_value);
    }

 out:
  ;
}

#endif
