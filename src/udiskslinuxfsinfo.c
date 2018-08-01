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

#define FS_EXT2      "ext2"
#define FS_EXT3      "ext3"
#define FS_EXT4      "ext4"
#define FS_VFAT      "vfat"
#define FS_NTFS      "ntfs"
#define FS_EXFAT     "exfat"
#define FS_XFS       "xfs"
#define FS_REISERFS  "reiserfs"
#define FS_NILFS2    "nilfs2"
#define FS_BTRFS     "btrfs"
#define FS_MINIX     "minix"
#define FS_UDF       "udf"
#define FS_F2FS      "f2fs"
#define SWAP         "swap"
#define PT_DOS       "dos"
#define PT_GPT       "gpt"
#define EMPTY        "empty"

const gchar *_fs_names[] =
  {
    FS_EXT2,
    FS_EXT3,
    FS_EXT4,
    FS_VFAT,
    FS_NTFS,
    FS_EXFAT,
    FS_XFS,
    FS_REISERFS,
    FS_NILFS2,
    FS_BTRFS,
    FS_MINIX,
    FS_UDF,
    FS_F2FS,
    SWAP,
    NULL
  };

const FSInfo _fs_info[] =
  {
    /* filesystems */
    {
      FS_EXT2,
      "e2label $DEVICE $LABEL",
      NULL,
      TRUE,  /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.ext2 -F -L $LABEL $OPTIONS $DEVICE",
      "mkfs.ext2 -n -F -L $LABEL $OPTIONS $DEVICE",
      "-E nodiscard", /* option_no_discard */
    },
    {
      FS_EXT3,
      "e2label $DEVICE $LABEL",
      NULL,
      TRUE,  /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.ext3 -F -L $LABEL $OPTIONS $DEVICE",
      "mkfs.ext3 -n -F -L $LABEL $OPTIONS $DEVICE",
      "-E nodiscard", /* option_no_discard */
    },
    {
      FS_EXT4,
      "e2label $DEVICE $LABEL",
      NULL,
      TRUE,  /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.ext4 -F -L $LABEL $OPTIONS $DEVICE",
      "mkfs.ext4 -n -F -L $LABEL $OPTIONS $DEVICE",
      "-E nodiscard", /* option_no_discard */
    },
    {
      FS_VFAT,
      "dosfslabel $DEVICE $LABEL",
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkfs.vfat -I -n $LABEL $DEVICE",
      NULL,
      NULL, /* option_no_discard */
    },
    {
      FS_NTFS,
      "ntfslabel $DEVICE $LABEL",
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkntfs -f -F -L $LABEL $DEVICE",
      "mkntfs -n -f -F -L $LABEL $DEVICE",
      NULL, /* option_no_discard */
    },
    {
      FS_EXFAT,
      "exfatlabel $DEVICE $LABEL",
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkexfatfs -n $LABEL $DEVICE",
      NULL,
      NULL, /* option_no_discard */
    },
    {
      FS_XFS,
      "xfs_admin -L $LABEL $DEVICE",
      "xfs_admin -L -- $DEVICE",
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.xfs -f -L $LABEL $OPTIONS $DEVICE",
      "mkfs.xfs -N -f -L $LABEL $OPTIONS $DEVICE",
      "-K" /* option_no_discard */
    },
    {
      FS_REISERFS,
      "reiserfstune -l $LABEL $DEVICE",
      NULL,
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.reiserfs -q -l $LABEL $DEVICE",
      NULL,
      NULL, /* option_no_discard */
    },
    {
      FS_NILFS2,
      "nilfs-tune -L $LABEL $DEVICE",
      NULL,
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.nilfs2 -L $LABEL $DEVICE",
      NULL,
      NULL, /* option_no_discard */
    },
    {
      FS_BTRFS,
      "btrfs filesystem label $DEVICE $LABEL",
      NULL,
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.btrfs -L $LABEL $OPTIONS $DEVICE",
      NULL,
      "-K", /* option_no_discard */
    },
    {
      FS_MINIX,
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkfs.minix $DEVICE",
      NULL,
      NULL,
    },
    {
      FS_UDF,
      "udflabel --utf8 $DEVICE $LABEL",
      NULL,
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkudffs --utf8 --media-type=hd --udfrev=0x201 --blocksize=$BLOCKSIZE --vid $LABEL --lvid $LABEL $DEVICE",
      NULL,
      NULL,
    },
    {
      FS_F2FS,
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      TRUE,  /* supports_owners */
      "mkfs.f2fs -l $LABEL $DEVICE",
      NULL,
      NULL,
    },
    /* swap space */
    {
      SWAP,
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "mkswap -L $LABEL $DEVICE",
      NULL,
    },
    /* partition tables */
    {
      PT_DOS,
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "parted --script $DEVICE mktable msdos",
      NULL,
    },
    {
      PT_GPT,
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "parted --script $DEVICE mktable gpt",
      NULL,
    },
    /* empty */
    {
      EMPTY,
      NULL,
      NULL,
      FALSE, /* supports_online_label_rename */
      FALSE, /* supports_owners */
      "wipefs --all $DEVICE",
      NULL,
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

/**
 * get_supported_filesystems:
 *
 * Returns: a NULL terminated list of supported filesystems. Do not free or
 * modify.
 */
const gchar **
get_supported_filesystems (void)
{
  return _fs_names;
}
