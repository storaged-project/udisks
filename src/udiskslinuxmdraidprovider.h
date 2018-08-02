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

#ifndef __UDISKS_LINUX_MDRAID_PROVIDER_H__
#define __UDISKS_LINUX_MDRAID_PROVIDER_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

typedef struct _UDisksLinuxMDRaidProvider UDisksLinuxMDRaidProvider;

struct _UDisksLinuxMDRaidProvider
{
  /* maps from array UUID and sysfs_path to UDisksLinuxMDRaidObject instances */
  GHashTable *uuid_to_mdraid;
  GHashTable *sysfs_path_to_mdraid;
  GHashTable *sysfs_path_to_mdraid_members;
};

static inline void udisks_linux_mdraid_provider_start(UDisksLinuxMDRaidProvider *provider)
{
  provider->uuid_to_mdraid = g_hash_table_new_full (g_str_hash,
                                                    g_str_equal,
                                                    g_free,
                                                    (GDestroyNotify) g_object_unref);
  provider->sysfs_path_to_mdraid = g_hash_table_new_full (g_str_hash,
                                                          g_str_equal,
                                                          g_free,
                                                          NULL);
  provider->sysfs_path_to_mdraid_members = g_hash_table_new_full (g_str_hash,
                                                                  g_str_equal,
                                                                  g_free,
                                                                  NULL);
}

static inline void udisks_linux_mdraid_provider_finalize(UDisksLinuxMDRaidProvider *provider)
{
  g_hash_table_unref (provider->uuid_to_mdraid);
  g_hash_table_unref (provider->sysfs_path_to_mdraid);
  g_hash_table_unref (provider->sysfs_path_to_mdraid_members);
}

void
handle_block_uevent_for_mdraid (UDisksDaemon              *daemon,
                                UDisksLinuxMDRaidProvider *provider,
                                const gchar               *action,
                                UDisksLinuxDevice         *device);

G_END_DECLS

#endif /* __UDISKS_LINUX_MDRAID_PROVIDER_H__ */
