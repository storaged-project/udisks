/*
 * Copyright (C) 2009 David Zeuthen <david@fubar.dk>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include "devkit-disks-device.h"
#include "devkit-disks-device-private.h"

static gboolean
emit_changed_idle_cb (gpointer data)
{
  DevkitDisksDevice *device = DEVKIT_DISKS_DEVICE (data);

  //g_debug ("XXX emitting 'changed' in idle");

  g_signal_emit_by_name (device->priv->daemon,
                         "device-changed",
                         device->priv->object_path);
  g_signal_emit_by_name (device, "changed");

  device->priv->emit_changed_idle_id = 0;

  /* remove the idle source */
  return FALSE;
}

static void
emit_changed (DevkitDisksDevice *device, const gchar *name)
{
  //g_debug ("property %s changed for %s", name, device->priv->device_file);

  if (device->priv->object_path != NULL)
    {
      /* schedule a 'changed' signal in idle if one hasn't been scheduled already */
      if (device->priv->emit_changed_idle_id == 0) {
        device->priv->emit_changed_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT,
                                                              emit_changed_idle_cb,
                                                              g_object_ref (device),
                                                              (GDestroyNotify) g_object_unref);
      }
    }
}

static gboolean
ptr_str_array_equals_strv (GPtrArray *a, GStrv b)
{
  guint n;
  guint b_len;

  if (a->len == 0 && b == NULL)
    return TRUE;

  b_len = (b != NULL ? g_strv_length (b) : 0);

  if (a->len != b_len)
    return FALSE;

  for (n = 0; n < a->len; n++)
    {
      if (g_strcmp0 ((gchar *) a->pdata[n], b[n]) != 0)
	return FALSE;
    }

  return TRUE;
}

static void
ptr_str_array_free (GPtrArray *p)
{
  g_ptr_array_foreach (p, (GFunc) g_free, NULL);
  g_ptr_array_free (p, TRUE);
}

static GPtrArray *
ptr_str_array_from_strv (GStrv s)
{
  GPtrArray *ret;
  guint n;

  ret = g_ptr_array_new ();
  for (n = 0; s != NULL && s[n] != NULL; n++)
    g_ptr_array_add (ret, g_strdup (s[n]));

  return ret;
}

void
devkit_disks_device_set_job_in_progress (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->job_in_progress != value))
    {
      device->priv->job_in_progress = value;
      emit_changed (device, "job_in_progress");
    }
}

void
devkit_disks_device_set_job_id (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->job_id, value) != 0))
    {
      g_free (device->priv->job_id);
      device->priv->job_id = g_strdup (value);
      emit_changed (device, "job_id");
    }
}

void
devkit_disks_device_set_job_initiated_by_uid (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->job_initiated_by_uid != value))
    {
      device->priv->job_initiated_by_uid = value;
      emit_changed (device, "job_initiated_by_uid");
    }
}

void
devkit_disks_device_set_job_is_cancellable (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->job_is_cancellable != value))
    {
      device->priv->job_is_cancellable = value;
      emit_changed (device, "job_is_cancellable");
    }
}

void
devkit_disks_device_set_job_percentage (DevkitDisksDevice *device, gdouble value)
{
  if (G_UNLIKELY (device->priv->job_percentage != value))
    {
      device->priv->job_percentage = value;
      emit_changed (device, "job_percentage");
    }
}

void
devkit_disks_device_set_device_file (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->device_file, value) != 0))
    {
      g_free (device->priv->device_file);
      device->priv->device_file = g_strdup (value);
      emit_changed (device, "device_file");
    }
}

void
devkit_disks_device_set_device_file_by_id (DevkitDisksDevice *device, GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->device_file_by_id, value)))
    {
      ptr_str_array_free (device->priv->device_file_by_id);
      device->priv->device_file_by_id = ptr_str_array_from_strv (value);
      emit_changed (device, "device_file_by_id");
    }
}

void
devkit_disks_device_set_device_file_by_path (DevkitDisksDevice *device, GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->device_file_by_path, value)))
    {
      ptr_str_array_free (device->priv->device_file_by_path);
      device->priv->device_file_by_path = ptr_str_array_from_strv (value);
      emit_changed (device, "device_file_by_path");
    }
}

void
devkit_disks_device_set_device_is_system_internal (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_system_internal != value))
    {
      device->priv->device_is_system_internal = value;
      emit_changed (device, "device_is_system_internal");
    }
}

