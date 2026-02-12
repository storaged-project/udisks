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
#include <string.h>
#include <stdlib.h>

#include <blockdev/nvme.h>

#include "udiskslogging.h"
#include "udiskslinuxmanagernvme.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udisksstate.h"
#include "udiskslinuxblockobject.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxdevice.h"
#include "udiskslinuxprovider.h"
#include "udiskssimplejob.h"

/**
 * SECTION:udiskslinuxmanagernvme
 * @title: UDisksLinuxManagerNVMe
 * @short_description: Linux implementation of #UDisksManagerNVMe
 *
 * This type provides an implementation of the #UDisksManagerNVMe
 * interface on Linux.
 */

typedef struct _UDisksLinuxManagerNVMeClass   UDisksLinuxManagerNVMeClass;

/**
 * UDisksLinuxManagerNVMe:
 *
 * The #UDisksLinuxManagerNVMe structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxManagerNVMe
{
  UDisksManagerNVMeSkeleton parent_instance;

  UDisksDaemon *daemon;
  GFileMonitor *etc_nvme_dir_monitor;
};

struct _UDisksLinuxManagerNVMeClass
{
  UDisksManagerNVMeSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON
};

static void on_etc_nvme_dir_monitor_changed (GFileMonitor     *monitor,
                                             GFile            *file,
                                             GFile            *other_file,
                                             GFileMonitorEvent event_type,
                                             gpointer          user_data);

static void manager_update (UDisksLinuxManagerNVMe *manager);

static void manager_iface_init (UDisksManagerNVMeIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxManagerNVMe, udisks_linux_manager_nvme, UDISKS_TYPE_MANAGER_NVME_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MANAGER_NVME, manager_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_manager_nvme_finalize (GObject *object)
{
  UDisksLinuxManagerNVMe *manager = UDISKS_LINUX_MANAGER_NVME (object);

  if (manager->etc_nvme_dir_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (manager->etc_nvme_dir_monitor,
                                            G_CALLBACK (on_etc_nvme_dir_monitor_changed),
                                            manager);
      g_object_unref (manager->etc_nvme_dir_monitor);
    }

  G_OBJECT_CLASS (udisks_linux_manager_nvme_parent_class)->finalize (object);
}

static void
udisks_linux_manager_nvme_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  UDisksLinuxManagerNVMe *manager = UDISKS_LINUX_MANAGER_NVME (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_manager_nvme_get_daemon (manager));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_nvme_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  UDisksLinuxManagerNVMe *manager = UDISKS_LINUX_MANAGER_NVME (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (manager->daemon == NULL);
      /* we don't take a reference to the daemon */
      manager->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_manager_nvme_init (UDisksLinuxManagerNVMe *manager)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (manager),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_manager_nvme_constructed (GObject *obj)
{
  UDisksLinuxManagerNVMe *manager = UDISKS_LINUX_MANAGER_NVME (obj);
  GFile *file;
  gchar *etc_nvme_path;
  GError *error = NULL;

  G_OBJECT_CLASS (udisks_linux_manager_nvme_parent_class)->constructed (obj);

  etc_nvme_path = g_build_path (G_DIR_SEPARATOR_S, PACKAGE_SYSCONF_DIR, "nvme", NULL);
  file = g_file_new_for_path (etc_nvme_path);
  manager->etc_nvme_dir_monitor = g_file_monitor_directory (file,
                                                            G_FILE_MONITOR_NONE,
                                                            NULL,
                                                            &error);
  if (manager->etc_nvme_dir_monitor != NULL)
    {
      g_signal_connect (manager->etc_nvme_dir_monitor,
                        "changed",
                        G_CALLBACK (on_etc_nvme_dir_monitor_changed),
                        manager);
    }
  else
    {
      udisks_warning ("Error monitoring directory %s: %s (%s, %d)",
                      etc_nvme_path,
                      error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  g_object_unref (file);
  g_free (etc_nvme_path);

  manager_update (manager);
}

static void
udisks_linux_manager_nvme_class_init (UDisksLinuxManagerNVMeClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed  = udisks_linux_manager_nvme_constructed;
  gobject_class->finalize     = udisks_linux_manager_nvme_finalize;
  gobject_class->set_property = udisks_linux_manager_nvme_set_property;
  gobject_class->get_property = udisks_linux_manager_nvme_get_property;

  /**
   * UDisksLinuxManagerNVMe:daemon:
   *
   * The #UDisksDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_linux_manager_nvme_new:
 * @daemon: A #UDisksDaemon.
 *
 * Creates a new #UDisksLinuxManagerNVMe instance.
 *
 * Returns: A new #UDisksLinuxManagerNVMe. Free with g_object_unref().
 */
UDisksManagerNVMe *
udisks_linux_manager_nvme_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_MANAGER_NVME (g_object_new (UDISKS_TYPE_LINUX_MANAGER_NVME,
                                            "daemon", daemon,
                                            NULL));
}

/**
 * udisks_linux_manager_nvme_get_daemon:
 * @manager: A #UDisksLinuxManagerNVMe.
 *
 * Gets the daemon used by @manager.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @manager.
 */
UDisksDaemon *
udisks_linux_manager_nvme_get_daemon (UDisksLinuxManagerNVMe *manager)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MANAGER_NVME (manager), NULL);
  return manager->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_etc_nvme_dir_monitor_changed (GFileMonitor     *monitor,
                                 GFile            *file,
                                 GFile            *other_file,
                                 GFileMonitorEvent event_type,
                                 gpointer          user_data)
{
  UDisksLinuxManagerNVMe *manager = UDISKS_LINUX_MANAGER_NVME (user_data);

  if (event_type == G_FILE_MONITOR_EVENT_CREATED ||
      event_type == G_FILE_MONITOR_EVENT_DELETED ||
      event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
      manager_update (manager);
    }
}

static void
manager_update (UDisksLinuxManagerNVMe *manager)
{
  gchar *host_nqn;
  gchar *host_id;

  host_nqn = bd_nvme_get_host_nqn (NULL);
  host_id = bd_nvme_get_host_id (NULL);
  if (! host_nqn || strlen (host_nqn) < 1)
    {
      g_free (host_nqn);
      host_nqn = bd_nvme_generate_host_nqn (NULL);
    }

  udisks_manager_nvme_set_host_nqn (UDISKS_MANAGER_NVME (manager), host_nqn);
  udisks_manager_nvme_set_host_id (UDISKS_MANAGER_NVME (manager), host_id);

  g_free (host_nqn);
  g_free (host_id);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
parse_sysfs_addr(const gchar  *addr,
                 const gchar  *transport,
                 gchar       **traddr,
                 gchar       **trsvcid,
                 gchar       **host_traddr,
                 gchar       **host_iface)
{
    gchar **s, **ss;

    if (g_strcmp0 (transport, "pcie") == 0 ||
        g_strcmp0 (transport, "loop") == 0)
        return;

    s = g_strsplit (addr, ",", -1);
    for (ss = s; *ss; ss++)
      {
        if (g_ascii_strncasecmp (*ss, "traddr=", 7) == 0)
            *traddr = g_strdup (*ss + 7);
        else if (g_ascii_strncasecmp (*ss, "trsvcid=", 8) == 0)
            *trsvcid = g_strdup (*ss + 8);
        else if (g_ascii_strncasecmp (*ss, "host_traddr=", 12) == 0)
            *host_traddr = g_strdup (*ss + 12);
        else if (g_ascii_strncasecmp (*ss, "host_iface=", 11) == 0)
            *host_iface = g_strdup (*ss + 11);
      }
    g_strfreev (s);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  const gchar *subsysnqn;
  const gchar *transport;
  const gchar *transport_addr;
  const gchar *transport_svcid;
  const gchar *host_traddr;
  const gchar *host_iface;
  const gchar *host_nqn;
  const gchar *host_id;
} WaitForConnectData;

static gboolean
fabrics_object_matches (UDisksNVMeController *ctrl,
                        UDisksNVMeFabrics    *fab,
                        WaitForConnectData   *data)
{
  gchar *traddr = NULL;
  gchar *trsvcid = NULL;
  gchar *host_traddr = NULL;
  gchar *host_iface = NULL;
  gboolean match;

  if (g_strcmp0 (udisks_nvme_controller_get_subsystem_nqn (ctrl), data->subsysnqn) != 0 ||
      g_strcmp0 (udisks_nvme_fabrics_get_transport (fab), data->transport) != 0 ||
      (data->host_nqn && g_strcmp0 (udisks_nvme_fabrics_get_host_nqn (fab), data->host_nqn) != 0) ||
      (data->host_id && g_strcmp0 (udisks_nvme_fabrics_get_host_id (fab), data->host_id) != 0))
    return FALSE;

  if (data->transport_addr || data->transport_svcid || data->host_traddr || data->host_iface)
    {
      parse_sysfs_addr (udisks_nvme_fabrics_get_transport_address (fab),
                        udisks_nvme_fabrics_get_transport (fab),
                        &traddr, &trsvcid, &host_traddr, &host_iface);

      match = (!data->transport_addr || g_strcmp0 (traddr, data->transport_addr) == 0) &&
              (!data->transport_svcid || g_strcmp0 (trsvcid, data->transport_svcid) == 0) &&
              (!data->host_traddr || g_strcmp0 (host_traddr, data->host_traddr) == 0) &&
              (!data->host_iface || g_strcmp0 (host_iface, data->host_iface) == 0);

      g_free (traddr);
      g_free (trsvcid);
      g_free (host_traddr);
      g_free (host_iface);

      return match;
    }

  return TRUE;
}

static UDisksObject *
wait_for_fabrics_object (UDisksDaemon *daemon,
                         gpointer      user_data)
{
  WaitForConnectData *data = user_data;
  UDisksObject *ret = NULL;
  GList *objects, *l;

  objects = udisks_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksNVMeController *ctrl;
      UDisksNVMeFabrics *fab;

      ctrl = udisks_object_get_nvme_controller (object);
      fab = udisks_object_get_nvme_fabrics (object);
      if (ctrl && fab && fabrics_object_matches (ctrl, fab, data))
        ret = g_object_ref (object);
      g_clear_object (&ctrl);
      g_clear_object (&fab);
      if (ret != NULL)
        break;
    }

  g_list_free_full (objects, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static BDExtraArg **
fabrics_options_to_extra (GVariant *arg_options)
{
  GPtrArray *a;
  GVariantIter iter;
  GVariant *value;
  gchar *key;

  a = g_ptr_array_new ();
  g_variant_iter_init (&iter, arg_options);
  while (g_variant_iter_loop (&iter, "{sv}", &key, &value))
    {
      gchar *v;

      if (g_ascii_strcasecmp (key, "transport_svcid") == 0 ||
          g_ascii_strcasecmp (key, "host_traddr") == 0 ||
          g_ascii_strcasecmp (key, "host_iface") == 0 ||
          g_ascii_strcasecmp (key, "host_nqn") == 0 ||
          g_ascii_strcasecmp (key, "host_id") == 0)
        continue;

      if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
        v = g_variant_dup_string (value, NULL);
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTESTRING))
        v = g_variant_dup_bytestring (value, NULL);
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
        v = g_strdup (g_variant_get_boolean (value) ? "True" : "False");
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTE))
        v = g_strdup_printf ("%u", g_variant_get_byte (value));
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT16))
        v = g_strdup_printf ("%d", g_variant_get_int16 (value));
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT16))
        v = g_strdup_printf ("%u", g_variant_get_uint16 (value));
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
        v = g_strdup_printf ("%d", g_variant_get_int32 (value));
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
        v = g_strdup_printf ("%u", g_variant_get_uint32 (value));
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT64))
        v = g_strdup_printf ("%" G_GINT64_FORMAT, g_variant_get_int64 (value));
      else if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
        v = g_strdup_printf ("%" G_GUINT64_FORMAT, g_variant_get_uint64 (value));
      else
        {
          udisks_warning ("fabrics_options_to_extra: unhandled extra option '%s' of type %s, ignoring",
                          key, g_variant_get_type_string (value));
          continue;
        }
      g_ptr_array_add (a, bd_extra_arg_new (key, v));
      g_free (v);
    }

  g_ptr_array_add (a, NULL);
  return (BDExtraArg **) g_ptr_array_free (a, FALSE);
}

