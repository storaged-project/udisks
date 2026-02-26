/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2022 Tomas Bzatek <tbzatek@redhat.com>
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

#include <sys/types.h>
#include <sys/stat.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>

#include "udiskslogging.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxnvmecontroller.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksbasejob.h"
#include "udiskssimplejob.h"
#include "udisksthreadedjob.h"
#include "udiskslinuxdevice.h"

/**
 * SECTION:udiskslinuxnvmecontroller
 * @title: UDisksLinuxNVMeController
 * @short_description: Linux implementation of #UDisksNVMeController
 *
 * This type provides an implementation of the #UDisksNVMeController
 * interface on Linux.
 */

typedef struct _UDisksLinuxNVMeControllerClass   UDisksLinuxNVMeControllerClass;

/**
 * UDisksLinuxNVMeController:
 *
 * The #UDisksLinuxNVMeController structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxNVMeController
{
  UDisksNVMeControllerSkeleton parent_instance;

  GMutex             smart_lock;
  guint64            smart_updated;
  BDNVMESmartLog    *smart_log;

  GCond              selftest_cond;
  BDNVMESelfTestLog *selftest_log;
  UDisksThreadedJob *selftest_job;

  BDNVMESanitizeLog *sanitize_log;
  UDisksThreadedJob *sanitize_job;
};

struct _UDisksLinuxNVMeControllerClass
{
  UDisksNVMeControllerSkeletonClass parent_class;
};

static void nvme_controller_iface_init (UDisksNVMeControllerIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxNVMeController, udisks_linux_nvme_controller, UDISKS_TYPE_NVME_CONTROLLER_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_NVME_CONTROLLER, nvme_controller_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_nvme_controller_finalize (GObject *object)
{
  UDisksLinuxNVMeController *ctrl = UDISKS_LINUX_NVME_CONTROLLER (object);

  if (ctrl->smart_log != NULL)
    bd_nvme_smart_log_free (ctrl->smart_log);
  if (ctrl->selftest_log != NULL)
    bd_nvme_self_test_log_free (ctrl->selftest_log);
  if (ctrl->sanitize_log != NULL)
    bd_nvme_sanitize_log_free (ctrl->sanitize_log);
  g_mutex_clear (&ctrl->smart_lock);
  g_cond_clear (&ctrl->selftest_cond);

  if (G_OBJECT_CLASS (udisks_linux_nvme_controller_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_nvme_controller_parent_class)->finalize (object);
}

static void
udisks_linux_nvme_controller_init (UDisksLinuxNVMeController *ctrl)
{
  g_mutex_init (&ctrl->smart_lock);
  g_cond_init (&ctrl->selftest_cond);

  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (ctrl),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_nvme_controller_class_init (UDisksLinuxNVMeControllerClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = udisks_linux_nvme_controller_finalize;
}

/**
 * udisks_linux_nvme_controller_new:
 *
 * Creates a new #UDisksLinuxNVMeController instance.
 *
 * Returns: A new #UDisksLinuxNVMeController. Free with g_object_unref().
 */
