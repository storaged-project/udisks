/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
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
#include "device.h"
#include "device-private.h"

static gboolean
emit_changed_idle_cb (gpointer data)
{
  Device *device = DEVICE (data);

  //g_debug ("XXX emitting 'changed' in idle");

  if (!device->priv->removed)
    {
      g_print ("**** EMITTING CHANGED for %s\n", device->priv->native_path);
      g_signal_emit_by_name (device->priv->daemon, "device-changed", device->priv->object_path);
      g_signal_emit_by_name (device, "changed");
    }
  device->priv->emit_changed_idle_id = 0;

  /* remove the idle source */
  return FALSE;
}

static void
emit_changed (Device *device,
              const gchar *name)
{
  //g_debug ("property %s changed for %s", name, device->priv->device_file);

  if (device->priv->object_path != NULL)
    {
      /* schedule a 'changed' signal in idle if one hasn't been scheduled already */
      if (device->priv->emit_changed_idle_id == 0)
        {
          device->priv->emit_changed_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT,
                                                                emit_changed_idle_cb,
                                                                g_object_ref (device),
                                                                (GDestroyNotify) g_object_unref);
        }
    }
}

static gboolean
ptr_str_array_equals_strv (GPtrArray *a,
                           GStrv b)
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
device_set_device_automount_hint (Device *device,
                                  const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->device_automount_hint, value) != 0))
    {
      g_free (device->priv->device_automount_hint);
      device->priv->device_automount_hint = g_strdup (value);
      emit_changed (device, "device_automount_hint");
    }
}

void
device_set_device_detection_time (Device *device,
                                  guint64 value)
{
  if (G_UNLIKELY (device->priv->device_detection_time != value))
    {
      device->priv->device_detection_time = value;
      emit_changed (device, "device_detection_time");
    }
}

void
device_set_device_media_detection_time (Device *device,
                                        guint64 value)
{
  if (G_UNLIKELY (device->priv->device_media_detection_time != value))
    {
      device->priv->device_media_detection_time = value;
      emit_changed (device, "device_media_detection_time");
    }
}

void
device_set_job_in_progress (Device *device,
                            gboolean value)
{
  if (G_UNLIKELY (device->priv->job_in_progress != value))
    {
      device->priv->job_in_progress = value;
      emit_changed (device, "job_in_progress");
    }
}

void
device_set_job_id (Device *device,
                   const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->job_id, value) != 0))
    {
      g_free (device->priv->job_id);
      device->priv->job_id = g_strdup (value);
      emit_changed (device, "job_id");
    }
}

void
device_set_job_initiated_by_uid (Device *device,
                                 guint value)
{
  if (G_UNLIKELY (device->priv->job_initiated_by_uid != value))
    {
      device->priv->job_initiated_by_uid = value;
      emit_changed (device, "job_initiated_by_uid");
    }
}

void
device_set_job_is_cancellable (Device *device,
                               gboolean value)
{
  if (G_UNLIKELY (device->priv->job_is_cancellable != value))
    {
      device->priv->job_is_cancellable = value;
      emit_changed (device, "job_is_cancellable");
    }
}

void
device_set_job_percentage (Device *device,
                           gdouble value)
{
  if (G_UNLIKELY (device->priv->job_percentage != value))
    {
      device->priv->job_percentage = value;
      emit_changed (device, "job_percentage");
    }
}

void
device_set_device_file (Device *device,
                        const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->device_file, value) != 0))
    {
      g_free (device->priv->device_file);
      device->priv->device_file = g_strdup (value);
      emit_changed (device, "device_file");
    }
}

void
device_set_device_file_presentation (Device *device,
                                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->device_file_presentation, value) != 0))
    {
      g_free (device->priv->device_file_presentation);
      device->priv->device_file_presentation = g_strdup (value);
      emit_changed (device, "device_file_presentation");
    }
}

void
device_set_device_file_by_id (Device *device,
                              GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->device_file_by_id, value)))
    {
      ptr_str_array_free (device->priv->device_file_by_id);
      device->priv->device_file_by_id = ptr_str_array_from_strv (value);
      emit_changed (device, "device_file_by_id");
    }
}

void
device_set_device_file_by_path (Device *device,
                                GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->device_file_by_path, value)))
    {
      ptr_str_array_free (device->priv->device_file_by_path);
      device->priv->device_file_by_path = ptr_str_array_from_strv (value);
      emit_changed (device, "device_file_by_path");
    }
}

void
device_set_device_is_system_internal (Device *device,
                                      gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_system_internal != value))
    {
      device->priv->device_is_system_internal = value;
      emit_changed (device, "device_is_system_internal");
    }
}

