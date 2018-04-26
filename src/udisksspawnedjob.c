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

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>

#include "udisksbasejob.h"
#include "udisksspawnedjob.h"
#include "udisks-daemon-marshal.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udisksspawnedjob
 * @title: UDisksSpawnedJob
 * @short_description: Job that spawns a command
 *
 * This type provides an implementation of the #UDisksJob interface
 * for jobs that are implemented by spawning a command line.
 */

typedef struct _UDisksSpawnedJobClass   UDisksSpawnedJobClass;

/**
 * UDisksSpawnedJob:
 *
 * The #UDisksSpawnedJob structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksSpawnedJob
{
  UDisksBaseJob parent_instance;

  gchar *command_line;
  gulong cancellable_handler_id;

  GMainContext *main_context;

  GString *input_string;

  uid_t run_as_uid;
  uid_t run_as_euid;
  gid_t real_egid;
  gid_t real_gid;
  uid_t real_uid;
  char *real_pwname;
  const gchar *input_string_cursor;

  GPid child_pid;
  gint child_stdin_fd;
  gint child_stdout_fd;
  gint child_stderr_fd;

  GIOChannel *child_stdin_channel;
  GIOChannel *child_stdout_channel;
  GIOChannel *child_stderr_channel;

  GSource *child_watch_source;
  GSource *child_stdin_source;
  GSource *child_stdout_source;
  GSource *child_stderr_source;

  GString *child_stdout;
  GString *child_stderr;
};

struct _UDisksSpawnedJobClass
{
  UDisksBaseJobClass parent_class;

  gboolean (*spawned_job_completed) (UDisksSpawnedJob  *job,
                                     GError            *error,
                                     gint               status,
                                     GString           *standard_output,
                                     GString           *standard_error);
};

static void job_iface_init (UDisksJobIface *iface);

enum
{
  PROP_0,
  PROP_COMMAND_LINE,
  PROP_INPUT_STRING,
  PROP_RUN_AS_UID,
  PROP_RUN_AS_EUID
};

enum
{
  SPAWNED_JOB_COMPLETED_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static gboolean udisks_spawned_job_spawned_job_completed_default (UDisksSpawnedJob  *job,
                                                                  GError            *error,
                                                                  gint               status,
                                                                  GString           *standard_output,
                                                                  GString           *standard_error);

static void udisks_spawned_job_release_resources (UDisksSpawnedJob *job);

G_DEFINE_TYPE_WITH_CODE (UDisksSpawnedJob, udisks_spawned_job, UDISKS_TYPE_BASE_JOB,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_JOB, job_iface_init));

typedef GString AutowipeBuffer;
static GType autowipe_buffer_get_type (void);
static void autowipe_buffer_free (gpointer data);
static gpointer autowipe_buffer_copy (gpointer data);

G_DEFINE_BOXED_TYPE (AutowipeBuffer, autowipe_buffer,
                     autowipe_buffer_copy,
                     autowipe_buffer_free);

static void
udisks_spawned_job_finalize (GObject *object)
{
  UDisksSpawnedJob *job = UDISKS_SPAWNED_JOB (object);

  udisks_spawned_job_release_resources (job);

  if (job->main_context != NULL)
    g_main_context_unref (job->main_context);

  g_free (job->command_line);

  if (job->input_string != NULL)
    g_boxed_free (autowipe_buffer_get_type (), (gpointer) job->input_string);

  if (G_OBJECT_CLASS (udisks_spawned_job_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_spawned_job_parent_class)->finalize (object);
}

static void
udisks_spawned_job_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  UDisksSpawnedJob *job = UDISKS_SPAWNED_JOB (object);

  switch (prop_id)
    {
    case PROP_COMMAND_LINE:
      g_value_set_string (value, udisks_spawned_job_get_command_line (job));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_spawned_job_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  UDisksSpawnedJob *job = UDISKS_SPAWNED_JOB (object);

  switch (prop_id)
    {
    case PROP_COMMAND_LINE:
      g_assert (job->command_line == NULL);
      job->command_line = g_value_dup_string (value);
      break;

    case PROP_INPUT_STRING:
      g_assert (job->input_string == NULL);
      job->input_string = (GString*) g_value_dup_boxed (value);
      if (job->input_string != NULL)
        {
          job->input_string_cursor = job->input_string->str;
        }
      break;

    case PROP_RUN_AS_UID:
      job->run_as_uid = g_value_get_uint (value);
      break;

    case PROP_RUN_AS_EUID:
      job->run_as_euid = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksSpawnedJob *job;
  GError *error;
} EmitCompletedData;

static gboolean
emit_completed_with_error_in_idle_cb (gpointer user_data)
{
  EmitCompletedData *data = user_data;
  gboolean ret;

  g_signal_emit (data->job,
                 signals[SPAWNED_JOB_COMPLETED_SIGNAL],
                 0,
                 data->error,
                 0,                        /* status */
                 data->job->child_stdout,  /* standard_output */
                 data->job->child_stderr,  /* standard_error */
                 &ret);
  g_object_unref (data->job);
  g_clear_error (&(data->error));
  g_free (data);
  return FALSE;
}

