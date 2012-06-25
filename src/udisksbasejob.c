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
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisks-daemon-marshal.h"

/**
 * SECTION:udisksbasejob
 * @title: UDisksBaseJob
 * @short_description: Base class for jobs.
 *
 * This type provides common features needed by all job types.
 */

struct _UDisksBaseJobPrivate
{
  GCancellable *cancellable;
  UDisksDaemon *daemon;
};

static void job_iface_init (UDisksJobIface *iface);

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_CANCELLABLE,
};

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (UDisksBaseJob, udisks_base_job, UDISKS_TYPE_JOB_SKELETON,
                                  G_IMPLEMENT_INTERFACE (UDISKS_TYPE_JOB, job_iface_init));

static void
udisks_base_job_finalize (GObject *object)
{
  UDisksBaseJob *job = UDISKS_BASE_JOB (object);

  if (job->priv->cancellable != NULL)
    {
      g_object_unref (job->priv->cancellable);
      job->priv->cancellable = NULL;
    }

  if (G_OBJECT_CLASS (udisks_base_job_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_base_job_parent_class)->finalize (object);
}

static void
udisks_base_job_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  UDisksBaseJob *job = UDISKS_BASE_JOB (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_base_job_get_daemon (job));
      break;

    case PROP_CANCELLABLE:
      g_value_set_object (value, job->priv->cancellable);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_base_job_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  UDisksBaseJob *job = UDISKS_BASE_JOB (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (job->priv->daemon == NULL);
      /* we don't take a reference to the daemon */
      job->priv->daemon = g_value_get_object (value);
      break;

    case PROP_CANCELLABLE:
      g_assert (job->priv->cancellable == NULL);
      job->priv->cancellable = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_base_job_constructed (GObject *object)
{
  UDisksBaseJob *job = UDISKS_BASE_JOB (object);

  if (job->priv->cancellable == NULL)
    job->priv->cancellable = g_cancellable_new ();

  if (G_OBJECT_CLASS (udisks_base_job_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_base_job_parent_class)->constructed (object);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_base_job_init (UDisksBaseJob *job)
{
  gint64 now_usec;

  job->priv = G_TYPE_INSTANCE_GET_PRIVATE (job, UDISKS_TYPE_BASE_JOB, UDisksBaseJobPrivate);

  now_usec = g_get_real_time ();
  udisks_job_set_start_time (UDISKS_JOB (job), now_usec / G_USEC_PER_SEC);
}

static void
udisks_base_job_class_init (UDisksBaseJobClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_base_job_finalize;
  gobject_class->constructed  = udisks_base_job_constructed;
  gobject_class->set_property = udisks_base_job_set_property;
  gobject_class->get_property = udisks_base_job_get_property;

  /**
   * UDisksBaseJob:daemon:
   *
   * The #UDisksDaemon the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the object is for",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksBaseJob:cancellable:
   *
   * The #GCancellable to use.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CANCELLABLE,
                                   g_param_spec_object ("cancellable",
                                                        "Cancellable",
                                                        "The GCancellable to use",
                                                        G_TYPE_CANCELLABLE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (klass, sizeof (UDisksBaseJobPrivate));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_base_job_get_cancellable:
 * @job: A #UDisksBaseJob.
 *
 * Gets the #GCancellable for @job.
 *
 * Returns: A #GCancellable. Do not free, the object belongs to @job.
 */
GCancellable *
udisks_base_job_get_cancellable  (UDisksBaseJob  *job)
{
  g_return_val_if_fail (UDISKS_IS_BASE_JOB (job), NULL);
  return job->priv->cancellable;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_base_job_get_daemon:
 * @job: A #UDisksBaseJob.
 *
 * Gets the #UDisksDaemon for @job.
 *
 * Returns: A #UDisksDaemon. Do not free, the object belongs to @job.
 */
UDisksDaemon *
udisks_base_job_get_daemon  (UDisksBaseJob  *job)
{
  g_return_val_if_fail (UDISKS_IS_BASE_JOB (job), NULL);
  return job->priv->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_base_job_add_object:
 * @job: A #UDisksBaseJob.
 * @object: A #UDisksObject.
 *
 * Adds the object path for @object to the <link
 * linkend="gdbus-property-org-freedesktop-UDisks2-Job.Objects">Objects</link>
 * array. If the object path is already in the array, does nothing.
 */
void
udisks_base_job_add_object (UDisksBaseJob  *job,
                            UDisksObject   *object)
{
  const gchar *object_path;
  const gchar *const *paths;
  const gchar **p;
  guint n;

  g_return_if_fail (UDISKS_IS_BASE_JOB (job));
  g_return_if_fail (UDISKS_IS_OBJECT (object));

  object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
  paths = udisks_job_get_objects (UDISKS_JOB (job));
  for (n = 0; paths != NULL && paths[n] != NULL; n++)
    {
      if (g_strcmp0 (paths[n], object_path) == 0)
        goto out;
    }

  p = g_new0 (const gchar *, n + 2);
  p[n] = object_path;
  udisks_job_set_objects (UDISKS_JOB (job), p);
  g_free (p);

 out:
  ;
}

/**
 * udisks_base_job_remove_object:
 * @job: A #UDisksBaseJob.
 * @object: A #UDisksObject.
 *
 * Removes the object path for @object to the <link
 * linkend="gdbus-property-org-freedesktop-UDisks2-Job.Objects">Objects</link>
 * array. If the object path is not in the array, does nothing.
 */
void
udisks_base_job_remove_object (UDisksBaseJob  *job,
                               UDisksObject   *object)
{
  const gchar *object_path;
  const gchar *const *paths;
  GPtrArray *p = NULL;
  guint n;

  g_return_if_fail (UDISKS_IS_BASE_JOB (job));
  g_return_if_fail (UDISKS_IS_OBJECT (object));

  object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
  paths = udisks_job_get_objects (UDISKS_JOB (job));
  for (n = 0; paths != NULL && paths[n] != NULL; n++)
    {
      if (g_strcmp0 (paths[n], object_path) != 0)
        {
          if (p == NULL)
            p = g_ptr_array_new ();
          g_ptr_array_add (p, (gpointer) paths[n]);
        }
    }

  if (p != NULL)
    {
      g_ptr_array_add (p, NULL);
      udisks_job_set_objects (UDISKS_JOB (job), (const gchar *const *) p->pdata);
      g_ptr_array_free (p, TRUE);
    }
  else
    {
      udisks_job_set_objects (UDISKS_JOB (job), NULL);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_cancel (UDisksJob              *_job,
               GDBusMethodInvocation  *invocation,
               GVariant               *options)
{
  UDisksBaseJob *job = UDISKS_BASE_JOB (_job);
  UDisksObject *object = NULL;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (job, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_get_caller_uid_sync (job->priv->daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &caller_gid,
                                               NULL,
                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Translators: Shown in authentication dialog when canceling a job.
   */
  message = N_("Authentication is required to cancel a job");
  action_id = "org.freedesktop.udisks2.cancel-job";
  if (caller_uid != udisks_job_get_started_by_uid (UDISKS_JOB (job)))
    action_id = "org.freedesktop.udisks2.cancel-job-other-user";

  if (!udisks_daemon_util_check_authorization_sync (job->priv->daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  if (g_cancellable_is_cancelled (job->priv->cancellable))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_ALREADY_CANCELLED,
                                             "The job has already been cancelled");
    }
  else
    {
      g_cancellable_cancel (job->priv->cancellable);
      udisks_job_complete_cancel (UDISKS_JOB (job), invocation);
    }

 out:
  g_clear_object (&object);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
job_iface_init (UDisksJobIface *iface)
{
  iface->handle_cancel   = handle_cancel;
}

/* ---------------------------------------------------------------------------------------------------- */
