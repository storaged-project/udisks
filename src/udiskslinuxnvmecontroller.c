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
#include "udiskslinuxblockobject.h"
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

  GMutex          smart_lock;
  guint64         smart_updated;
  BDNVMESmartLog *smart_log;

  gboolean        secure_erase_in_progress;
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
  g_mutex_clear (&ctrl->smart_lock);

  if (G_OBJECT_CLASS (udisks_linux_nvme_controller_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_nvme_controller_parent_class)->finalize (object);
}


static void
udisks_linux_nvme_controller_init (UDisksLinuxNVMeController *ctrl)
{
  g_mutex_init (&ctrl->smart_lock);

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
update_smart (UDisksLinuxNVMeController *ctrl,
              UDisksLinuxDevice         *device)
{
  BDNVMESmartLog *smart_log = NULL;
  guint64 updated = 0;

  g_mutex_lock (&ctrl->smart_lock);
  if (ctrl->smart_log != NULL)
    {
      smart_log = bd_nvme_smart_log_copy (ctrl->smart_log);
      updated = ctrl->smart_updated;
    }
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
  const gchar *subsysnqn = NULL;
  const gchar *transport = NULL;
  const gchar *state = NULL;
  BDNVMESmartLog *smart_log;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    return FALSE;

  g_object_freeze_notify (G_OBJECT (object));

  subsysnqn = g_udev_device_get_sysfs_attr (device->udev_device, "subsysnqn");
  cntl_id = g_udev_device_get_sysfs_attr_as_int (device->udev_device, "cntlid");
  transport = g_udev_device_get_sysfs_attr (device->udev_device, "transport");
  state = g_udev_device_get_sysfs_attr (device->udev_device, "state");

  if (device->nvme_ctrl_info)
    {
      udisks_nvme_controller_set_nvme_revision (iface, device->nvme_ctrl_info->nvme_ver);
      udisks_nvme_controller_set_unallocated_capacity (iface, device->nvme_ctrl_info->size_unalloc);
      udisks_nvme_controller_set_fguid (iface, device->nvme_ctrl_info->fguid);

      cntl_id = device->nvme_ctrl_info->ctrl_id;
      if (device->nvme_ctrl_info->subsysnqn && strlen (device->nvme_ctrl_info->subsysnqn) > 0)
        subsysnqn = device->nvme_ctrl_info->subsysnqn;
    }

  udisks_nvme_controller_set_controller_id (iface, cntl_id);
  if (subsysnqn)
    udisks_nvme_controller_set_subsystem_nqn (iface, subsysnqn);
  if (transport)
    udisks_nvme_controller_set_transport (iface, transport);
  if (state)
    udisks_nvme_controller_set_state (iface, state);

  smart_log = bd_nvme_get_smart_log (g_udev_device_get_device_file (device->udev_device), NULL);
  if (smart_log != NULL)
    {
      g_mutex_lock (&ctrl->smart_lock);

      bd_nvme_smart_log_free (ctrl->smart_log);
      ctrl->smart_log = smart_log;
      ctrl->smart_updated = time (NULL);

      g_mutex_unlock (&ctrl->smart_lock);

      update_smart (ctrl, device);
    }

  g_object_thaw_notify (G_OBJECT (object));

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (ctrl));
  g_object_unref (device);

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
 * The calling thread is blocked until the data has been obtained.
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
  BDNVMESmartLog *smart_log;

  object = udisks_daemon_util_dup_object (ctrl, error);
  if (object == NULL)
    return FALSE;

  if (ctrl->secure_erase_in_progress)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_DEVICE_BUSY,
                   "Secure erase in progress");
      g_object_unref (object);
      return FALSE;
    }

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  g_assert (device != NULL);

  smart_log = bd_nvme_get_smart_log (g_udev_device_get_device_file (device->udev_device), error);
  if (smart_log != NULL)
    {
      g_mutex_lock (&ctrl->smart_lock);

      bd_nvme_smart_log_free (ctrl->smart_log);
      ctrl->smart_log = smart_log;
      ctrl->smart_updated = time (NULL);

      g_mutex_unlock (&ctrl->smart_lock);

      update_smart (ctrl, device);

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
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to update SMART data from $(drive)");
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
nvme_controller_iface_init (UDisksNVMeControllerIface *iface)
{
  iface->handle_smart_update = handle_smart_update;
  iface->handle_smart_get_attributes = handle_smart_get_attributes;
}
