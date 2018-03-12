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
#include <unistd.h>
#include <blockdev/fs.h>

#include "udiskslinuxfilesystemhelpers.h"
#include "udiskslogging.h"


static gboolean recursive_chown (const gchar *directory, uid_t caller_uid, gid_t caller_gid)
{
  GDir * gdir = NULL;
  const gchar *fname = NULL;
  GError *local_error = NULL;
  gchar path[PATH_MAX + 1] = {0};

  gdir = g_dir_open (directory, 0, &local_error);
  if (gdir == NULL)
    {
      g_clear_error (&local_error);
      return FALSE;
    }

  if (chown (directory, caller_uid, caller_gid) != 0)
    {
      g_dir_close (gdir);
      return FALSE;
    }

  while ((fname = g_dir_read_name (gdir)))
    {
      snprintf (path, sizeof (path), "%s/%s", directory, fname);
      if (g_file_test (path, G_FILE_TEST_IS_DIR))
        {
          if (!recursive_chown (path, caller_uid, caller_gid))
            {
              g_dir_close (gdir);
              return FALSE;
            }
        }
      else if (g_file_test (path, G_FILE_TEST_IS_REGULAR) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
        {
          if (chown (path, caller_uid, caller_gid) != 0)
            {
              g_dir_close (gdir);
              return FALSE;
            }
        }
    }

  g_dir_close (gdir);
  return TRUE;
}


gboolean take_filesystem_ownership (const gchar *device,
                                    const gchar *fstype,
                                    uid_t caller_uid,
                                    gid_t caller_gid,
                                    gboolean recursive,
                                    GError **error)

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
            unmount = TRUE;  // unmount during cleanup
        }
    }

  if (recursive)
    {
      if (!recursive_chown (mountpoint, caller_uid, caller_gid))
        {
            g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                         "Cannot recursively chown %s to uid=%u and gid=%u: %m",
                         mountpoint, caller_uid, caller_gid);

          success = FALSE;
          goto out;
        }
    }
  else
    {
      if (chown (mountpoint, caller_uid, caller_gid) != 0)
        {
          g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                       "Cannot chown %s to uid=%u and gid=%u: %m",
                       mountpoint, caller_uid, caller_gid);
          success = FALSE;
          goto out;
        }
    }

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