static void
emit_completed_with_error_in_idle (UDisksSpawnedJob *job,
                                   GError           *error)
{
  EmitCompletedData *data;
  GSource *idle_source;

  g_return_if_fail (UDISKS_IS_SPAWNED_JOB (job));
  g_return_if_fail (error != NULL);

  data = g_new0 (EmitCompletedData, 1);
  data->job = g_object_ref (job);
  data->error = g_error_copy (error);
  idle_source = g_idle_source_new ();
  g_source_set_priority (idle_source, G_PRIORITY_DEFAULT);
  g_source_set_callback (idle_source,
                         emit_completed_with_error_in_idle_cb,
                         data,
                         NULL);
  g_source_attach (idle_source, job->main_context);
  g_source_unref (idle_source);
}

/* called in the thread where @cancellable was cancelled */
static void
on_cancelled (GCancellable *cancellable,
              gpointer      user_data)
{
  UDisksSpawnedJob *job = UDISKS_SPAWNED_JOB (user_data);
  GError *error;

  error = NULL;
  g_warn_if_fail (g_cancellable_set_error_if_cancelled (cancellable, &error));
  emit_completed_with_error_in_idle (job, error);
  g_clear_error (&error);
}

static gboolean
read_child_stderr (GIOChannel *channel,
                   GIOCondition condition,
                   gpointer user_data)
{
  UDisksSpawnedJob *job = UDISKS_SPAWNED_JOB (user_data);
  gchar buf[1024];
  gsize bytes_read;

  g_io_channel_read_chars (channel, buf, sizeof buf, &bytes_read, NULL);
  g_string_append_len (job->child_stderr, buf, bytes_read);
  return TRUE;
}

static gboolean
read_child_stdout (GIOChannel *channel,
                   GIOCondition condition,
                   gpointer user_data)
{
  UDisksSpawnedJob *job = UDISKS_SPAWNED_JOB (user_data);
  gchar buf[1024];
  gsize bytes_read;

  g_io_channel_read_chars (channel, buf, sizeof buf, &bytes_read, NULL);
  g_string_append_len (job->child_stdout, buf, bytes_read);
  return TRUE;
}

static gboolean
write_child_stdin (GIOChannel *channel,
                   GIOCondition condition,
                   gpointer user_data)
{
  UDisksSpawnedJob *job = UDISKS_SPAWNED_JOB (user_data);
  gsize bytes_written;
  gsize bytes_to_write = 0;

  if (job->input_string != NULL)
    {
      bytes_to_write = job->input_string->len - (job->input_string_cursor - job->input_string->str);
    }

  if (job->input_string_cursor == NULL || bytes_to_write == 0)
    {
      /* nothing left to write; close our end so the child will get EOF */
      g_io_channel_unref (job->child_stdin_channel);
      g_source_destroy (job->child_stdin_source);
      g_warn_if_fail (close (job->child_stdin_fd) == 0);
      job->child_stdin_channel = NULL;
      job->child_stdin_source = NULL;
      job->child_stdin_fd = -1;
      return FALSE;
    }

  g_io_channel_write_chars (channel,
                            job->input_string_cursor,
                            bytes_to_write,
                            &bytes_written,
                            NULL);
  g_io_channel_flush (channel, NULL);
  job->input_string_cursor += bytes_written;

  /* keep writing */
  return TRUE;
}

