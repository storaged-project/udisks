/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Peter Hatina <phatina@redhat.com>
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
 */

#include "config.h"

#include <blockdev/btrfs.h>
#include "udisksbtrfsutil.h"

const gchar *btrfs_subvolume_fmt = "(tts)";
const gchar *btrfs_subvolumes_fmt = "a(tts)";
const gchar *btrfs_policy_action_id = "org.freedesktop.udisks2.btrfs.manage-btrfs";

GVariant *
btrfs_subvolumes_to_gvariant (BDBtrfsSubvolumeInfo **subvolumes_info,
                              gint                  *subvolumes_cnt)
{
  BDBtrfsSubvolumeInfo **infos;
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE (btrfs_subvolumes_fmt));
  if (! subvolumes_info)
    return g_variant_builder_end (&builder);

  *subvolumes_cnt = 0;

  for (infos = subvolumes_info; *infos; ++infos, ++*subvolumes_cnt)
    {
      g_variant_builder_add (&builder,
                             btrfs_subvolume_fmt,
                             (*infos)->id,
                             (*infos)->parent_id,
                             (*infos)->path);
    }

  return g_variant_builder_end (&builder);
}

void
btrfs_free_subvolumes_info (BDBtrfsSubvolumeInfo **subvolumes_info)
{
  BDBtrfsSubvolumeInfo **infos = NULL;

  if (! subvolumes_info)
    return;

  for (infos = subvolumes_info; *infos; ++infos)
    bd_btrfs_subvolume_info_free (*infos);
  g_free ((gpointer) subvolumes_info);
}
