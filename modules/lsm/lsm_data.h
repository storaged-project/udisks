/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Red Hat, Inc.
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
 * Author: Gris Ge <fge@redhat.com>
 *
 */

/*
 * This file contains:
 *  1. LSM data to udisks interface data converting.
 *  2. Provide simple abstracted interface of LSM to udisks codes.
 */

#ifndef __LSM_DATA_H__
#define __LSM_DATA_H__

#include <libstoragemgmt/libstoragemgmt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <glib.h>

//#include <libconfig.h>

G_BEGIN_DECLS

#define _MAX_RAID_TYPE_LEN 10
#define _MAX_STATUS_INFO_LEN 255

typedef struct _UDisksDaemon UDisksDaemon;

struct StdLsmVolData
{
  char raid_type[_MAX_RAID_TYPE_LEN];
  char status_info[_MAX_STATUS_INFO_LEN];
  gboolean is_raid_degraded;
  gboolean is_raid_reconstructing;
  gboolean is_raid_verifying;
  gboolean is_raid_error;
  gboolean is_ok;
  uint32_t min_io_size;
  uint32_t opt_io_size;
  uint32_t raid_disk_count;
};

void std_lsm_data_init (UDisksDaemon *daemon);

/*
 * The cached lsm volume/vpd83 list will not refresh automatically. This is
 * might cause new volume get incorrectly marked as not managed by
 * std_lsm_vol_data_get ().
 * This method is used to manually refresh that cache.
 */
void std_lsm_vpd83_list_refresh (void);

void std_lsm_data_teardown (void);

struct StdLsmVolData *std_lsm_vol_data_get (const char *vpd83);

void std_lsm_vol_data_free (struct StdLsmVolData *std_lsm_vol_data);

uint32_t std_lsm_refresh_time_get (void);

gboolean std_lsm_vpd83_is_managed (const char *vpd83);

G_END_DECLS
#endif /* __LSM_DATA_H__ */
