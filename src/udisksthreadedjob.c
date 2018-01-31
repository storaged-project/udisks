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

#include <sys/types.h>
#include <sys/wait.h>

#include "udisksbasejob.h"
#include "udisksthreadedjob.h"
#include "udisks-daemon-marshal.h"
#include "udisksdaemon.h"

/**
 * SECTION:udisksthreadedjob
 * @title: UDisksThreadedJob
 * @short_description: Job that runs in a thread
 *
 * This type provides an implementation of the #UDisksJob interface
 * for jobs that run in a thread.
 */

typedef struct _UDisksThreadedJobClass   UDisksThreadedJobClass;

/**
 * UDisksThreadedJob:
 *
 * The #UDisksThreadedJob structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksThreadedJob
{
  UDisksBaseJob parent_instance;

  UDisksThreadedJobFunc job_func;
  gpointer user_data;
  GDestroyNotify user_data_free_func;

  gboolean job_result;
  GError *job_error;
};

struct _UDisksThreadedJobClass
{
  UDisksBaseJobClass parent_class;

  gboolean (*threaded_job_completed) (UDisksThreadedJob  *job,
                                      gboolean            result,
                                      GError             *error);
};

static void job_iface_init (UDisksJobIface *iface);

enum
{
  PROP_0,
  PROP_JOB_FUNC,
  PROP_USER_DATA,
  PROP_USER_DATA_FREE_FUNC
};

enum
{
  THREADED_JOB_COMPLETED_SIGNAL,
  LAST_SIGNAL
};

static gulong signals[LAST_SIGNAL] = { 0 };

static gboolean udisks_threaded_job_threaded_job_completed_default (UDisksThreadedJob  *job,
                                                                    gboolean            result,
                                                                    GError             *error);

G_DEFINE_TYPE_WITH_CODE (UDisksThreadedJob, udisks_threaded_job, UDISKS_TYPE_BASE_JOB,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_JOB, job_iface_init));

static void
udisks_threaded_job_finalize (GObject *object)
{
  UDisksThreadedJob *job = UDISKS_THREADED_JOB (object);

  if (job->job_error != NULL)
    g_clear_error (&(job->job_error));

  if (job->user_data_free_func != NULL)
    job->user_data_free_func (job->user_data);

  if (G_OBJECT_CLASS (udisks_threaded_job_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_threaded_job_parent_class)->finalize (object);
}

static void
udisks_threaded_job_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  UDisksThreadedJob *job = UDISKS_THREADED_JOB (object);

  switch (prop_id)
    {
    case PROP_JOB_FUNC:
      g_value_set_pointer (value, job->job_func);
      break;

    case PROP_USER_DATA:
      g_value_set_pointer (value, job->user_data);
      break;

    case PROP_USER_DATA_FREE_FUNC:
      g_value_set_pointer (value, job->user_data_free_func);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_threaded_job_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  UDisksThreadedJob *job = UDISKS_THREADED_JOB (object);

  switch (prop_id)
    {
    case PROP_JOB_FUNC:
      g_assert (job->job_func == NULL);
      job->job_func = g_value_get_pointer (value);
      break;

    case PROP_USER_DATA:
      g_assert (job->user_data == NULL);
      job->user_data = g_value_get_pointer (value);
      break;

    case PROP_USER_DATA_FREE_FUNC:
      g_assert (job->user_data_free_func == NULL);
      job->user_data_free_func = g_value_get_pointer (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
job_complete (gpointer user_data)
{
  UDisksThreadedJob *job = UDISKS_THREADED_JOB (user_data);
  gboolean ret;

  /* take a reference so it's safe for a signal-handler to release the last one */
  g_object_ref (job);
  g_signal_emit (job,
                 signals[THREADED_JOB_COMPLETED_SIGNAL],
                 0,
                 job->job_result,
                 job->job_error,
                 &ret);
  g_object_unref (job);
  return FALSE;
}

static void
run_task_job (GTask            *task,
              gpointer          source_object,
              gpointer          task_data,
              GCancellable     *cancellable)
{
  UDisksThreadedJob *job = UDISKS_THREADED_JOB (task_data);

  g_assert (!job->job_result);
  g_assert_no_error (job->job_error);

  if (!g_cancellable_set_error_if_cancelled (cancellable, &job->job_error))
    {
      job->job_result = job->job_func (job,
                                       cancellable,
                                       job->user_data,
                                       &job->job_error);
    }

  g_main_context_invoke (g_task_get_context (task), job_complete, job);
}

static void
udisks_threaded_job_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_threaded_job_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_threaded_job_parent_class)->constructed (object);

  g_assert (g_thread_supported ());
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_threaded_job_init (UDisksThreadedJob *job)
{
}

