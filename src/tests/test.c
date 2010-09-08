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

#include <types.h>
#include <udisksdaemon.h>
#include <udisksspawnedjob.h>

#include "testutil.h"

static GMainLoop *loop;

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
  g_assert_cmpstr (message, ==, expected_message);
  g_assert (!success);
}

static gboolean
on_timeout (gpointer user_data)
{
  GCancellable *cancellable = G_CANCELLABLE (user_data);
  g_cancellable_cancel (cancellable);
  g_main_loop_quit (loop);
  return FALSE;
}

static gboolean
on_spawned_job_completed (UDisksSpawnedJob *job,
                          GError           *error,
                          gint              status,
                          gchar            *standard_output,
                          gchar            *standard_error,
                          gpointer          user_data)
{
  gboolean *handler_ran = user_data;
  g_assert_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT);
  g_assert (!*handler_ran);
  *handler_ran = TRUE;
  return FALSE; /* allow other handlers to run (otherwise _g_assert_signal_received() will not work) */
}

static void
test_spawned_job (void)
{
  UDisksSpawnedJob *job;
  GCancellable *cancellable;
  gboolean handler_ran;

  /* successful run */
  job = udisks_spawned_job_new ("/bin/true", NULL);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_success), NULL);
  g_object_unref (job);

  /* failed but completed launch */
  job = udisks_spawned_job_new ("/bin/false", NULL);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Command-line `/bin/false' exited with non-zero exit code 1\n"
                             "\n"
                             "stdout: `'\n"
                             "\n"
                             "stderr: `'\n");
  g_object_unref (job);

  /* missing program */
  job = udisks_spawned_job_new ("/path/to/unknown/file", NULL);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Failed to execute command-line `/path/to/unknown/file': Error spawning command-line `/path/to/unknown/file': Failed to execute child process \"/path/to/unknown/file\" (No such file or directory) (g-exec-error-quark, 8)");
  g_object_unref (job);

  /* cancelled at start */
  cancellable = g_cancellable_new ();
  g_cancellable_cancel (cancellable);
  job = udisks_spawned_job_new ("/bin/true", cancellable);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Failed to execute command-line `/bin/true': Operation was cancelled (g-io-error-quark, 19)");
  g_object_unref (job);
  g_object_unref (cancellable);

  /* cancelled in the middle */
  cancellable = g_cancellable_new ();
  job = udisks_spawned_job_new ("/bin/sleep 0.5", cancellable);
  g_timeout_add (10, on_timeout, cancellable); /* 10 msec */
  g_main_loop_run (loop);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Failed to execute command-line `/bin/sleep 0.5': Operation was cancelled (g-io-error-quark, 19)");
  g_object_unref (job);
  g_object_unref (cancellable);

  /* check we can override ::spawned-job-completed */
  job = udisks_spawned_job_new ("/path/to/unknown/file", NULL /* GCancellable */);
  handler_ran = FALSE;
  g_signal_connect (job, "spawned-job-completed", G_CALLBACK (on_spawned_job_completed), &handler_ran);
  _g_assert_signal_received (job, "completed", G_CALLBACK (on_completed_expect_failure),
                             "Failed to execute command-line `/path/to/unknown/file': Error spawning command-line `/path/to/unknown/file': Failed to execute child process \"/path/to/unknown/file\" (No such file or directory) (g-exec-error-quark, 8)");
  g_assert (handler_ran);
  g_object_unref (job);

  /* premature termination */
  job = udisks_spawned_job_new ("/bin/sleep 1000", NULL /* GCancellable */);
  g_object_unref (job);
}

int
main (int    argc,
      char **argv)
{
  int ret;

  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  g_test_add_func ("/udisks/daemon/spawned_job", test_spawned_job);

  ret = g_test_run();

  g_main_loop_unref (loop);
  return ret;
}