void
device_set_device_is_partition (Device *device,
                                gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_partition != value))
    {
      device->priv->device_is_partition = value;
      emit_changed (device, "device_is_partition");
    }
}

void
device_set_device_is_partition_table (Device *device,
                                      gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_partition_table != value))
    {
      device->priv->device_is_partition_table = value;
      emit_changed (device, "device_is_partition_table");
    }
}

void
device_set_device_is_removable (Device *device,
                                gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_removable != value))
    {
      device->priv->device_is_removable = value;
      emit_changed (device, "device_is_removable");
    }
}

void
device_set_device_is_media_available (Device *device,
                                      gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_available != value))
    {
      device->priv->device_is_media_available = value;
      emit_changed (device, "device_is_media_available");
    }
}

void
device_set_device_is_media_change_detected (Device *device,
                                            gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_change_detected != value))
    {
      device->priv->device_is_media_change_detected = value;
      emit_changed (device, "device_is_media_change_detected");
    }
}

void
device_set_device_is_media_change_detection_polling (Device *device,
                                                     gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_change_detection_polling != value))
    {
      device->priv->device_is_media_change_detection_polling = value;
      emit_changed (device, "device_is_media_change_detection_polling");
    }
}

void
device_set_device_is_media_change_detection_inhibitable (Device *device,
                                                         gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_change_detection_inhibitable != value))
    {
      device->priv->device_is_media_change_detection_inhibitable = value;
      emit_changed (device, "device_is_media_change_detection_inhibitable");
    }
}

void
device_set_device_is_media_change_detection_inhibited (Device *device,
                                                       gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_media_change_detection_inhibited != value))
    {
      device->priv->device_is_media_change_detection_inhibited = value;
      emit_changed (device, "device_is_media_change_detection_inhibited");
    }
}

void
device_set_device_is_read_only (Device *device,
                                gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_read_only != value))
    {
      device->priv->device_is_read_only = value;
      emit_changed (device, "device_is_read_only");
    }
}

void
device_set_device_is_drive (Device *device,
                            gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_drive != value))
    {
      device->priv->device_is_drive = value;
      emit_changed (device, "device_is_drive");
    }
}

void
device_set_device_is_optical_disc (Device *device,
                                   gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_optical_disc != value))
    {
      device->priv->device_is_optical_disc = value;
      emit_changed (device, "device_is_optical_disc");
    }
}

void
device_set_device_is_luks (Device *device,
                           gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_luks != value))
    {
      device->priv->device_is_luks = value;
      emit_changed (device, "device_is_luks");
    }
}

void
device_set_device_is_luks_cleartext (Device *device,
                                     gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_luks_cleartext != value))
    {
      device->priv->device_is_luks_cleartext = value;
      emit_changed (device, "device_is_luks_cleartext");
    }
}

void
device_set_device_is_linux_md_component (Device *device,
                                         gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_linux_md_component != value))
    {
      device->priv->device_is_linux_md_component = value;
      emit_changed (device, "device_is_linux_md_component");
    }
}

void
device_set_device_is_linux_md (Device *device,
                               gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_linux_md != value))
    {
      device->priv->device_is_linux_md = value;
      emit_changed (device, "device_is_linux_md");
    }
}

void
device_set_device_is_linux_lvm2_lv (Device *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_linux_lvm2_lv != value))
    {
      device->priv->device_is_linux_lvm2_lv = value;
      emit_changed (device, "device_is_linux_lvm2_lv");
    }
}

void
device_set_device_is_linux_lvm2_pv (Device *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_linux_lvm2_pv != value))
    {
      device->priv->device_is_linux_lvm2_pv = value;
      emit_changed (device, "device_is_linux_lvm2_pv");
    }
}

void
device_set_device_is_linux_dmmp (Device *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_linux_dmmp != value))
    {
      device->priv->device_is_linux_dmmp = value;
      emit_changed (device, "device_is_linux_dmmp");
    }
}

void
device_set_device_is_linux_dmmp_component (Device *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_linux_dmmp_component != value))
    {
      device->priv->device_is_linux_dmmp_component = value;
      emit_changed (device, "device_is_linux_dmmp_component");
    }
}

void
device_set_device_is_linux_loop (Device *device, gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_linux_loop != value))
    {
      device->priv->device_is_linux_loop = value;
      emit_changed (device, "device_is_linux_loop");
    }
}

void
device_set_device_size (Device *device,
                        guint64 value)
{
  if (G_UNLIKELY (device->priv->device_size != value))
    {
      device->priv->device_size = value;
      emit_changed (device, "device_size");
    }
}

void
device_set_device_block_size (Device *device,
                              guint64 value)
{
  if (G_UNLIKELY (device->priv->device_block_size != value))
    {
      device->priv->device_block_size = value;
      emit_changed (device, "device_block_size");
    }
}

