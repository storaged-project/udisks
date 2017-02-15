/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <david@fubar.dk>
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <string.h>

#include <udisksdaemontypes.h>
#include <udisksdaemon.h>
#include <udisksspawnedjob.h>
#include <udisksthreadedjob.h>

#include "testutil.h"

static GMainLoop *loop;
static GThread *main_thread;
static char* last_failure_message;

/* ---------------------------------------------------------------------------------------------------- */

static void
on_completed_expect_success (UDisksJob   *object,
                             gboolean     success,
                             const gchar *message,
                             gpointer     user_data)
{
  g_assert (g_thread_self () == main_thread);
  g_assert (success);
}

static void
on_completed_expect_failure (UDisksJob   *object,
                             gboolean     success,
                             const gchar *message,
                             gpointer     user_data)
{
  const gchar *expected_message = user_data;
  g_assert (g_thread_self () == main_thread);
  if (expected_message != NULL)
    g_assert_cmpstr (message, ==, expected_message);
  g_free (last_failure_message);
  last_failure_message = g_strdup (message);
  g_assert (!success);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_successful (void)
{
  UDisksSpawnedJob *job;

  job = udisks_spawned_job_new ("/bin/true", NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_success), NULL);
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_failure (void)
{
  UDisksSpawnedJob *job;

  job = udisks_spawned_job_new ("/bin/false", NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             (gpointer) "Command-line `/bin/false' exited with non-zero exit status 1: ");
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_missing_program (void)
{
  UDisksSpawnedJob *job;

  job = udisks_spawned_job_new ("/path/to/unknown/file", NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure), NULL);
  /* different GLib versions have different quoting style, be liberal */
  g_assert (strstr (last_failure_message, "Error spawning command-line"));
  g_assert (strstr (last_failure_message, "Failed to execute child process"));
  g_assert (strstr (last_failure_message, "/path/to/unknown/file"));
  g_assert (strstr (last_failure_message, "No such file or directory"));
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_cancelled_at_start (void)
{
  UDisksSpawnedJob *job;
  GCancellable *cancellable;

  cancellable = g_cancellable_new ();
  g_cancellable_cancel (cancellable);
  job = udisks_spawned_job_new ("/bin/true", NULL, getuid (), geteuid (), NULL, cancellable);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             (gpointer) "Operation was cancelled (g-io-error-quark, 19)");
  g_object_unref (job);
  g_object_unref (cancellable);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_timeout (gpointer user_data)
{
  GCancellable *cancellable = G_CANCELLABLE (user_data);
  g_cancellable_cancel (cancellable);
  g_main_loop_quit (loop);
  return FALSE;
}

static void
test_spawned_job_cancelled_midway (void)
{
  UDisksSpawnedJob *job;
  GCancellable *cancellable;

  cancellable = g_cancellable_new ();
  job = udisks_spawned_job_new ("/bin/sleep 0.5", NULL, getuid (), geteuid (), NULL, cancellable);
  udisks_spawned_job_start (job);
  g_timeout_add (10, on_timeout, cancellable); /* 10 msec */
  g_main_loop_run (loop);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             (gpointer) "Operation was cancelled (g-io-error-quark, 19)");
  g_object_unref (job);
  g_object_unref (cancellable);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_spawned_job_completed (UDisksSpawnedJob *job,
                          GError           *error,
                          gint              status,
                          GString          *standard_output,
                          GString          *standard_error,
                          gpointer          user_data)
{
  gboolean *handler_ran = user_data;
  g_assert_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT);
  g_assert (!*handler_ran);
  *handler_ran = TRUE;
  return FALSE; /* allow other handlers to run (otherwise _g_assert_signal_received() will not work) */
}

static void
test_spawned_job_override_signal_handler (void)
{
  UDisksSpawnedJob *job;
  gboolean handler_ran;

  job = udisks_spawned_job_new ("/path/to/unknown/file", NULL, getuid (), geteuid (), NULL, NULL /* GCancellable */);
  udisks_spawned_job_start (job);
  handler_ran = FALSE;
  g_signal_connect (job, "spawned-job-completed", G_CALLBACK (on_spawned_job_completed), &handler_ran);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure), NULL);
  /* different GLib versions have different quoting style, be liberal */
  g_assert (strstr (last_failure_message, "Error spawning command-line"));
  g_assert (strstr (last_failure_message, "Failed to execute child process"));
  g_assert (strstr (last_failure_message, "/path/to/unknown/file"));
  g_assert (strstr (last_failure_message, "No such file or directory"));
  g_assert (handler_ran);
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_premature_termination (void)
{
  UDisksSpawnedJob *job;

  job = udisks_spawned_job_new ("/bin/sleep 1000", NULL, getuid (), geteuid (), NULL, NULL /* GCancellable */);
  udisks_spawned_job_start (job);
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
read_stdout_on_spawned_job_completed (UDisksSpawnedJob *job,
                                      GError           *error,
                                      gint              status,
                                      GString          *standard_output,
                                      GString          *standard_error,
                                      gpointer          user_data)
{
  g_assert_no_error (error);
  g_assert_cmpstr (standard_output->str, ==,
                   "Hello Stdout\n"
                   "Line 2\n");
  g_assert_cmpstr (standard_error->str, ==, "");
  g_assert (WIFEXITED (status));
  g_assert (WEXITSTATUS (status) == 0);
  return FALSE;
}

static void
test_spawned_job_read_stdout (void)
{
  UDisksSpawnedJob *job;
  gchar *s;

  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 0");
  job = udisks_spawned_job_new (s, NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "spawned-job-completed", G_CALLBACK (read_stdout_on_spawned_job_completed), NULL);
  g_object_unref (job);
  g_free (s);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
read_stderr_on_spawned_job_completed (UDisksSpawnedJob *job,
                                      GError           *error,
                                      gint              status,
                                      GString          *standard_output,
                                      GString          *standard_error,
                                      gpointer          user_data)
{
  g_assert_no_error (error);
  g_assert_cmpstr (standard_output->str, ==, "");
  g_assert_cmpstr (standard_error->str, ==,
                   "Hello Stderr\n"
                   "Line 2\n");
  g_assert (WIFEXITED (status));
  g_assert (WEXITSTATUS (status) == 0);
  return FALSE;
}

static void
test_spawned_job_read_stderr (void)
{
  UDisksSpawnedJob *job;
  gchar *s;

  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 1");
  job = udisks_spawned_job_new (s, NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "spawned-job-completed", G_CALLBACK (read_stderr_on_spawned_job_completed), NULL);
  g_object_unref (job);
  g_free (s);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
exit_status_on_spawned_job_completed (UDisksSpawnedJob *job,
                                      GError           *error,
                                      gint              status,
                                      GString          *standard_output,
                                      GString          *standard_error,
                                      gpointer          user_data)
{
  g_assert_no_error (error);
  g_assert_cmpstr (standard_output->str, ==, "");
  g_assert_cmpstr (standard_error->str, ==, "");
  g_assert (WIFEXITED (status));
  g_assert (WEXITSTATUS (status) == GPOINTER_TO_INT (user_data));
  return FALSE;
}

static void
test_spawned_job_exit_status (void)
{
  UDisksSpawnedJob *job;
  gchar *s;

  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 2");
  job = udisks_spawned_job_new (s, NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "spawned-job-completed", G_CALLBACK (exit_status_on_spawned_job_completed),
                             GINT_TO_POINTER (1));
  g_object_unref (job);
  g_free (s);

  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 3");
  job = udisks_spawned_job_new (s, NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "spawned-job-completed", G_CALLBACK (exit_status_on_spawned_job_completed),
                             GINT_TO_POINTER (2));
  g_object_unref (job);
  g_free (s);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_abnormal_termination (void)
{
  UDisksSpawnedJob *job;
  gchar *s;

  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 4");
  job = udisks_spawned_job_new (s, NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             (gpointer) "Command-line `./udisks-test-helper 4' was signaled with signal SIGSEGV (11): "
                             "OK, deliberately causing a segfault\n");
  g_object_unref (job);
  g_free (s);

  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 5");
  job = udisks_spawned_job_new (s, NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             (gpointer) "Command-line `./udisks-test-helper 5' was signaled with signal SIGABRT (6): "
                             "OK, deliberately abort()'ing\n");
  g_object_unref (job);
  g_free (s);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
binary_output_on_spawned_job_completed (UDisksSpawnedJob *job,
                                        GError           *error,
                                        gint              status,
                                        GString          *standard_output,
                                        GString          *standard_error,
                                        gpointer          user_data)
{
  guint n;

  g_assert_no_error (error);
  g_assert_cmpstr (standard_error->str, ==, "");
  g_assert (WIFEXITED (status));
  g_assert (WEXITSTATUS (status) == 0);

  g_assert_cmpint (standard_output->len, ==, 200);
  for (n = 0; n < 100; n++)
    {
      g_assert_cmpint (standard_output->str[n*2+0], ==, n);
      g_assert_cmpint (standard_output->str[n*2+1], ==, 0);
    }
  return FALSE;
}

static void
test_spawned_job_binary_output (void)
{
  UDisksSpawnedJob *job;
  gchar *s;

  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 6");
  job = udisks_spawned_job_new (s, NULL, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "spawned-job-completed", G_CALLBACK (binary_output_on_spawned_job_completed), NULL);
  g_object_unref (job);
  g_free (s);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
input_string_on_spawned_job_completed (UDisksSpawnedJob *job,
                                       GError           *error,
                                       gint              status,
                                       GString          *standard_output,
                                       GString          *standard_error,
                                       gpointer          user_data)
{
  g_assert_no_error (error);
  g_assert_cmpstr (standard_error->str, ==, "");
  g_assert (WIFEXITED (status));
  g_assert (WEXITSTATUS (status) == 0);
  g_assert_cmpstr (standard_output->str, ==, "Woah, you said `foobar', partner!\n");
  return FALSE;
}

static void
test_spawned_job_input_string (void)
{
  UDisksSpawnedJob *job;
  gchar *s;
  GString *input;

  input = g_string_new ("foobar");
  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 7");
  job = udisks_spawned_job_new (s, input, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "spawned-job-completed", G_CALLBACK (input_string_on_spawned_job_completed), NULL);
  g_object_unref (job);
  g_free (s);
  g_string_free (input, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
binary_input_string_on_spawned_job_completed (UDisksSpawnedJob *job,
                                              GError           *error,
                                              gint              status,
                                              GString          *standard_output,
                                              GString          *standard_error,
                                              gpointer          user_data)
{
  g_assert_no_error (error);
  g_assert_cmpstr (standard_error->str, ==, "");
  g_assert (WIFEXITED (status));
  g_assert (WEXITSTATUS (status) == 0);
  g_assert_cmpstr (standard_output->str, ==, "Woah, you said `affe00affe', partner!\n");
  return FALSE;
}

static void
test_spawned_job_binary_input_string (void)
{
  UDisksSpawnedJob *job;
  gchar *s;
  GString *input;

  input = g_string_new_len ("\xaf\xfe\0\xaf\xfe", 5);
  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 8");
  job = udisks_spawned_job_new (s, input, getuid (), geteuid (), NULL, NULL);
  udisks_spawned_job_start (job);
  _g_assert_signal_received (job, "spawned-job-completed", G_CALLBACK (binary_input_string_on_spawned_job_completed), NULL);
  g_object_unref (job);
  g_free (s);
  g_string_free (input, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
threaded_job_successful_func (UDisksThreadedJob   *job,
                              GCancellable        *cancellable,
                              gpointer             user_data,
                              GError             **error)
{
  g_assert (g_thread_self () != main_thread);
  return TRUE;
}

static void
test_threaded_job_successful (void)
{
  UDisksThreadedJob *job;

  job = udisks_threaded_job_new (threaded_job_successful_func, NULL, NULL, NULL, NULL);
  udisks_threaded_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_success), NULL);
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
threaded_job_failure_func (UDisksThreadedJob   *job,
                           GCancellable        *cancellable,
                           gpointer             user_data,
                           GError             **error)
{
  g_assert (g_thread_self () != main_thread);
  g_set_error (error,
               G_KEY_FILE_ERROR,
               G_KEY_FILE_ERROR_INVALID_VALUE,
               "some error");
  return FALSE;
}

static void
test_threaded_job_failure (void)
{
  UDisksThreadedJob *job;

  job = udisks_threaded_job_new (threaded_job_failure_func, NULL, NULL, NULL, NULL);
  udisks_threaded_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             (gpointer) "Threaded job failed with error: some error (g-key-file-error-quark, 5)");
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_threaded_job_cancelled_at_start (void)
{
  UDisksThreadedJob *job;
  GCancellable *cancellable;

  cancellable = g_cancellable_new ();
  g_cancellable_cancel (cancellable);
  job = udisks_threaded_job_new (threaded_job_successful_func, NULL, NULL, NULL, cancellable);
  udisks_threaded_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             (gpointer) "Threaded job failed with error: Operation was cancelled (g-io-error-quark, 19)");
  g_object_unref (job);
  g_object_unref (cancellable);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
threaded_job_sleep_until_cancelled (UDisksThreadedJob   *job,
                                    GCancellable        *cancellable,
                                    gpointer             user_data,
                                    GError             **error)
{
  gint *count = user_data;

  /* could probably do this a lot more elegantly... */
  while (TRUE)
    {
      *count += 1;
      if (g_cancellable_set_error_if_cancelled (cancellable, error))
        {
          break;
        }
      g_usleep (G_USEC_PER_SEC / 100);
    }
  return FALSE;
}

static void
test_threaded_job_cancelled_midway (void)
{
  UDisksThreadedJob *job;
  GCancellable *cancellable;
  gint count;

  cancellable = g_cancellable_new ();
  count = 0;
  job = udisks_threaded_job_new (threaded_job_sleep_until_cancelled, &count, NULL, NULL, cancellable);
  g_timeout_add (10, on_timeout, cancellable); /* 10 msec */
  udisks_threaded_job_start (job);
  g_main_loop_run (loop);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             (gpointer) "Threaded job failed with error: Operation was cancelled (g-io-error-quark, 19)");
  g_assert_cmpint (count, >, 0);
  g_object_unref (job);
  g_object_unref (cancellable);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_threaded_job_completed (UDisksThreadedJob  *job,
                           gboolean            result,
                           GError             *error,
                           gpointer            user_data)
{
  gboolean *handler_ran = user_data;
  g_assert (g_thread_self () == main_thread);
  g_assert (!result);
  g_assert_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE);
  g_assert (!*handler_ran);
  *handler_ran = TRUE;
  return FALSE; /* allow other handlers to run (otherwise _g_assert_signal_received() will not work) */
}

static void
test_threaded_job_override_signal_handler (void)
{
  UDisksThreadedJob *job;
  gboolean handler_ran;

  job = udisks_threaded_job_new (threaded_job_failure_func, NULL, NULL, NULL, NULL);
  handler_ran = FALSE;
  g_signal_connect (job, "threaded-job-completed", G_CALLBACK (on_threaded_job_completed), &handler_ran);
  udisks_threaded_job_start (job);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             (gpointer) "Threaded job failed with error: some error (g-key-file-error-quark, 5)");
  g_assert (handler_ran);
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int    argc,
      char **argv)
{
  int ret;

  /* Acquire the main context for this thread.  No main loop is running; this
   * avoids a race condition which can occur when calling
   * g_main_context_invoke ().
   */
  GMainContext *context = g_main_context_ref_thread_default ();
  g_main_context_acquire (context);

  g_test_init (&argc, &argv, NULL);

  loop = g_main_loop_new (context, FALSE);
  main_thread = g_thread_self ();

  g_test_add_func ("/udisks/daemon/spawned_job/successful", test_spawned_job_successful);
  g_test_add_func ("/udisks/daemon/spawned_job/failure", test_spawned_job_failure);
  g_test_add_func ("/udisks/daemon/spawned_job/missing_program", test_spawned_job_missing_program);
  g_test_add_func ("/udisks/daemon/spawned_job/cancelled_at_start", test_spawned_job_cancelled_at_start);
  g_test_add_func ("/udisks/daemon/spawned_job/cancelled_midway", test_spawned_job_cancelled_midway);
  g_test_add_func ("/udisks/daemon/spawned_job/override_signal_handler", test_spawned_job_override_signal_handler);
  g_test_add_func ("/udisks/daemon/spawned_job/premature_termination", test_spawned_job_premature_termination);
  g_test_add_func ("/udisks/daemon/spawned_job/read_stdout", test_spawned_job_read_stdout);
  g_test_add_func ("/udisks/daemon/spawned_job/read_stderr", test_spawned_job_read_stderr);
  g_test_add_func ("/udisks/daemon/spawned_job/exit_status", test_spawned_job_exit_status);
  g_test_add_func ("/udisks/daemon/spawned_job/abnormal_termination", test_spawned_job_abnormal_termination);
  g_test_add_func ("/udisks/daemon/spawned_job/binary_output", test_spawned_job_binary_output);
  g_test_add_func ("/udisks/daemon/spawned_job/input_string", test_spawned_job_input_string);
  g_test_add_func ("/udisks/daemon/spawned_job/binary_input_string", test_spawned_job_binary_input_string);
  g_test_add_func ("/udisks/daemon/threaded_job/successful", test_threaded_job_successful);
  g_test_add_func ("/udisks/daemon/threaded_job/failure", test_threaded_job_failure);
  g_test_add_func ("/udisks/daemon/threaded_job/cancelled_at_start", test_threaded_job_cancelled_at_start);
  g_test_add_func ("/udisks/daemon/threaded_job/cancelled_midway", test_threaded_job_cancelled_midway);
  g_test_add_func ("/udisks/daemon/threaded_job/override_signal_handler", test_threaded_job_override_signal_handler);

  ret = g_test_run();

  /* Release the thread's main context. */
  g_main_context_release (context);
  g_main_context_unref (context);

  g_main_loop_unref (loop);
  return ret;
}
