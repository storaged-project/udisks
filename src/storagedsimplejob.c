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

#include "storagedbasejob.h"
#include "storagedsimplejob.h"
#include "storaged-daemon-marshal.h"
#include "storageddaemon.h"

/**
 * SECTION:storagedsimplejob
 * @title: StoragedSimpleJob
 * @short_description: A simple job
 *
 * This type provides an implementation of the #StoragedJob interface
 * for simple jobs.
 */

typedef struct _StoragedSimpleJobClass   StoragedSimpleJobClass;

/**
 * StoragedSimpleJob:
 *
 * The #StoragedSimpleJob structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedSimpleJob
{
  StoragedBaseJob parent_instance;
};

struct _StoragedSimpleJobClass
{
  StoragedBaseJobClass parent_class;
};

static void job_iface_init (StoragedJobIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedSimpleJob, storaged_simple_job, STORAGED_TYPE_BASE_JOB,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_JOB, job_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_simple_job_init (StoragedSimpleJob *job)
{
}

static void
storaged_simple_job_class_init (StoragedSimpleJobClass *klass)
{
}

/**
 * storaged_simple_job_new:
 * @daemon: A #StoragedDaemon.
 * @cancellable: A #GCancellable or %NULL.
 *
 * Creates a new #StoragedSimpleJob instance.
 *
 * Call storaged_simple_job_complete() to compelte the returned job.
 *
 * Returns: A new #StoragedSimpleJob. Free with g_object_unref().
 */
StoragedSimpleJob *
storaged_simple_job_new (StoragedDaemon  *daemon,
                         GCancellable    *cancellable)
{
  /* g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL); */
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  return STORAGED_SIMPLE_JOB (g_object_new (STORAGED_TYPE_SIMPLE_JOB,
                                          "daemon", daemon,
                                          "cancellable", cancellable,
                                          NULL));
}

/**
 * storaged_simple_job_complete:
 * @job: A #StoragedSimpleJob.
 * @succeess: Whether the job succeeded.
 * @message: An error message or %NULL.
 *
 * Completes @job.
 */
void
storaged_simple_job_complete (StoragedSimpleJob     *job,
                              gboolean               success,
                              const gchar           *message)
{
  g_return_if_fail (STORAGED_IS_SIMPLE_JOB (job));
  storaged_job_emit_completed (STORAGED_JOB (job), success, message);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
job_iface_init (StoragedJobIface *iface)
{
  /* For Cancel(), just use the implementation from our super class (StoragedBaseJob) */
  /* iface->handle_cancel   = handle_cancel; */
}
