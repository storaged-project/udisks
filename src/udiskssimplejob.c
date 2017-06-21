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
#include "udiskssimplejob.h"
#include "udisks-daemon-marshal.h"
#include "udisksdaemon.h"

/**
 * SECTION:udiskssimplejob
 * @title: UDisksSimpleJob
 * @short_description: A simple job
 *
 * This type provides an implementation of the #UDisksJob interface
 * for simple jobs.
 */

typedef struct _UDisksSimpleJobClass   UDisksSimpleJobClass;

/**
 * UDisksSimpleJob:
 *
 * The #UDisksSimpleJob structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksSimpleJob
{
  UDisksBaseJob parent_instance;
};

struct _UDisksSimpleJobClass
{
  UDisksBaseJobClass parent_class;
};

static void job_iface_init (UDisksJobIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksSimpleJob, udisks_simple_job, UDISKS_TYPE_BASE_JOB,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_JOB, job_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_simple_job_init (UDisksSimpleJob *job)
{
}

static void
udisks_simple_job_class_init (UDisksSimpleJobClass *klass)
{
}

/**
 * udisks_simple_job_new:
 * @daemon: A #UDisksDaemon.
 * @cancellable: A #GCancellable or %NULL.
 *
 * Creates a new #UDisksSimpleJob instance.
 *
 * Call udisks_simple_job_complete() to compelte the returned job.
 *
 * Returns: A new #UDisksSimpleJob. Free with g_object_unref().
 */
UDisksSimpleJob *
udisks_simple_job_new (UDisksDaemon  *daemon,
                       GCancellable  *cancellable)
{
  /* g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL); */
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  return UDISKS_SIMPLE_JOB (g_object_new (UDISKS_TYPE_SIMPLE_JOB,
                                          "daemon", daemon,
                                          "cancellable", cancellable,
                                          NULL));
}

/**
 * udisks_simple_job_complete:
 * @job: A #UDisksSimpleJob.
 * @succeess: Whether the job succeeded.
 * @message: An error message or %NULL.
 *
 * Completes @job.
 */
void
udisks_simple_job_complete (UDisksSimpleJob     *job,
                            gboolean             success,
                            const gchar         *message)
{
  g_return_if_fail (UDISKS_IS_SIMPLE_JOB (job));
  udisks_job_emit_completed (UDISKS_JOB (job), success, (message) ? message : "");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
job_iface_init (UDisksJobIface *iface)
{
  /* For Cancel(), just use the implementation from our super class (UDisksBaseJob) */
  /* iface->handle_cancel   = handle_cancel; */
}