static void
child_watch_cb (GPid     pid,
                gint     status,
                gpointer user_data)
{
  UDisksSpawnedJob *job = UDISKS_SPAWNED_JOB (user_data);
  gchar *buf;
  gsize buf_size;
  gboolean ret;

  if (g_io_channel_read_to_end (job->child_stdout_channel, &buf, &buf_size, NULL) == G_IO_STATUS_NORMAL)
    {
      g_string_append_len (job->child_stdout, buf, buf_size);
      g_free (buf);
    }
  if (g_io_channel_read_to_end (job->child_stderr_channel, &buf, &buf_size, NULL) == G_IO_STATUS_NORMAL)
    {
      g_string_append_len (job->child_stderr, buf, buf_size);
      g_free (buf);
    }

  //g_debug ("helper(pid %5d): completed with exit code %d\n", job->child_pid, WEXITSTATUS (status));

  /* take a reference so it's safe for a signal-handler to release the last one */
  g_object_ref (job);
  g_signal_emit (job,
                 signals[SPAWNED_JOB_COMPLETED_SIGNAL],
                 0,
                 NULL, /* GError */
                 status,
                 job->child_stdout,
                 job->child_stderr,
                 &ret);
  job->child_pid = 0;
  job->child_watch_source = NULL;
  udisks_spawned_job_release_resources (job);
  g_object_unref (job);
}

/* careful, this is in the fork()'ed child so all utility threads etc are not available */
static void
child_setup (gpointer user_data)
{
  UDisksSpawnedJob *job = UDISKS_SPAWNED_JOB (user_data);

  if (job->run_as_uid == getuid () && job->run_as_euid == geteuid ())
    goto out;

  /* become the user...
   *
   * TODO: this might need to involve running the whole PAM 'session'
   * stack as done by e.g. pkexec(1) and various login managers
   * otherwise things like the SELinux context might not be entirely
   * right. What we really need is some library function to
   * impersonate a pid or uid. What a mess.
   */
  if (setgroups (0, NULL) != 0)
    {
      g_printerr ("Error resetting groups: %m\n");
      abort ();
    }
  if (initgroups (job->real_pwname, job->real_gid) != 0)
    {
      g_printerr ("Error initializing groups for user %s and group %d: %m\n",
                  job->real_pwname, (gint) job->real_gid);
      abort ();
    }
  if (setregid (job->real_gid, job->real_egid) != 0)
    {
      g_printerr ("Error setting real+effective gid %d and %d: %m\n",
                  (gint) job->real_gid, (gint) job->real_egid);
      abort ();
    }
  if (setreuid (job->real_uid, job->run_as_euid) != 0)
    {
      g_printerr ("Error setting real+effective uid %d and %d: %m\n",
                  (gint) job->real_uid, (gint) job->run_as_euid);
      abort ();
    }

 out:
  ;
}

static void
udisks_spawned_job_init (UDisksSpawnedJob *job)
{
  job->child_stdout = g_string_new (NULL);
  job->child_stderr = g_string_new (NULL);
  job->child_stdin_fd = -1;
  job->child_stdout_fd = -1;
  job->child_stderr_fd = -1;
}