void
devkit_disks_device_set_device_is_partition (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_partition != value))
    {
      device->priv->device_is_partition = value;
      emit_changed (device, "device_is_partition");
    }
}

void
devkit_disks_device_set_device_is_partition_table (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_partition_table != value))
    {
      device->priv->device_is_partition_table = value;
      emit_changed (device, "device_is_partition_table");
    }
}

void
devkit_disks_device_set_device_is_removable (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_removable != value))
    {
      device->priv->device_is_removable = value;
      emit_changed (device, "device_is_removable");
    }
}

void
devkit_disks_device_set_device_is_media_available (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_available != value))
    {
      device->priv->device_is_media_available = value;
      emit_changed (device, "device_is_media_available");
    }
}

void
devkit_disks_device_set_device_is_media_change_detected (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_change_detected != value))
    {
      device->priv->device_is_media_change_detected = value;
      emit_changed (device, "device_is_media_change_detected");
    }
}

void
devkit_disks_device_set_device_is_media_change_detection_polling (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_change_detection_polling != value))
    {
      device->priv->device_is_media_change_detection_polling = value;
      emit_changed (device, "device_is_media_change_detection_polling");
    }
}

void
devkit_disks_device_set_device_is_media_change_detection_inhibitable (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_change_detection_inhibitable != value))
    {
      device->priv->device_is_media_change_detection_inhibitable = value;
      emit_changed (device, "device_is_media_change_detection_inhibitable");
    }
}

void
devkit_disks_device_set_device_is_media_change_detection_inhibited (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_change_detection_inhibited != value))
    {
      device->priv->device_is_media_change_detection_inhibited = value;
      emit_changed (device, "device_is_media_change_detection_inhibited");
    }
}

void
devkit_disks_device_set_device_is_read_only (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_read_only != value))
    {
      device->priv->device_is_read_only = value;
      emit_changed (device, "device_is_read_only");
    }
}

void
devkit_disks_device_set_device_is_drive (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_drive != value))
    {
      device->priv->device_is_drive = value;
      emit_changed (device, "device_is_drive");
    }
}

void
devkit_disks_device_set_device_is_optical_disc (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_optical_disc != value))
    {
      device->priv->device_is_optical_disc = value;
      emit_changed (device, "device_is_optical_disc");
    }
}

void
devkit_disks_device_set_device_is_luks (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_luks != value))
    {
      device->priv->device_is_luks = value;
      emit_changed (device, "device_is_luks");
    }
}

void
devkit_disks_device_set_device_is_luks_cleartext (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_luks_cleartext != value))
    {
      device->priv->device_is_luks_cleartext = value;
      emit_changed (device, "device_is_luks_cleartext");
    }
}

void
devkit_disks_device_set_device_is_linux_md_component (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_linux_md_component != value))
    {
      device->priv->device_is_linux_md_component = value;
      emit_changed (device, "device_is_linux_md_component");
    }
}

void
devkit_disks_device_set_device_is_linux_md (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_linux_md != value))
    {
      device->priv->device_is_linux_md = value;
      emit_changed (device, "device_is_linux_md");
    }
}

void
devkit_disks_device_set_device_size (DevkitDisksDevice *device, guint64 value)
{
  if (G_UNLIKELY (device->priv->device_size != value))
    {
      device->priv->device_size = value;
      emit_changed (device, "device_size");
    }
}

void
devkit_disks_device_set_device_block_size (DevkitDisksDevice *device, guint64 value)
{
  if (G_UNLIKELY (device->priv->device_block_size != value))
    {
      device->priv->device_block_size = value;
      emit_changed (device, "device_block_size");
    }
}

void
devkit_disks_device_set_device_is_mounted (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_mounted != value))
    {
      device->priv->device_is_mounted = value;
      emit_changed (device, "device_is_mounted");
    }
}

void
devkit_disks_device_set_device_mount_paths (DevkitDisksDevice *device, GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->device_mount_paths, value)))
    {
      ptr_str_array_free (device->priv->device_mount_paths);
      device->priv->device_mount_paths = ptr_str_array_from_strv (value);
      emit_changed (device, "device_mount_paths");
    }
}

void
devkit_disks_device_set_device_presentation_name (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->device_presentation_name, value) != 0))
    {
      g_free (device->priv->device_presentation_name);
      device->priv->device_presentation_name = g_strdup (value);
      emit_changed (device, "device_presentation_name");
    }
}