void
device_set_device_is_mounted (Device *device,
                              gboolean value)
{
  if (G_UNLIKELY (device->priv->device_is_mounted != value))
    {
      device->priv->device_is_mounted = value;
      emit_changed (device, "device_is_mounted");
    }
}

void
device_set_device_mount_paths (Device *device,
                               GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->device_mount_paths, value)))
    {
      ptr_str_array_free (device->priv->device_mount_paths);
      device->priv->device_mount_paths = ptr_str_array_from_strv (value);
      emit_changed (device, "device_mount_paths");
    }
}

void
device_set_device_presentation_hide (Device *device,
                                     gboolean value)
{
  if (G_UNLIKELY (device->priv->device_presentation_hide != value))
    {
      device->priv->device_presentation_hide = value;
      emit_changed (device, "device_presentation_hide");
    }
}

void
device_set_device_presentation_nopolicy (Device *device,
                                         gboolean value)
{
  if (G_UNLIKELY (device->priv->device_presentation_nopolicy != value))
    {
      device->priv->device_presentation_nopolicy = value;
      emit_changed (device, "device_presentation_nopolicy");
    }
}

void
device_set_device_presentation_name (Device *device,
                                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->device_presentation_name, value) != 0))
    {
      g_free (device->priv->device_presentation_name);
      device->priv->device_presentation_name = g_strdup (value);
      emit_changed (device, "device_presentation_name");
    }
}

void
device_set_device_presentation_icon_name (Device *device,
                                          const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->device_presentation_icon_name, value) != 0))
    {
      g_free (device->priv->device_presentation_icon_name);
      device->priv->device_presentation_icon_name = g_strdup (value);
      emit_changed (device, "device_presentation_icon_name");
    }
}

void
device_set_device_mounted_by_uid (Device *device,
                                  guint value)
{
  if (G_UNLIKELY (device->priv->device_mounted_by_uid != value))
    {
      device->priv->device_mounted_by_uid = value;
      emit_changed (device, "device_mounted_by_uid");
    }
}

void
device_set_id_usage (Device *device,
                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_usage, value) != 0))
    {
      g_free (device->priv->id_usage);
      device->priv->id_usage = g_strdup (value);
      emit_changed (device, "id_usage");
    }
}

void
device_set_id_type (Device *device,
                    const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_type, value) != 0))
    {
      g_free (device->priv->id_type);
      device->priv->id_type = g_strdup (value);
      emit_changed (device, "id_type");
    }
}

void
device_set_id_version (Device *device,
                       const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_version, value) != 0))
    {
      g_free (device->priv->id_version);
      device->priv->id_version = g_strdup (value);
      emit_changed (device, "id_version");
    }
}

void
device_set_id_uuid (Device *device,
                    const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_uuid, value) != 0))
    {
      g_free (device->priv->id_uuid);
      device->priv->id_uuid = g_strdup (value);
      emit_changed (device, "id_uuid");
    }
}

void
device_set_id_label (Device *device,
                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->id_label, value) != 0))
    {
      g_free (device->priv->id_label);
      device->priv->id_label = g_strdup (value);
      emit_changed (device, "id_label");
    }
}

void
device_set_partition_slave (Device *device,
                            const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_slave, value) != 0))
    {
      g_free (device->priv->partition_slave);
      device->priv->partition_slave = g_strdup (value);
      emit_changed (device, "partition_slave");
    }
}

void
device_set_partition_scheme (Device *device,
                             const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_scheme, value) != 0))
    {
      g_free (device->priv->partition_scheme);
      device->priv->partition_scheme = g_strdup (value);
      emit_changed (device, "partition_scheme");
    }
}

void
device_set_partition_type (Device *device,
                           const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_type, value) != 0))
    {
      g_free (device->priv->partition_type);
      device->priv->partition_type = g_strdup (value);
      emit_changed (device, "partition_type");
    }
}

void
device_set_partition_label (Device *device,
                            const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_label, value) != 0))
    {
      g_free (device->priv->partition_label);
      device->priv->partition_label = g_strdup (value);
      emit_changed (device, "partition_label");
    }
}

void
device_set_partition_uuid (Device *device,
                           const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_uuid, value) != 0))
    {
      g_free (device->priv->partition_uuid);
      device->priv->partition_uuid = g_strdup (value);
      emit_changed (device, "partition_uuid");
    }
}

void
device_set_partition_flags (Device *device,
                            GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->partition_flags, value)))
    {
      ptr_str_array_free (device->priv->partition_flags);
      device->priv->partition_flags = ptr_str_array_from_strv (value);
      emit_changed (device, "partition_flags");
    }
}