UDisksNVMeController *
udisks_linux_nvme_controller_new (void)
{
  return UDISKS_NVME_CONTROLLER (g_object_new (UDISKS_TYPE_LINUX_NVME_CONTROLLER, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/* may be called from *any* thread when the SMART data has been updated */
static void
update_iface_smart (UDisksLinuxNVMeController *ctrl)
{
  BDNVMESmartLog *smart_log = NULL;
  BDNVMESelfTestLog *selftest_log = NULL;
  BDNVMESanitizeLog *sanitize_log = NULL;
  guint64 updated = 0;

  g_mutex_lock (&ctrl->smart_lock);
  if (ctrl->smart_log != NULL)
    {
      smart_log = bd_nvme_smart_log_copy (ctrl->smart_log);
      updated = ctrl->smart_updated;
    }
  if (ctrl->selftest_log != NULL)
    selftest_log = bd_nvme_self_test_log_copy (ctrl->selftest_log);
  if (ctrl->sanitize_log != NULL)
    sanitize_log = bd_nvme_sanitize_log_copy (ctrl->sanitize_log);
  g_mutex_unlock (&ctrl->smart_lock);

  g_object_freeze_notify (G_OBJECT (ctrl));
  udisks_nvme_controller_set_smart_updated (UDISKS_NVME_CONTROLLER (ctrl), updated);
  if (smart_log != NULL)
    {
      GPtrArray *a;

      a = g_ptr_array_new ();
      if ((smart_log->critical_warning & BD_NVME_SMART_CRITICAL_WARNING_SPARE) == BD_NVME_SMART_CRITICAL_WARNING_SPARE)
        g_ptr_array_add (a, g_strdup ("spare"));
      if ((smart_log->critical_warning & BD_NVME_SMART_CRITICAL_WARNING_TEMPERATURE) == BD_NVME_SMART_CRITICAL_WARNING_TEMPERATURE)
        g_ptr_array_add (a, g_strdup ("temperature"));
      if ((smart_log->critical_warning & BD_NVME_SMART_CRITICAL_WARNING_DEGRADED) == BD_NVME_SMART_CRITICAL_WARNING_DEGRADED)
        g_ptr_array_add (a, g_strdup ("degraded"));
      if ((smart_log->critical_warning & BD_NVME_SMART_CRITICAL_WARNING_READONLY) == BD_NVME_SMART_CRITICAL_WARNING_READONLY)
        g_ptr_array_add (a, g_strdup ("readonly"));
      if ((smart_log->critical_warning & BD_NVME_SMART_CRITICAL_WARNING_VOLATILE_MEM) == BD_NVME_SMART_CRITICAL_WARNING_VOLATILE_MEM)
        g_ptr_array_add (a, g_strdup ("volatile_mem"));
      if ((smart_log->critical_warning & BD_NVME_SMART_CRITICAL_WARNING_PMR_READONLY) == BD_NVME_SMART_CRITICAL_WARNING_PMR_READONLY)
        g_ptr_array_add (a, g_strdup ("pmr_readonly"));
      g_ptr_array_add (a, NULL);
      udisks_nvme_controller_set_smart_critical_warning (UDISKS_NVME_CONTROLLER (ctrl),
                                                         (const gchar *const *) a->pdata);

      udisks_nvme_controller_set_smart_power_on_hours (UDISKS_NVME_CONTROLLER (ctrl), smart_log->power_on_hours);
      udisks_nvme_controller_set_smart_temperature (UDISKS_NVME_CONTROLLER (ctrl), smart_log->temperature);

      bd_nvme_smart_log_free (smart_log);
      g_ptr_array_free (a, TRUE);
    }
  else
    {
      /* fallback, smart_log has never been retrieved successfully */
      const gchar * const warning[] = { NULL };

      udisks_nvme_controller_set_smart_critical_warning (UDISKS_NVME_CONTROLLER (ctrl), warning);
      udisks_nvme_controller_set_smart_power_on_hours (UDISKS_NVME_CONTROLLER (ctrl), 0);
      udisks_nvme_controller_set_smart_temperature (UDISKS_NVME_CONTROLLER (ctrl), 0);
    }

  if (selftest_log != NULL)
    {
      const gchar *status = "success";
      gint compl = -1;

      if (selftest_log->current_operation != BD_NVME_SELF_TEST_ACTION_NOT_RUNNING)
        {
          compl = 100 - selftest_log->current_operation_completion;
          status = "inprogress";
        }
      else
      if (selftest_log->entries && *selftest_log->entries)
        status = bd_nvme_self_test_result_to_string ((*selftest_log->entries)->result, NULL);

      udisks_nvme_controller_set_smart_selftest_percent_remaining (UDISKS_NVME_CONTROLLER (ctrl), compl);
      udisks_nvme_controller_set_smart_selftest_status (UDISKS_NVME_CONTROLLER (ctrl), status);
      bd_nvme_self_test_log_free (selftest_log);
    }
  else
    {
      /* fallback */
      udisks_nvme_controller_set_smart_selftest_percent_remaining (UDISKS_NVME_CONTROLLER (ctrl), -1);
      udisks_nvme_controller_set_smart_selftest_status (UDISKS_NVME_CONTROLLER (ctrl), "");
    }

  if (sanitize_log != NULL)
    {
      const gchar *status = "success";
      gint compl = -1;

      switch (sanitize_log->sanitize_status)
        {
          case BD_NVME_SANITIZE_STATUS_NEVER_SANITIZED:
            status = "never_sanitized";
            break;
          case BD_NVME_SANITIZE_STATUS_IN_PROGRESS:
            status = "inprogress";
            compl = 100 - sanitize_log->sanitize_progress;
            break;
          case BD_NVME_SANITIZE_STATUS_SUCCESS:
          case BD_NVME_SANITIZE_STATUS_SUCCESS_NO_DEALLOC:
            status = "success";
            break;
          case BD_NVME_SANITIZE_STATUS_FAILED:
            status = "failure";
            break;
        }

      udisks_nvme_controller_set_sanitize_percent_remaining (UDISKS_NVME_CONTROLLER (ctrl), compl);
      udisks_nvme_controller_set_sanitize_status (UDISKS_NVME_CONTROLLER (ctrl), status);
      bd_nvme_sanitize_log_free (sanitize_log);
    }
  else
    {
      /* fallback */
      udisks_nvme_controller_set_sanitize_percent_remaining (UDISKS_NVME_CONTROLLER (ctrl), -1);
      udisks_nvme_controller_set_sanitize_status (UDISKS_NVME_CONTROLLER (ctrl), "");
    }

  g_object_thaw_notify (G_OBJECT (ctrl));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_nvme_controller_update:
 * @ctrl: A #UDisksLinuxNVMeController.
 * @object: The enclosing #UDisksLinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_nvme_controller_update (UDisksLinuxNVMeController *ctrl,
                                     UDisksLinuxDriveObject    *object)
{
  UDisksNVMeController *iface = UDISKS_NVME_CONTROLLER (ctrl);
  UDisksLinuxDevice *device;
  gint cntl_id = 0;
  gchar *subsysnqn = NULL;
  gchar *state = NULL;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    return FALSE;

  g_object_freeze_notify (G_OBJECT (object));

  subsysnqn = g_strdup (g_udev_device_get_sysfs_attr (device->udev_device, "subsysnqn"));
  cntl_id = g_udev_device_get_sysfs_attr_as_int (device->udev_device, "cntlid");
  state = g_strdup (g_udev_device_get_sysfs_attr (device->udev_device, "state"));

  if (device->nvme_ctrl_info)
    {
      udisks_nvme_controller_set_nvme_revision (iface, device->nvme_ctrl_info->nvme_ver);
      udisks_nvme_controller_set_unallocated_capacity (iface, device->nvme_ctrl_info->size_unalloc);
      udisks_nvme_controller_set_fguid (iface, device->nvme_ctrl_info->fguid);

      cntl_id = device->nvme_ctrl_info->ctrl_id;
      if (device->nvme_ctrl_info->subsysnqn && strlen (device->nvme_ctrl_info->subsysnqn) > 0)
        {
          g_free (subsysnqn);
          subsysnqn = g_strdup (device->nvme_ctrl_info->subsysnqn);
        }
    }

  udisks_nvme_controller_set_controller_id (iface, cntl_id);
  if (subsysnqn)
    {
      g_strchomp (subsysnqn);
      udisks_nvme_controller_set_subsystem_nqn (iface, subsysnqn);
    }
  if (state)
    {
      g_strchomp (state);
      udisks_nvme_controller_set_state (iface, state);
    }

  udisks_linux_nvme_controller_refresh_smart_sync (ctrl, NULL, NULL);

  g_object_thaw_notify (G_OBJECT (object));

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (ctrl));
  g_object_unref (device);

  g_free (subsysnqn);
  g_free (state);

  return FALSE;   /* don't re-apply the drive 'configuration' (PM, etc.) */
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_nvme_controller_refresh_smart_sync:
 * @ctrl: The #UDisksLinuxNVMeController to refresh.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error.
 *
 * Synchronously refreshes SMART/Health Information Log on @ctrl.
 * Includes Sanitize Status information. The calling thread
 * is blocked until the data has been obtained.
 *
 * This may only be called if @ctrl has been associated with a
 * #UDisksLinuxDriveObject instance.
 *
 * This method may be called from any thread.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
udisks_linux_nvme_controller_refresh_smart_sync (UDisksLinuxNVMeController  *ctrl,
                                                 GCancellable               *cancellable,
                                                 GError                    **error)
{
  UDisksLinuxDriveObject *object;
  UDisksLinuxDevice *device;
  BDNVMESmartLog *smart_log = NULL;
  BDNVMESelfTestLog *selftest_log = NULL;
  BDNVMESanitizeLog *sanitize_log = NULL;
  const gchar *dev_file;

  object = udisks_daemon_util_dup_object (ctrl, error);
  if (object == NULL)
    return FALSE;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "No udev device");
      g_object_unref (object);
      return FALSE;
    }
  dev_file = g_udev_device_get_device_file (device->udev_device);
  if (dev_file == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "No device file available");
      g_object_unref (device);
      g_object_unref (object);
      return FALSE;
    }
  if (device->nvme_ctrl_info == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "No probed controller info available");
      g_object_unref (device);
      g_object_unref (object);
      return FALSE;
    }
  if (device->nvme_ctrl_info->controller_type != BD_NVME_CTRL_TYPE_UNKNOWN &&
      device->nvme_ctrl_info->controller_type != BD_NVME_CTRL_TYPE_IO)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "NVMe Health Information is only supported on I/O controllers");
      g_object_unref (device);
      g_object_unref (object);
      return FALSE;
    }

  /* Controller capabilities check - there's no authoritative way to find out which
   * log pages are actually supported, taking controller feature flags in account instead.
   * The "Supported Log Pages" log page support only came with NVMe 2.0 specification.
   */
  smart_log = bd_nvme_get_smart_log (dev_file, error);
  if ((device->nvme_ctrl_info->features & BD_NVME_CTRL_FEAT_SELFTEST) == BD_NVME_CTRL_FEAT_SELFTEST)
    selftest_log = bd_nvme_get_self_test_log (dev_file, NULL);
  if ((device->nvme_ctrl_info->features & BD_NVME_CTRL_FEAT_SANITIZE_CRYPTO) == BD_NVME_CTRL_FEAT_SANITIZE_CRYPTO ||
      (device->nvme_ctrl_info->features & BD_NVME_CTRL_FEAT_SANITIZE_BLOCK) == BD_NVME_CTRL_FEAT_SANITIZE_BLOCK ||
      (device->nvme_ctrl_info->features & BD_NVME_CTRL_FEAT_SANITIZE_OVERWRITE) == BD_NVME_CTRL_FEAT_SANITIZE_OVERWRITE)
    sanitize_log = bd_nvme_get_sanitize_log (dev_file, NULL);
  if (smart_log || selftest_log || sanitize_log)
    {
      g_mutex_lock (&ctrl->smart_lock);

      if (smart_log)
        {
          bd_nvme_smart_log_free (ctrl->smart_log);
          ctrl->smart_log = smart_log;
          ctrl->smart_updated = time (NULL);
        }
      if (selftest_log)
        {
          bd_nvme_self_test_log_free (ctrl->selftest_log);
          ctrl->selftest_log = selftest_log;
        }
      if (sanitize_log)
        {
          bd_nvme_sanitize_log_free (ctrl->sanitize_log);
          ctrl->sanitize_log = sanitize_log;
        }

      g_mutex_unlock (&ctrl->smart_lock);

      update_iface_smart (ctrl);

      /* ensure property changes are sent before the method return */
      g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (ctrl));
    }

  g_clear_object (&device);
  g_clear_object (&object);
  return smart_log != NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_smart_update (UDisksNVMeController  *_object,
                     GDBusMethodInvocation *invocation,
                     GVariant              *options)
{
  UDisksLinuxNVMeController *ctrl = UDISKS_LINUX_NVME_CONTROLLER (_object);
  UDisksLinuxDriveObject *object;
  UDisksDaemon *daemon = NULL;
  GError *error = NULL;
  const gchar *message;
  const gchar *action_id;

  object = udisks_daemon_util_dup_object (_object, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * refreshes SMART data from a disk.
   *
   * Do not translate $(device.name), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to update SMART data from $(device.name)");
  action_id = "org.freedesktop.udisks2.nvme-smart-update";

  /* Check that the user is authorized */
  daemon = udisks_linux_drive_object_get_daemon (object);
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  if (!udisks_linux_nvme_controller_refresh_smart_sync (ctrl,
                                                        NULL, /* cancellable */
                                                        &error))
    {
      udisks_debug ("Error updating NVMe Health Information for %s: %s (%s, %d)",
                    g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                    error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_nvme_controller_complete_smart_update (_object, invocation);

 out:
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_smart_get_attributes (UDisksNVMeController  *object,
                             GDBusMethodInvocation *invocation,
                             GVariant              *options)
{
  UDisksLinuxNVMeController *ctrl = UDISKS_LINUX_NVME_CONTROLLER (object);
  BDNVMESmartLog *smart_log = NULL;
  GVariantBuilder builder;

  g_mutex_lock (&ctrl->smart_lock);
  smart_log = bd_nvme_smart_log_copy (ctrl->smart_log);
  g_mutex_unlock (&ctrl->smart_lock);

  if (smart_log == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "SMART data not collected");
    }
  else
    {
      GVariantBuilder array_builder;
      guint i;

      g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

      g_variant_builder_add (&builder, "{sv}",
                             "avail_spare",
                             g_variant_new_byte (smart_log->avail_spare));
      g_variant_builder_add (&builder, "{sv}",
                             "spare_thresh",
                             g_variant_new_byte (smart_log->spare_thresh));
      g_variant_builder_add (&builder, "{sv}",
                             "percent_used",
                             g_variant_new_byte (smart_log->percent_used));

      if (smart_log->total_data_read > 0)
        g_variant_builder_add (&builder, "{sv}",
                               "total_data_read",
                               g_variant_new_uint64 (smart_log->total_data_read));
      if (smart_log->total_data_written > 0)
        g_variant_builder_add (&builder, "{sv}",
                               "total_data_written",
                               g_variant_new_uint64 (smart_log->total_data_written));

      g_variant_builder_add (&builder, "{sv}",
                             "ctrl_busy_time",
                             g_variant_new_uint64 (smart_log->ctrl_busy_time));
      g_variant_builder_add (&builder, "{sv}",
                             "power_cycles",
                             g_variant_new_uint64 (smart_log->power_cycles));
      g_variant_builder_add (&builder, "{sv}",
                             "unsafe_shutdowns",
                             g_variant_new_uint64 (smart_log->unsafe_shutdowns));
      g_variant_builder_add (&builder, "{sv}",
                             "media_errors",
                             g_variant_new_uint64 (smart_log->media_errors));
      g_variant_builder_add (&builder, "{sv}",
                             "num_err_log_entries",
                             g_variant_new_uint64 (smart_log->num_err_log_entries));

      g_variant_builder_init (&array_builder, G_VARIANT_TYPE_ARRAY);
      for (i = 0; i < G_N_ELEMENTS (smart_log->temp_sensors); i++)
        g_variant_builder_add_value (&array_builder, g_variant_new_uint16 (smart_log->temp_sensors[i]));
      g_variant_builder_add (&builder, "{sv}",
                             "temp_sensors",
                             g_variant_builder_end (&array_builder));

      if (smart_log->wctemp > 0)
        g_variant_builder_add (&builder, "{sv}",
                               "wctemp",
                               g_variant_new_uint16 (smart_log->wctemp));
      if (smart_log->cctemp > 0)
        g_variant_builder_add (&builder, "{sv}",
                               "cctemp",
                               g_variant_new_uint16 (smart_log->cctemp));

      g_variant_builder_add (&builder, "{sv}",
                             "warning_temp_time",
                             g_variant_new_uint32 (smart_log->warning_temp_time));
      g_variant_builder_add (&builder, "{sv}",
                             "critical_temp_time",
                             g_variant_new_uint32 (smart_log->critical_temp_time));

      udisks_nvme_controller_complete_smart_get_attributes (object, invocation,
                                                            g_variant_builder_end (&builder));
      bd_nvme_smart_log_free (smart_log);
    }

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
selftest_job_func_done (UDisksLinuxNVMeController *ctrl)
{
  g_mutex_lock (&ctrl->smart_lock);
  ctrl->selftest_job = NULL;
  /* nobody may be listening, send the signal anyway */
  g_cond_signal (&ctrl->selftest_cond);
  g_mutex_unlock (&ctrl->smart_lock);
  g_object_unref (ctrl);
}

static gboolean
selftest_job_func (UDisksThreadedJob  *job,
                   GCancellable       *cancellable,
                   gpointer            user_data,
                   GError            **error)
{
  UDisksLinuxNVMeController *ctrl = UDISKS_LINUX_NVME_CONTROLLER (user_data);
  UDisksLinuxDriveObject *object;
  UDisksLinuxDevice *device = NULL;
  gboolean ret = FALSE;

  object = udisks_daemon_util_dup_object (ctrl, error);
  if (object == NULL)
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "No udev device");
      goto out;
    }

  udisks_job_set_progress_valid (UDISKS_JOB (job), TRUE);
  udisks_job_set_progress (UDISKS_JOB (job), 0.0);

  while (TRUE)
    {
      gboolean still_in_progress;
      gdouble progress;
      GPollFD poll_fd;

      if (!udisks_linux_nvme_controller_refresh_smart_sync (ctrl,
                                                            NULL,  /* cancellable */
                                                            error))
        {
          udisks_warning ("Unable to retrieve selftest log for %s while polling during the test operation: %s (%s, %d)",
                          g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                          (*error)->message, g_quark_to_string ((*error)->domain), (*error)->code);
          goto out;
        }

      g_mutex_lock (&ctrl->smart_lock);
      still_in_progress = ctrl->selftest_log && ctrl->selftest_log->current_operation != BD_NVME_SELF_TEST_ACTION_NOT_RUNNING;
      progress = (ctrl->selftest_log ? ctrl->selftest_log->current_operation_completion : 0) / 100.0;
      g_mutex_unlock (&ctrl->smart_lock);

      if (!still_in_progress)
        {
          ret = TRUE;
          goto out;
        }

      if (progress < 0.0)
        progress = 0.0;
      if (progress > 1.0)
        progress = 1.0;
      udisks_job_set_progress (UDISKS_JOB (job), progress);

      /* Sleep for 30 seconds or until we're cancelled */
      if (g_cancellable_make_pollfd (cancellable, &poll_fd))
        {
          gint poll_ret;
          do
            {
              poll_ret = g_poll (&poll_fd, 1, 30 * 1000);
            }
          while (poll_ret == -1 && errno == EINTR);
          g_cancellable_release_fd (cancellable);
        }
      else
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Error creating pollfd for cancellable");
          goto out;
        }

      /* Check if we're cancelled */
      if (g_cancellable_is_cancelled (cancellable))
        {
          GError *c_error;

          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_CANCELLED,
                       "Self-test was cancelled");

          /* OK, cancelled ... still need to a) abort the test; and b) update the status */
          c_error = NULL;
          if (!bd_nvme_device_self_test (g_udev_device_get_device_file (device->udev_device),
                                         BD_NVME_SELF_TEST_ACTION_ABORT,
                                         &c_error))
            {
              udisks_warning ("Error aborting device selftest for %s on cancel path: %s (%s, %d)",
                              g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                              c_error->message, g_quark_to_string (c_error->domain), c_error->code);
              g_clear_error (&c_error);
            }
          if (!udisks_linux_nvme_controller_refresh_smart_sync (ctrl,
                                                                NULL,  /* cancellable */
                                                                &c_error))
            {
              udisks_warning ("Error updating drive health information for %s on cancel path: %s (%s, %d)",
                              g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                              c_error->message, g_quark_to_string (c_error->domain), c_error->code);
              g_clear_error (&c_error);
            }
          goto out;
        }
    }

  ret = TRUE;

 out:
  /* terminate the job */
  g_clear_object (&device);
  g_clear_object (&object);
  return ret;
}