void
devkit_disks_device_set_device_presentation_icon_name (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->device_presentation_icon_name, value) != 0))
    {
      g_free (device->priv->device_presentation_icon_name);
      device->priv->device_presentation_icon_name = g_strdup (value);
      emit_changed (device, "device_presentation_icon_name");
    }
}

void
devkit_disks_device_set_device_mounted_by_uid (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->device_mounted_by_uid != value))
    {
      device->priv->device_mounted_by_uid = value;
      emit_changed (device, "device_mounted_by_uid");
    }
}

void
devkit_disks_device_set_id_usage (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_usage, value) != 0))
    {
      g_free (device->priv->id_usage);
      device->priv->id_usage = g_strdup (value);
      emit_changed (device, "id_usage");
    }
}

void
devkit_disks_device_set_id_type (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_type, value) != 0))
    {
      g_free (device->priv->id_type);
      device->priv->id_type = g_strdup (value);
      emit_changed (device, "id_type");
    }
}

void
devkit_disks_device_set_id_version (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_version, value) != 0))
    {
      g_free (device->priv->id_version);
      device->priv->id_version = g_strdup (value);
      emit_changed (device, "id_version");
    }
}

void
devkit_disks_device_set_id_uuid (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_uuid, value) != 0))
    {
      g_free (device->priv->id_uuid);
      device->priv->id_uuid = g_strdup (value);
      emit_changed (device, "id_uuid");
    }
}

void
devkit_disks_device_set_id_label (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_label, value) != 0))
    {
      g_free (device->priv->id_label);
      device->priv->id_label = g_strdup (value);
      emit_changed (device, "id_label");
    }
}

void
devkit_disks_device_set_partition_slave (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_slave, value) != 0))
    {
      g_free (device->priv->partition_slave);
      device->priv->partition_slave = g_strdup (value);
      emit_changed (device, "partition_slave");
    }
}

void
devkit_disks_device_set_partition_scheme (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_scheme, value) != 0))
    {
      g_free (device->priv->partition_scheme);
      device->priv->partition_scheme = g_strdup (value);
      emit_changed (device, "partition_scheme");
    }
}

void
devkit_disks_device_set_partition_type (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_type, value) != 0))
    {
      g_free (device->priv->partition_type);
      device->priv->partition_type = g_strdup (value);
      emit_changed (device, "partition_type");
    }
}

void
devkit_disks_device_set_partition_label (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_label, value) != 0))
    {
      g_free (device->priv->partition_label);
      device->priv->partition_label = g_strdup (value);
      emit_changed (device, "partition_label");
    }
}

void
devkit_disks_device_set_partition_uuid (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_uuid, value) != 0))
    {
      g_free (device->priv->partition_uuid);
      device->priv->partition_uuid = g_strdup (value);
      emit_changed (device, "partition_uuid");
    }
}

void
devkit_disks_device_set_partition_flags (DevkitDisksDevice *device, GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->partition_flags, value)))
    {
      ptr_str_array_free (device->priv->partition_flags);
      device->priv->partition_flags = ptr_str_array_from_strv (value);
      emit_changed (device, "partition_flags");
    }
}

void
devkit_disks_device_set_partition_number (DevkitDisksDevice *device, gint value)
{
  if (G_UNLIKELY (device->priv->partition_number != value))
    {
      device->priv->partition_number = value;
      emit_changed (device, "partition_number");
    }
}

void
devkit_disks_device_set_partition_offset (DevkitDisksDevice *device, guint64 value)
{
  if (G_UNLIKELY (device->priv->partition_offset != value))
    {
      device->priv->partition_offset = value;
      emit_changed (device, "partition_offset");
    }
}

void
devkit_disks_device_set_partition_size (DevkitDisksDevice *device, guint64 value)
{
  if (G_UNLIKELY (device->priv->partition_size != value))
    {
      device->priv->partition_size = value;
      emit_changed (device, "partition_size");
    }
}

void
devkit_disks_device_set_partition_table_scheme (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_table_scheme, value) != 0))
    {
      g_free (device->priv->partition_table_scheme);
      device->priv->partition_table_scheme = g_strdup (value);
      emit_changed (device, "partition_table_scheme");
    }
}

void
devkit_disks_device_set_partition_table_count (DevkitDisksDevice *device, gint value)
{
  if (G_UNLIKELY (device->priv->partition_table_count != value))
    {
      device->priv->partition_table_count = value;
      emit_changed (device, "partition_table_count");
    }
}

