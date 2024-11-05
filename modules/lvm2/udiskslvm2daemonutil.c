/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2013 Marius Vollmer <marius.vollmer@redhat.com>
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

#include "config.h"
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>

#include <stdio.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include <blockdev/fs.h>
#include <blockdev/lvm.h>

#include <limits.h>
#include <stdlib.h>

#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udisksstate.h>
#include <src/udiskslogging.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdriveobject.h>
#include <src/udisksmodulemanager.h>

#include "udiskslvm2daemonutil.h"

/**
 * SECTION:udiskslvm2daemonutil
 * @title: Utilities
 * @short_description: Various utility routines
 *
 * Various utility routines.
 */

gboolean
udisks_daemon_util_lvm2_block_is_unused (UDisksBlock  *block,
                                         GError      **error)
{
  const gchar *device_file;
  int fd;
  gint num_tries = 0;

  device_file = udisks_block_get_device (block);

  while ((fd = open (device_file, O_RDONLY | O_EXCL)) < 0)
    {
      g_usleep (100 * 1000); /* microseconds */
      if (num_tries++ > 10)
        break;
    }
  if (fd < 0)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error opening device %s for unused block device detection: %m",
                   device_file);
      return FALSE;
    }
  close (fd);

  return TRUE;
}

gboolean
udisks_daemon_util_lvm2_wipe_block (UDisksDaemon  *daemon,
                                    UDisksBlock   *block,
                                    GError       **error)
{
  UDisksObject *block_object;
  UDisksPhysicalVolume *physical_volume;
  const gchar *volume_group_objpath;
  UDisksObject *volume_group_object = NULL;
  UDisksVolumeGroup *volume_group;
  gchar *volume_group_name = NULL;
  gboolean was_partitioned;
  const gchar *device_file;
  gboolean ret = TRUE;
  GError *local_error = NULL;

  /* Find the name of the volume group that this device is a physical
   * member of, if any.  Easy.
   */
  block_object = udisks_daemon_util_dup_object (block, error);
  if (block_object == NULL)
    {
      ret = FALSE;
      goto out;
    }
  physical_volume = udisks_object_peek_physical_volume (block_object);
  if (physical_volume)
    {
      volume_group_objpath = udisks_physical_volume_get_volume_group (physical_volume);
      volume_group_object = udisks_daemon_find_object (daemon, volume_group_objpath);
      if (volume_group_object)
        {
          volume_group = udisks_object_peek_volume_group (volume_group_object);
          if (volume_group)
            volume_group_name = g_strdup (udisks_volume_group_get_name (volume_group));
        }
    }

  was_partitioned = (udisks_object_peek_partition_table (block_object) != NULL);

  device_file = udisks_block_get_device (block);

  if (!bd_fs_clean (device_file, FALSE, &local_error))
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "%s", local_error->message);
      g_clear_error (&local_error);
      ret = FALSE;
      goto out;
    }

  if (was_partitioned &&
      !udisks_linux_block_object_reread_partition_table (UDISKS_LINUX_BLOCK_OBJECT (block_object), error))
    {
      ret = FALSE;
      goto out;
    }

  /* Try to bring affected volume group back into consistency. */
  if (volume_group_name != NULL &&
      !bd_lvm_vgreduce (volume_group_name, NULL /* device */, NULL /* extra */, &local_error))
    {
      udisks_warning ("%s", local_error->message);
      g_clear_error (&local_error);
    }

  /* Make sure lvmetad knows about all this.
   *
   * XXX - We need to do this because of a bug in the LVM udev rules
   * which often fail to run pvscan on "change" events.
   *
   * https://bugzilla.redhat.com/show_bug.cgi?id=1063813
   */
  if (!bd_lvm_pvscan (device_file, TRUE, NULL, &local_error))
    {
      udisks_warning ("%s", local_error->message);
      g_clear_error (&local_error);
    }

 out:
  g_clear_object (&volume_group_object);
  g_clear_object (&block_object);
  g_free (volume_group_name);
  return ret;
}

/* -------------------------------------------------------------------------------- */

gboolean
udisks_daemon_util_lvm2_name_is_reserved (const gchar *name)
{
 return (strchr (name, '[')
         || strchr (name, ']')
         || strstr (name, "_mlog")
         || strstr (name, "_mimage")
         || strstr (name, "_rimage")
         || strstr (name, "_rmeta")
         || strstr (name, "_tdata")
         || strstr (name, "_tmeta")
         || strstr (name, "_pmspare")
         || g_str_has_prefix (name, "pvmove")
         || g_str_has_prefix (name, "snapshot"));
}

/* -------------------------------------------------------------------------------- */

void
udisks_daemon_util_lvm2_trigger_udev (const gchar *device_file)
{
  int fd = open (device_file, O_RDWR);
  if (fd >= 0)
    close (fd);
}

/* -------------------------------------------------------------------------------- */

GStrv
udisks_daemon_util_lvm2_gather_pvs (UDisksDaemon                  *daemon,
                                    UDisksLinuxVolumeGroupObject  *vgroup_object,
                                    const gchar *const            *arg_pvs,
                                    GError                       **error)
{
  GStrv result = g_new0 (gchar *, g_strv_length ((GStrv)arg_pvs)+1);

  for (int i = 0; arg_pvs[i] != NULL; i++)
    {
      UDisksObject *pvol_object = NULL;
      UDisksBlock *block = NULL;
      UDisksPhysicalVolume *pvol = NULL;

      pvol_object = udisks_daemon_find_object (daemon, arg_pvs[i]);
      if (pvol_object == NULL)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Invalid object path %s at index %u",
                       arg_pvs[i], i);
          goto out;
        }

      block = udisks_object_get_block (pvol_object);
      pvol = udisks_object_get_physical_volume (pvol_object);
      if (block == NULL || pvol == NULL)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Object path %s for index %u is not a physical volume",
                       arg_pvs[i], i);
          if (block)
            g_object_unref (block);
          if (pvol)
            g_object_unref (pvol);
          g_object_unref (pvol_object);
          goto out;
        }

      if (g_strcmp0 (udisks_physical_volume_get_volume_group (pvol),
                     g_dbus_object_get_object_path (G_DBUS_OBJECT (vgroup_object))) != 0)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Physical volume %s for index %u does not belong to this volume group",
                       arg_pvs[i], i);
          g_object_unref (pvol_object);
          g_object_unref (pvol);
          g_object_unref (block);
          goto out;
        }

      result[i] = udisks_block_dup_device (block);
      g_object_unref (block);
      g_object_unref (pvol);
      g_object_unref (pvol_object);
    }

  return result;

 out:
  g_strfreev (result);
  return NULL;
}