static gboolean
handle_smart_selftest_start (UDisksNVMeController  *_ctrl,
                             GDBusMethodInvocation *invocation,
                             const gchar           *type,
                             GVariant              *options)
{
  UDisksLinuxNVMeController *ctrl = UDISKS_LINUX_NVME_CONTROLLER (_ctrl);
  UDisksLinuxDriveObject *object;
  UDisksDaemon *daemon;
  UDisksLinuxDevice *device = NULL;
  BDNVMESelfTestAction action;
  BDNVMESelfTestLog *self_test_log;
  uid_t caller_uid;
  gint64 time_est = 0;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (ctrl, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  g_mutex_lock (&ctrl->smart_lock);
  if (ctrl->selftest_job != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "There is already device self-test running");
      g_mutex_unlock (&ctrl->smart_lock);
      goto out;
    }
  if (ctrl->sanitize_job != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "There is already a sanitize operation running");
      g_mutex_unlock (&ctrl->smart_lock);
      goto out;
    }
  g_mutex_unlock (&ctrl->smart_lock);

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No udev device");
      goto out;
    }
  if (device->nvme_ctrl_info == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No probed controller info available");
      goto out;
    }
  if ((device->nvme_ctrl_info->features & BD_NVME_CTRL_FEAT_SELFTEST) != BD_NVME_CTRL_FEAT_SELFTEST)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "The NVMe controller has no support for self-test operations");
      goto out;
    }

  if (g_strcmp0 (type, "short") == 0)
    action = BD_NVME_SELF_TEST_ACTION_SHORT;
  else if (g_strcmp0 (type, "extended") == 0)
    action = BD_NVME_SELF_TEST_ACTION_EXTENDED;
  else if (g_strcmp0 (type, "vendor-specific") == 0)
    action = BD_NVME_SELF_TEST_ACTION_VENDOR_SPECIFIC;
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unknown self-test type %s", type);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    "org.freedesktop.udisks2.nvme-smart-selftest",
                                                    options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * initiates a device self-test.
                                                     *
                                                     * Do not translate $(device.name), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to start a device self-test on $(device.name)"),
                                                    invocation))
    goto out;

  /* Time estimates */
  if (action == BD_NVME_SELF_TEST_ACTION_EXTENDED)
    time_est = (gint64) device->nvme_ctrl_info->selftest_ext_time * 60 * 1000000;

  /* Check that the Device Self-test (Log Identifier 06h) log page can be retrieved,
   * otherwise we wouldn't be able to detect the test progress and its completion.
   */
  self_test_log = bd_nvme_get_self_test_log (g_udev_device_get_device_file (device->udev_device), &error);
  if (!self_test_log)
    {
      udisks_warning ("Unable to retrieve selftest log for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  bd_nvme_self_test_log_free (self_test_log);

  /* Trigger the self-test operation and launch the monitoring job atomically */
  g_mutex_lock (&ctrl->smart_lock);
  if (!bd_nvme_device_self_test (g_udev_device_get_device_file (device->udev_device), action, &error))
    {
      g_mutex_unlock (&ctrl->smart_lock);
      udisks_warning ("Error starting device selftest for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  ctrl->selftest_job = UDISKS_THREADED_JOB (udisks_daemon_launch_threaded_job (daemon,
                                                                               UDISKS_OBJECT (object),
                                                                               "nvme-selftest",
                                                                               caller_uid,
                                                                               FALSE,
                                                                               selftest_job_func,
                                                                               g_object_ref (ctrl),
                                                                               (GDestroyNotify) selftest_job_func_done,
                                                                               NULL)); /* GCancellable */
  if (time_est > 0)
    {
      udisks_base_job_set_auto_estimate (UDISKS_BASE_JOB (ctrl->selftest_job), FALSE);
      udisks_job_set_expected_end_time (UDISKS_JOB (ctrl->selftest_job),
                                        g_get_real_time () + time_est);
    }
  udisks_threaded_job_start (ctrl->selftest_job);
  g_mutex_unlock (&ctrl->smart_lock);

  udisks_nvme_controller_complete_smart_selftest_start (_ctrl, invocation);

 out:
  g_clear_object (&device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_smart_selftest_abort (UDisksNVMeController  *_ctrl,
                             GDBusMethodInvocation *invocation,
                             GVariant              *options)
{
  UDisksLinuxNVMeController *ctrl = UDISKS_LINUX_NVME_CONTROLLER (_ctrl);
  UDisksLinuxDriveObject *object;
  UDisksDaemon *daemon;
  UDisksLinuxDevice *device = NULL;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (ctrl, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    "org.freedesktop.udisks2.nvme-smart-selftest",
                                                    options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * aborts a running device self-test.
                                                     *
                                                     * Do not translate $(device.name), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to abort a device self-test on $(device.name)"),
                                                    invocation))
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No udev device");
      goto out;
    }

  if (!bd_nvme_device_self_test (g_udev_device_get_device_file (device->udev_device),
                                 BD_NVME_SELF_TEST_ACTION_ABORT,
                                 &error))
    {
      udisks_warning ("Error aborting device selftest for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Cancel the running job */
  g_mutex_lock (&ctrl->smart_lock);
  if (ctrl->selftest_job != NULL)
    {
      GCancellable *cancellable;

      cancellable = g_object_ref (udisks_base_job_get_cancellable (UDISKS_BASE_JOB (ctrl->selftest_job)));
      g_mutex_unlock (&ctrl->smart_lock);
      /* This may trigger selftest_job_func_done() to be run as a result
       * of cancellation, leading to deadlock.
       */
      g_cancellable_cancel (cancellable);
      g_object_unref (cancellable);
      g_mutex_lock (&ctrl->smart_lock);
      while (ctrl->selftest_job != NULL)
        g_cond_wait (&ctrl->selftest_cond, &ctrl->smart_lock);
    }
  g_mutex_unlock (&ctrl->smart_lock);

  if (!udisks_linux_nvme_controller_refresh_smart_sync (ctrl,
                                                        NULL,  /* cancellable */
                                                        &error))
    {
      udisks_warning ("Error updating health information for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_nvme_controller_complete_smart_selftest_abort (_ctrl, invocation);

 out:
  g_clear_object (&device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
sanitize_job_func_done (UDisksLinuxNVMeController *ctrl)
{
  g_mutex_lock (&ctrl->smart_lock);
  ctrl->sanitize_job = NULL;
  g_mutex_unlock (&ctrl->smart_lock);
  g_object_unref (ctrl);
}

static gboolean
sanitize_job_func (UDisksThreadedJob  *job,
                   GCancellable       *cancellable,
                   gpointer            user_data,
                   GError            **error)
{
  UDisksLinuxNVMeController *ctrl = UDISKS_LINUX_NVME_CONTROLLER (user_data);
  UDisksLinuxDriveObject *object;
  UDisksLinuxDevice *device = NULL;
  UDisksDaemon *daemon;
  gboolean ret = FALSE;

  object = udisks_daemon_util_dup_object (ctrl, error);
  if (object == NULL)
    goto out;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "No udev device");
      goto out;
    }

  udisks_job_set_progress_valid (UDISKS_JOB (job), TRUE);
  udisks_job_set_progress (UDISKS_JOB (job), 0.0);

  while (TRUE)
    {
      gboolean still_in_progress;
      gdouble progress;
      GPollFD poll_fd;

      if (!udisks_linux_nvme_controller_refresh_smart_sync (ctrl,
                                                            NULL,  /* cancellable */
                                                            error))
        {
          udisks_warning ("Unable to retrieve sanitize status log for %s while polling during the sanitize operation: %s (%s, %d)",
                          g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                          (*error)->message, g_quark_to_string ((*error)->domain), (*error)->code);
          goto out;
        }

      g_mutex_lock (&ctrl->smart_lock);
      still_in_progress = ctrl->sanitize_log && ctrl->sanitize_log->sanitize_status == BD_NVME_SANITIZE_STATUS_IN_PROGRESS;
      progress = (ctrl->sanitize_log ? ctrl->sanitize_log->sanitize_progress : 0) / 100.0;
      g_mutex_unlock (&ctrl->smart_lock);

      if (!still_in_progress)
        {
          /* Finish the sanitize operation */
          if (!bd_nvme_sanitize (g_udev_device_get_device_file (device->udev_device),
                                 BD_NVME_SANITIZE_ACTION_EXIT_FAILURE,
                                 TRUE,  /* no_dealloc */
                                 0,     /* overwrite_pass_count */
                                 0,     /* overwrite_pattern */
                                 FALSE, /* overwrite_invert_pattern */
                                 error))
            {
              udisks_warning ("Error submitting the sanitize exit failure request for %s: %s (%s, %d)",
                              g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                              (*error)->message, g_quark_to_string ((*error)->domain), (*error)->code);
              goto out;
            }
          break;
        }

      if (progress < 0.0)
        progress = 0.0;
      if (progress > 1.0)
        progress = 1.0;
      udisks_job_set_progress (UDISKS_JOB (job), progress);

      /* Sleep for 10 seconds or until we're cancelled */
      if (g_cancellable_make_pollfd (cancellable, &poll_fd))
        {
          gint poll_ret;
          do
            {
              poll_ret = g_poll (&poll_fd, 1, 10 * 1000);
            }
          while (poll_ret == -1 && errno == EINTR);
          g_cancellable_release_fd (cancellable);
        }
      else
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Error creating pollfd for cancellable");
          goto out;
        }

      /* No way to abort a running sanitize operation */
    }

  ret = TRUE;

  daemon = udisks_linux_drive_object_get_daemon (object);
  /* TODO: trigger uevents on all namespaces? */
  udisks_daemon_util_trigger_uevent_sync (daemon, NULL,
                                          g_udev_device_get_sysfs_path (device->udev_device),
                                          UDISKS_DEFAULT_WAIT_TIMEOUT);

 out:
  /* terminate the job */
  g_clear_object (&device);
  g_clear_object (&object);
  return ret;
}

static gboolean
handle_sanitize_start (UDisksNVMeController  *_object,
                       GDBusMethodInvocation *invocation,
                       const gchar           *arg_action,
                       GVariant              *arg_options)
{
  UDisksLinuxNVMeController *ctrl = UDISKS_LINUX_NVME_CONTROLLER (_object);
  UDisksLinuxDriveObject *object;
  UDisksDaemon *daemon;
  UDisksLinuxDevice *device = NULL;
  BDNVMESanitizeAction action;
  BDNVMEControllerFeature ctrl_feature;
  guchar overwrite_pass_count = 0;
  guint32 overwrite_pattern = 0;
  gboolean overwrite_invert_pattern = FALSE;
  BDNVMESanitizeLog *sanitize_log;
  uid_t caller_uid;
  gint64 time_est = 0;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (ctrl, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  g_mutex_lock (&ctrl->smart_lock);
  if (ctrl->selftest_job != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "There is already device self-test running");
      g_mutex_unlock (&ctrl->smart_lock);
      goto out;
    }
  if (ctrl->sanitize_job != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "There is already a sanitize operation running");
      g_mutex_unlock (&ctrl->smart_lock);
      goto out;
    }
  g_mutex_unlock (&ctrl->smart_lock);

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No udev device");
      goto out;
    }
  if (device->nvme_ctrl_info == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No probed controller info available");
      goto out;
    }

  if (g_strcmp0 (arg_action, "block-erase") == 0)
    {
      action = BD_NVME_SANITIZE_ACTION_BLOCK_ERASE;
      ctrl_feature = BD_NVME_CTRL_FEAT_SANITIZE_BLOCK;
    }
  else if (g_strcmp0 (arg_action, "overwrite") == 0)
    {
      action = BD_NVME_SANITIZE_ACTION_OVERWRITE;
      ctrl_feature = BD_NVME_CTRL_FEAT_SANITIZE_OVERWRITE;
    }
  else if (g_strcmp0 (arg_action, "crypto-erase") == 0)
    {
      action = BD_NVME_SANITIZE_ACTION_CRYPTO_ERASE;
      ctrl_feature = BD_NVME_CTRL_FEAT_SANITIZE_CRYPTO;
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Unknown sanitize action %s", arg_action);
      goto out;
    }

  if ((device->nvme_ctrl_info->features & ctrl_feature) != ctrl_feature)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "The NVMe controller has no support for the %s sanitize operation",
                                             arg_action);
      goto out;
    }

  g_variant_lookup (arg_options, "overwrite_pass_count", "y", &overwrite_pass_count);
  g_variant_lookup (arg_options, "overwrite_pattern", "u", &overwrite_pattern);
  g_variant_lookup (arg_options, "overwrite_invert_pattern", "b", &overwrite_invert_pattern);

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    "org.freedesktop.udisks2.nvme-sanitize",
                                                    arg_options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * initiates a sanitize operation.
                                                     *
                                                     * Do not translate $(device.name), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to perform a sanitize operation of $(device.name)"),
                                                    invocation))
    goto out;

  /* Check that the Sanitize Status (Log Identifier 81h) log page can be retrieved,
   * otherwise we wouldn't be able to detect the sanitize progress and its status.
   */
  sanitize_log = bd_nvme_get_sanitize_log (g_udev_device_get_device_file (device->udev_device), &error);
  if (!sanitize_log)
    {
      udisks_warning ("Unable to retrieve sanitize status log for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  if (sanitize_log->sanitize_status == BD_NVME_SANITIZE_STATUS_IN_PROGRESS)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "There is already a sanitize operation running");
      bd_nvme_sanitize_log_free (sanitize_log);
      goto out;
    }
  /* Time estimates */
  switch (action)
    {
      case BD_NVME_SANITIZE_ACTION_BLOCK_ERASE:
          time_est = (gint64) sanitize_log->time_for_block_erase_nd * 1000000;
          break;
      case BD_NVME_SANITIZE_ACTION_OVERWRITE:
          time_est = (gint64) sanitize_log->time_for_overwrite_nd * 1000000;
          break;
      case BD_NVME_SANITIZE_ACTION_CRYPTO_ERASE:
          time_est = (gint64) sanitize_log->time_for_crypto_erase_nd * 1000000;
          break;
      default:
          udisks_warning ("Invalid sanitize action");
    }
  /* FIXME: in case of BD_NVME_SANITIZE_STATUS_FAILED, is it necessary to call
   * bd_nvme_sanitize(action=BD_NVME_SANITIZE_ACTION_EXIT_FAILURE) before
   * submitting new job?
   */
  bd_nvme_sanitize_log_free (sanitize_log);

  /* Trigger the sanitize operation and launch the monitoring job atomically */
  g_mutex_lock (&ctrl->smart_lock);
  if (!bd_nvme_sanitize (g_udev_device_get_device_file (device->udev_device),
                         action,
                         TRUE, /* no_dealloc */
                         overwrite_pass_count,
                         overwrite_pattern,
                         overwrite_invert_pattern,
                         &error))
    {
      g_mutex_unlock (&ctrl->smart_lock);
      udisks_warning ("Error starting the sanitize operation for %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  ctrl->sanitize_job = UDISKS_THREADED_JOB (udisks_daemon_launch_threaded_job (daemon,
                                                                               UDISKS_OBJECT (object),
                                                                               "nvme-sanitize",
                                                                               caller_uid,
                                                                               FALSE,
                                                                               sanitize_job_func,
                                                                               g_object_ref (ctrl),
                                                                               (GDestroyNotify) sanitize_job_func_done,
                                                                               NULL)); /* GCancellable */
  udisks_base_job_set_auto_estimate (UDISKS_BASE_JOB (ctrl->sanitize_job), FALSE);
  udisks_job_set_expected_end_time (UDISKS_JOB (ctrl->sanitize_job),
                                    g_get_real_time () + time_est);
  udisks_threaded_job_start (ctrl->sanitize_job);
  g_mutex_unlock (&ctrl->smart_lock);

  udisks_nvme_controller_complete_sanitize_start (_object, invocation);

 out:
  g_clear_object (&device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
nvme_controller_iface_init (UDisksNVMeControllerIface *iface)
{
  iface->handle_smart_update = handle_smart_update;
  iface->handle_smart_get_attributes = handle_smart_get_attributes;
  iface->handle_smart_selftest_start = handle_smart_selftest_start;
  iface->handle_smart_selftest_abort = handle_smart_selftest_abort;
  iface->handle_sanitize_start = handle_sanitize_start;
}