void
devkit_disks_device_set_drive_vendor (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_vendor, value) != 0))
    {
      g_free (device->priv->drive_vendor);
      device->priv->drive_vendor = g_strdup (value);
      emit_changed (device, "drive_vendor");
    }
}

void
devkit_disks_device_set_drive_model (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_model, value) != 0))
    {
      g_free (device->priv->drive_model);
      device->priv->drive_model = g_strdup (value);
      emit_changed (device, "drive_model");
    }
}

void
devkit_disks_device_set_drive_revision (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_revision, value) != 0))
    {
      g_free (device->priv->drive_revision);
      device->priv->drive_revision = g_strdup (value);
      emit_changed (device, "drive_revision");
    }
}

void
devkit_disks_device_set_drive_serial (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_serial, value) != 0))
    {
      g_free (device->priv->drive_serial);
      device->priv->drive_serial = g_strdup (value);
      emit_changed (device, "drive_serial");
    }
}

void
devkit_disks_device_set_drive_connection_interface (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_connection_interface, value) != 0))
    {
      g_free (device->priv->drive_connection_interface);
      device->priv->drive_connection_interface = g_strdup (value);
      emit_changed (device, "drive_connection_interface");
    }
}

void
devkit_disks_device_set_drive_connection_speed (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->drive_connection_speed != value))
    {
      device->priv->drive_connection_speed = value;
      emit_changed (device, "drive_connection_speed");
    }
}

void
devkit_disks_device_set_drive_media_compatibility (DevkitDisksDevice *device, GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->drive_media_compatibility, value)))
    {
      ptr_str_array_free (device->priv->drive_media_compatibility);
      device->priv->drive_media_compatibility = ptr_str_array_from_strv (value);
      emit_changed (device, "drive_media_compatibility");
    }
}

void
devkit_disks_device_set_drive_media (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_media, value) != 0))
    {
      g_free (device->priv->drive_media);
      device->priv->drive_media = g_strdup (value);
      emit_changed (device, "drive_media");
    }
}

void
devkit_disks_device_set_drive_is_media_ejectable (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_is_media_ejectable != value))
    {
      device->priv->drive_is_media_ejectable = value;
      emit_changed (device, "drive_is_media_ejectable");
    }
}

void
devkit_disks_device_set_drive_requires_eject (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_requires_eject != value))
    {
      device->priv->drive_requires_eject = value;
      emit_changed (device, "drive_requires_eject");
    }
}

void
devkit_disks_device_set_optical_disc_is_blank (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->optical_disc_is_blank != value))
    {
      device->priv->optical_disc_is_blank = value;
      emit_changed (device, "optical_disc_is_blank");
    }
}

void
devkit_disks_device_set_optical_disc_is_appendable (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->optical_disc_is_appendable != value))
    {
      device->priv->optical_disc_is_appendable = value;
      emit_changed (device, "optical_disc_is_appendable");
    }
}

void
devkit_disks_device_set_optical_disc_is_closed (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->optical_disc_is_closed != value))
    {
      device->priv->optical_disc_is_closed = value;
      emit_changed (device, "optical_disc_is_closed");
    }
}

void
devkit_disks_device_set_optical_disc_num_tracks (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->optical_disc_num_tracks != value))
    {
      device->priv->optical_disc_num_tracks = value;
      emit_changed (device, "optical_disc_num_tracks");
    }
}

void
devkit_disks_device_set_optical_disc_num_audio_tracks (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->optical_disc_num_audio_tracks != value))
    {
      device->priv->optical_disc_num_audio_tracks = value;
      emit_changed (device, "optical_disc_num_audio_tracks");
    }
}

void
devkit_disks_device_set_optical_disc_num_sessions (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->optical_disc_num_sessions != value))
    {
      device->priv->optical_disc_num_sessions = value;
      emit_changed (device, "optical_disc_num_sessions");
    }
}

void
devkit_disks_device_set_luks_holder (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->luks_holder, value) != 0))
    {
      g_free (device->priv->luks_holder);
      device->priv->luks_holder = g_strdup (value);
      emit_changed (device, "luks_holder");
    }
}

void
devkit_disks_device_set_luks_cleartext_slave (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->luks_cleartext_slave, value) != 0))
    {
      g_free (device->priv->luks_cleartext_slave);
      device->priv->luks_cleartext_slave = g_strdup (value);
      emit_changed (device, "luks_cleartext_slave");
    }
}