static gboolean
handle_connect (UDisksManagerNVMe     *object,
                GDBusMethodInvocation *invocation,
                const gchar           *arg_subsysnqn,
                const gchar           *arg_transport,
                const gchar           *arg_transport_addr,
                GVariant              *arg_options)
{
  UDisksLinuxManagerNVMe *manager = UDISKS_LINUX_MANAGER_NVME (object);
  const gchar *transport_svcid = NULL;
  const gchar *host_traddr = NULL;
  const gchar *host_iface = NULL;
  const gchar *host_nqn = NULL;
  const gchar *host_id = NULL;
  BDExtraArg **extra_args = NULL;
  uid_t caller_uid;
  UDisksObject *ctrl_object = NULL;
  WaitForConnectData wait_data;
  UDisksLinuxDevice *device = NULL;
  UDisksLinuxProvider *provider;
  GError *error = NULL;

  if (arg_transport_addr && strlen (arg_transport_addr) == 0)
    arg_transport_addr = NULL;

  if (!udisks_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (manager->daemon,
                                                    NULL,
                                                    "org.freedesktop.udisks2.nvme-connect",
                                                    arg_options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * requests connection to a NVMeoF controller.
                                                     */
                                                    N_("Authentication is required to connect to an NVMe over Fabrics controller"),
                                                    invocation))
    goto out;

  g_variant_lookup (arg_options, "transport_svcid", "&s", &transport_svcid);
  g_variant_lookup (arg_options, "host_traddr", "&s", &host_traddr);
  g_variant_lookup (arg_options, "host_iface", "&s", &host_iface);
  g_variant_lookup (arg_options, "host_nqn", "^&ay", &host_nqn);
  g_variant_lookup (arg_options, "host_id", "^&ay", &host_id);
  extra_args = fabrics_options_to_extra (arg_options);

  if (!bd_nvme_connect (arg_subsysnqn,
                        arg_transport,
                        arg_transport_addr,
                        transport_svcid,
                        host_traddr,
                        host_iface,
                        host_nqn,
                        host_id,
                        (const BDExtraArg **) extra_args,
                        &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* Determine the resulting object */
  wait_data.subsysnqn = arg_subsysnqn;
  wait_data.transport = arg_transport;
  wait_data.transport_addr = arg_transport_addr;
  wait_data.transport_svcid = transport_svcid;
  wait_data.host_traddr = host_traddr;
  wait_data.host_iface = host_iface;
  wait_data.host_nqn = host_nqn;
  wait_data.host_id = host_id;

  ctrl_object = udisks_daemon_wait_for_object_sync (manager->daemon,
                                                    wait_for_fabrics_object,
                                                    &wait_data,
                                                    NULL,
                                                    UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                    &error);
  if (ctrl_object == NULL)
    {
      g_prefix_error (&error, "Error waiting for NVMeoF controller object: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  device = udisks_linux_drive_object_get_device (UDISKS_LINUX_DRIVE_OBJECT (ctrl_object), TRUE /* get_hw */);
  if (device)
    {
      provider = udisks_daemon_get_linux_provider (manager->daemon);
      udisks_linux_provider_trigger_nvme_subsystem_uevent (provider, arg_subsysnqn, UDISKS_UEVENT_ACTION_ADD, device);
    }

  udisks_manager_nvme_complete_connect (object,
                                        invocation,
                                        g_dbus_object_get_object_path (G_DBUS_OBJECT (ctrl_object)));

 out:
  if (ctrl_object != NULL)
    g_object_unref (ctrl_object);
  g_clear_object (&device);
  bd_extra_arg_list_free (extra_args);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksObject *object;
  const gchar *hostnqn;
  const gchar *hostid;
} WaitForHostNQNData;

static UDisksObject *
wait_for_hostnqn (UDisksDaemon *daemon,
                  gpointer      user_data)
{
  WaitForHostNQNData *data = user_data;
  UDisksManagerNVMe *manager;

  manager = udisks_object_peek_manager_nvme (data->object);
  if ((data->hostnqn && g_strcmp0 (udisks_manager_nvme_get_host_nqn (manager), data->hostnqn) == 0) ||
      (data->hostid  && g_strcmp0 (udisks_manager_nvme_get_host_id (manager), data->hostid) == 0))
    return g_object_ref (data->object);

  return NULL;
}

static gboolean
handle_set_host_nqn (UDisksManagerNVMe     *_manager,
                     GDBusMethodInvocation *invocation,
                     const gchar           *arg_hostnqn,
                     GVariant              *arg_options)
{
  UDisksLinuxManagerNVMe *manager = UDISKS_LINUX_MANAGER_NVME (_manager);
  UDisksObject *object;
  uid_t caller_uid;
  UDisksObject *wait_object = NULL;
  WaitForHostNQNData wait_data;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (_manager, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (manager->daemon,
                                                    NULL,
                                                    "org.freedesktop.udisks2.nvme-set-hostnqn-id",
                                                    arg_options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * requests setting new NVMe Host NQN value.
                                                     */
                                                    N_("Authentication is required to set NVMe Host NQN"),
                                                    invocation))
    goto out;

  if (!bd_nvme_set_host_nqn (arg_hostnqn, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  wait_data.object = object;
  wait_data.hostnqn = arg_hostnqn;
  wait_data.hostid = NULL;

  wait_object = udisks_daemon_wait_for_object_sync (manager->daemon,
                                                    wait_for_hostnqn,
                                                    &wait_data,
                                                    NULL,
                                                    UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                    &error);
  if (wait_object == NULL)
    {
      g_prefix_error (&error, "Error waiting for new Host NQN value: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_manager_nvme_complete_set_host_nqn (_manager, invocation);

 out:
  if (wait_object != NULL)
    g_object_unref (wait_object);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static gboolean
handle_set_host_id (UDisksManagerNVMe     *_manager,
                    GDBusMethodInvocation *invocation,
                    const gchar           *arg_hostid,
                    GVariant              *arg_options)
{
  UDisksLinuxManagerNVMe *manager = UDISKS_LINUX_MANAGER_NVME (_manager);
  UDisksObject *object;
  uid_t caller_uid;
  UDisksObject *wait_object = NULL;
  WaitForHostNQNData wait_data;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (_manager, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_get_caller_uid_sync (manager->daemon, invocation, NULL /* GCancellable */, &caller_uid, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (manager->daemon,
                                                    NULL,
                                                    "org.freedesktop.udisks2.nvme-set-hostnqn-id",
                                                    arg_options,
                                                    /* Translators: Shown in authentication dialog when the user
                                                     * requests setting new NVMe Host ID value.
                                                     */
                                                    N_("Authentication is required to set NVMe Host ID"),
                                                    invocation))
    goto out;

  if (!bd_nvme_set_host_id (arg_hostid, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  wait_data.object = object;
  wait_data.hostnqn = NULL;
  wait_data.hostid = arg_hostid;

  wait_object = udisks_daemon_wait_for_object_sync (manager->daemon,
                                                    wait_for_hostnqn,
                                                    &wait_data,
                                                    NULL,
                                                    UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                    &error);
  if (wait_object == NULL)
    {
      g_prefix_error (&error, "Error waiting for new Host ID value: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  udisks_manager_nvme_complete_set_host_id (_manager, invocation);

 out:
  if (wait_object != NULL)
    g_object_unref (wait_object);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
manager_iface_init (UDisksManagerNVMeIface *iface)
{
  iface->handle_connect = handle_connect;
  iface->handle_set_host_nqn = handle_set_host_nqn;
  iface->handle_set_host_id = handle_set_host_id;
}
