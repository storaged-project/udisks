/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2011 Martin Pitt <martin.pitt@ubuntu.com>
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

#include <string.h>
#include <glib.h>

#include "config.h"
#include "udiskslinuxfsinfo.h"

const FSInfo _fs_info[] =
  {
    /* filesystems */
    {
      "ext2",
      "e2label $DEVICE $LABEL",
      NULL,
      TRUE,  /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.ext2 -F -L $LABEL $DEVICE",
    },
    {
      "ext3",
      "e2label $DEVICE $LABEL",
      NULL,
      TRUE,  /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.ext3 -F -L $LABEL $DEVICE",
    },
    {
      "ext4",
      "e2label $DEVICE $LABEL",
      NULL,
      TRUE,  /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.ext4 -F -L $LABEL $DEVICE",
    },
    {
      "vfat",
      "dosfslabel $DEVICE $LABEL",
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkfs.vfat -I -n $LABEL $DEVICE",
    },
    {
      "ntfs",
      "ntfslabel $DEVICE $LABEL",
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkntfs -f -F -L $LABEL $DEVICE",
    },
    {
      "exfat",
      "exfatlabel $DEVICE $LABEL",
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkexfatfs -n $LABEL $DEVICE",
    },
    {
      "xfs",
      "xfs_admin -L $LABEL $DEVICE",
      "xfs_admin -L -- $DEVICE",
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.xfs -f -L $LABEL $DEVICE",
    },
    {
      "reiserfs",
      "reiserfstune -l $LABEL $DEVICE",
      NULL,
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.reiserfs -q -l $LABEL $DEVICE",
    },
    {
      "nilfs2",
      "nilfs-tune -L $LABEL $DEVICE",
      NULL,
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.nilfs2 -L $LABEL $DEVICE",
    },
    {
      "btrfs",
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.btrfs -L $LABEL $DEVICE",
    },
    {
      "minix",
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkfs.minix $DEVICE",
    },
    {
      "udf",
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkudffs --vid $LABEL $DEVICE",
    },
    {
      "f2fs",
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.f2fs -l $LABEL $DEVICE",
    },
    /* swap space */
    {
      "swap",
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkswap -L $LABEL $DEVICE",
    },
    /* partition tables */
    {
      "dos",
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "parted --script $DEVICE mktable msdos",
    },
    {
      "gpt",
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "parted --script $DEVICE mktable gpt",
    },
    /* empty */
    {
      "empty",
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "wipefs --all $DEVICE",
    },
  };

/**
 * get_fs_info:
 *
 * Look up #FSInfo record for a particular file system.
 * @fstype: file system type name
 *
 * Returns: (transfer none): #FSInfo struct for @fstype, or %NULL if that file
 *   system is unknown. Do not free or modify.
 */
const FSInfo *
get_fs_info (const gchar *fstype)
{
  guint n;

  g_return_val_if_fail (fstype != NULL, NULL);

  for (n = 0; n < sizeof(_fs_info)/sizeof(FSInfo); n++)
    {
      if (strcmp (_fs_info[n].fstype, fstype) == 0)
        return &_fs_info[n];
    }

  return NULL;
}
