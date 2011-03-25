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

#include <syslog.h>

#include <glib/gstdio.h>

#include "udisksdaemon.h"
#include "udisksprovider.h"
#include "udiskslinuxprovider.h"
#include "udisksfstabprovider.h"
#include "udisksiscsiprovider.h"
#include "udisksmountmonitor.h"
#include "udiskspersistentstore.h"
#include "udisksbasejob.h"
#include "udisksspawnedjob.h"
#include "udisksthreadedjob.h"
#include "udiskssimplejob.h"

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
  UDisksFstabProvider *fstab_provider;
  UDisksIScsiProvider *iscsi_provider;

  PolkitAuthority *authority;
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
  PROP_MOUNT_MONITOR
};

G_DEFINE_TYPE (UDisksDaemon, udisks_daemon, G_TYPE_OBJECT);

static void
udisks_daemon_finalize (GObject *object)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);

  g_object_unref (daemon->authority);
  g_object_unref (daemon->persistent_store);
  g_object_unref (daemon->fstab_provider);
  g_object_unref (daemon->iscsi_provider);
  g_object_unref (daemon->object_manager);
  g_object_unref (daemon->linux_provider);
  g_object_unref (daemon->mount_monitor);
  g_object_unref (daemon->connection);

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
  udisks_persistent_store_mounted_fs_cleanup (daemon->persistent_store);
}

#define TMP_STATEDIR "/dev/.udisks2"

static void
udisks_daemon_constructed (GObject *object)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);
  GError *error;

  error = NULL;
  daemon->authority = polkit_authority_get_sync (NULL, &error);
  if (daemon->authority == NULL)
    {
      g_warning ("Error initializing PolicyKit authority: %s (%s, %d)",
                 error->message,
                 g_quark_to_string (error->domain),
                 error->code);
      g_error_free (error);
    }

  daemon->object_manager = g_dbus_object_manager_server_new (daemon->connection, "/org/freedesktop/UDisks2");

  if (!g_file_test (TMP_STATEDIR, G_FILE_TEST_IS_DIR))
    {
      if (g_mkdir (TMP_STATEDIR, 0700) != 0)
        {
          g_warning ("Error creating directory %s: %m", TMP_STATEDIR);
        }
    }

  daemon->mount_monitor = udisks_mount_monitor_new ();

  daemon->persistent_store = udisks_persistent_store_new (daemon,
                                                          PACKAGE_LOCALSTATE_DIR "/lib/udisks2",
                                                          TMP_STATEDIR);

  /* Cleanup stale mount points
   * - Because we might have mounted something at /media/EOS_DIGITAL and then
   *   power was removed and then there's a stale mount point at reboot
   *
   * Also do this every time something is mounted
   * - Because the user may manually do 'umount /dev/sdb1' as root
   */
  udisks_persistent_store_mounted_fs_cleanup (daemon->persistent_store);
  g_signal_connect (daemon->mount_monitor,
                    "mount-removed",
                    G_CALLBACK (mount_monitor_on_mount_removed),
                    daemon);

  /* now add providers */
  daemon->linux_provider = udisks_linux_provider_new (daemon);
  daemon->fstab_provider = udisks_fstab_provider_new (daemon);
  daemon->iscsi_provider = udisks_iscsi_provider_new (daemon);

  /* need to add iSCSI targets before LUN and block devices */
  udisks_provider_start (UDISKS_PROVIDER (daemon->iscsi_provider));
  udisks_provider_start (UDISKS_PROVIDER (daemon->linux_provider));
  udisks_provider_start (UDISKS_PROVIDER (daemon->fstab_provider));

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
 * udisks_daemon_get_fstab_provider:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the /etc/fstab Provider, if any.
 *
 * Returns: A #UDisksFstabProvider or %NULL. Do not free, the object is owned by @daemon.
 */
UDisksFstabProvider *
udisks_daemon_get_fstab_provider (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->fstab_provider;
}

