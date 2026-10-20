/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2026 Wang Yu <wangyu@uniontech.com>
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

#ifndef __UDISKS_MOUNT_UTIL_H__
#define __UDISKS_MOUNT_UTIL_H__

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>

#define UDISKS_MOUNT_UTIL_XSTR(s) UDISKS_MOUNT_UTIL_STR(s)
#define UDISKS_MOUNT_UTIL_STR(s) #s
#define UDISKS_MOUNT_UTIL_PATH_MAX_FMT "%" UDISKS_MOUNT_UTIL_XSTR(PATH_MAX) "s"

static gboolean
udisks_mount_util_get_btrfs_device_from_mountinfo (const gchar *line,
                                                    dev_t       *block_device)
{
  const gchar *sep;
  gchar fstype[PATH_MAX + 1];
  gchar mount_source[PATH_MAX + 1];
  struct stat statbuf;

  sep = strstr (line, " - ");
  if (sep == NULL)
    return FALSE;

  if (sscanf (sep + 3,
              UDISKS_MOUNT_UTIL_PATH_MAX_FMT " " UDISKS_MOUNT_UTIL_PATH_MAX_FMT,
              fstype,
              mount_source) != 2)
    return FALSE;

  fstype[sizeof fstype - 1] = '\0';
  mount_source[sizeof mount_source - 1] = '\0';

  if (g_strcmp0 (fstype, "btrfs") != 0 ||
      !g_str_has_prefix (mount_source, "/dev/") ||
      stat (mount_source, &statbuf) != 0 ||
      !S_ISBLK (statbuf.st_mode))
    return FALSE;

  *block_device = statbuf.st_rdev;
  return TRUE;
}

#undef UDISKS_MOUNT_UTIL_PATH_MAX_FMT
#undef UDISKS_MOUNT_UTIL_STR
#undef UDISKS_MOUNT_UTIL_XSTR

#endif /* __UDISKS_MOUNT_UTIL_H__ */
