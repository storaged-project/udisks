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
#include "udiskslvm2dbusutil.h"
#include "udiskslvm2state.h"
#include "udisks-lvm2-generated.h"

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

  device_file = udisks_block_get_device (block);
  fd = open (device_file, O_RDONLY | O_EXCL);
  if (fd < 0)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error opening device %s: %m",
                   device_file);
      return FALSE;
    }
  close (fd);

  return TRUE;
}

static gboolean
run_sync (const gchar *prog, ...)
{
  va_list ap;
  GError **error;
  enum { max_argc = 20 };
  const gchar *argv[max_argc+1];
  int argc = 0;
  const gchar *arg;
  gchar *standard_output;
  gchar *standard_error;
  gint exit_status;

  argv[argc++] = prog;
  va_start (ap, prog);
  while ((arg = va_arg (ap, const gchar *)))
    {
      if (argc < max_argc)
        argv[argc] = arg;
      argc++;
    }
  error = va_arg (ap, GError **);
  va_end (ap);

  if (argc > max_argc)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Too many arguments.");
      return FALSE;
    }

  argv[argc] = NULL;
  if (!g_spawn_sync (NULL,
                     (gchar **)argv,
                     NULL,
                     G_SPAWN_SEARCH_PATH,
                     NULL,
                     NULL,
                     &standard_output,
                     &standard_error,
                     &exit_status,
                     error))
    return FALSE;

  if (!g_spawn_check_exit_status (exit_status, error))
    {
      g_prefix_error (error, "stdout: '%s', stderr: '%s', ", standard_output, standard_error);
      g_free (standard_output);
      g_free (standard_error);
      return FALSE;
    }

  g_free (standard_output);
  g_free (standard_error);
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
  UDisksObject *volume_group_object;
  UDisksVolumeGroup *volume_group;
  gchar *volume_group_name = NULL;
  gboolean was_partitioned;

  const gchar *device_file;
  int fd = -1;
  gchar zeroes[512];
  gboolean ret = TRUE;
  GError *local_error = NULL;

  /* Find the name of the volume group that this device is a physical
   * member of, if any.  Easy.
   */

  block_object = UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (block)));
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

  /* Remove partition table */
  memset (zeroes, 0, 512);
  fd = open (device_file, O_RDWR | O_EXCL);
  if (fd < 0)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error opening device %s: %m",
                   device_file);
      ret = FALSE;
      goto out;
    }

  if (write (fd, zeroes, 512) != 512)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error erasing device %s: %m",
                   device_file);
      ret = FALSE;
      goto out;
    }

  if (was_partitioned && ioctl (fd, BLKRRPART, NULL) < 0)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error removing partition devices of %s: %m",
                   device_file);
      ret = FALSE;
      goto out;
    }
  close (fd);
  fd = -1;

  /* wipe other labels */
  if (!run_sync ("wipefs", "-a", device_file, NULL, error))
    {
      ret = FALSE;
      goto out;
    }

  /* Try to bring affected volume group back into consistency. */
  if (volume_group_name != NULL)
    if (!bd_lvm_vgreduce (volume_group_name, NULL /* device */, NULL /* extra */, &local_error)) {
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
  if (!run_sync ("pvscan", "--cache", device_file, NULL, &local_error))
    {
      udisks_warning ("%s", local_error->message);
      g_clear_error (&local_error);
    }

 out:
  if (fd >= 0)
    close (fd);
  g_free (volume_group_name);
  return ret;
}

/* -------------------------------------------------------------------------------- */

UDisksLinuxVolumeGroupObject *
udisks_daemon_util_lvm2_find_volume_group_object (UDisksDaemon *daemon,
                                                  const gchar  *name)
{
  UDisksLVM2State *state;
  UDisksModuleManager *manager;

  manager = udisks_daemon_get_module_manager (daemon);
  g_assert (manager != NULL);

  state = (UDisksLVM2State *) udisks_module_manager_get_module_state_pointer (manager, LVM2_MODULE_NAME);
  g_assert (state != NULL);

  return g_hash_table_lookup (udisks_lvm2_state_get_name_to_volume_group (state), name);
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
