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

gboolean lvcreate_thin_pool_job_func (UDisksThreadedJob  *job,
                                      GCancellable       *cancellable,
                                      gpointer            user_data,
                                      GError            **error)
{
    guint64 md_size = 0;
    LVJobData *data = user_data;

    /* get metadata size */
    md_size = bd_lvm_get_thpool_meta_size (data->new_lv_size, BD_LVM_DEFAULT_CHUNK_SIZE, 100 /* snapshots */, error);
    if (md_size == 0)
      /* error is set */
      return FALSE;

    md_size = bd_lvm_round_size_to_pe (md_size, data->extent_size, TRUE /* round_up */, error);
    if (md_size == 0)
      /* error is set */
      return FALSE;

    /* create a thin pool of given total size (with part of space being used for
       metadata), but also leave space for the pmspare device (of the same size
       as metadata space) which need to be created */
    return bd_lvm_thpoolcreate (data->vg_name, data->new_lv_name, data->new_lv_size - 2*md_size, md_size, BD_LVM_DEFAULT_CHUNK_SIZE,
                                NULL /* profile */, NULL /* extra_args */, error);
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
    BDExtraArg *extra[4] = {NULL, NULL, NULL, NULL};
    gint extra_top = -1;
    gboolean ret = FALSE;

    if (data->force)
        extra[++extra_top] = bd_extra_arg_new ("-f", "");
    if (data->resize_fs)
      {
        extra[++extra_top] = bd_extra_arg_new ("-r", "");
        extra[++extra_top] = bd_extra_arg_new ("--yes", "");
      }

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

gboolean vgcreate_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    VGJobData *data = user_data;
    return bd_lvm_vgcreate (data->vg_name, data->pvs, 0 /* PE size*/, NULL /* extra_args */, error);
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

gboolean pvcreate_job_func (UDisksThreadedJob  *job,
                            GCancellable       *cancellable,
                            gpointer            user_data,
                            GError            **error)
{
    PVJobData *data = user_data;
    return bd_lvm_pvcreate (data->path, 0 /* data_alignment */, 0 /* metadata_size */, NULL /* extra_args */, error);
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


void vg_list_free (BDLVMVGdata **vg_list) {
  if (!vg_list)
    /* nothing to do */
    return;

  for (BDLVMVGdata **vg_list_p = vg_list; *vg_list_p; vg_list_p++)
    bd_lvm_vgdata_free (*vg_list_p);
  g_free (vg_list);
}

void pv_list_free (BDLVMPVdata **pv_list) {
  if (!pv_list)
    /* nothing to do */
    return;

  for (BDLVMPVdata **pv_list_p = pv_list; *pv_list_p; pv_list_p++)
    bd_lvm_pvdata_free (*pv_list_p);
  g_free (pv_list);
}

void lv_list_free (BDLVMLVdata **lv_list) {
  if (!lv_list)
    /* nothing to do */
    return;

  for (BDLVMLVdata **lv_list_p = lv_list; *lv_list_p; lv_list_p++)
    bd_lvm_lvdata_free (*lv_list_p);
  g_free (lv_list);
}

void vgs_pvs_data_free (VGsPVsData *data) {
  vg_list_free (data->vgs);
  pv_list_free (data->pvs);
  g_free (data);
}

void vgs_task_func (GTask        *task,
                    gpointer      source_obj,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
  GError *error = NULL;
  VGsPVsData *ret = g_new0 (VGsPVsData, 1);

  ret->vgs = bd_lvm_vgs (&error);
  if (!ret->vgs) {
    vgs_pvs_data_free (ret);
    g_task_return_error (task, error);
    return;
  }

  ret->pvs = bd_lvm_pvs (&error);
  if (!ret->pvs) {
    vgs_pvs_data_free (ret);
    g_task_return_error (task, error);
  }
  else
    g_task_return_pointer (task, ret, (GDestroyNotify) vgs_pvs_data_free);
}

void lvs_task_func (GTask        *task,
                    gpointer      source_obj,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
  GError *error = NULL;
  BDLVMLVdata **ret = NULL;
  gchar *vg_name = (gchar*) task_data;

  ret = bd_lvm_lvs (vg_name, &error);
  if (!ret)
    g_task_return_error (task, error);
  else
    g_task_return_pointer (task, ret, (GDestroyNotify) lv_list_free);
}