void
device_set_partition_number (Device *device,
                             gint value)
{
  if (G_UNLIKELY (device->priv->partition_number != value))
    {
      device->priv->partition_number = value;
      emit_changed (device, "partition_number");
    }
}

void
device_set_partition_offset (Device *device,
                             guint64 value)
{
  if (G_UNLIKELY (device->priv->partition_offset != value))
    {
      device->priv->partition_offset = value;
      emit_changed (device, "partition_offset");
    }
}

void
device_set_partition_size (Device *device,
                           guint64 value)
{
  if (G_UNLIKELY (device->priv->partition_size != value))
    {
      device->priv->partition_size = value;
      emit_changed (device, "partition_size");
    }
}

void
device_set_partition_alignment_offset (Device *device,
                                       guint64 value)
{
  if (G_UNLIKELY (device->priv->partition_alignment_offset != value))
    {
      device->priv->partition_alignment_offset = value;
      emit_changed (device, "partition_alignment_offset");
    }
}

void
device_set_partition_table_scheme (Device *device,
                                   const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->partition_table_scheme, value) != 0))
    {
      g_free (device->priv->partition_table_scheme);
      device->priv->partition_table_scheme = g_strdup (value);
      emit_changed (device, "partition_table_scheme");
    }
}

void
device_set_partition_table_count (Device *device,
                                  gint value)
{
  if (G_UNLIKELY (device->priv->partition_table_count != value))
    {
      device->priv->partition_table_count = value;
      emit_changed (device, "partition_table_count");
    }
}

void
device_set_drive_vendor (Device *device,
                         const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_vendor, value) != 0))
    {
      g_free (device->priv->drive_vendor);
      device->priv->drive_vendor = g_strdup (value);
      emit_changed (device, "drive_vendor");
    }
}

void
device_set_drive_model (Device *device,
                        const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_model, value) != 0))
    {
      g_free (device->priv->drive_model);
      device->priv->drive_model = g_strdup (value);
      emit_changed (device, "drive_model");
    }
}

void
device_set_drive_revision (Device *device,
                           const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_revision, value) != 0))
    {
      g_free (device->priv->drive_revision);
      device->priv->drive_revision = g_strdup (value);
      emit_changed (device, "drive_revision");
    }
}

void
device_set_drive_serial (Device *device,
                         const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_serial, value) != 0))
    {
      g_free (device->priv->drive_serial);
      device->priv->drive_serial = g_strdup (value);
      emit_changed (device, "drive_serial");
    }
}

void
device_set_drive_wwn (Device *device,
                      const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_wwn, value) != 0))
    {
      g_free (device->priv->drive_wwn);
      device->priv->drive_wwn = g_strdup (value);
      emit_changed (device, "drive_wwn");
    }
}

void
device_set_drive_connection_interface (Device *device,
                                       const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_connection_interface, value) != 0))
    {
      g_free (device->priv->drive_connection_interface);
      device->priv->drive_connection_interface = g_strdup (value);
      emit_changed (device, "drive_connection_interface");
    }
}

void
device_set_drive_connection_speed (Device *device,
                                   guint value)
{
  if (G_UNLIKELY (device->priv->drive_connection_speed != value))
    {
      device->priv->drive_connection_speed = value;
      emit_changed (device, "drive_connection_speed");
    }
}

void
device_set_drive_media_compatibility (Device *device,
                                      GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->drive_media_compatibility, value)))
    {
      ptr_str_array_free (device->priv->drive_media_compatibility);
      device->priv->drive_media_compatibility = ptr_str_array_from_strv (value);
      emit_changed (device, "drive_media_compatibility");
    }
}

void
device_set_drive_media (Device *device,
                        const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_media, value) != 0))
    {
      g_free (device->priv->drive_media);
      device->priv->drive_media = g_strdup (value);
      emit_changed (device, "drive_media");
    }
}

void
device_set_drive_is_media_ejectable (Device *device,
                                     gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_is_media_ejectable != value))
    {
      device->priv->drive_is_media_ejectable = value;
      emit_changed (device, "drive_is_media_ejectable");
    }
}

void
device_set_drive_can_detach (Device *device,
                             gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_can_detach != value))
    {
      device->priv->drive_can_detach = value;
      emit_changed (device, "drive_can_detach");
    }
}

void
device_set_drive_can_spindown (Device *device,
                               gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_can_spindown != value))
    {
      device->priv->drive_can_spindown = value;
      emit_changed (device, "drive_can_spindown");
    }
}

void
device_set_drive_is_rotational (Device *device,
                                gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_is_rotational != value))
    {
      device->priv->drive_is_rotational = value;
      emit_changed (device, "drive_is_rotational");
    }
}

