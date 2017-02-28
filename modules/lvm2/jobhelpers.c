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

#include <glib.h>
#include <blockdev/lvm.h>
#include <blockdev/utils.h>

#include <src/udisksthreadedjob.h>

#include "jobhelpers.h"

gboolean lvcreate_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    LVJobData *data = user_data;
    return bd_lvm_lvcreate (data->vg_name, data->new_lv_name, data->new_lv_size, NULL /* type */, NULL /* pvs */, NULL /* extra_args */, error);
}

gboolean lvcreate_thin_job_func (UDisksThreadedJob  *job,
                                 GCancellable       *cancellable,
                                 gpointer            user_data,
                                 GError            **error)
{
    LVJobData *data = user_data;
    return bd_lvm_thlvcreate (data->vg_name, data->pool_name, data->new_lv_name, data->new_lv_size, NULL /* extra_args */, error);
}

gboolean lvremove_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    LVJobData *data = user_data;
    return bd_lvm_lvremove (data->vg_name, data->lv_name, TRUE /* force */, NULL /* extra_args */, error);
}

gboolean lvrename_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    LVJobData *data = user_data;
    return bd_lvm_lvrename (data->vg_name, data->lv_name, data->new_lv_name, NULL /* extra_args */, error);
}

gboolean lvresize_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    LVJobData *data = user_data;
    BDExtraArg *extra[3] = {NULL, NULL, NULL};
    gint extra_top = -1;
    gboolean ret = FALSE;

    if (data->force)
        extra[++extra_top] = bd_extra_arg_new ("-f", "");
    if (data->resize_fs)
        extra[++extra_top] = bd_extra_arg_new ("-r", "");

    ret = bd_lvm_lvresize (data->vg_name, data->lv_name, data->new_lv_size, (const BDExtraArg**) extra, error);
    for (; extra_top >= 0; extra_top--)
        bd_extra_arg_free (extra[extra_top]);

    return ret;
}

gboolean lvactivate_job_func (UDisksThreadedJob  *job,
                              GCancellable       *cancellable,
                              gpointer            user_data,
                              GError            **error)
{
    LVJobData *data = user_data;
    return bd_lvm_lvactivate (data->vg_name, data->lv_name, TRUE /* ignore_skip */, NULL /* extra_args */, error);
}

gboolean lvdeactivate_job_func (UDisksThreadedJob  *job,
                                GCancellable       *cancellable,
                                gpointer            user_data,
                                GError            **error)
{
    LVJobData *data = user_data;
    return bd_lvm_lvdeactivate (data->vg_name, data->lv_name, NULL /* extra_args */, error);
}

gboolean lvsnapshot_create_job_func (UDisksThreadedJob  *job,
                                     GCancellable       *cancellable,
                                     gpointer            user_data,
                                     GError            **error)
{
    LVJobData *data = user_data;

    /* create an old or thin snapshot based on the given parameters (size) */
    if (data->new_lv_size > 0)
      return bd_lvm_lvsnapshotcreate (data->vg_name, data->lv_name, data->new_lv_name, data->new_lv_size, NULL /* extra_args */, error);
    else
      return bd_lvm_thsnapshotcreate (data->vg_name, data->lv_name, data->new_lv_name, NULL /* pool_name */, NULL /* extra_args */, error);
}

gboolean lvcache_attach_job_func (UDisksThreadedJob  *job,
                                  GCancellable       *cancellable,
                                  gpointer            user_data,
                                  GError            **error)
{
    LVJobData *data = user_data;
    return bd_lvm_cache_attach (data->vg_name, data->lv_name, data->pool_name, NULL /* extra_args */, error);
}

gboolean lvcache_detach_job_func (UDisksThreadedJob  *job,
                                  GCancellable       *cancellable,
                                  gpointer            user_data,
                                  GError            **error)
{
    LVJobData *data = user_data;
    return bd_lvm_cache_detach (data->vg_name, data->lv_name, data->destroy, NULL /* extra_args */, error);
}


gboolean vgremove_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    VGJobData *data = user_data;
    return bd_lvm_vgremove (data->vg_name, NULL /* extra_args */, error);
}

gboolean vgrename_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    VGJobData *data = user_data;
    return bd_lvm_vgrename (data->vg_name, data->new_vg_name, NULL /* extra_args */, error);
}

gboolean vgextend_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    VGJobData *data = user_data;
    return bd_lvm_vgextend (data->vg_name, data->pv_path, NULL /* extra_args */, error);
}

gboolean vgreduce_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    VGJobData *data = user_data;
    return bd_lvm_vgreduce (data->vg_name, data->pv_path, NULL /* extra_args */, error);
}

gboolean pvremove_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    VGJobData *data = user_data;
    return bd_lvm_pvremove (data->pv_path, NULL /* extra_args */, error);
}

gboolean pvmove_job_func (UDisksThreadedJob  *job,
                          GCancellable       *cancellable,
                          gpointer            user_data,
                          GError            **error)
{
    VGJobData *data = user_data;
    return bd_lvm_pvmove (data->pv_path, NULL /* dest */, NULL /* extra_args */, error);
}