static void
udisks_spawned_job_class_init (UDisksSpawnedJobClass *klass)
{
  GObjectClass *gobject_class;

  klass->spawned_job_completed = udisks_spawned_job_spawned_job_completed_default;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_spawned_job_finalize;
  gobject_class->set_property = udisks_spawned_job_set_property;
  gobject_class->get_property = udisks_spawned_job_get_property;

  /**
   * UDisksSpawnedJob:command-line:
   *
   * The command-line to run.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_COMMAND_LINE,
                                   g_param_spec_string ("command-line",
                                                        "Command Line",
                                                        "The command-line to run",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksSpawnedJob:input-string:
   *
   * String that will be written to stdin of the spawned program or
   * %NULL to not write anything.
   *
   * This is passed as autowipe_buffer (rather than G_TYPE_GSTRING) to nuke
   * the contents after usage since the input string may contain key material.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_INPUT_STRING,
                                   g_param_spec_boxed  ("input-string",
                                                        "Input String",
                                                        "String to write to stdin of the spawned program",
                                                        autowipe_buffer_get_type (),
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksSpawnedJob:run-as-uid:
   *
   * The #uid_t to run the program as.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_RUN_AS_UID,
                                   g_param_spec_uint ("run-as-uid",
                                                      "Run As",
                                                      "The uid_t to run the program as",
                                                      0, G_MAXUINT, 0,
                                                      G_PARAM_WRITABLE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  /**
   * UDisksSpawnedJob:run-as-euid:
   *
   * The effective #uid_t to run the program as.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_RUN_AS_EUID,
                                   g_param_spec_uint ("run-as-euid",
                                                      "Run As (effective)",
                                                      "The effective uid_t to run the program as",
                                                      0, G_MAXUINT, 0,
                                                      G_PARAM_WRITABLE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  /**
   * UDisksSpawnedJob::spawned-job-completed:
   * @job: The #UDisksSpawnedJob emitting the signal.
   * @error: %NULL if running the whole command line succeeded, otherwise a #GError that is set.
   * @status: The exit status of the command line that was run.
   * @standard_output: Standard output from the command line that was run.
   * @standard_error: Standard error output from the command line that was run.
   *
   * Emitted when the spawned job is complete. If spawning the command
   * failed or if the job was cancelled, @error will
   * non-%NULL. Otherwise you can use macros such as WIFEXITED() and
   * WEXITSTATUS() on the @status integer to obtain more information.
   *
   * The default implementation simply emits the #UDisksJob::completed
   * signal with @success set to %TRUE if, and only if, @error is
   * %NULL, WIFEXITED() evaluates to %TRUE and WEXITSTATUS() is
   * zero. Additionally, @message on that signal is set to
   * @standard_error regards of whether @success is %TRUE or %FALSE.
   *
   * You can avoid the default implementation by returning %TRUE from
   * your signal handler.
   *
   * This signal is emitted in the
   * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
   * of the thread that @job was created in.
   *
   * Returns: %TRUE if the signal was handled, %FALSE to let other
   * handlers run.
   */
  signals[SPAWNED_JOB_COMPLETED_SIGNAL] =
    g_signal_new ("spawned-job-completed",
                  UDISKS_TYPE_SPAWNED_JOB,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (UDisksSpawnedJobClass, spawned_job_completed),
                  g_signal_accumulator_true_handled,
                  NULL,
                  udisks_daemon_marshal_BOOLEAN__BOXED_INT_BOXED_BOXED,
                  G_TYPE_BOOLEAN,
                  4,
                  G_TYPE_ERROR,
                  G_TYPE_INT,
                  G_TYPE_GSTRING,
                  G_TYPE_GSTRING);
}

/**
 * udisks_spawned_job_new:
 * @command_line: The command line to run.
 * @input_string: A string to write to stdin of the spawned program or %NULL.
 * @run_as_uid: The #uid_t to run the program as.
 * @run_as_euid: The effective #uid_t to run the program as.
 * @daemon: A #UDisksDaemon.
 * @cancellable: A #GCancellable or %NULL.
 *
 * Creates a new #UDisksSpawnedJob instance.
 *
 * The job is not started automatically! Use udisks_spawned_job_start() to start
 * the job after #UDisksSpawnedJob::spawned-job-completed or
 * #UDisksJob::completed signals are connected (to get notified when the job is
 * done). This is to prevent a race condition with the spawned process
 * terminating before the signals are connected in which case the signal
 * handlers are never triggered.
 *
 * Returns: A new #UDisksSpawnedJob. Free with g_object_unref().
 */
