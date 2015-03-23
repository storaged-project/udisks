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

#include <limits.h>
#include <stdlib.h>

#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedstate.h>
#include <src/storagedlogging.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storagedlinuxdriveobject.h>
#include <src/storagedmodulemanager.h>

#include "storagedlvm2daemonutil.h"
#include "storagedlvm2dbusutil.h"
#include "storagedlvm2state.h"
#include "storaged-lvm2-generated.h"

/**
 * SECTION:storagedlvm2daemonutil
 * @title: Utilities
 * @short_description: Various utility routines
 *
 * Various utility routines.
 */

gboolean
storaged_daemon_util_lvm2_block_is_unused (StoragedBlock *block,
                                           GError       **error)
{
  const gchar *device_file;
  int fd;

  device_file = storaged_block_get_device (block);
  fd = open (device_file, O_RDONLY | O_EXCL);
  if (fd < 0)
    {
      g_set_error (error, STORAGED_ERROR, STORAGED_ERROR_FAILED,
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
      g_set_error (error, STORAGED_ERROR, STORAGED_ERROR_FAILED,
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
storaged_daemon_util_lvm2_wipe_block (StoragedDaemon *daemon,
                                      StoragedBlock  *block,
                                      GError        **error)
{
  StoragedObject *block_object;
  StoragedPhysicalVolume *physical_volume;
  const gchar *volume_group_objpath;
  StoragedObject *volume_group_object;
  StoragedVolumeGroup *volume_group;
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

  block_object = STORAGED_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (block)));
  physical_volume = storaged_object_peek_physical_volume (block_object);
  if (physical_volume)
    {
      volume_group_objpath = storaged_physical_volume_get_volume_group (physical_volume);
      volume_group_object = storaged_daemon_find_object (daemon, volume_group_objpath);
      if (volume_group_object)
        {
          volume_group = storaged_object_peek_volume_group (volume_group_object);
          if (volume_group)
            volume_group_name = g_strdup (storaged_volume_group_get_name (volume_group));
        }
    }

  was_partitioned = (storaged_object_peek_partition_table (block_object) != NULL);

  device_file = storaged_block_get_device (block);

  /* Remove partition table */
  memset (zeroes, 0, 512);
  fd = open (device_file, O_RDWR | O_EXCL);
  if (fd < 0)
    {
      g_set_error (error, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                   "Error opening device %s: %m",
                   device_file);
      ret = FALSE;
      goto out;
    }

  if (write (fd, zeroes, 512) != 512)
    {
      g_set_error (error, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                   "Error erasing device %s: %m",
                   device_file);
      ret = FALSE;
      goto out;
    }

  if (was_partitioned && ioctl (fd, BLKRRPART, NULL) < 0)
    {
      g_set_error (error, STORAGED_ERROR, STORAGED_ERROR_FAILED,
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
    run_sync ("vgreduce", volume_group_name, "--removemissing", NULL, NULL);

  /* Make sure lvmetad knows about all this.
   *
   * XXX - We need to do this because of a bug in the LVM udev rules
   * which often fail to run pvscan on "change" events.
   *
   * https://bugzilla.redhat.com/show_bug.cgi?id=1063813
   */
  if (!run_sync ("pvscan", "--cache", device_file, NULL, &local_error))
    {
      storaged_warning ("%s", local_error->message);
      g_clear_error (&local_error);
    }

 out:
  if (fd >= 0)
    close (fd);
  g_free (volume_group_name);
  return ret;
}

/* -------------------------------------------------------------------------------- */

struct VariantReaderData {
  const GVariantType *type;
  void (*callback) (GPid pid, GVariant *result, GError *error, gpointer user_data);
  gpointer user_data;
  GPid pid;
  GIOChannel *output_channel;
  GByteArray *output;
  gint output_watch;
};

static gboolean
variant_reader_child_output (GIOChannel *source,
                             GIOCondition condition,
                             gpointer user_data)
{
  struct VariantReaderData *data = user_data;
  guint8 buf[1024];
  gsize bytes_read;

  g_io_channel_read_chars (source, (gchar *)buf, sizeof buf, &bytes_read, NULL);
  g_byte_array_append (data->output, buf, bytes_read);
  return TRUE;
}

static void
variant_reader_watch_child (GPid     pid,
                            gint     status,
                            gpointer user_data)
{
  struct VariantReaderData *data = user_data;
  guint8 *buf;
  gsize buf_size;
  GVariant *result;
  GError *error = NULL;

  data->pid = 0;

  if (!g_spawn_check_exit_status (status, &error))
    {
      data->callback (pid, NULL, error, data->user_data);
      g_error_free (error);
      g_byte_array_free (data->output, TRUE);
    }
  else
    {
      if (g_io_channel_read_to_end (data->output_channel, (gchar **)&buf, &buf_size, NULL) == G_IO_STATUS_NORMAL)
        {
          g_byte_array_append (data->output, buf, buf_size);
          g_free (buf);
        }

      result = g_variant_new_from_data (data->type,
                                        data->output->data,
                                        data->output->len,
                                        TRUE,
                                        g_free, NULL);
      g_byte_array_free (data->output, FALSE);
      data->callback (pid, result, NULL, data->user_data);
      g_variant_unref (result);
    }
}

static void
variant_reader_destroy (gpointer user_data)
{
  struct VariantReaderData *data = user_data;

  g_source_remove (data->output_watch);
  g_io_channel_unref (data->output_channel);
  g_free (data);
}

GPid
storaged_daemon_util_lvm2_spawn_for_variant (const gchar       **argv,
                                             const GVariantType *type,
                                             void (*callback) (GPid pid,
                                                               GVariant *result,
                                                               GError *error,
                                                               gpointer user_data),
                                             gpointer            user_data)
{
  GError *error = NULL;
  struct VariantReaderData *data;
  GPid pid;
  gint output_fd;

  if (!g_spawn_async_with_pipes (NULL,
                                 (gchar **)argv,
                                 NULL,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 &pid,
                                 NULL,
                                 &output_fd,
                                 NULL,
                                 &error))
    {
      callback (0, NULL, error, user_data);
      g_error_free (error);
      return 0;
    }

  data = g_new0 (struct VariantReaderData, 1);

  data->type = type;
  data->callback = callback;
  data->user_data = user_data;

  data->pid = pid;
  data->output = g_byte_array_new ();
  data->output_channel = g_io_channel_unix_new (output_fd);
  g_io_channel_set_encoding (data->output_channel, NULL, NULL);
  g_io_channel_set_flags (data->output_channel, G_IO_FLAG_NONBLOCK, NULL);
  data->output_watch = g_io_add_watch (data->output_channel, G_IO_IN, variant_reader_child_output, data);

  g_child_watch_add_full (G_PRIORITY_DEFAULT_IDLE,
                          pid, variant_reader_watch_child, data, variant_reader_destroy);
  return pid;
}

StoragedLinuxVolumeGroupObject *
storaged_daemon_util_lvm2_find_volume_group_object (StoragedDaemon *daemon,
                                                    const gchar    *name)
{
  StoragedLVM2State *state;
  StoragedModuleManager *manager;

  manager = storaged_daemon_get_module_manager (daemon);
  g_assert (manager != NULL);

  state = (StoragedLVM2State *) storaged_module_manager_get_module_state_pointer (manager, LVM2_MODULE_NAME);
  g_assert (state != NULL);

  return g_hash_table_lookup (state->name_to_volume_group, name);
}

/* -------------------------------------------------------------------------------- */

gboolean
storaged_daemon_util_lvm2_name_is_reserved (const gchar *name)
{
 /* XXX - get this from lvm2app */

 return (strstr (name, "_mlog")
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
storaged_daemon_util_lvm2_trigger_udev (const gchar *device_file)
{
  int fd = open (device_file, O_RDWR);
  if (fd >= 0)
    close (fd);
}
