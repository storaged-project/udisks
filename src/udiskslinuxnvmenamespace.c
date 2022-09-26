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

#include <errno.h>
#include <sys/types.h>
#include <string.h>

#include <blockdev/nvme.h>

#include "udiskslinuxnvmenamespace.h"
#include "udiskslogging.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udisksprivate.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxprovider.h"
#include "udisksdaemonutil.h"
#include "udisksbasejob.h"
#include "udiskssimplejob.h"
#include "udisksthreadedjob.h"
#include "udiskslinuxdevice.h"

/**
 * SECTION:udiskslinuxnvmenamespace
 * @title: UDisksLinuxNVMeNamespace
 * @short_description: Linux implementation of #UDisksNVMeNamespace
 *
 * This type provides an implementation of the #UDisksNVMeNamespace
 * interface on Linux.
 */

typedef struct _UDisksLinuxNVMeNamespaceClass   UDisksLinuxNVMeNamespaceClass;

/**
 * UDisksLinuxNVMeNamespace:
 *
 * The #UDisksLinuxNVMeNamespace structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxNVMeNamespace
{
  UDisksNVMeNamespaceSkeleton parent_instance;

  GMutex             format_lock;
  UDisksThreadedJob *format_job;
};

struct _UDisksLinuxNVMeNamespaceClass
{
  UDisksNVMeNamespaceSkeletonClass parent_class;
};

static void nvme_namespace_iface_init (UDisksNVMeNamespaceIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxNVMeNamespace, udisks_linux_nvme_namespace, UDISKS_TYPE_NVME_NAMESPACE_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_NVME_NAMESPACE, nvme_namespace_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_nvme_namespace_init (UDisksLinuxNVMeNamespace *ns)
{
  g_mutex_init (&ns->format_lock);

  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (ns),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_nvme_namespace_finalize (GObject *object)
{
  UDisksLinuxNVMeNamespace *ns = UDISKS_LINUX_NVME_NAMESPACE (object);

  g_mutex_clear (&ns->format_lock);

  if (G_OBJECT_CLASS (udisks_linux_nvme_namespace_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_nvme_namespace_parent_class)->finalize (object);
}

static void
udisks_linux_nvme_namespace_class_init (UDisksLinuxNVMeNamespaceClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = udisks_linux_nvme_namespace_finalize;
}

/**
 * udisks_linux_nvme_namespace_new:
 *
 * Creates a new #UDisksLinuxNVMeNamespace instance.
 *
 * Returns: A new #UDisksLinuxNVMeNamespace. Free with g_object_unref().
 */
