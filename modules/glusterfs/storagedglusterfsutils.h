/* -*- mode: c; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * copyright (c) 2015 samikshan bairagya <sbairagy@redhat.com>
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license as published by
 * the free software foundation; either version 2 of the license, or
 * (at your option) any later version.
 *
 * this program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * merchantability or fitness for a particular purpose.  see the
 * gnu general public license for more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program; if not, write to the free software
 * foundation, inc., 51 franklin st, fifth floor, boston, ma  02110-1301  usa
 */

#ifndef __STORAGED_GLUSTERFS_UTILS_H__           
#define __STORAGED_GLUSTERFS_UTILS_H__           

#include <src/storageddaemontypes.h>
#include "storagedglusterfstypes.h"

G_BEGIN_DECLS

void storaged_glusterfs_spawn_for_variant (const gchar **argv,
                                           const GVariantType *type,
                                           void (*callback) (GPid pid,
                                                             GVariant *result,
                                                             GError *error,
                                                             gpointer user_data),
                                           gpointer user_data);


/* StoragedLinuxVolumeGroupObject * storaged_daemon_util_lvm2_find_volume_group_object (StoragedDaemon *daemon, const gchar *name); */

/* void storaged_daemon_util_lvm2_trigger_udev (const gchar *device_file);                                                          */

G_END_DECLS

#endif /* __STORAGED_GLUSTERFS_UTILS_H__ */