void
devkit_disks_device_set_luks_cleartext_unlocked_by_uid (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->luks_cleartext_unlocked_by_uid != value))
    {
      device->priv->luks_cleartext_unlocked_by_uid = value;
      emit_changed (device, "luks_cleartext_unlocked_by_uid");
    }
}

void
devkit_disks_device_set_linux_md_component_level (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_level, value) != 0))
    {
      g_free (device->priv->linux_md_component_level);
      device->priv->linux_md_component_level = g_strdup (value);
      emit_changed (device, "linux_md_component_level");
    }
}

void
devkit_disks_device_set_linux_md_component_num_raid_devices (DevkitDisksDevice *device, gint value)
{
  if (G_UNLIKELY (device->priv->linux_md_component_num_raid_devices != value))
    {
      device->priv->linux_md_component_num_raid_devices = value;
      emit_changed (device, "linux_md_component_num_raid_devices");
    }
}

void
devkit_disks_device_set_linux_md_component_uuid (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_uuid, value) != 0))
    {
      g_free (device->priv->linux_md_component_uuid);
      device->priv->linux_md_component_uuid = g_strdup (value);
      emit_changed (device, "linux_md_component_uuid");
    }
}

void
devkit_disks_device_set_linux_md_component_home_host (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_home_host, value) != 0))
    {
      g_free (device->priv->linux_md_component_home_host);
      device->priv->linux_md_component_home_host = g_strdup (value);
      emit_changed (device, "linux_md_component_home_host");
    }
}

void
devkit_disks_device_set_linux_md_component_name (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_name, value) != 0))
    {
      g_free (device->priv->linux_md_component_name);
      device->priv->linux_md_component_name = g_strdup (value);
      emit_changed (device, "linux_md_component_name");
    }
}

void
devkit_disks_device_set_linux_md_component_version (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_version, value) != 0))
    {
      g_free (device->priv->linux_md_component_version);
      device->priv->linux_md_component_version = g_strdup (value);
      emit_changed (device, "linux_md_component_version");
    }
}

void
devkit_disks_device_set_linux_md_component_holder (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_holder, value) != 0))
    {
      g_free (device->priv->linux_md_component_holder);
      device->priv->linux_md_component_holder = g_strdup (value);
      emit_changed (device, "linux_md_component_holder");
    }
}

void
devkit_disks_device_set_linux_md_component_state (DevkitDisksDevice *device, GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->linux_md_component_state, value)))
    {
      ptr_str_array_free (device->priv->linux_md_component_state);
      device->priv->linux_md_component_state = ptr_str_array_from_strv (value);
      emit_changed (device, "linux_md_component_state");
    }
}

void
devkit_disks_device_set_linux_md_state (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_state, value) != 0))
    {
      g_free (device->priv->linux_md_state);
      device->priv->linux_md_state = g_strdup (value);
      emit_changed (device, "linux_md_state");
    }
}

void
devkit_disks_device_set_linux_md_level (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_level, value) != 0))
    {
      g_free (device->priv->linux_md_level);
      device->priv->linux_md_level = g_strdup (value);
      emit_changed (device, "linux_md_level");
    }
}

void
devkit_disks_device_set_linux_md_num_raid_devices (DevkitDisksDevice *device, gint value)
{
  if (G_UNLIKELY (device->priv->linux_md_num_raid_devices != value))
    {
      device->priv->linux_md_num_raid_devices = value;
      emit_changed (device, "linux_md_num_raid_devices");
    }
}

void
devkit_disks_device_set_linux_md_uuid (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_uuid, value) != 0))
    {
      g_free (device->priv->linux_md_uuid);
      device->priv->linux_md_uuid = g_strdup (value);
      emit_changed (device, "linux_md_uuid");
    }
}

void
devkit_disks_device_set_linux_md_home_host (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_home_host, value) != 0))
    {
      g_free (device->priv->linux_md_home_host);
      device->priv->linux_md_home_host = g_strdup (value);
      emit_changed (device, "linux_md_home_host");
    }
}

void
devkit_disks_device_set_linux_md_name (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_name, value) != 0))
    {
      g_free (device->priv->linux_md_name);
      device->priv->linux_md_name = g_strdup (value);
      emit_changed (device, "linux_md_name");
    }
}