UDisksSpawnedJob *
udisks_spawned_job_new (const gchar  *command_line,
                        GString      *input_string,
                        uid_t         run_as_uid,
                        uid_t         run_as_euid,
                        UDisksDaemon *daemon,
                        GCancellable *cancellable)
{
  g_return_val_if_fail (command_line != NULL, NULL);
  /* g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL); */
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  return UDISKS_SPAWNED_JOB (g_object_new (UDISKS_TYPE_SPAWNED_JOB,
                                           "command-line", command_line,
                                           "input-string", input_string,
                                           "run-as-uid", run_as_uid,
                                           "run-as-euid", run_as_euid,
                                           "daemon", daemon,
                                           "cancellable", cancellable,
                                           NULL));
}

/**
 * udisks_spawned_job_get_command_line:
 * @job: A #UDisksSpawnedJob.
 *
 * Gets the command line that @job was constructed with.
 *
 * Returns: A string owned by @job. Do not free.
 */
const gchar *
udisks_spawned_job_get_command_line (UDisksSpawnedJob *job)
{
  g_return_val_if_fail (UDISKS_IS_SPAWNED_JOB (job), NULL);
  return job->command_line;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
job_iface_init (UDisksJobIface *iface)
{
  /* For Cancel(), just use the implementation from our super class (UDisksBaseJob) */
  /* iface->handle_cancel   = handle_cancel; */
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_signal_name (gint signal_number)
{
  switch (signal_number)
    {
#define _HANDLE_SIG(sig) case sig: return #sig;
    _HANDLE_SIG (SIGHUP);
    _HANDLE_SIG (SIGINT);
    _HANDLE_SIG (SIGQUIT);
    _HANDLE_SIG (SIGILL);
    _HANDLE_SIG (SIGABRT);
    _HANDLE_SIG (SIGFPE);
    _HANDLE_SIG (SIGKILL);
    _HANDLE_SIG (SIGSEGV);
    _HANDLE_SIG (SIGPIPE);
    _HANDLE_SIG (SIGALRM);
    _HANDLE_SIG (SIGTERM);
    _HANDLE_SIG (SIGUSR1);
    _HANDLE_SIG (SIGUSR2);
    _HANDLE_SIG (SIGCHLD);
    _HANDLE_SIG (SIGCONT);
    _HANDLE_SIG (SIGSTOP);
    _HANDLE_SIG (SIGTSTP);
    _HANDLE_SIG (SIGTTIN);
    _HANDLE_SIG (SIGTTOU);
    _HANDLE_SIG (SIGBUS);
    _HANDLE_SIG (SIGPOLL);
    _HANDLE_SIG (SIGPROF);
    _HANDLE_SIG (SIGSYS);
    _HANDLE_SIG (SIGTRAP);
    _HANDLE_SIG (SIGURG);
    _HANDLE_SIG (SIGVTALRM);
    _HANDLE_SIG (SIGXCPU);
    _HANDLE_SIG (SIGXFSZ);
#undef _HANDLE_SIG
    default:
      break;
    }
  return "UNKNOWN_SIGNAL";
}

static gboolean
udisks_spawned_job_spawned_job_completed_default (UDisksSpawnedJob  *job,
                                                  GError            *error,
                                                  gint               status,
                                                  GString           *standard_output,
                                                  GString           *standard_error)
{
#if 0
  g_debug ("in udisks_spawned_job_spawned_job_completed_default()\n"
           " command_line `%s'\n"
           " error->message=`%s'\n"
           " status=%d (WIFEXITED=%d WEXITSTATUS=%d)\n"
           " standard_output=`%s' (%d bytes)\n"
           " standard_error=`%s' (%d bytes)\n",
           job->command_line,
           error != NULL ? error->message : "(error not set)",
           status,
           WIFEXITED (status), WEXITSTATUS (status),
           standard_output->str, (gint) standard_output->len,
           standard_error->str, (gint) standard_error->len);
#endif

  if (error != NULL)
    {
      gchar *message;
      message = g_strdup_printf ("%s (%s, %d)",
                                 error->message,
                                 g_quark_to_string (error->domain),
                                 error->code);
      udisks_job_emit_completed (UDISKS_JOB (job),
                                 FALSE,
                                 message);
      g_free (message);
    }
  else if (WIFEXITED (status) && WEXITSTATUS (status) == 0)
    {
      udisks_job_emit_completed (UDISKS_JOB (job),
                                 TRUE,
                                 standard_error->str);
    }
  else
    {
      GString *message;
      message = g_string_new (NULL);
      if (WIFEXITED (status))
        {
          g_string_append_printf (message,
                                  "Command-line `%s' exited with non-zero exit status %d:",
                                  job->command_line,
                                  WEXITSTATUS (status));
        }
      else if (WIFSIGNALED (status))
        {
          g_string_append_printf (message,
                                  "Command-line `%s' was signaled with signal %s (%d):",
                                  job->command_line,
                                  get_signal_name (WTERMSIG (status)),
                                  WTERMSIG (status));
        }
      if (standard_output->len > 0 && standard_error->len)
        {
          g_string_append_printf (message,
                                  "\n"
                                  "stdout: `%s'\n"
                                  "stderr: `%s'",
                                  standard_output->str,
                                  standard_error->str);
        }
      else if (standard_output->len > 0)
        {
          g_string_append_printf (message, " %s", standard_output->str);
        }
      else
        {
          g_string_append_printf (message, " %s", standard_error->str);
        }

      udisks_job_emit_completed (UDISKS_JOB (job),
                                 FALSE,
                                 message->str);
      g_string_free (message, TRUE);
    }
  return TRUE;
}

static void
child_watch_from_release_cb (GPid     pid,
                             gint     status,
                             gpointer user_data)
{
}

/* called when we're done running the command line */
static void
udisks_spawned_job_release_resources (UDisksSpawnedJob *job)
{
  /* Nuke the child, if necessary */
  if (job->child_watch_source != NULL)
    {
      g_source_destroy (job->child_watch_source);
      job->child_watch_source = NULL;
    }

  if (job->child_pid != 0)
    {
      GSource *source;

      //g_debug ("ugh, need to kill %d", (gint) job->child_pid);
      kill (job->child_pid, SIGTERM);

      /* OK, we need to reap for the child ourselves - we don't want
       * to use waitpid() because that might block the calling
       * thread (the child might handle SIGTERM and use several
       * seconds for cleanup/rollback).
       *
       * So we use GChildWatch instead.
       *
       * Note that we might be called from the finalizer so avoid
       * taking references to ourselves. We do need to pass the
       * GSource so we can nuke it once handled.
       */
      source = g_child_watch_source_new (job->child_pid);
#if __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
/* parameters of the callback depend on the source and can be different
 * from the required "generic" GSourceFunc, see:
 * https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#g-source-set-callback
 */
      g_source_set_callback (source,
                             (GSourceFunc) child_watch_from_release_cb,
                             source,
                             (GDestroyNotify) g_source_destroy);
#if __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif
      g_source_attach (source, job->main_context);
      g_source_unref (source);

      job->child_pid = 0;
    }

  if (job->child_stdout != NULL)
    {
      g_string_free (job->child_stdout, TRUE);
      job->child_stdout = NULL;
    }

  if (job->child_stderr != NULL)
    {
      g_string_free (job->child_stderr, TRUE);
      job->child_stderr = NULL;
    }

  if (job->child_stdin_channel != NULL)
    {
      g_io_channel_unref (job->child_stdin_channel);
      job->child_stdin_channel = NULL;
    }
  if (job->child_stdout_channel != NULL)
    {
      g_io_channel_unref (job->child_stdout_channel);
      job->child_stdout_channel = NULL;
    }
  if (job->child_stderr_channel != NULL)
    {
      g_io_channel_unref (job->child_stderr_channel);
      job->child_stderr_channel = NULL;
    }

  if (job->child_stdin_source != NULL)
    {
      g_source_destroy (job->child_stdin_source);
      job->child_stdin_source = NULL;
    }
  if (job->child_stdout_source != NULL)
    {
      g_source_destroy (job->child_stdout_source);
      job->child_stdout_source = NULL;
    }
  if (job->child_stderr_source != NULL)
    {
      g_source_destroy (job->child_stderr_source);
      job->child_stderr_source = NULL;
    }

  if (job->child_stdin_fd != -1)
    {
      g_warn_if_fail (close (job->child_stdin_fd) == 0);
      job->child_stdin_fd = -1;
    }
  if (job->child_stdout_fd != -1)
    {
      g_warn_if_fail (close (job->child_stdout_fd) == 0);
      job->child_stdout_fd = -1;
    }
  if (job->child_stderr_fd != -1)
    {
      g_warn_if_fail (close (job->child_stderr_fd) == 0);
      job->child_stderr_fd = -1;
    }

  if (job->cancellable_handler_id > 0)
    {
      g_cancellable_disconnect (udisks_base_job_get_cancellable (UDISKS_BASE_JOB (job)), job->cancellable_handler_id);
      job->cancellable_handler_id = 0;
    }

  if (job->real_pwname != NULL)
    {
      free (job->real_pwname);
      job->real_pwname = NULL;
    }
}

/**
 * udisks_spawned_job_start:
 * @job: the job to start
 *
 * Connect to the #UDisksSpawnedJob::spawned-job-completed or
 * #UDisksJob::completed signals to get notified when the job is done.
 *
 * */
void udisks_spawned_job_start (UDisksSpawnedJob *job)
{
  GError *error;
  gint child_argc;
  gchar **child_argv;
  struct passwd pwstruct;
  gchar pwbuf[8192];
  struct passwd *pw = NULL;
  int rc;

  job->main_context = g_main_context_get_thread_default ();
  if (job->main_context != NULL)
    g_main_context_ref (job->main_context);

  /* could already be cancelled */
  error = NULL;
  if (g_cancellable_set_error_if_cancelled (udisks_base_job_get_cancellable (UDISKS_BASE_JOB (job)), &error))
    {
      emit_completed_with_error_in_idle (job, error);
      g_clear_error (&error);
      goto out;
    }

  job->cancellable_handler_id = g_cancellable_connect (udisks_base_job_get_cancellable (UDISKS_BASE_JOB (job)),
                                                       G_CALLBACK (on_cancelled),
                                                       job,
                                                       NULL);

  error = NULL;
  if (!g_shell_parse_argv (job->command_line,
                           &child_argc,
                           &child_argv,
                           &error))
    {
      g_prefix_error (&error,
                      "Error parsing command-line `%s': ",
                      job->command_line);
      emit_completed_with_error_in_idle (job, error);
      g_clear_error (&error);
      goto out;
    }

  /* Save real egid and gid info for the child process */
  if (job->run_as_uid != getuid () || job->run_as_euid != geteuid ())
    {
      rc = getpwuid_r (job->run_as_euid, &pwstruct, pwbuf, sizeof pwbuf, &pw);
      if (rc != 0 || pw == NULL)
        {
          g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                      "No password record for uid %d: %m\n", (gint) job->run_as_euid);
          emit_completed_with_error_in_idle (job, error);
          g_clear_error (&error);
          goto out;
        }
      job->real_egid = pw->pw_gid;

      rc = getpwuid_r (job->run_as_uid, &pwstruct, pwbuf, sizeof pwbuf, &pw);
      if (rc != 0 || pw == NULL)
        {
          g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                      "No password record for uid %d: %m\n", (gint) job->run_as_uid);
          emit_completed_with_error_in_idle (job, error);
          g_clear_error (&error);
          goto out;
        }
      job->real_gid = pw->pw_gid;
      job->real_uid = pw->pw_uid;
      job->real_pwname = strdup (pw->pw_name);
    }

  error = NULL;
  if (!g_spawn_async_with_pipes (NULL, /* working directory */
                                 child_argv,
                                 NULL, /* envp */
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                 child_setup, /* child_setup */
                                 job, /* child_setup's user_data */
                                 &(job->child_pid),
                                 job->input_string != NULL ? &(job->child_stdin_fd) : NULL,
                                 &(job->child_stdout_fd),
                                 &(job->child_stderr_fd),
                                 &error))
    {
      g_prefix_error (&error,
                      "Error spawning command-line `%s': ",
                      job->command_line);
      emit_completed_with_error_in_idle (job, error);
      g_clear_error (&error);
      goto out;
    }

  job->child_watch_source = g_child_watch_source_new (job->child_pid);
#if __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
/* parameters of the callback depend on the source and can be different
 * from the required "generic" GSourceFunc, see:
 * https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#g-source-set-callback
 */
  g_source_set_callback (job->child_watch_source, (GSourceFunc) child_watch_cb, job, NULL);
#if __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif
  g_source_attach (job->child_watch_source, job->main_context);
  g_source_unref (job->child_watch_source);

  if (job->child_stdin_fd != -1)
    {
      if (job->input_string)
        job->input_string_cursor = job->input_string->str;

      job->child_stdin_channel = g_io_channel_unix_new (job->child_stdin_fd);
      // we want to write binary, suppress checking the encoding:
      g_io_channel_set_encoding (job->child_stdin_channel, NULL, NULL);
      g_io_channel_set_flags (job->child_stdin_channel, G_IO_FLAG_NONBLOCK, NULL);
      job->child_stdin_source = g_io_create_watch (job->child_stdin_channel, G_IO_OUT);
#if __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
/* parameters of the callback depend on the source and can be different
 * from the required "generic" GSourceFunc, see:
 * https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#g-source-set-callback
 */
      g_source_set_callback (job->child_stdin_source, (GSourceFunc) write_child_stdin, job, NULL);
#if __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif
      g_source_attach (job->child_stdin_source, job->main_context);
      g_source_unref (job->child_stdin_source);
    }

  job->child_stdout_channel = g_io_channel_unix_new (job->child_stdout_fd);
  g_io_channel_set_flags (job->child_stdout_channel, G_IO_FLAG_NONBLOCK, NULL);
  job->child_stdout_source = g_io_create_watch (job->child_stdout_channel, G_IO_IN);
#if __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
/* parameters of the callback depend on the source and can be different
 * from the required "generic" GSourceFunc, see:
 * https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#g-source-set-callback
 */
  g_source_set_callback (job->child_stdout_source, (GSourceFunc) read_child_stdout, job, NULL);
#if __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif
  g_source_attach (job->child_stdout_source, job->main_context);
  g_source_unref (job->child_stdout_source);

  job->child_stderr_channel = g_io_channel_unix_new (job->child_stderr_fd);
  g_io_channel_set_flags (job->child_stderr_channel, G_IO_FLAG_NONBLOCK, NULL);
  job->child_stderr_source = g_io_create_watch (job->child_stderr_channel, G_IO_IN);
#if __GNUC__ >= 8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
/* parameters of the callback depend on the source and can be different
 * from the required "generic" GSourceFunc, see:
 * https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html#g-source-set-callback
 */
  g_source_set_callback (job->child_stderr_source, (GSourceFunc) read_child_stderr, job, NULL);
#if __GNUC__ >= 8
#pragma GCC diagnostic pop
#endif
  g_source_attach (job->child_stderr_source, job->main_context);
  g_source_unref (job->child_stderr_source);

 out:
  ;
}

/* manage strings with potentially unsafe content */

static gpointer
autowipe_buffer_copy (gpointer data)
{
  GString *orig = (GString*) data;
  GString *copy = NULL;

  if (orig != NULL)
    {
      copy = g_string_new_len (orig->str, orig->len);
    }

  return (gpointer) copy;
}

static void
autowipe_buffer_free (gpointer data)
{
  GString *string = (GString*) data;
  udisks_string_wipe_and_free (string);
}
