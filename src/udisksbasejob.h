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

#ifndef __UDISKS_BASE_JOB_H__
#define __UDISKS_BASE_JOB_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_BASE_JOB         (udisks_base_job_get_type ())
#define UDISKS_BASE_JOB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_BASE_JOB, UDisksBaseJob))
#define UDISKS_BASE_JOB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), UDISKS_TYPE_BASE_JOB, UDisksBaseJobClass))
#define UDISKS_BASE_JOB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), UDISKS_TYPE_BASE_JOB, UDisksBaseJobClass))
#define UDISKS_IS_BASE_JOB(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_BASE_JOB))
#define UDISKS_IS_BASE_JOB_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), UDISKS_TYPE_BASE_JOB))

typedef struct _UDisksBaseJobClass     UDisksBaseJobClass;
typedef struct _UDisksBaseJobPrivate   UDisksBaseJobPrivate;

/**
 * UDisksBaseJob:
 *
 * The #UDisksBaseJob structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksBaseJob
{
  /*< private >*/
  UDisksJobSkeleton parent_instance;
  UDisksBaseJobPrivate *priv;
};

/**
 * UDisksBaseJobClass:
 * @parent_class: Parent class.
 *
 * Class structure for #UDisksBaseJob.
 */
struct _UDisksBaseJobClass
{
  UDisksJobSkeletonClass parent_class;
  /*< private >*/
  gpointer padding[8];
};

GType              udisks_base_job_get_type          (void) G_GNUC_CONST;
UDisksDaemon      *udisks_base_job_get_daemon        (UDisksBaseJob  *job);
GCancellable      *udisks_base_job_get_cancellable   (UDisksBaseJob  *job);
gboolean           udisks_base_job_get_auto_estimate (UDisksBaseJob  *job);
void               udisks_base_job_set_auto_estimate (UDisksBaseJob  *job,
                                                      gboolean        value);

void               udisks_base_job_add_object        (UDisksBaseJob  *job,
                                                      UDisksObject   *object);
void               udisks_base_job_remove_object     (UDisksBaseJob  *job,
                                                      UDisksObject   *object);

G_END_DECLS

#endif /* __UDISKS_BASE_JOB_H__ */