/**
 * udisks_daemon_get_iscsi_provider:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the iSCSI target provider, if any.
 *
 * Returns: A #UDisksIScsiProvider or %NULL. Do not free, the object is owned by @daemon.
 */
UDisksIScsiProvider *
udisks_daemon_get_iscsi_provider (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->iscsi_provider;
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
 * Returns: A #PolkitAuthority instance. Do not free, the object is owned by @daemon.
 */
PolkitAuthority *
udisks_daemon_get_authority (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->authority;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_job_completed (UDisksJob    *job,
                  gboolean      success,
                  const gchar  *message,
                  gpointer      user_data)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (user_data);
  GDBusObjectStub *object;

  object = G_DBUS_OBJECT_STUB (g_dbus_interface_get_object (G_DBUS_INTERFACE (job)));
  g_assert (object != NULL);

  /* Unexport job */
  g_dbus_object_manager_server_unexport (daemon->object_manager,
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  g_dbus_object_stub_remove_interface (object, G_DBUS_INTERFACE_STUB (job));
  g_object_unref (object);

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
 * #UDisksJob::completed signal is emitted on the object.
 *
 * Returns: A #UDisksSimpleJob object. Do not free, the object
 * belongs to @manager.
 */
UDisksBaseJob *
udisks_daemon_launch_simple_job (UDisksDaemon    *daemon,
                                 GCancellable    *cancellable)
{
  UDisksSimpleJob *job;
  GDBusObjectStub *object;
  gchar *object_path;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);

  job = udisks_simple_job_new (cancellable);

  /* TODO: protect job_id by a mutex */
  object_path = g_strdup_printf ("/org/freedesktop/UDisks2/jobs/%d", job_id++);
  object = g_dbus_object_stub_new (object_path);
  g_dbus_object_stub_add_interface (object, G_DBUS_INTERFACE_STUB (job));
  g_free (object_path);

  g_dbus_object_manager_server_export (daemon->object_manager, object);
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
 * #UDisksJob::completed signal is emitted on the object.
 *
 * Returns: A #UDisksThreadedJob object. Do not free, the object
 * belongs to @manager.
 */
UDisksBaseJob *
udisks_daemon_launch_threaded_job  (UDisksDaemon    *daemon,
                                    UDisksThreadedJobFunc job_func,
                                    gpointer         user_data,
                                    GDestroyNotify   user_data_free_func,
                                    GCancellable    *cancellable)
{
  UDisksThreadedJob *job;
  GDBusObjectStub *object;
  gchar *object_path;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (job_func != NULL, NULL);

  job = udisks_threaded_job_new (job_func,
                                 user_data,
                                 user_data_free_func,
                                 cancellable);

  /* TODO: protect job_id by a mutex */
  object_path = g_strdup_printf ("/org/freedesktop/UDisks2/jobs/%d", job_id++);
  object = g_dbus_object_stub_new (object_path);
  g_dbus_object_stub_add_interface (object, G_DBUS_INTERFACE_STUB (job));
  g_free (object_path);

  g_dbus_object_manager_server_export (daemon->object_manager, object);
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
 * @cancellable: A #GCancellable or %NULL.
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
 * #UDisksJob::completed signal is emitted on the object.
 *
 * Returns: A #UDisksSpawnedJob object. Do not free, the object
 * belongs to @manager.
 */
UDisksBaseJob *
udisks_daemon_launch_spawned_job (UDisksDaemon    *daemon,
                                  GCancellable    *cancellable,
                                  const gchar     *input_string,
                                  const gchar     *command_line_format,
                                  ...)
{
  va_list var_args;
  gchar *command_line;
  UDisksSpawnedJob *job;
  GDBusObjectStub *object;
  gchar *object_path;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (command_line_format != NULL, NULL);

  va_start (var_args, command_line_format);
  command_line = g_strdup_vprintf (command_line_format, var_args);
  va_end (var_args);
  job = udisks_spawned_job_new (command_line, input_string, cancellable);
  g_free (command_line);

  /* TODO: protect job_id by a mutex */
  object_path = g_strdup_printf ("/org/freedesktop/UDisks2/jobs/%d", job_id++);
  object = g_dbus_object_stub_new (object_path);
  g_dbus_object_stub_add_interface (object, G_DBUS_INTERFACE_STUB (job));
  g_free (object_path);

  g_dbus_object_manager_server_export (daemon->object_manager, object);
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
  gchar *message;
} SpawnedJobSyncData;

static void
spawned_job_sync_on_job_completed (UDisksJob    *job,
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
 * @cancellable: A #GCancellable or %NULL.
 * @input_string: A string to write to stdin of the spawned program or %NULL.
 * @out_message: Return location for the @message parameter of the #UDisksJob::completed signal.
 * @command_line_format: printf()-style format for the command line to spawn.
 * @...: Arguments for @command_line_format.
 *
 * Like udisks_daemon_launch_spawned_job() but blocks the calling
 * thread until the job completes.
 *
 * Returns: The @success parameter of the #UDisksJob::completed.
 */
gboolean
udisks_daemon_launch_spawned_job_sync (UDisksDaemon    *daemon,
                                       GCancellable    *cancellable,
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
  data.message = NULL;

  va_start (var_args, command_line_format);
  command_line = g_strdup_vprintf (command_line_format, var_args);
  va_end (var_args);
  job = udisks_daemon_launch_spawned_job (daemon,
                                          cancellable,
                                          input_string,
                                          "%s",
                                          command_line);
  g_signal_connect_after (job,
                          "completed",
                          G_CALLBACK (spawned_job_sync_on_job_completed),
                          &data);

  g_main_loop_run (data.loop);

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

typedef enum
{
  _COLOR_RESET,
  _COLOR_BOLD_ON,
  _COLOR_INVERSE_ON,
  _COLOR_BOLD_OFF,
  _COLOR_FG_BLACK,
  _COLOR_FG_RED,
  _COLOR_FG_GREEN,
  _COLOR_FG_YELLOW,
  _COLOR_FG_BLUE,
  _COLOR_FG_MAGENTA,
  _COLOR_FG_CYAN,
  _COLOR_FG_WHITE,
  _COLOR_BG_RED,
  _COLOR_BG_GREEN,
  _COLOR_BG_YELLOW,
  _COLOR_BG_BLUE,
  _COLOR_BG_MAGENTA,
  _COLOR_BG_CYAN,
  _COLOR_BG_WHITE
} _Color;

static gboolean _color_stdin_is_tty = FALSE;
static gboolean _color_initialized = FALSE;

static void
_color_init (void)
{
  if (_color_initialized)
    return;
  _color_initialized = TRUE;
  _color_stdin_is_tty = (isatty (STDIN_FILENO) != 0 && isatty (STDOUT_FILENO) != 0);
}

static const gchar *
_color_get (_Color color)
{
  const gchar *str;

  _color_init ();

  if (!_color_stdin_is_tty)
    return "";

  str = NULL;
  switch (color)
    {
    case _COLOR_RESET:      str="\x1b[0m"; break;
    case _COLOR_BOLD_ON:    str="\x1b[1m"; break;
    case _COLOR_INVERSE_ON: str="\x1b[7m"; break;
    case _COLOR_BOLD_OFF:   str="\x1b[22m"; break;
    case _COLOR_FG_BLACK:   str="\x1b[30m"; break;
    case _COLOR_FG_RED:     str="\x1b[31m"; break;
    case _COLOR_FG_GREEN:   str="\x1b[32m"; break;
    case _COLOR_FG_YELLOW:  str="\x1b[33m"; break;
    case _COLOR_FG_BLUE:    str="\x1b[34m"; break;
    case _COLOR_FG_MAGENTA: str="\x1b[35m"; break;
    case _COLOR_FG_CYAN:    str="\x1b[36m"; break;
    case _COLOR_FG_WHITE:   str="\x1b[37m"; break;
    case _COLOR_BG_RED:     str="\x1b[41m"; break;
    case _COLOR_BG_GREEN:   str="\x1b[42m"; break;
    case _COLOR_BG_YELLOW:  str="\x1b[43m"; break;
    case _COLOR_BG_BLUE:    str="\x1b[44m"; break;
    case _COLOR_BG_MAGENTA: str="\x1b[45m"; break;
    case _COLOR_BG_CYAN:    str="\x1b[46m"; break;
    case _COLOR_BG_WHITE:   str="\x1b[47m"; break;
    default:
      g_assert_not_reached ();
      break;
    }
  return str;
}

/* ---------------------------------------------------------------------------------------------------- */

G_LOCK_DEFINE_STATIC (log_lock);

static void
udisks_daemon_log_valist (UDisksDaemon    *daemon,
                          UDisksLogLevel   log_level,
                          const gchar     *format,
                          va_list          var_args)
{
  gchar *message;
  GTimeVal now;
  time_t now_time;
  struct tm *now_tm;
  gchar time_buf[128];
  const gchar *level_str;
  const gchar *level_color_str;
  gint syslog_priority;
  static gboolean have_called_openlog = FALSE;

  G_LOCK (log_lock);

  if (!have_called_openlog)
    {
      openlog ("udisksd",
               LOG_CONS|LOG_NDELAY|LOG_PID,
               LOG_DAEMON);
      have_called_openlog = TRUE;
    }


  message = g_strdup_vprintf (format, var_args);

  g_get_current_time (&now);
  now_time = (time_t) now.tv_sec;
  now_tm = localtime (&now_time);
  strftime (time_buf, sizeof time_buf, "%H:%M:%S", now_tm);

  switch (log_level)
    {
    case UDISKS_LOG_LEVEL_DEBUG:
      level_str = "[DEBUG]";
      syslog_priority = LOG_DEBUG;
      level_color_str = _color_get (_COLOR_FG_BLUE);
      break;

    case UDISKS_LOG_LEVEL_INFO:
      level_str = "[INFO]";
      syslog_priority = LOG_INFO;
      level_color_str = _color_get (_COLOR_FG_CYAN);
      break;

    case UDISKS_LOG_LEVEL_WARNING:
      level_str = "[WARNING]";
      syslog_priority = LOG_WARNING;
      level_color_str = _color_get (_COLOR_FG_YELLOW);
      break;

    case UDISKS_LOG_LEVEL_ERROR:
      level_str = "[ERROR]";
      syslog_priority = LOG_ERR;
      level_color_str = _color_get (_COLOR_FG_RED);
      break;

    default:
      g_assert_not_reached ();
      break;
    }
  g_print ("%s%s%s.%03d:%s %s%s%s:%s %s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_YELLOW),
           time_buf, (gint) now.tv_usec / 1000,
           _color_get (_COLOR_RESET),
           level_color_str,
           _color_get (_COLOR_BOLD_ON), level_str,
           _color_get (_COLOR_RESET),
           message);
  syslog (syslog_priority, "%s", message);
  g_free (message);

  G_UNLOCK (log_lock);
}

/**
 * udisks_daemon_log:
 * @daemon: A #UDisksDaemon.
 * @log_level: A log level from the #UDisksLogLevel enumeration.
 * @format: printf()-style format string.
 * @...: Arguments for @format.
 *
 * Logging method.
 */
void
udisks_daemon_log (UDisksDaemon    *daemon,
                   UDisksLogLevel   log_level,
                   const gchar     *format,
                   ...)
{
  va_list var_args;
  va_start (var_args, format);
  udisks_daemon_log_valist (daemon, log_level, format, var_args);
  va_end (var_args);
}
