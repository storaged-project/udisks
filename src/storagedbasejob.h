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

#ifndef __STORAGED_BASE_JOB_H__
#define __STORAGED_BASE_JOB_H__

#include "storageddaemontypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_BASE_JOB         (storaged_base_job_get_type ())
#define STORAGED_BASE_JOB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_BASE_JOB, StoragedBaseJob))
#define STORAGED_BASE_JOB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), STORAGED_TYPE_BASE_JOB, StoragedBaseJobClass))
#define STORAGED_BASE_JOB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), STORAGED_TYPE_BASE_JOB, StoragedBaseJobClass))
#define STORAGED_IS_BASE_JOB(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_BASE_JOB))
#define STORAGED_IS_BASE_JOB_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), STORAGED_TYPE_BASE_JOB))

typedef struct _StoragedBaseJobClass     StoragedBaseJobClass;
typedef struct _StoragedBaseJobPrivate   StoragedBaseJobPrivate;

/**
 * StoragedBaseJob:
 *
 * The #StoragedBaseJob structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedBaseJob
{
  /*< private >*/
  StoragedJobSkeleton parent_instance;
  StoragedBaseJobPrivate *priv;
};

/**
 * StoragedBaseJobClass:
 * @parent_class: Parent class.
 *
 * Class structure for #StoragedBaseJob.
 */
struct _StoragedBaseJobClass
{
  StoragedJobSkeletonClass parent_class;
  /*< private >*/
  gpointer padding[8];
};

GType              storaged_base_job_get_type          (void) G_GNUC_CONST;
StoragedDaemon    *storaged_base_job_get_daemon        (StoragedBaseJob  *job);
GCancellable      *storaged_base_job_get_cancellable   (StoragedBaseJob  *job);
gboolean           storaged_base_job_get_auto_estimate (StoragedBaseJob  *job);
void               storaged_base_job_set_auto_estimate (StoragedBaseJob  *job,
                                                        gboolean        value);

void               storaged_base_job_add_object        (StoragedBaseJob  *job,
                                                        StoragedObject   *object);
void               storaged_base_job_remove_object     (StoragedBaseJob  *job,
                                                        StoragedObject   *object);

G_END_DECLS

#endif /* __STORAGED_BASE_JOB_H__ */