void
devkit_disks_device_set_linux_md_version (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_version, value) != 0))
    {
      g_free (device->priv->linux_md_version);
      device->priv->linux_md_version = g_strdup (value);
      emit_changed (device, "linux_md_version");
    }
}

void
devkit_disks_device_set_linux_md_slaves (DevkitDisksDevice *device, GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->linux_md_slaves, value)))
    {
      ptr_str_array_free (device->priv->linux_md_slaves);
      device->priv->linux_md_slaves = ptr_str_array_from_strv (value);
      emit_changed (device, "linux_md_slaves");
    }
}

void
devkit_disks_device_set_linux_md_is_degraded (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->linux_md_is_degraded != value))
    {
      device->priv->linux_md_is_degraded = value;
      emit_changed (device, "linux_md_is_degraded");
    }
}

void
devkit_disks_device_set_linux_md_sync_action (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_sync_action, value) != 0))
    {
      g_free (device->priv->linux_md_sync_action);
      device->priv->linux_md_sync_action = g_strdup (value);
      emit_changed (device, "linux_md_sync_action");
    }
}

void
devkit_disks_device_set_linux_md_sync_percentage (DevkitDisksDevice *device, gdouble value)
{
  if (G_UNLIKELY (device->priv->linux_md_sync_percentage != value))
    {
      device->priv->linux_md_sync_percentage = value;
      emit_changed (device, "linux_md_sync_percentage");
    }
}

void
devkit_disks_device_set_linux_md_sync_speed (DevkitDisksDevice *device, guint64 value)
{
  if (G_UNLIKELY (device->priv->linux_md_sync_speed != value))
    {
      device->priv->linux_md_sync_speed = value;
      emit_changed (device, "linux_md_sync_speed");
    }
}

void
devkit_disks_device_set_dm_name (DevkitDisksDevice *device, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->dm_name, value) != 0))
    {
      g_free (device->priv->dm_name);
      device->priv->dm_name = g_strdup (value);
      emit_changed (device, "dm_name");
    }
}

void
devkit_disks_device_set_slaves_objpath (DevkitDisksDevice *device, GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->slaves_objpath, value)))
    {
      ptr_str_array_free (device->priv->slaves_objpath);
      device->priv->slaves_objpath = ptr_str_array_from_strv (value);
      emit_changed (device, "slaves_objpath");
    }
}

void
devkit_disks_device_set_holders_objpath (DevkitDisksDevice *device, GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->holders_objpath, value)))
    {
      ptr_str_array_free (device->priv->holders_objpath);
      device->priv->holders_objpath = ptr_str_array_from_strv (value);
      emit_changed (device, "holders_objpath");
    }
}


void
devkit_disks_device_set_drive_ata_smart_is_available (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_is_available != value))
    {
      device->priv->drive_ata_smart_is_available = value;
      emit_changed (device, "drive_ata_smart_is_available");
    }
}

void
devkit_disks_device_set_drive_ata_smart_is_failing (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_is_failing != value))
    {
      device->priv->drive_ata_smart_is_failing = value;
      emit_changed (device, "drive_ata_smart_is_failing");
    }
}

void
devkit_disks_device_set_drive_ata_smart_is_failing_valid (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_is_failing_valid != value))
    {
      device->priv->drive_ata_smart_is_failing_valid = value;
      emit_changed (device, "drive_ata_smart_is_failing_valid");
    }
}

void
devkit_disks_device_set_drive_ata_smart_has_bad_sectors (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_has_bad_sectors != value))
    {
      device->priv->drive_ata_smart_has_bad_sectors = value;
      emit_changed (device, "drive_ata_smart_has_bad_sectors");
    }
}

void
devkit_disks_device_set_drive_ata_smart_has_bad_attributes (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_has_bad_attributes != value))
    {
      device->priv->drive_ata_smart_has_bad_attributes = value;
      emit_changed (device, "drive_ata_smart_has_bad_attributes");
    }
}

void
devkit_disks_device_set_drive_ata_smart_temperature_kelvin (DevkitDisksDevice *device, gdouble value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_temperature_kelvin != value))
    {
      device->priv->drive_ata_smart_temperature_kelvin = value;
      emit_changed (device, "drive_ata_smart_temperature_kelvin");
    }
}