void
device_set_drive_rotation_rate (Device *device,
                                guint value)
{
  if (G_UNLIKELY (device->priv->drive_rotation_rate != value))
    {
      device->priv->drive_rotation_rate = value;
      emit_changed (device, "drive_rotation_rate");
    }
}

void
device_set_drive_write_cache (Device *device,
                              const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_write_cache, value) != 0))
    {
      g_free (device->priv->drive_write_cache);
      device->priv->drive_write_cache = g_strdup (value);
      emit_changed (device, "drive_write_cache");
    }
}

void
device_set_drive_adapter (Device *device,
                          const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->drive_adapter, value) != 0))
    {
      g_free (device->priv->drive_adapter);
      device->priv->drive_adapter = g_strdup (value);
      emit_changed (device, "drive_adapter");
    }
}

void
device_set_drive_ports (Device *device,
                        GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->drive_ports, value)))
    {
      ptr_str_array_free (device->priv->drive_ports);
      device->priv->drive_ports = ptr_str_array_from_strv (value);
      emit_changed (device, "drive_ports");
    }
}

void
device_set_drive_similar_devices (Device *device,
                                  GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->drive_similar_devices, value)))
    {
      ptr_str_array_free (device->priv->drive_similar_devices);
      device->priv->drive_similar_devices = ptr_str_array_from_strv (value);
      emit_changed (device, "drive_similar_devices");
    }
}

void
device_set_optical_disc_is_blank (Device *device,
                                  gboolean value)
{
  if (G_UNLIKELY (device->priv->optical_disc_is_blank != value))
    {
      device->priv->optical_disc_is_blank = value;
      emit_changed (device, "optical_disc_is_blank");
    }
}

void
device_set_optical_disc_is_appendable (Device *device,
                                       gboolean value)
{
  if (G_UNLIKELY (device->priv->optical_disc_is_appendable != value))
    {
      device->priv->optical_disc_is_appendable = value;
      emit_changed (device, "optical_disc_is_appendable");
    }
}

void
device_set_optical_disc_is_closed (Device *device,
                                   gboolean value)
{
  if (G_UNLIKELY (device->priv->optical_disc_is_closed != value))
    {
      device->priv->optical_disc_is_closed = value;
      emit_changed (device, "optical_disc_is_closed");
    }
}

void
device_set_optical_disc_num_tracks (Device *device,
                                    guint value)
{
  if (G_UNLIKELY (device->priv->optical_disc_num_tracks != value))
    {
      device->priv->optical_disc_num_tracks = value;
      emit_changed (device, "optical_disc_num_tracks");
    }
}

void
device_set_optical_disc_num_audio_tracks (Device *device,
                                          guint value)
{
  if (G_UNLIKELY (device->priv->optical_disc_num_audio_tracks != value))
    {
      device->priv->optical_disc_num_audio_tracks = value;
      emit_changed (device, "optical_disc_num_audio_tracks");
    }
}

void
device_set_optical_disc_num_sessions (Device *device,
                                      guint value)
{
  if (G_UNLIKELY (device->priv->optical_disc_num_sessions != value))
    {
      device->priv->optical_disc_num_sessions = value;
      emit_changed (device, "optical_disc_num_sessions");
    }
}

void
device_set_luks_holder (Device *device,
                        const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->luks_holder, value) != 0))
    {
      g_free (device->priv->luks_holder);
      device->priv->luks_holder = g_strdup (value);
      emit_changed (device, "luks_holder");
    }
}

void
device_set_luks_cleartext_slave (Device *device,
                                 const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->luks_cleartext_slave, value) != 0))
    {
      g_free (device->priv->luks_cleartext_slave);
      device->priv->luks_cleartext_slave = g_strdup (value);
      emit_changed (device, "luks_cleartext_slave");
    }
}

void
device_set_luks_cleartext_unlocked_by_uid (Device *device,
                                           guint value)
{
  if (G_UNLIKELY (device->priv->luks_cleartext_unlocked_by_uid != value))
    {
      device->priv->luks_cleartext_unlocked_by_uid = value;
      emit_changed (device, "luks_cleartext_unlocked_by_uid");
    }
}

void
device_set_linux_md_component_level (Device *device,
                                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_level, value) != 0))
    {
      g_free (device->priv->linux_md_component_level);
      device->priv->linux_md_component_level = g_strdup (value);
      emit_changed (device, "linux_md_component_level");
    }
}

void
device_set_linux_md_component_position (Device *device,
                                        gint value)
{
  if (G_UNLIKELY (device->priv->linux_md_component_position != value))
    {
      device->priv->linux_md_component_position = value;
      emit_changed (device, "linux_md_component_position");
    }
}

