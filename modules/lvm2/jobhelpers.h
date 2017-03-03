/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2017 Red Hat, Inc.
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
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 *
 */

#ifndef __LVM_JOB_HELPERS_H__
#define __LVM_JOB_HEPLERS_H__

#include <glib.h>
#include <blockdev/lvm.h>

#include <src/udisksthreadedjob.h>

G_BEGIN_DECLS

typedef struct {
  const gchar *vg_name;
  const gchar *lv_name;
  const gchar *new_lv_name;
  const gchar *pool_name;
  guint64 new_lv_size;
  gboolean resize_fs;
  gboolean force;
  gboolean destroy;
} LVJobData;

typedef struct {
  const gchar *vg_name;
  const gchar *new_vg_name;
  const gchar *pv_path;
  const gchar **pvs;
} VGJobData;

typedef struct {
  const gchar *path;
} PVJobData;

gboolean lvcreate_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean lvcreate_thin_job_func (UDisksThreadedJob  *job,
                                 GCancellable       *cancellable,
                                 gpointer            user_data,
                                 GError            **error);

gboolean lvremove_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean lvrename_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean lvresize_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean lvactivate_job_func (UDisksThreadedJob  *job,
                              GCancellable       *cancellable,
                              gpointer            user_data,
                              GError            **error);

gboolean lvdeactivate_job_func (UDisksThreadedJob  *job,
                                GCancellable       *cancellable,
                                gpointer            user_data,
                                GError            **error);

gboolean lvsnapshot_create_job_func (UDisksThreadedJob  *job,
                                     GCancellable       *cancellable,
                                     gpointer            user_data,
                                     GError            **error);

gboolean lvcache_attach_job_func (UDisksThreadedJob  *job,
                                  GCancellable       *cancellable,
                                  gpointer            user_data,
                                  GError            **error);

gboolean lvcache_detach_job_func (UDisksThreadedJob  *job,
                                  GCancellable       *cancellable,
                                  gpointer            user_data,
                                  GError            **error);


gboolean vgcreate_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean vgremove_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean vgrename_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean vgextend_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean vgreduce_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);


gboolean pvcreate_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean pvremove_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error);

gboolean pvmove_job_func (UDisksThreadedJob  *job,
                          GCancellable       *cancellable,
                          gpointer            user_data,
                          GError            **error);

G_END_DECLS

#endif  /* __LVM_JOB_HEPLERS_H__ */