static void
udisks_threaded_job_class_init (UDisksThreadedJobClass *klass)
{
  GObjectClass *gobject_class;

  klass->threaded_job_completed = udisks_threaded_job_threaded_job_completed_default;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_threaded_job_finalize;
  gobject_class->constructed  = udisks_threaded_job_constructed;
  gobject_class->set_property = udisks_threaded_job_set_property;
  gobject_class->get_property = udisks_threaded_job_get_property;

  /**
   * UDisksThreadedJob:job-func:
   *
   * The #UDisksThreadedJobFunc to use.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_JOB_FUNC,
                                   g_param_spec_pointer ("job-func",
                                                         "Job Function",
                                                         "The Job Function",
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_STRINGS));

  /**
   * UDisksThreadedJob:user-data:
   *
   * User data for the #UDisksThreadedJobFunc.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_USER_DATA,
                                   g_param_spec_pointer ("user-data",
                                                         "Job Function's user data",
                                                         "The Job Function user data",
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_STRINGS));

  /**
   * UDisksThreadedJob:user-data-free-func:
   *
   * Free function for user data for the #UDisksThreadedJobFunc.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_USER_DATA_FREE_FUNC,
                                   g_param_spec_pointer ("user-data-free-func",
                                                         "Job Function's user data free function",
                                                         "The Job Function user data free function",
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_STRINGS));

  /**
   * UDisksThreadedJob::threaded-job-completed:
   * @job: The #UDisksThreadedJob emitting the signal.
   * @result: The #gboolean returned by the #UDisksThreadedJobFunc.
   * @error: The #GError set by the #UDisksThreadedJobFunc.
   *
   * Emitted when the threaded job is complete.
   *
   * The default implementation simply emits the #UDisksJob::completed
   * signal with @success set to %TRUE if, and only if, @error is
   * %NULL. Otherwise, @message on that signal is set to a string
   * describing @error. You can avoid the default implementation by
   * returning %TRUE from your signal handler.
   *
   * This signal is emitted in the
   * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
   * of the thread that @job was created in.
   *
   * Returns: %TRUE if the signal was handled, %FALSE to let other
   * handlers run.
   */
  signals[THREADED_JOB_COMPLETED_SIGNAL] =
    g_signal_new ("threaded-job-completed",
                  UDISKS_TYPE_THREADED_JOB,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (UDisksThreadedJobClass, threaded_job_completed),
                  g_signal_accumulator_true_handled,
                  NULL,
                  udisks_daemon_marshal_BOOLEAN__BOOLEAN_BOXED,
                  G_TYPE_BOOLEAN,
                  2,
                  G_TYPE_BOOLEAN,
                  G_TYPE_ERROR);
}

/**
 * udisks_threaded_job_new:
 * @job_func: The function to run in another thread.
 * @user_data: User data to pass to @job_func.
 * @user_data_free_func: Function to free @user_data with or %NULL.
 * @daemon: A #UDisksDaemon.
 * @cancellable: A #GCancellable or %NULL.
 *
 * Creates a new #UDisksThreadedJob instance.
 *
 * The job is not started automatically! Use udisks_threaded_job_start() to
 * start the job after #UDisksThreadedJob::threaded-job-completed or
 * #UDisksJob::completed signals are connected (to get notified when the job is
 * done). This is to prevent a race condition with the @job_func finishing
 * before the signals are connected in which case the signal handlers are never
 * triggered.
 *
 * Returns: A new #UDisksThreadedJob. Free with g_object_unref().
 */
UDisksThreadedJob *
udisks_threaded_job_new (UDisksThreadedJobFunc  job_func,
                         gpointer               user_data,
                         GDestroyNotify         user_data_free_func,
                         UDisksDaemon          *daemon,
                         GCancellable          *cancellable)
{
  /* g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL); */
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  return UDISKS_THREADED_JOB (g_object_new (UDISKS_TYPE_THREADED_JOB,
                                            "job-func", job_func,
                                            "user-data", user_data,
                                            "user-data-free-func", user_data_free_func,
                                            "daemon", daemon,
                                            "cancellable", cancellable,
                                            NULL));
}

/**
 * udisks_threaded_job_start:
 * @job: the job to start
 *
 * Start the @job. Connect to the #UDisksThreadedJob::threaded-job-completed or
 * #UDisksJob::completed signals to get notified when the job is done.
 *
 * */
void udisks_threaded_job_start (UDisksThreadedJob *job) {
  GTask *task;

  task = g_task_new (NULL,
                     udisks_base_job_get_cancellable (UDISKS_BASE_JOB (job)),
                     NULL,
                     NULL);

  g_task_set_task_data (task, job, NULL);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_run_in_thread (task, run_task_job);
  g_object_unref (task);
}

/**
 * udisks_threaded_job_get_user_data:
 * @job: A #UDisksThreadedJob.
 *
 * Gets the @user_data parameter that @job was constructed with.
 *
 * Returns: A #gpointer owned by @job.
 */
gpointer
udisks_threaded_job_get_user_data (UDisksThreadedJob *job)
{
  g_return_val_if_fail (UDISKS_IS_THREADED_JOB (job), NULL);
  return job->user_data;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
job_iface_init (UDisksJobIface *iface)
{
  /* For Cancel(), just use the implementation from our super class (UDisksBaseJob) */
  /* iface->handle_cancel   = handle_cancel; */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
udisks_threaded_job_threaded_job_completed_default (UDisksThreadedJob  *job,
                                                    gboolean            result,
                                                    GError            *error)
{
  if (result)
    {
      udisks_job_emit_completed (UDISKS_JOB (job),
                                 TRUE,
                                 "");
    }
  else
    {
      GString *message;

      g_assert (error != NULL);

      message = g_string_new (NULL);
      g_string_append_printf (message,
                              "Threaded job failed with error: %s (%s, %d)",
                              error->message,
                              g_quark_to_string (error->domain),
                              error->code);
      udisks_job_emit_completed (UDISKS_JOB (job),
                                 FALSE,
                                 message->str);
      g_string_free (message, TRUE);
    }

  return TRUE;
}