void
devkit_disks_device_set_drive_ata_smart_power_on_seconds (DevkitDisksDevice *device, guint64 value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_power_on_seconds != value))
    {
      device->priv->drive_ata_smart_power_on_seconds = value;
      emit_changed (device, "drive_ata_smart_power_on_seconds");
    }
}

void
devkit_disks_device_set_drive_ata_smart_time_collected (DevkitDisksDevice *device, guint64 value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_time_collected != value))
    {
      device->priv->drive_ata_smart_time_collected = value;
      emit_changed (device, "drive_ata_smart_time_collected");
    }
}

void
devkit_disks_device_set_drive_ata_smart_offline_data_collection_status (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_offline_data_collection_status != value))
    {
      device->priv->drive_ata_smart_offline_data_collection_status = value;
      emit_changed (device, "drive_ata_smart_offline_data_collection_status");
    }
}

void
devkit_disks_device_set_drive_ata_smart_offline_data_collection_seconds (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_offline_data_collection_seconds != value))
    {
      device->priv->drive_ata_smart_offline_data_collection_seconds = value;
      emit_changed (device, "drive_ata_smart_offline_data_collection_seconds");
    }
}

void
devkit_disks_device_set_drive_ata_smart_self_test_execution_status (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_self_test_execution_status != value))
    {
      device->priv->drive_ata_smart_self_test_execution_status = value;
      emit_changed (device, "drive_ata_smart_self_test_execution_status");
    }
}

void
devkit_disks_device_set_drive_ata_smart_self_test_execution_percent_remaining (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_self_test_execution_percent_remaining != value))
    {
      device->priv->drive_ata_smart_self_test_execution_percent_remaining = value;
      emit_changed (device, "drive_ata_smart_self_test_execution_percent_remaining");
    }
}

void
devkit_disks_device_set_drive_ata_smart_short_and_extended_self_test_available (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_short_and_extended_self_test_available != value))
    {
      device->priv->drive_ata_smart_short_and_extended_self_test_available = value;
      emit_changed (device, "drive_ata_smart_short_and_extended_self_test_available");
    }
}

void
devkit_disks_device_set_drive_ata_smart_conveyance_self_test_available (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_conveyance_self_test_available != value))
    {
      device->priv->drive_ata_smart_conveyance_self_test_available = value;
      emit_changed (device, "drive_ata_smart_conveyance_self_test_available");
    }
}

void
devkit_disks_device_set_drive_ata_smart_start_self_test_available (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_start_self_test_available != value))
    {
      device->priv->drive_ata_smart_start_self_test_available = value;
      emit_changed (device, "drive_ata_smart_start_self_test_available");
    }
}

void
devkit_disks_device_set_drive_ata_smart_abort_self_test_available (DevkitDisksDevice *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_abort_self_test_available != value))
    {
      device->priv->drive_ata_smart_abort_self_test_available = value;
      emit_changed (device, "drive_ata_smart_abort_self_test_available");
    }
}

void
devkit_disks_device_set_drive_ata_smart_short_self_test_polling_minutes (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_short_self_test_polling_minutes != value))
    {
      device->priv->drive_ata_smart_short_self_test_polling_minutes = value;
      emit_changed (device, "drive_ata_smart_short_self_test_polling_minutes");
    }
}

void
devkit_disks_device_set_drive_ata_smart_extended_self_test_polling_minutes (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_extended_self_test_polling_minutes != value))
    {
      device->priv->drive_ata_smart_extended_self_test_polling_minutes = value;
      emit_changed (device, "drive_ata_smart_extended_self_test_polling_minutes");
    }
}

void
devkit_disks_device_set_drive_ata_smart_conveyance_self_test_polling_minutes (DevkitDisksDevice *device, guint value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_conveyance_self_test_polling_minutes != value))
    {
      device->priv->drive_ata_smart_conveyance_self_test_polling_minutes = value;
      emit_changed (device, "drive_ata_smart_conveyance_self_test_polling_minutes");
    }
}

void
devkit_disks_device_set_drive_ata_smart_attributes_steal (DevkitDisksDevice *device, GPtrArray *attributes)
{
  /* TODO: compare? Not really needed, this happens very rarely */

  g_ptr_array_foreach (device->priv->drive_ata_smart_attributes, (GFunc) g_value_array_free, NULL);
  g_ptr_array_free (device->priv->drive_ata_smart_attributes, TRUE);

  device->priv->drive_ata_smart_attributes = attributes;

  emit_changed (device, "drive_ata_smart_attributes");
}