UDisksNVMeNamespace *
udisks_linux_nvme_namespace_new (void)
{
  return UDISKS_NVME_NAMESPACE (g_object_new (UDISKS_TYPE_LINUX_NVME_NAMESPACE, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_nvme_namespace_update:
 * @ns: A #UDisksLinuxNVMeNamespace.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_nvme_namespace_update (UDisksLinuxNVMeNamespace *ns,
                                    UDisksLinuxBlockObject   *object)
{
  UDisksNVMeNamespace *iface = UDISKS_NVME_NAMESPACE (ns);
  UDisksLinuxDevice *device;
  guint nsid = 0;
  gint format_progress = -1;
  const gchar *nguid = NULL;
  const gchar *eui64 = NULL;
  const gchar *uuid = NULL;
  const gchar *wwn = NULL;

  device = udisks_linux_block_object_get_device (object);
  if (device == NULL)
    return;

  g_object_freeze_notify (G_OBJECT (object));
  g_mutex_lock (&ns->format_lock);

  nsid = g_udev_device_get_sysfs_attr_as_int (device->udev_device, "nsid");
  nguid = g_udev_device_get_sysfs_attr (device->udev_device, "nguid");
  /* not reading the 'uuid' attr to avoid bogus messages from the kernel:
   *   block nvme0n1: No UUID available providing old NGUID
   */
  wwn = g_udev_device_get_sysfs_attr (device->udev_device, "wwid");
  if (!wwn)
    wwn = g_udev_device_get_property (device->udev_device, "ID_WWN");

  if (device->nvme_ns_info)
    {
      nsid = device->nvme_ns_info->nsid;
      nguid = device->nvme_ns_info->nguid;
      eui64 = device->nvme_ns_info->eui64;
      uuid = device->nvme_ns_info->uuid;

      udisks_nvme_namespace_set_namespace_size (iface, device->nvme_ns_info->nsize);
      udisks_nvme_namespace_set_namespace_capacity (iface, device->nvme_ns_info->ncap);
      udisks_nvme_namespace_set_namespace_utilization (iface, device->nvme_ns_info->nuse);

      if (device->nvme_ns_info->current_lba_format.data_size > 0)
        {
          udisks_nvme_namespace_set_formatted_lbasize (iface,
              g_variant_new ("(qqy)", device->nvme_ns_info->current_lba_format.data_size,
                                      device->nvme_ns_info->current_lba_format.metadata_size,
                                      device->nvme_ns_info->current_lba_format.relative_performance));
        }

      if (device->nvme_ns_info->lba_formats && *device->nvme_ns_info->lba_formats)
        {
          GVariantBuilder builder;
          BDNVMELBAFormat **f;

          g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(qqy)"));

          for (f = device->nvme_ns_info->lba_formats; *f; f++)
            g_variant_builder_add (&builder, "(qqy)",
                                   (*f)->data_size,
                                   (*f)->metadata_size,
                                   (*f)->relative_performance);

          udisks_nvme_namespace_set_lbaformats (iface, g_variant_builder_end (&builder));
        }
      if ((device->nvme_ns_info->features & BD_NVME_NS_FEAT_FORMAT_PROGRESS) == BD_NVME_NS_FEAT_FORMAT_PROGRESS)
        format_progress = device->nvme_ns_info->format_progress_remaining;
    }

  udisks_nvme_namespace_set_nsid (iface, nsid);
  if (nguid)
    udisks_nvme_namespace_set_nguid (iface, nguid);
  if (eui64)
    udisks_nvme_namespace_set_eui64 (iface, eui64);
  if (uuid)
    udisks_nvme_namespace_set_uuid (iface, uuid);
  if (wwn)
    udisks_nvme_namespace_set_wwn (iface, wwn);
  udisks_nvme_namespace_set_format_percent_remaining (iface, format_progress);
  g_mutex_unlock (&ns->format_lock);

  g_object_thaw_notify (G_OBJECT (object));
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (ns));
  g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksLinuxNVMeNamespace *ns;
  gboolean feat_progress;
} FormatNSData;

static void
free_format_ns_data (FormatNSData *data)
{
  g_object_unref (data->ns);
  g_free (data);
}

static gboolean
format_ns_job_func (UDisksThreadedJob  *job,
                    GCancellable       *cancellable,
                    gpointer            user_data,
                    GError            **error)
{
  FormatNSData *data = user_data;
  UDisksLinuxBlockObject *object;
  UDisksLinuxDevice *device = NULL;
  gboolean ret = FALSE;

  object = udisks_daemon_util_dup_object (data->ns, error);
  if (object == NULL)
    goto out;

  device = udisks_linux_block_object_get_device (object);
  if (device == NULL)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                           "No udev device");
      goto out;
    }

  udisks_job_set_progress_valid (UDISKS_JOB (job), TRUE);
  udisks_job_set_progress (UDISKS_JOB (job), 0.0);

  while (!g_cancellable_is_cancelled (cancellable))
    {
      GPollFD poll_fd;

      if (data->feat_progress)
        {
          BDNVMENamespaceInfo *ns_info;
          gdouble progress;

          ns_info = bd_nvme_get_namespace_info (g_udev_device_get_device_file (device->udev_device), error);
          if (!ns_info)
            {
              udisks_warning ("Unable to retrieve namespace info for %s while polling during the format operation: %s (%s, %d)",
                              g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                              (*error)->message, g_quark_to_string ((*error)->domain), (*error)->code);
              goto out;
            }

          /* Update the properties */
          progress = (100 - ns_info->format_progress_remaining) / 100.0;

          g_mutex_lock (&data->ns->format_lock);
          udisks_nvme_namespace_set_format_percent_remaining (UDISKS_NVME_NAMESPACE (data->ns),
                                                              ns_info->format_progress_remaining);
          g_mutex_unlock (&data->ns->format_lock);

          if (progress < 0.0)
            progress = 0.0;
          if (progress > 1.0)
            progress = 1.0;
          udisks_job_set_progress (UDISKS_JOB (job), progress);

          bd_nvme_namespace_info_free (ns_info);
        }

      /* Sleep for 5 seconds or until we're cancelled */
      if (g_cancellable_make_pollfd (cancellable, &poll_fd))
        {
          gint poll_ret;
          do
            {
              poll_ret = g_poll (&poll_fd, 1, 5 * 1000);
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
    }

  ret = TRUE;

 out:
  /* terminate the job */
  g_mutex_lock (&data->ns->format_lock);
  data->ns->format_job = NULL;
  g_mutex_unlock (&data->ns->format_lock);
  g_clear_object (&device);
  g_clear_object (&object);
  return ret;
}


static gboolean
handle_format_namespace (UDisksNVMeNamespace   *_ns,
                         GDBusMethodInvocation *invocation,
                         GVariant              *arg_options)
{
  UDisksLinuxNVMeNamespace *ns = UDISKS_LINUX_NVME_NAMESPACE (_ns);
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon;
  UDisksLinuxDevice *device = NULL;
  guint16 lba_data_size = 0;
  guint16 metadata_size = 0;
  const gchar *arg_secure_erase = NULL;
  BDNVMEFormatSecureErase secure_erase = BD_NVME_FORMAT_SECURE_ERASE_NONE;
  uid_t caller_uid;
  FormatNSData *data;
  GCancellable *cancellable = NULL;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (_ns, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (object);
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  g_variant_lookup (arg_options, "lba_data_size", "q", &lba_data_size);
  g_variant_lookup (arg_options, "metadata_size", "q", &metadata_size);
  g_variant_lookup (arg_options, "secure_erase", "s", &arg_secure_erase);

  if (arg_secure_erase)
    {
      if (g_strcmp0 (arg_secure_erase, "user_data") == 0)
        secure_erase = BD_NVME_FORMAT_SECURE_ERASE_USER_DATA;
      else if (g_strcmp0 (arg_secure_erase, "crypto_erase") == 0)
        secure_erase = BD_NVME_FORMAT_SECURE_ERASE_CRYPTO;
      else
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Unknown secure erase type %s", arg_secure_erase);
          goto out;
        }
    }

  device = udisks_linux_block_object_get_device (object);
  if (device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No udev device");
      goto out;
    }
  if (device->nvme_ns_info == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No probed namespace info available");
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    "org.freedesktop.udisks2.nvme-format-namespace",
                                                    arg_options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * initiates a device self-test.
                                                     *
                                                     * Do not translate $(drive), it's a placeholder and
                                                     * will be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to format a namespace on $(drive)"),
                                                    invocation))
    goto out;

  /* Start the job */
  g_mutex_lock (&ns->format_lock);
  if (ns->format_job != NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "There is already a format operation running");
      g_mutex_unlock (&ns->format_lock);
      goto out;
    }
  cancellable = g_cancellable_new ();
  data = g_new0 (FormatNSData, 1);
  data->ns = g_object_ref (ns);
  data->feat_progress = (device->nvme_ns_info->features & BD_NVME_NS_FEAT_FORMAT_PROGRESS) == BD_NVME_NS_FEAT_FORMAT_PROGRESS;
  ns->format_job = UDISKS_THREADED_JOB (udisks_daemon_launch_threaded_job (daemon,
                                                                           UDISKS_OBJECT (object),
                                                                           "nvme-format-ns",
                                                                           caller_uid,
                                                                           format_ns_job_func,
                                                                           data,
                                                                           (GDestroyNotify) free_format_ns_data,
                                                                           cancellable));
  udisks_threaded_job_start (ns->format_job);
  g_mutex_unlock (&ns->format_lock);

  /* Trigger the format operation */
  if (!bd_nvme_format (g_udev_device_get_device_file (device->udev_device),
                       lba_data_size,
                       metadata_size,
                       secure_erase,
                       &error))
    {
      udisks_warning ("Error formatting namespace %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_dbus_method_invocation_take_error (invocation, error);
      g_cancellable_cancel (cancellable);
      goto out;
    }

  g_cancellable_cancel (cancellable);
  if (!udisks_linux_block_object_reread_partition_table (object, &error))
    {
      udisks_warning ("%s", error->message);
      g_clear_error (&error);
    }
  udisks_linux_block_object_trigger_uevent_sync (object, UDISKS_DEFAULT_WAIT_TIMEOUT);

  udisks_nvme_namespace_complete_format_namespace (_ns, invocation);

 out:
  g_clear_object (&device);
  g_clear_object (&object);
  g_clear_object (&cancellable);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
nvme_namespace_iface_init (UDisksNVMeNamespaceIface *iface)
{
  iface->handle_format_namespace = handle_format_namespace;
}
