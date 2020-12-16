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
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <blockdev/fs.h>

#include "udiskslinuxfilesystemhelpers.h"
#include "udiskslogging.h"


static gboolean
recursive_chown (const gchar *path,
                 uid_t        caller_uid,
                 gid_t        caller_gid,
                 gboolean     recursive,
                 GError     **error)
{
  int dirfd;
  DIR *dir;
  struct dirent *dirent;
  GSList *list, *l;

  g_return_val_if_fail (path != NULL, FALSE);

  if (lchown (path, caller_uid, caller_gid) != 0)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error changing ownership of %s to uid=%u and gid=%u: %m",
                   path, caller_uid, caller_gid);
      return FALSE;
    }

  if (! recursive)
    return TRUE;

  /* read and traverse through the directory */
  dirfd = open (path, O_DIRECTORY | O_NOFOLLOW);
  if (dirfd < 0)
    {
      if (errno == ENOTDIR)
        return TRUE;
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error opening directory %s: %m", path);
      return FALSE;
    }

  dir = fdopendir (dirfd);
  if (! dir)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error opening directory %s: %m", path);
      close (dirfd);
      return FALSE;
    }

  /* build a list of filenames to prevent fd exhaustion */
  list = NULL;
  while ((errno = 0, dirent = readdir (dir)))
    if (g_strcmp0 (dirent->d_name, ".") != 0 && g_strcmp0 (dirent->d_name, "..") != 0)
      list = g_slist_append (list, g_strdup (dirent->d_name));
  if (!dirent && errno != 0)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error reading directory %s: %m", path);
      closedir (dir);
      g_slist_free_full (list, g_free);
      return FALSE;
    }
  closedir (dir);

  /* recurse into parents */
  for (l = list; l; l = g_slist_next (l))
    {
      gchar *newpath;

      newpath = g_build_filename (path, l->data, NULL);
      if (! recursive_chown (newpath, caller_uid, caller_gid, TRUE, error))
        {
          g_free (newpath);
          g_slist_free_full (list, g_free);
          return FALSE;
        }
      g_free (newpath);
    }
  g_slist_free_full (list, g_free);

  return TRUE;
}

gboolean
take_filesystem_ownership (const gchar  *device,
                           const gchar  *fstype,
                           uid_t         caller_uid,
                           gid_t         caller_gid,
                           gboolean      recursive,
                           GError      **error)

{
  gchar *mountpoint = NULL;
  GError *local_error = NULL;
  gboolean unmount = FALSE;
  gboolean success = TRUE;

  mountpoint = bd_fs_get_mountpoint (device, &local_error);
  if (mountpoint == NULL)
    {
      if (local_error != NULL)
        {
          g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                       "Error when getting mountpoint for %s: %s.",
                       device, local_error->message);
          g_clear_error (&local_error);
          success = FALSE;
          goto out;
        }
      else
        {
          /* device is not mounted, we need to mount it */
          mountpoint = g_mkdtemp (g_strdup ("/run/udisks2/temp-mount-XXXXXX"));
          if (mountpoint == NULL)
            {
              g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "Cannot create temporary mountpoint.");
              success = FALSE;
              goto out;
            }

          /* TODO: mount to a private mount namespace */
          if (!bd_fs_mount (device, mountpoint, fstype, NULL, NULL, &local_error))
            {
              g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "Cannot mount %s at %s: %s",
                           device, mountpoint, local_error->message);
              g_clear_error (&local_error);
              if (g_rmdir (mountpoint) != 0)
                udisks_warning ("Error removing temporary mountpoint directory %s.", mountpoint);
              success = FALSE;
              goto out;
            }
          else
            unmount = TRUE;  /* unmount during cleanup */
        }
    }

  /* actual chown */
  success = recursive_chown (mountpoint, caller_uid, caller_gid, recursive, error);
  if (! success)
    goto out;

  if (chmod (mountpoint, 0700) != 0)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Cannot chmod %s to mode 0700: %m",
                   mountpoint);
      success = FALSE;
      goto out;
    }

 out:
  if (unmount)
    {
      if (! bd_fs_unmount (mountpoint, FALSE, FALSE, NULL, &local_error))
        {
          udisks_warning ("Error unmounting temporary mountpoint %s: %s",
                          mountpoint, local_error->message);
          g_clear_error (&local_error);
        }
      if (g_rmdir (mountpoint) != 0)
        udisks_warning ("Error removing temporary mountpoint directory %s.", mountpoint);
    }

  g_free (mountpoint);

  return success;
}
