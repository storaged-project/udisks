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

#include <sys/types.h>
#include <sys/wait.h>

#include <types.h>
#include <udisksdaemon.h>
#include <udisksspawnedjob.h>

#include "testutil.h"

static GMainLoop *loop;

/* ---------------------------------------------------------------------------------------------------- */

static void
on_completed_expect_success (UDisksJob   *object,
                             gboolean     success,
                             const gchar *message,
                             gpointer     user_data)
{
  g_assert (success);
}

static void
on_completed_expect_failure (UDisksJob   *object,
                             gboolean     success,
                             const gchar *message,
                             gpointer     user_data)
{
  const gchar *expected_message = user_data;
  if (expected_message != NULL)
    g_assert_cmpstr (message, ==, expected_message);
  g_assert (!success);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_successful (void)
{
  UDisksSpawnedJob *job;

  job = udisks_spawned_job_new ("/bin/true", NULL);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_success), NULL);
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_failure (void)
{
  UDisksSpawnedJob *job;

  job = udisks_spawned_job_new ("/bin/false", NULL);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Command-line `/bin/false' exited with non-zero exit status 1.\n"
                             "stdout: `'\n"
                             "\n"
                             "stderr: `'\n");
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_missing_program (void)
{
  UDisksSpawnedJob *job;

  job = udisks_spawned_job_new ("/path/to/unknown/file", NULL);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Failed to execute command-line `/path/to/unknown/file': Error spawning command-line `/path/to/unknown/file': Failed to execute child process \"/path/to/unknown/file\" (No such file or directory) (g-exec-error-quark, 8)");
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
  job = udisks_spawned_job_new ("/bin/true", cancellable);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Failed to execute command-line `/bin/true': Operation was cancelled (g-io-error-quark, 19)");
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
  job = udisks_spawned_job_new ("/bin/sleep 0.5", cancellable);
  g_timeout_add (10, on_timeout, cancellable); /* 10 msec */
  g_main_loop_run (loop);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Failed to execute command-line `/bin/sleep 0.5': Operation was cancelled (g-io-error-quark, 19)");
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

  job = udisks_spawned_job_new ("/path/to/unknown/file", NULL /* GCancellable */);
  handler_ran = FALSE;
  g_signal_connect (job, "spawned-job-completed", G_CALLBACK (on_spawned_job_completed), &handler_ran);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Failed to execute command-line `/path/to/unknown/file': Error spawning command-line `/path/to/unknown/file': Failed to execute child process \"/path/to/unknown/file\" (No such file or directory) (g-exec-error-quark, 8)");
  g_assert (handler_ran);
  g_object_unref (job);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
test_spawned_job_premature_termination (void)
{
  UDisksSpawnedJob *job;

  job = udisks_spawned_job_new ("/bin/sleep 1000", NULL /* GCancellable */);
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
  g_assert (error == NULL);
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
  job = udisks_spawned_job_new (s, NULL);
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
  g_assert (error == NULL);
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
  job = udisks_spawned_job_new (s, NULL);
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
  g_assert (error == NULL);
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
  job = udisks_spawned_job_new (s, NULL);
  _g_assert_signal_received (job, "spawned-job-completed", G_CALLBACK (exit_status_on_spawned_job_completed),
                             GINT_TO_POINTER (1));
  g_object_unref (job);
  g_free (s);

  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 3");
  job = udisks_spawned_job_new (s, NULL);
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
  job = udisks_spawned_job_new (s, NULL);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Command-line `./udisks-test-helper 4' was signaled with signal SIGSEGV (11).\n"
                             "stdout: `OK, deliberately causing a segfault\n"
                             "'\n"
                             "\n"
                             "stderr: `'\n");
  g_object_unref (job);
  g_free (s);

  s = g_strdup_printf (UDISKS_TEST_DIR "/udisks-test-helper 5");
  job = udisks_spawned_job_new (s, NULL);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Command-line `./udisks-test-helper 5' was signaled with signal SIGABRT (6).\n"
                             "stdout: `OK, deliberately abort()'ing\n"
                             "'\n"
                             "\n"
                             "stderr: `'\n");
  g_object_unref (job);
  g_free (s);
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int    argc,
      char **argv)
{
  int ret;

  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  loop = g_main_loop_new (NULL, FALSE);

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

  ret = g_test_run();

  g_main_loop_unref (loop);
  return ret;
}