void
device_set_linux_md_component_num_raid_devices (Device *device,
                                                gint value)
{
  if (G_UNLIKELY (device->priv->linux_md_component_num_raid_devices != value))
    {
      device->priv->linux_md_component_num_raid_devices = value;
      emit_changed (device, "linux_md_component_num_raid_devices");
    }
}

void
device_set_linux_md_component_uuid (Device *device,
                                    const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_uuid, value) != 0))
    {
      g_free (device->priv->linux_md_component_uuid);
      device->priv->linux_md_component_uuid = g_strdup (value);
      emit_changed (device, "linux_md_component_uuid");
    }
}

void
device_set_linux_md_component_home_host (Device *device,
                                         const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_home_host, value) != 0))
    {
      g_free (device->priv->linux_md_component_home_host);
      device->priv->linux_md_component_home_host = g_strdup (value);
      emit_changed (device, "linux_md_component_home_host");
    }
}

void
device_set_linux_md_component_name (Device *device,
                                    const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_name, value) != 0))
    {
      g_free (device->priv->linux_md_component_name);
      device->priv->linux_md_component_name = g_strdup (value);
      emit_changed (device, "linux_md_component_name");
    }
}

void
device_set_linux_md_component_version (Device *device,
                                       const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_version, value) != 0))
    {
      g_free (device->priv->linux_md_component_version);
      device->priv->linux_md_component_version = g_strdup (value);
      emit_changed (device, "linux_md_component_version");
    }
}

void
device_set_linux_md_component_holder (Device *device,
                                      const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_component_holder, value) != 0))
    {
      g_free (device->priv->linux_md_component_holder);
      device->priv->linux_md_component_holder = g_strdup (value);
      emit_changed (device, "linux_md_component_holder");
    }
}

void
device_set_linux_md_component_state (Device *device,
                                     GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->linux_md_component_state, value)))
    {
      ptr_str_array_free (device->priv->linux_md_component_state);
      device->priv->linux_md_component_state = ptr_str_array_from_strv (value);
      emit_changed (device, "linux_md_component_state");
    }
}

void
device_set_linux_md_state (Device *device,
                           const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_state, value) != 0))
    {
      g_free (device->priv->linux_md_state);
      device->priv->linux_md_state = g_strdup (value);
      emit_changed (device, "linux_md_state");
    }
}

void
device_set_linux_md_level (Device *device,
                           const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_level, value) != 0))
    {
      g_free (device->priv->linux_md_level);
      device->priv->linux_md_level = g_strdup (value);
      emit_changed (device, "linux_md_level");
    }
}

void
device_set_linux_md_num_raid_devices (Device *device,
                                      gint value)
{
  if (G_UNLIKELY (device->priv->linux_md_num_raid_devices != value))
    {
      device->priv->linux_md_num_raid_devices = value;
      emit_changed (device, "linux_md_num_raid_devices");
    }
}

void
device_set_linux_md_uuid (Device *device,
                          const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_uuid, value) != 0))
    {
      g_free (device->priv->linux_md_uuid);
      device->priv->linux_md_uuid = g_strdup (value);
      emit_changed (device, "linux_md_uuid");
    }
}

void
device_set_linux_md_home_host (Device *device,
                               const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_home_host, value) != 0))
    {
      g_free (device->priv->linux_md_home_host);
      device->priv->linux_md_home_host = g_strdup (value);
      emit_changed (device, "linux_md_home_host");
    }
}

void
device_set_linux_md_name (Device *device,
                          const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_name, value) != 0))
    {
      g_free (device->priv->linux_md_name);
      device->priv->linux_md_name = g_strdup (value);
      emit_changed (device, "linux_md_name");
    }
}

void
device_set_linux_md_version (Device *device,
                             const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_version, value) != 0))
    {
      g_free (device->priv->linux_md_version);
      device->priv->linux_md_version = g_strdup (value);
      emit_changed (device, "linux_md_version");
    }
}

void
device_set_linux_md_slaves (Device *device,
                            GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->linux_md_slaves, value)))
    {
      ptr_str_array_free (device->priv->linux_md_slaves);
      device->priv->linux_md_slaves = ptr_str_array_from_strv (value);
      emit_changed (device, "linux_md_slaves");
    }
}

void
device_set_linux_md_is_degraded (Device *device,
                                 gboolean value)
{
  if (G_UNLIKELY (device->priv->linux_md_is_degraded != value))
    {
      device->priv->linux_md_is_degraded = value;
      emit_changed (device, "linux_md_is_degraded");
    }
}

void
device_set_linux_md_sync_action (Device *device,
                                 const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_md_sync_action, value) != 0))
    {
      g_free (device->priv->linux_md_sync_action);
      device->priv->linux_md_sync_action = g_strdup (value);
      emit_changed (device, "linux_md_sync_action");
    }
}

void
device_set_linux_md_sync_percentage (Device *device,
                                     gdouble value)
{
  if (G_UNLIKELY (device->priv->linux_md_sync_percentage != value))
    {
      device->priv->linux_md_sync_percentage = value;
      emit_changed (device, "linux_md_sync_percentage");
    }
}

void
device_set_linux_md_sync_speed (Device *device,
                                guint64 value)
{
  if (G_UNLIKELY (device->priv->linux_md_sync_speed != value))
    {
      device->priv->linux_md_sync_speed = value;
      emit_changed (device, "linux_md_sync_speed");
    }
}

void
device_set_dm_name (Device *device,
                    const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->dm_name, value) != 0))
    {
      g_free (device->priv->dm_name);
      device->priv->dm_name = g_strdup (value);
      emit_changed (device, "dm_name");
    }
}

void
device_set_slaves_objpath (Device *device,
                           GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->slaves_objpath, value)))
    {
      ptr_str_array_free (device->priv->slaves_objpath);
      device->priv->slaves_objpath = ptr_str_array_from_strv (value);
      emit_changed (device, "slaves_objpath");
    }
}

void
device_set_holders_objpath (Device *device,
                            GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->holders_objpath, value)))
    {
      ptr_str_array_free (device->priv->holders_objpath);
      device->priv->holders_objpath = ptr_str_array_from_strv (value);
      emit_changed (device, "holders_objpath");
    }
}

void
device_set_drive_ata_smart_is_available (Device *device,
                                         gboolean value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_is_available != value))
    {
      device->priv->drive_ata_smart_is_available = value;
      emit_changed (device, "drive_ata_smart_is_available");
    }
}

void
device_set_drive_ata_smart_time_collected (Device *device,
                                           guint64 value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_time_collected != value))
    {
      device->priv->drive_ata_smart_time_collected = value;
      emit_changed (device, "drive_ata_smart_time_collected");
    }
}

void
device_set_drive_ata_smart_status (Device *device,
                                   SkSmartOverall value)
{
  if (G_UNLIKELY (device->priv->drive_ata_smart_status != value))
    {
      device->priv->drive_ata_smart_status = value;
      emit_changed (device, "drive_ata_smart_status");
    }
}

void
device_set_drive_ata_smart_blob_steal (Device *device,
                                       gchar *blob,
                                       gsize blob_size)
{
  /* TODO: compare? Not really needed, this happens very rarely */

  g_free (device->priv->drive_ata_smart_blob);
  device->priv->drive_ata_smart_blob = blob;
  device->priv->drive_ata_smart_blob_size = blob_size;

  emit_changed (device, "drive_ata_smart_blob");
}


void
device_set_linux_lvm2_lv_name (Device *device,
                               const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_lvm2_lv_name, value) != 0))
    {
      g_free (device->priv->linux_lvm2_lv_name);
      device->priv->linux_lvm2_lv_name = g_strdup (value);
      emit_changed (device, "linux_lvm2_lv_name");
    }
}

void
device_set_linux_lvm2_lv_uuid (Device *device,
                               const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_lvm2_lv_uuid, value) != 0))
    {
      g_free (device->priv->linux_lvm2_lv_uuid);
      device->priv->linux_lvm2_lv_uuid = g_strdup (value);
      emit_changed (device, "linux_lvm2_lv_uuid");
    }
}

void
device_set_linux_lvm2_lv_group_name (Device *device,
                                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_lvm2_lv_group_name, value) != 0))
    {
      g_free (device->priv->linux_lvm2_lv_group_name);
      device->priv->linux_lvm2_lv_group_name = g_strdup (value);
      emit_changed (device, "linux_lvm2_lv_group_name");
    }
}

void
device_set_linux_lvm2_lv_group_uuid (Device *device,
                                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_lvm2_lv_group_uuid, value) != 0))
    {
      g_free (device->priv->linux_lvm2_lv_group_uuid);
      device->priv->linux_lvm2_lv_group_uuid = g_strdup (value);
      emit_changed (device, "linux_lvm2_lv_group_uuid");
    }
}



void
device_set_linux_lvm2_pv_uuid (Device *device,
                               const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_lvm2_pv_uuid, value) != 0))
    {
      g_free (device->priv->linux_lvm2_pv_uuid);
      device->priv->linux_lvm2_pv_uuid = g_strdup (value);
      emit_changed (device, "linux_lvm2_pv_uuid");
    }
}

void
device_set_linux_lvm2_pv_num_metadata_areas (Device *device,
                                             guint value)
{
  if (G_UNLIKELY (device->priv->linux_lvm2_pv_num_metadata_areas != value))
    {
      device->priv->linux_lvm2_pv_num_metadata_areas = value;
      emit_changed (device, "linux_lvm2_pv_num_metadata_areas");
    }
}

void
device_set_linux_lvm2_pv_group_name (Device *device,
                                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_lvm2_pv_group_name, value) != 0))
    {
      g_free (device->priv->linux_lvm2_pv_group_name);
      device->priv->linux_lvm2_pv_group_name = g_strdup (value);
      emit_changed (device, "linux_lvm2_pv_group_name");
    }
}

void
device_set_linux_lvm2_pv_group_uuid (Device *device,
                                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_lvm2_pv_group_uuid, value) != 0))
    {
      g_free (device->priv->linux_lvm2_pv_group_uuid);
      device->priv->linux_lvm2_pv_group_uuid = g_strdup (value);
      emit_changed (device, "linux_lvm2_pv_group_uuid");
    }
}

void
device_set_linux_lvm2_pv_group_size (Device *device,
                                     guint64 value)
{
  if (G_UNLIKELY (device->priv->linux_lvm2_pv_group_size != value))
    {
      device->priv->linux_lvm2_pv_group_size = value;
      emit_changed (device, "linux_lvm2_pv_group_size");
    }
}

void
device_set_linux_lvm2_pv_group_unallocated_size (Device *device,
                                                 guint64 value)
{
  if (G_UNLIKELY (device->priv->linux_lvm2_pv_group_unallocated_size != value))
    {
      device->priv->linux_lvm2_pv_group_unallocated_size = value;
      emit_changed (device, "linux_lvm2_pv_group_unallocated_size");
    }
}

void
device_set_linux_lvm2_pv_group_extent_size (Device *device,
                                            guint64 value)
{
  if (G_UNLIKELY (device->priv->linux_lvm2_pv_group_extent_size != value))
    {
      device->priv->linux_lvm2_pv_group_extent_size = value;
      emit_changed (device, "linux_lvm2_pv_group_extent_size");
    }
}

void
device_set_linux_lvm2_pv_group_sequence_number (Device *device,
                                                guint64 value)
{
  if (G_UNLIKELY (device->priv->linux_lvm2_pv_group_sequence_number != value))
    {
      device->priv->linux_lvm2_pv_group_sequence_number = value;
      emit_changed (device, "linux_lvm2_pv_group_sequence_number");
    }
}

void
device_set_linux_lvm2_pv_group_physical_volumes (Device *device,
                                                 GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->linux_lvm2_pv_group_physical_volumes, value)))
    {
      ptr_str_array_free (device->priv->linux_lvm2_pv_group_physical_volumes);
      device->priv->linux_lvm2_pv_group_physical_volumes = ptr_str_array_from_strv (value);
      emit_changed (device, "linux_lvm2_pv_group_physical_volumes");
    }
}

void
device_set_linux_lvm2_pv_group_logical_volumes (Device *device,
                                                GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->linux_lvm2_pv_group_logical_volumes, value)))
    {
      ptr_str_array_free (device->priv->linux_lvm2_pv_group_logical_volumes);
      device->priv->linux_lvm2_pv_group_logical_volumes = ptr_str_array_from_strv (value);
      emit_changed (device, "linux_lvm2_pv_group_logical_volumes");
    }
}


void
device_set_linux_dmmp_component_holder (Device *device,
                                        const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_dmmp_component_holder, value) != 0))
    {
      g_free (device->priv->linux_dmmp_component_holder);
      device->priv->linux_dmmp_component_holder = g_strdup (value);
      emit_changed (device, "linux_dmmp_component_holder");
    }
}

void
device_set_linux_dmmp_name (Device *device,
                            const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_dmmp_name, value) != 0))
    {
      g_free (device->priv->linux_dmmp_name);
      device->priv->linux_dmmp_name = g_strdup (value);
      emit_changed (device, "linux_dmmp_name");
    }
}

void
device_set_linux_dmmp_parameters (Device *device,
                                  const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_dmmp_parameters, value) != 0))
    {
      g_free (device->priv->linux_dmmp_parameters);
      device->priv->linux_dmmp_parameters = g_strdup (value);
      emit_changed (device, "linux_dmmp_parameters");
    }
}

void
device_set_linux_dmmp_slaves (Device *device,
                              GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (device->priv->linux_dmmp_slaves, value)))
    {
      ptr_str_array_free (device->priv->linux_dmmp_slaves);
      device->priv->linux_dmmp_slaves = ptr_str_array_from_strv (value);
      emit_changed (device, "linux_dmmp_slaves");
    }
}

void
device_set_linux_loop_filename (Device *device,
                                const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (device->priv->linux_loop_filename, value) != 0))
    {
      g_free (device->priv->linux_loop_filename);
      device->priv->linux_loop_filename = g_strdup (value);
      emit_changed (device, "linux_loop_filename");
    }
}
