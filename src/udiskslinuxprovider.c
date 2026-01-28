/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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
#include <gio/gunixmounts.h>

#include <string.h>

#include "udiskslogging.h"
#include "udisksdaemon.h"
#include "udisksprovider.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxblockobject.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxmdraidobject.h"
#include "udiskslinuxmanager.h"
#include "udiskslinuxmanagernvme.h"
#include "udisksstate.h"
#include "udiskslinuxdevice.h"
#include "udisksmodulemanager.h"
#include "udisksmodule.h"
#include "udisksmoduleobject.h"
#include "udisksdaemonutil.h"
#include "udisksconfigmanager.h"
#include "udisksutabentry.h"

/**
 * SECTION:udiskslinuxprovider
 * @title: UDisksLinuxProvider
 * @short_description: Provides Linux-specific objects
 *
 * This object is used to add/remove Linux specific objects of type
 * #UDisksLinuxBlockObject, #UDisksLinuxDriveObject and #UDisksLinuxMDRaidObject.
 */

typedef struct _UDisksLinuxProviderClass   UDisksLinuxProviderClass;

/**
 * UDisksLinuxProvider:
 *
 * The #UDisksLinuxProvider structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxProvider
{
  UDisksProvider parent_instance;

  GUdevClient *gudev_client;
  GMainContext *uevent_monitor_context;
  GMainLoop *uevent_monitor_loop;
  GThread *uevent_monitor_thread;
  GAsyncQueue *probe_request_queue;
  GThread *probe_request_thread;

  UDisksObjectSkeleton *manager_object;

  /* maps from sysfs path to UDisksLinuxBlockObject objects */
  GHashTable *sysfs_to_block;

  /* maps from VPD (serial, wwn) and sysfs_path to UDisksLinuxDriveObject instances */
  GHashTable *vpd_to_drive;
  GHashTable *sysfs_path_to_drive;

  /* maps from array UUID and sysfs_path to UDisksLinuxMDRaidObject instances */
  GHashTable *uuid_to_mdraid;
  GHashTable *sysfs_path_to_mdraid;
  GHashTable *sysfs_path_to_mdraid_members;

  /* maps from UDisksModule to nested hashtables containing object skeleton instances */
  GHashTable *module_objects;

  GUnixMountMonitor *mount_monitor;
  GFileMonitor *etc_udisks2_dir_monitor;

  /* Module interfaces hashtable */
  GHashTable *module_ifaces;

  /* set to TRUE only in the coldplug phase */
  gboolean coldplug;

  guint housekeeping_timeout;
  guint64 housekeeping_last;
  gboolean housekeeping_running;
};

G_LOCK_DEFINE_STATIC (provider_lock);

struct _UDisksLinuxProviderClass
{
  UDisksProviderClass parent_class;

  /* Signals */
  void (*uevent_probed) (UDisksLinuxProvider *provider,
                         const gchar         *action,
                         UDisksLinuxDevice   *device);
};

static void udisks_linux_provider_handle_uevent (UDisksLinuxProvider *provider,
                                                 const gchar         *action,
                                                 UDisksLinuxDevice   *device);

static gboolean on_housekeeping_timeout (gpointer user_data);

static void mount_monitor_on_mountpoints_changed (GUnixMountMonitor *monitor,
                                                  gpointer           user_data);

static void crypttab_monitor_on_entry_added (UDisksCrypttabMonitor *monitor,
                                             UDisksCrypttabEntry   *entry,
                                             gpointer               user_data);

static void crypttab_monitor_on_entry_removed (UDisksCrypttabMonitor *monitor,
                                               UDisksCrypttabEntry   *entry,
                                               gpointer               user_data);

static void utab_monitor_on_entry_added (UDisksUtabMonitor *monitor,
                                         UDisksUtabEntry   *entry,
                                         gpointer           user_data);

static void utab_monitor_on_entry_removed (UDisksUtabMonitor *monitor,
                                           UDisksUtabEntry   *entry,
                                           gpointer           user_data);

static void on_etc_udisks2_dir_monitor_changed (GFileMonitor     *monitor,
                                                GFile            *file,
                                                GFile            *other_file,
                                                GFileMonitorEvent event_type,
                                                gpointer          user_data);

static void detach_module_interfaces (UDisksLinuxProvider *provider);
static void ensure_modules (UDisksLinuxProvider *provider);

enum
  {
    UEVENT_PROBED_SIGNAL,
    LAST_SIGNAL,
  };

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (UDisksLinuxProvider, udisks_linux_provider, UDISKS_TYPE_PROVIDER);

static void
udisks_linux_provider_finalize (GObject *object)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (object);
  UDisksDaemon *daemon;
  UDisksModuleManager *module_manager;

  /* stop the uevent monitor thread and wait for it */
  g_main_loop_quit (provider->uevent_monitor_loop);
  g_thread_join (provider->uevent_monitor_thread);
  g_main_loop_unref (provider->uevent_monitor_loop);
  g_main_context_unref (provider->uevent_monitor_context);

  /* stop the request thread and wait for it */
  g_async_queue_push (provider->probe_request_queue, (gpointer) 0xdeadbeef);
  g_thread_join (provider->probe_request_thread);
  g_async_queue_unref (provider->probe_request_queue);

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));

  module_manager = udisks_daemon_get_module_manager (daemon);
  g_signal_handlers_disconnect_by_func (module_manager, ensure_modules, provider);
  detach_module_interfaces (provider);

  if (provider->etc_udisks2_dir_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (provider->etc_udisks2_dir_monitor,
                                            G_CALLBACK (on_etc_udisks2_dir_monitor_changed),
                                            provider);
      g_object_unref (provider->etc_udisks2_dir_monitor);
    }

  g_hash_table_unref (provider->sysfs_to_block);
  g_hash_table_unref (provider->vpd_to_drive);
  g_hash_table_unref (provider->sysfs_path_to_drive);
  g_hash_table_unref (provider->uuid_to_mdraid);
  g_hash_table_unref (provider->sysfs_path_to_mdraid);
  g_hash_table_unref (provider->sysfs_path_to_mdraid_members);
  g_hash_table_unref (provider->module_objects);
  g_object_unref (provider->gudev_client);

  g_hash_table_unref (provider->module_ifaces);

  udisks_object_skeleton_set_manager (provider->manager_object, NULL);
  g_object_unref (provider->manager_object);

  if (provider->housekeeping_timeout > 0)
    g_source_remove (provider->housekeeping_timeout);

  g_signal_handlers_disconnect_by_func (provider->mount_monitor,
                                        G_CALLBACK (mount_monitor_on_mountpoints_changed),
                                        provider);
  g_signal_handlers_disconnect_by_func (udisks_daemon_get_crypttab_monitor (daemon),
                                        G_CALLBACK (crypttab_monitor_on_entry_added),
                                        provider);
  g_signal_handlers_disconnect_by_func (udisks_daemon_get_crypttab_monitor (daemon),
                                        G_CALLBACK (crypttab_monitor_on_entry_removed),
                                        provider);

  g_object_unref (provider->mount_monitor);

  if (G_OBJECT_CLASS (udisks_linux_provider_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_provider_parent_class)->finalize (object);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksLinuxProvider *provider;
  GUdevDevice *udev_device;
  UDisksLinuxDevice *udisks_device;
  gboolean known_block;
} ProbeRequest;

static void
probe_request_free (ProbeRequest *request)
{
  g_clear_object (&request->provider);
  g_clear_object (&request->udev_device);
  g_clear_object (&request->udisks_device);
  g_slice_free (ProbeRequest, request);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called in main thread with a processed ProbeRequest struct - see probe_request_thread_func() */
static gboolean
on_idle_with_probed_uevent (gpointer user_data)
{
  ProbeRequest *request = user_data;

  udisks_linux_provider_handle_uevent (request->provider,
                                       g_udev_device_get_action (request->udev_device),
                                       request->udisks_device);
  g_signal_emit (request->provider,
                 signals[UEVENT_PROBED_SIGNAL],
                 0,
                 g_udev_device_get_action (request->udev_device),
                 request->udisks_device);
  probe_request_free (request);
  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
uevent_is_spurious (GUdevDevice *dev)
{
  if (g_strcmp0 (g_udev_device_get_action (dev), "change") != 0)
    return FALSE;

  if (g_strcmp0 (g_udev_device_get_subsystem (dev), "block") != 0)
    return FALSE;

  if (g_strcmp0 (g_udev_device_get_devtype (dev), "disk") != 0)
    return FALSE;

  if (g_udev_device_has_property (dev, "ID_TYPE"))
    return FALSE;

  /* see kernel block/genhd.c: disk_uevents[] */
  if (g_udev_device_get_property_as_int (dev, "DISK_MEDIA_CHANGE") == 1)
    return TRUE;
  if (g_udev_device_get_property_as_int (dev, "DISK_EJECT_REQUEST") == 1)
    return TRUE;

  return FALSE;
}

static void
probe_device_thread_func (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
  ProbeRequest *request = task_data;
  request->udisks_device = udisks_linux_device_new_sync (request->udev_device, udisks_linux_provider_get_udev_client(request->provider));
  g_task_return_pointer (task, request, NULL);
}

static void
on_probe_job_completed (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GTask *task = G_TASK (res);
  ProbeRequest *request = g_task_propagate_pointer (task, NULL);
  g_idle_add (on_idle_with_probed_uevent, request);
}

static gpointer
probe_request_thread_func (gpointer user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  ProbeRequest *request;
  gboolean dev_initialized = FALSE;
  guint n_tries = 0;

  do
    {
      request = g_async_queue_pop (provider->probe_request_queue);

      /* used by _finalize() above to stop this thread - if received, we can
       * no longer use @provider
       */
      if (request == (gpointer) 0xdeadbeef)
        goto out;

      /* Try to wait for the device to become initialized(*) before we start
       * gathering data for it.
       *
       * (*) "Check if udev has already handled the device and has set up device
       *      node permissions and context, or has renamed a network device.
       *      This is only implemented for devices with a device node or network
       *      interfaces. All other devices return 1 here."
       *        -- UDEV docs
       *
       * */
      dev_initialized = g_udev_device_get_is_initialized (request->udev_device);
      for (n_tries = 5; !dev_initialized && (n_tries > 0); n_tries--)
        {
          g_usleep (100000);      /* microseconds */
          dev_initialized = g_udev_device_get_is_initialized (request->udev_device);
        }

      /* ignore spurious uevents */
      if (!request->known_block && uevent_is_spurious (request->udev_device))
        {
          probe_request_free (request);
          continue;
        }

      /* now that we've probed the device, post the request back to the main thread */
      GTask *task = g_task_new (provider, NULL, on_probe_job_completed, NULL);
      g_task_set_task_data (task, request, (GDestroyNotify) probe_request_free);
      g_task_run_in_thread (task, probe_device_thread_func);
      g_object_unref (task);
    }
  while (TRUE);

 out:
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_uevent (GUdevClient  *client,
           const gchar  *action,
           GUdevDevice  *device,
           gpointer      user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  ProbeRequest *request;
  const gchar *sysfs_path;

  request = g_slice_new0 (ProbeRequest);
  request->provider = g_object_ref (provider);
  request->udev_device = g_object_ref (device);

  sysfs_path = g_udev_device_get_sysfs_path (device);
  request->known_block = sysfs_path != NULL && g_hash_table_contains (provider->sysfs_to_block, sysfs_path);

  /* process uevent in "probing-thread" */
  g_async_queue_push (provider->probe_request_queue, request);
}


static const gchar *udev_subsystems[] = {"block", "iscsi_connection", "scsi", "nvme", NULL};

static gpointer
uevent_monitor_thread_func (gpointer user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  GUdevClient *gudev_client;

  g_main_context_push_thread_default (provider->uevent_monitor_context);

  gudev_client = g_udev_client_new (udev_subsystems);
  g_signal_connect (gudev_client,
                    "uevent",
                    G_CALLBACK (on_uevent),
                    provider);

  g_main_loop_run (provider->uevent_monitor_loop);

  g_signal_handlers_disconnect_by_func (gudev_client,
                                        G_CALLBACK (on_uevent),
                                        provider);
  g_main_context_pop_thread_default (provider->uevent_monitor_context);
  g_object_unref (gudev_client);

  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_provider_init (UDisksLinuxProvider *provider)
{
}

static void
udisks_linux_provider_constructed (GObject *object)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (object);
  UDisksDaemon *daemon;
  UDisksConfigManager *config_manager;
  GFile *file;
  GError *error = NULL;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  config_manager = udisks_daemon_get_config_manager (daemon);

  /* get ourselves an udev client */
  provider->gudev_client = g_udev_client_new (udev_subsystems);

  provider->probe_request_queue = g_async_queue_new ();
  provider->probe_request_thread = g_thread_new ("udisks-probing-thread",
                                                 probe_request_thread_func,
                                                 provider);

  provider->uevent_monitor_context = g_main_context_new ();
  provider->uevent_monitor_loop = g_main_loop_new (provider->uevent_monitor_context, FALSE);
  provider->uevent_monitor_thread = g_thread_new ("udisks-uevent-monitor-thread",
                                                  uevent_monitor_thread_func,
                                                  provider);

  provider->mount_monitor = g_unix_mount_monitor_get ();

  provider->module_ifaces = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  file = g_file_new_for_path (udisks_config_manager_get_config_dir (config_manager));
  provider->etc_udisks2_dir_monitor = g_file_monitor_directory (file,
                                                                G_FILE_MONITOR_NONE,
                                                                NULL,
                                                                &error);
  if (provider->etc_udisks2_dir_monitor != NULL)
    {
      g_signal_connect (provider->etc_udisks2_dir_monitor,
                        "changed",
                        G_CALLBACK (on_etc_udisks2_dir_monitor_changed),
                        provider);
    }
  else
    {
      udisks_warning ("Error monitoring directory %s: %s (%s, %d)",
                      udisks_config_manager_get_config_dir (config_manager),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  g_object_unref (file);
}

static void
synthesize_uevent_for_id (UDisksLinuxProvider *provider,
                          const gchar         *id,
                          const gchar         *action)
{
  GHashTableIter iter;
  UDisksLinuxDriveObject *drive_object;

  /* TODO: could have a GHashTable from id to UDisksLinuxDriveObject */
  g_hash_table_iter_init (&iter, provider->sysfs_path_to_drive);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &drive_object))
    {
      UDisksDrive *drive = udisks_object_get_drive (UDISKS_OBJECT (drive_object));
      if (drive != NULL)
        {
          if (g_strcmp0 (udisks_drive_get_id (drive), id) == 0)
            {
              udisks_debug ("synthesizing %s event on drive with id %s", action, id);
              udisks_linux_drive_object_uevent (drive_object, action, NULL);
            }
          g_object_unref (drive);
        }
    }
}

static gchar *
dup_id_from_config_name (const gchar *conf_filename)
{
  gchar *id;

  udisks_debug ("Found config file %s", conf_filename);

  if (g_str_has_suffix (conf_filename, ".conf"))
    {
      id = g_strndup (conf_filename, strlen (conf_filename) - 5);
      return id;
    }
  return NULL;
}

static void
on_etc_udisks2_dir_monitor_changed (GFileMonitor     *monitor,
                                    GFile            *file,
                                    GFile            *other_file,
                                    GFileMonitorEvent event_type,
                                    gpointer          user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);

  if (event_type == G_FILE_MONITOR_EVENT_CREATED ||
      event_type == G_FILE_MONITOR_EVENT_DELETED ||
      event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
      gchar *filename = g_file_get_basename (file);
      gchar *id = dup_id_from_config_name (filename);
      if (id)
          synthesize_uevent_for_id (provider, id, "change");
      g_free (id);
      g_free (filename);
    }
}

static guint
count_alphas (const gchar *str)
{
  guint n = 0;
  while (g_ascii_isalpha (str[n]))
    n++;
  return n;
}

static gint
device_name_cmp (const gchar *a,
                 const gchar *b)
{
  /* Ensures that sda comes before sdz%d and sdz%d comes before sdaa%d */
  if (g_str_has_prefix (a, "sd") && g_str_has_prefix (b, "sd"))
    {
      gint la = count_alphas (a);
      gint lb = count_alphas (b);
      if (la != lb)
        return la - lb;
      else
        return g_strcmp0 (a, b);
    }
  else
    {
      return g_strcmp0 (a, b);
    }
}

static gint
udev_device_name_cmp (GUdevDevice *a,
                      GUdevDevice *b)
{
  return device_name_cmp (g_udev_device_get_name (a), g_udev_device_get_name (b));
}

static GList *
get_udisks_devices (UDisksLinuxProvider *provider)
{
  GList *devices;
  GList *udisks_devices;
  GList *l;

  devices = g_udev_client_query_by_subsystem (provider->gudev_client, "block");
  devices = g_list_concat (devices, g_udev_client_query_by_subsystem (provider->gudev_client, "nvme"));

  /* make sure we process sda before sdz and sdz before sdaa */
  devices = g_list_sort (devices, (GCompareFunc) udev_device_name_cmp);

  udisks_devices = NULL;
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      if (!g_udev_device_get_is_initialized (device))
        continue;
      udisks_devices = g_list_prepend (udisks_devices, udisks_linux_device_new_sync (device, provider->gudev_client));
    }
  udisks_devices = g_list_reverse (udisks_devices);
  g_list_free_full (devices, g_object_unref);

  return udisks_devices;
}

static void
do_coldplug (UDisksLinuxProvider *provider,
             GList               *udisks_devices)
{
  GList *l;

  for (l = udisks_devices; l != NULL; l = l->next)
    {
      UDisksLinuxDevice *device = l->data;
      udisks_linux_provider_handle_uevent (provider, "add", device);
    }
}

static void
detach_module_interfaces (UDisksLinuxProvider *provider)
{
  GHashTableIter iter;
  GDBusInterfaceSkeleton *iface;

  g_hash_table_iter_init (&iter, provider->module_ifaces);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer) &iface))
    {
      g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (provider->manager_object), iface);
    }
  g_hash_table_remove_all (provider->module_ifaces);
}

static void
ensure_modules (UDisksLinuxProvider *provider)
{
  UDisksDaemon *daemon;
  UDisksModuleManager *module_manager;
  GList *udisks_devices;
  GList *modules;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  module_manager = udisks_daemon_get_module_manager (daemon);

  modules = udisks_module_manager_get_modules (module_manager);

  if (modules)
    {
      GList *l;

      /* Attach additional interfaces from modules. */
      udisks_debug ("Modules loaded, attaching interfaces...");

      for (l = modules; l != NULL; l = l->next)
        {
          UDisksModule *module = l->data;
          GDBusInterfaceSkeleton *iface;

          /* skip modules that already have their manager interface exported */
          if (! g_hash_table_contains (provider->module_ifaces, udisks_module_get_name (module)))
            {
              iface = udisks_module_new_manager (module);
              if (iface != NULL)
                {
                  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (provider->manager_object), iface);
                  g_hash_table_replace (provider->module_ifaces, g_strdup (udisks_module_get_name (module)), iface);
                }
            }
        }
      g_list_free_full (modules, g_object_unref);
    }
  else
    {
      /* Detach additional interfaces from modules. */
      udisks_debug ("Modules unloading, detaching interfaces...");
      detach_module_interfaces (provider);
    }

  /* Perform coldplug */
  udisks_debug ("Performing coldplug...");
  udisks_devices = get_udisks_devices (provider);
  do_coldplug (provider, udisks_devices);
  g_list_free_full (udisks_devices, g_object_unref);
  udisks_debug ("Coldplug complete");
}

/*
 * The logind's PrepareForSleep D-Bus signal handler. There is one boolean
 * value in the 'parameters' GVariant tuple. When TRUE, the system is about to
 * suspend/hibernate, when FALSE the system has just woken up. Since the ATA
 * drives reset their configuration during suspend it needs to be re-read and
 * applied again.
 */
static void
on_system_sleep_signal (GDBusConnection *connection,
                         const gchar *sender_name,
                         const gchar *object_path,
                         const gchar *interface_name,
                         const gchar *signal_name,
                         GVariant *parameters,
                         gpointer user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  UDisksDaemon *daemon;
  UDisksConfigManager *config_manager;
  GDir *etc_dir;
  GError *error;
  const gchar *filename;
  GVariant *tmp_bool;
  gboolean suspending;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  config_manager = udisks_daemon_get_config_manager (daemon);

  if (g_variant_n_children(parameters) != 1)
    {
      udisks_warning("Error: incorrect number of parameters to resume signal handler");
      return;
    }
  tmp_bool = g_variant_get_child_value (parameters, 0);
  if (!g_variant_is_of_type (tmp_bool, G_VARIANT_TYPE_BOOLEAN))
    {
      udisks_warning("Error: incorrect parameter type of resume signal handler");
      g_variant_unref (tmp_bool);
      return;
    }
  suspending = g_variant_get_boolean (tmp_bool);
  g_variant_unref (tmp_bool);
  if (suspending)
    return;

  etc_dir = g_dir_open (udisks_config_manager_get_config_dir (config_manager), 0, &error);
  if (!etc_dir)
    {
      udisks_warning ("Error reading directory %s: %s (%s, %d)",
                      udisks_config_manager_get_config_dir (config_manager),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      return;
    }

  while ((filename = g_dir_read_name (etc_dir)))
    if (g_str_has_suffix (filename, ".conf"))
      {
        gchar *id = dup_id_from_config_name (filename);
        synthesize_uevent_for_id (provider, id, "reconfigure");
        g_free (id);
      }

  g_dir_close (etc_dir);
}

static void
udisks_linux_provider_start (UDisksProvider *_provider)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (_provider);
  UDisksDaemon *daemon;
  UDisksManager *manager;
  UDisksManagerNVMe *manager_nvme;
  UDisksModuleManager *module_manager;
  GList *udisks_devices;
  guint n;
  GDBusConnection *dbus_conn;

  provider->coldplug = TRUE;

  if (UDISKS_PROVIDER_CLASS (udisks_linux_provider_parent_class)->start != NULL)
    UDISKS_PROVIDER_CLASS (udisks_linux_provider_parent_class)->start (_provider);

  provider->sysfs_to_block = g_hash_table_new_full (g_str_hash,
                                                    g_str_equal,
                                                    g_free,
                                                    (GDestroyNotify) g_object_unref);
  provider->vpd_to_drive = g_hash_table_new_full (g_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  (GDestroyNotify) g_object_unref);
  provider->sysfs_path_to_drive = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         g_free,
                                                         NULL);
  provider->uuid_to_mdraid = g_hash_table_new_full (g_str_hash,
                                                    g_str_equal,
                                                    g_free,
                                                    (GDestroyNotify) g_object_unref);
  provider->sysfs_path_to_mdraid = g_hash_table_new_full (g_str_hash,
                                                          g_str_equal,
                                                          g_free,
                                                          NULL);
  provider->sysfs_path_to_mdraid_members = g_hash_table_new_full (g_str_hash,
                                                                  g_str_equal,
                                                                  g_free,
                                                                  NULL);
  provider->module_objects = g_hash_table_new_full (g_direct_hash,
                                                    g_direct_equal,
                                                    NULL,
                                                    (GDestroyNotify) g_hash_table_unref);

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));

  provider->manager_object = udisks_object_skeleton_new ("/org/freedesktop/UDisks2/Manager");
  manager = udisks_linux_manager_new (daemon);
  udisks_object_skeleton_set_manager (provider->manager_object, manager);
  g_object_unref (manager);
  manager_nvme = udisks_linux_manager_nvme_new (daemon);
  udisks_object_skeleton_set_manager_nvme (provider->manager_object, manager_nvme);
  g_object_unref (manager_nvme);

  module_manager = udisks_daemon_get_module_manager (daemon);
  g_signal_connect_swapped (module_manager, "modules-activated", G_CALLBACK (ensure_modules), provider);

  g_dbus_object_manager_server_export (udisks_daemon_get_object_manager (daemon),
                                       G_DBUS_OBJECT_SKELETON (provider->manager_object));

  /* probe for extra data we don't get from udev */
  udisks_info ("Initialization (device probing)");
  udisks_devices = get_udisks_devices (provider);

  /* do two coldplug runs to handle dependencies between devices */
  for (n = 0; n < 2; n++)
    {
      udisks_info ("Initialization (coldplug %u/2)", n + 1);
      do_coldplug (provider, udisks_devices);
    }
  g_list_free_full (udisks_devices, g_object_unref);
  udisks_info ("Initialization complete");

  /* schedule housekeeping for every 10 minutes */
  provider->housekeeping_timeout = g_timeout_add_seconds (10*60,
                                                          on_housekeeping_timeout,
                                                          provider);
  /* ... and also do an initial run */
  on_housekeeping_timeout (provider);

  provider->coldplug = FALSE;

  /* update Block:Configuration whenever fstab or crypttab entries are added or removed */
  g_signal_connect (provider->mount_monitor,
                    "mountpoints-changed",
                    G_CALLBACK (mount_monitor_on_mountpoints_changed),
                    provider);
  g_signal_connect (udisks_daemon_get_crypttab_monitor (daemon),
                    "entry-added",
                    G_CALLBACK (crypttab_monitor_on_entry_added),
                    provider);
  g_signal_connect (udisks_daemon_get_crypttab_monitor (daemon),
                    "entry-removed",
                    G_CALLBACK (crypttab_monitor_on_entry_removed),
                    provider);
  g_signal_connect (udisks_daemon_get_utab_monitor (daemon),
                    "entry-added",
                    G_CALLBACK (utab_monitor_on_entry_added),
                    provider);
  g_signal_connect (udisks_daemon_get_utab_monitor (daemon),
                    "entry-removed",
                    G_CALLBACK (utab_monitor_on_entry_removed),
                    provider);

  /* The drive configurations need to be re-applied when system wakes up from suspend/hibernate */
  dbus_conn = udisks_daemon_get_connection (daemon);
  g_dbus_connection_signal_subscribe (dbus_conn,
                                      "org.freedesktop.login1",         /* sender */
                                      "org.freedesktop.login1.Manager", /* interface */
                                      "PrepareForSleep",                /* signal */
                                      "/org/freedesktop/login1",        /* object path */
                                      NULL,                             /* arg0 */
                                      G_DBUS_SIGNAL_FLAGS_NONE,         /* flags */
                                      on_system_sleep_signal,           /* callback function */
                                      provider,                         /* callback user data */
                                      NULL);                            /* user data freeing func */
}

static void
udisks_linux_provider_class_init (UDisksLinuxProviderClass *klass)
{
  GObjectClass *gobject_class;
  UDisksProviderClass *provider_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed  = udisks_linux_provider_constructed;
  gobject_class->finalize     = udisks_linux_provider_finalize;

  provider_class        = UDISKS_PROVIDER_CLASS (klass);
  provider_class->start = udisks_linux_provider_start;

  /**
   * UDisksLinuxProvider::uevent-probed
   * @provider: A #UDisksProvider.
   * @action: The action for the uevent e.g. "add", "remove", "change", "move", "online" or "offline".
   * @device: The #UDisksLinuxDevice that was probed.
   *
   * Emitted after the @device is probed.
   *
   * This signal is emitted in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread that @provider was created in.
   */
  signals[UEVENT_PROBED_SIGNAL] = g_signal_new ("uevent-probed",
                                                G_TYPE_FROM_CLASS (klass),
                                                G_SIGNAL_RUN_LAST,
                                                G_STRUCT_OFFSET (UDisksLinuxProviderClass, uevent_probed),
                                                NULL,
                                                NULL,
                                                g_cclosure_marshal_generic,
                                                G_TYPE_NONE,
                                                2,
                                                G_TYPE_STRING,
                                                UDISKS_TYPE_LINUX_DEVICE);
}

/**
 * udisks_linux_provider_new:
 * @daemon: A #UDisksDaemon.
 *
 * Create a new provider object for Linux-specific objects / functionality.
 *
 * Returns: A #UDisksLinuxProvider object. Free with g_object_unref().
 */
UDisksLinuxProvider *
udisks_linux_provider_new (UDisksDaemon  *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_LINUX_PROVIDER (g_object_new (UDISKS_TYPE_LINUX_PROVIDER,
                                              "daemon", daemon,
                                              NULL));
}

/**
 * udisks_linux_provider_get_udev_client:
 * @provider: A #UDisksLinuxProvider.
 *
 * Gets the #GUdevClient used by @provider.
 *
 * Returns: A #GUdevClient owned by @provider. Do not free.
 */
GUdevClient *
udisks_linux_provider_get_udev_client (UDisksLinuxProvider *provider)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_PROVIDER (provider), NULL);
  return provider->gudev_client;
}


/**
 * udisks_linux_provider_get_coldplug:
 * @provider: A #UDisksLinuxProvider.
 *
 * Gets whether @provider is in the coldplug phase.
 *
 * Returns: %TRUE if in the coldplug phase, %FALSE otherwise.
 **/
gboolean
udisks_linux_provider_get_coldplug (UDisksLinuxProvider *provider)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_PROVIDER (provider), FALSE);
  return provider->coldplug;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
perform_initial_housekeeping_for_drive (GTask           *task,
                                        gpointer         source_object,
                                        gpointer         task_data,
                                        GCancellable    *cancellable)
{
  UDisksLinuxDriveObject *object = UDISKS_LINUX_DRIVE_OBJECT (source_object);
  GError *error;

  error = NULL;
  if (!udisks_linux_drive_object_housekeeping (object, 0,
                                               NULL, /* TODO: cancellable */
                                               &error))
    {
      udisks_warning ("Error performing initial housekeeping for drive %s: %s (%s, %d)",
                      g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                      error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  g_task_return_boolean (task, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called with lock held */

static void
maybe_remove_mdraid_object (UDisksLinuxProvider     *provider,
                            UDisksLinuxMDRaidObject *object)
{
  gchar *object_uuid = NULL;
  UDisksDaemon *daemon = NULL;

  /* remove the object only if there are no devices left */
  if (udisks_linux_mdraid_object_have_devices (object))
    goto out;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));

  object_uuid = g_strdup (udisks_linux_mdraid_object_get_uuid (object));
  g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (daemon),
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  g_warn_if_fail (g_hash_table_remove (provider->uuid_to_mdraid, object_uuid));

 out:
  g_free (object_uuid);
}

static void
handle_block_uevent_for_mdraid_with_uuid (UDisksLinuxProvider *provider,
                                          const gchar         *action,
                                          UDisksLinuxDevice   *device,
                                          const gchar         *uuid,
                                          gboolean             is_member)
{
  UDisksLinuxMDRaidObject *object;
  UDisksDaemon *daemon;
  const gchar *sysfs_path;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  /* if uuid is NULL or bogus, consider it a remove event */
  if (uuid == NULL || g_strcmp0 (uuid, "00000000:00000000:00000000:00000000") == 0)
    action = "remove";
  else
    {
      /* sometimes the bogus UUID looks legit, but it is still bogus. */
      if (!is_member)
        {
          UDisksLinuxMDRaidObject *candidate = g_hash_table_lookup (provider->sysfs_path_to_mdraid, sysfs_path);
          if (candidate != NULL &&
              g_strcmp0 (uuid, udisks_linux_mdraid_object_get_uuid (candidate)) != 0)
            {
              udisks_debug ("UUID of %s became bogus (changed from %s to %s)",
                            sysfs_path, udisks_linux_mdraid_object_get_uuid (candidate), uuid);
              action = "remove";
            }
        }
    }

  if (g_strcmp0 (action, "remove") == 0)
    {
      /* first check if this device was a member */
      object = g_hash_table_lookup (provider->sysfs_path_to_mdraid_members, sysfs_path);
      if (object != NULL)
        {
          udisks_linux_mdraid_object_uevent (object, action, device, TRUE /* is_member */);
          g_warn_if_fail (g_hash_table_remove (provider->sysfs_path_to_mdraid_members, sysfs_path));
          maybe_remove_mdraid_object (provider, object);
        }

      /* then check if the device was the raid device */
      object = g_hash_table_lookup (provider->sysfs_path_to_mdraid, sysfs_path);
      if (object != NULL)
        {
          udisks_linux_mdraid_object_uevent (object, action, device, FALSE /* is_member */);
          g_warn_if_fail (g_hash_table_remove (provider->sysfs_path_to_mdraid, sysfs_path));
          maybe_remove_mdraid_object (provider, object);
        }
    }
  else
    {
      if (uuid == NULL)
        goto out;

      object = g_hash_table_lookup (provider->uuid_to_mdraid, uuid);
      if (object != NULL)
        {
          if (is_member)
            {
              if (g_hash_table_lookup (provider->sysfs_path_to_mdraid_members, sysfs_path) == NULL)
                g_hash_table_insert (provider->sysfs_path_to_mdraid_members, g_strdup (sysfs_path), object);
            }
          else
            {
              if (g_hash_table_lookup (provider->sysfs_path_to_mdraid, sysfs_path) == NULL)
                g_hash_table_insert (provider->sysfs_path_to_mdraid, g_strdup (sysfs_path), object);
            }
          udisks_linux_mdraid_object_uevent (object, action, device, is_member);
        }
      else
        {
          object = udisks_linux_mdraid_object_new (daemon, uuid);
          udisks_linux_mdraid_object_uevent (object, action, device, is_member);
          g_dbus_object_manager_server_export_uniquely (udisks_daemon_get_object_manager (daemon),
                                                        G_DBUS_OBJECT_SKELETON (object));
          g_hash_table_insert (provider->uuid_to_mdraid, g_strdup (uuid), object);
          if (is_member)
            g_hash_table_insert (provider->sysfs_path_to_mdraid_members, g_strdup (sysfs_path), object);
          else
            g_hash_table_insert (provider->sysfs_path_to_mdraid, g_strdup (sysfs_path), object);
        }
    }

 out:
  ;
}

static void
handle_block_uevent_for_mdraid (UDisksLinuxProvider *provider,
                                const gchar         *action,
                                UDisksLinuxDevice   *device)
{
  const gchar *uuid;
  const gchar *member_uuid;

  /* For nested RAID levels, a device can be both a member of one
   * array and the RAID device for another. Therefore we need to
   * consider both UUIDs.
   *
   * For removal, we also need to consider the case where there is no
   * UUID.
   */
  uuid = g_udev_device_get_property (device->udev_device, "UDISKS_MD_UUID");
  member_uuid = g_udev_device_get_property (device->udev_device, "UDISKS_MD_MEMBER_UUID");

  if (uuid != NULL)
    handle_block_uevent_for_mdraid_with_uuid (provider, action, device, uuid, FALSE);

  if (member_uuid != NULL)
    handle_block_uevent_for_mdraid_with_uuid (provider, action, device, member_uuid, TRUE);

  if (uuid == NULL && member_uuid == NULL)
    handle_block_uevent_for_mdraid_with_uuid (provider, action, device, NULL, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called with lock held */
static void
handle_block_uevent_for_drive (UDisksLinuxProvider *provider,
                               const gchar         *action,
                               UDisksLinuxDevice   *device)
{
  UDisksLinuxDriveObject *object;
  UDisksDaemon *daemon;
  GTask *task;
  const gchar *sysfs_path;
  gchar *vpd;

  vpd = NULL;
  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  if (g_strcmp0 (action, "remove") == 0)
    {
      object = g_hash_table_lookup (provider->sysfs_path_to_drive, sysfs_path);
      if (object != NULL)
        {
          GList *devices;

          udisks_linux_drive_object_uevent (object, action, device);

          g_warn_if_fail (g_hash_table_remove (provider->sysfs_path_to_drive, sysfs_path));

          devices = udisks_linux_drive_object_get_devices (object);
          if (devices == NULL)
            {
              const gchar *existing_vpd;
              existing_vpd = g_object_get_data (G_OBJECT (object), "x-vpd");
              g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (daemon),
                                                     g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
              g_warn_if_fail (g_hash_table_remove (provider->vpd_to_drive, existing_vpd));
            }
          g_list_free_full (devices, g_object_unref);
        }
    }
  else
    {
      if (!udisks_linux_drive_object_should_include_device (provider->gudev_client, device, &vpd))
        goto out;

      if (vpd == NULL)
        {
          udisks_debug ("Ignoring block device %s with no serial or WWN",
                        g_udev_device_get_sysfs_path (device->udev_device));
          goto out;
        }
      object = g_hash_table_lookup (provider->vpd_to_drive, vpd);
      if (object != NULL)
        {
          if (g_hash_table_lookup (provider->sysfs_path_to_drive, sysfs_path) == NULL)
            g_hash_table_insert (provider->sysfs_path_to_drive, g_strdup (sysfs_path), object);
          udisks_linux_drive_object_uevent (object, action, device);
        }
      else
        {
          if (g_strcmp0 (action, "add") == 0) /* don't create new drive object on "change" event */
            {
              object = udisks_linux_drive_object_new (daemon, device);
              if (object != NULL)
                {
                  g_object_set_data_full (G_OBJECT (object), "x-vpd", g_strdup (vpd), g_free);
                  g_dbus_object_manager_server_export_uniquely (udisks_daemon_get_object_manager (daemon),
                                                                G_DBUS_OBJECT_SKELETON (object));
                  g_hash_table_insert (provider->vpd_to_drive, g_strdup (vpd), object);
                  g_hash_table_insert (provider->sysfs_path_to_drive, g_strdup (sysfs_path), object);

                  /* schedule initial housekeeping for the drive unless coldplugging */
                  if (!provider->coldplug)
                    {
                      task = g_task_new (object, NULL, NULL, NULL);
                      g_task_run_in_thread (task, perform_initial_housekeeping_for_drive);
                      g_object_unref (task);
                    }
                }
            }
          else
            {
              udisks_critical ("Couldn't find existing drive object for device %s (uevent action '%s', VPD '%s')",
                               sysfs_path, action, vpd);
            }
        }
    }

 out:
  g_free (vpd);
}

/* ---------------------------------------------------------------------------------------------------- */

/* Things that need to be done when receiving 'remove' uevent before removing
   the dbus object.
   Currently used only to properly unset the 'CleartextDevice' property after
   removing cleartext device (e.g. when closing/locking a LUKS device)
 */
static void
block_pre_remove (UDisksLinuxProvider *provider,
                  UDisksLinuxBlockObject *object)
{
  UDisksDaemon *daemon = NULL;
  UDisksBlock *block = NULL;
  UDisksObject *backing_object = NULL;
  UDisksEncrypted *encrypted = NULL;
  gchar *backing_path = NULL;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  block = udisks_object_peek_block (UDISKS_OBJECT (object));
  if (block == NULL)
    goto out;

  backing_path = udisks_block_dup_crypto_backing_device (block);
  if (!backing_path || g_strcmp0 (backing_path, "/") == 0)
    goto out;

  backing_object = udisks_daemon_find_object (daemon, backing_path);
  if (backing_object == NULL)
    goto out;

  encrypted = udisks_object_peek_encrypted (UDISKS_OBJECT (backing_object));
  if (encrypted == NULL)
    goto out;

  udisks_encrypted_set_cleartext_device (UDISKS_ENCRYPTED (encrypted), "/");

out:
  g_clear_object (&backing_object);
  g_free (backing_path);
}

/* called with lock held */
static void
handle_block_uevent_for_block (UDisksLinuxProvider *provider,
                               const gchar         *action,
                               UDisksLinuxDevice   *device)
{
  const gchar *sysfs_path;
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon;

  if (g_strcmp0 (g_udev_device_get_subsystem (device->udev_device), "block") != 0)
    return;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  if (g_strcmp0 (action, "remove") == 0)
    {
      object = g_hash_table_lookup (provider->sysfs_to_block, sysfs_path);
      if (object != NULL)
        {
          /* TODO: consider sending the 'remove' uevent to block objects and propagate
           *       it to module interfaces so that proper cleanup could be done. Modules
           *       are still liable to perform cleanup within their object destructors.
           *       It is equally important for modules to avoid taking reference to
           *       #UDisksLinuxBlockObject as it creates recursive references and
           *       the block object may never get freed.
           */
          block_pre_remove (provider, object);
          g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (daemon),
                                                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
          g_warn_if_fail (g_hash_table_remove (provider->sysfs_to_block, sysfs_path));
        }
    }
  else
    {
      object = g_hash_table_lookup (provider->sysfs_to_block, sysfs_path);
      if (object != NULL)
        {
          udisks_linux_block_object_uevent (object, action, device);
        }
      else
        {
          object = udisks_linux_block_object_new (daemon, device);
          g_dbus_object_manager_server_export_uniquely (udisks_daemon_get_object_manager (daemon),
                                                        G_DBUS_OBJECT_SKELETON (object));
          g_hash_table_insert (provider->sysfs_to_block, g_strdup (sysfs_path), object);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/* called with lock held */
static void
handle_block_uevent_for_modules (UDisksLinuxProvider *provider,
                                 const gchar         *action,
                                 UDisksLinuxDevice   *device)
{
  GDBusObjectSkeleton *object;
  UDisksDaemon *daemon;
  UDisksModuleManager *module_manager;
  GList *modules;
  GList *l;
  GList *modules_to_remove = NULL;

  /* TODO: modules might be theoretically able to handle non-block devices */
  if (g_strcmp0 (g_udev_device_get_subsystem (device->udev_device), "block") != 0)
    return;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  module_manager = udisks_daemon_get_module_manager (daemon);

  /* The object hierarchy is as follows:
   *
   *   provider->module_objects
   *      key: pointer to #UDisksModule
   *      value: nested hashtable
   *          key: a #UDisksObjectSkeleton instance implementing the #UDisksModuleObject interface
   *          value: -- unused
   */

  /* The following algorithm brings some guarantees to existing instances:
   *  - every instance can claim one or more devices
   *  - existing instances are asked first and only when none is interested in claiming the device
   *    a new instance for the current UDisksModule is attempted to be created
   */
  modules = udisks_module_manager_get_modules (module_manager);
  for (l = modules; l; l = l->next)
    {
      UDisksModule *module = l->data;
      gboolean handled = FALSE;
      GList *instances_to_remove = NULL;
      GHashTable *inst_table;

      inst_table = g_hash_table_lookup (provider->module_objects, module);
      if (inst_table)
        {
          GHashTableIter iter;

          /* First try existing objects and ask them to process the uevent. */
          g_hash_table_iter_init (&iter, inst_table);
          while (g_hash_table_iter_next (&iter, (gpointer *) &object, NULL))
            {
              gboolean keep = TRUE;
              if (udisks_module_object_process_uevent (UDISKS_MODULE_OBJECT (object), action, device, &keep))
                {
                  handled = TRUE;
                  if (!keep)
                    {
                      /* Queue for removal. */
                      instances_to_remove = g_list_append (instances_to_remove, object);
                    }
                }
            }

          /* Batch remove instances to prevent uevent storm. */
          if (instances_to_remove != NULL)
            {
              GList *ll;

              for (ll = instances_to_remove; ll; ll = ll->next)
                {
                  object = ll->data;
                  g_warn_if_fail (g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (daemon),
                                                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object))));
                  g_warn_if_fail (g_hash_table_remove (inst_table, object));
                }
              if (g_hash_table_size (inst_table) == 0)
                {
                  /* No more instances, queue for removal. */
                  modules_to_remove = g_list_append (modules_to_remove, module);
                  inst_table = NULL;
                }
              g_list_free (instances_to_remove);
            }
        }

      /* No module object claimed or was interested in this device, try creating new instance for the current module. */
      if (! handled && g_strcmp0 (action, "remove") != 0)
        {
          GDBusObjectSkeleton **objects, **ll;

          objects = udisks_module_new_object (module, device);
          for (ll = objects; ll && *ll; ll++)
            {
              g_dbus_object_manager_server_export_uniquely (udisks_daemon_get_object_manager (daemon),
                                                            G_DBUS_OBJECT_SKELETON (*ll));
              if (inst_table == NULL)
                {
                  inst_table = g_hash_table_new_full (g_direct_hash,
                                                      g_direct_equal,
                                                      (GDestroyNotify) g_object_unref,
                                                      NULL);
                  g_hash_table_insert (provider->module_objects, module, inst_table);
                }
              g_hash_table_add (inst_table, *ll);
            }
          g_free (objects);
        }

      /* Generic module uevent handler */
      udisks_module_handle_uevent (module, device);
    }

  /* Remove empty module instance tables. */
  if (modules_to_remove != NULL)
    {
      for (l = modules_to_remove; l; l = l->next)
        {
          g_warn_if_fail (g_hash_table_size (l->data) == 0);
          g_warn_if_fail (g_hash_table_remove (provider->module_objects, l->data));
        }
      g_list_free (modules_to_remove);
    }

  g_list_free_full (modules, g_object_unref);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called with lock held */
static void
handle_block_uevent (UDisksLinuxProvider *provider,
                     const gchar         *action,
                     UDisksLinuxDevice   *device)
{
  /* We use the sysfs block device for all of
   *
   *  - UDisksLinuxDriveObject
   *  - UDisksLinuxMDRaidObject
   *  - UDisksLinuxBlockObject
   *
   * objects. Ensure that drive and mdraid objects are added before
   * and removed after block objects.
   */
  if (g_strcmp0 (action, "remove") == 0)
    {
      handle_block_uevent_for_block (provider, action, device);
      handle_block_uevent_for_drive (provider, action, device);
      handle_block_uevent_for_mdraid (provider, action, device);
      handle_block_uevent_for_modules (provider, action, device);
    }
  else
    {
      if (g_udev_device_get_property_as_boolean (device->udev_device, "DM_UDEV_DISABLE_OTHER_RULES_FLAG"))
        {
          /* Ignore the uevent if the device-mapper layer requests
           * that other rules ignore this uevent
           *
           * It's somewhat nasty to do this but it avoids all kinds of
           * race-conditions caused by the design of device-mapper
           * (such as temporary-cryptsetup nodes and cleartext devices
           * without ID_FS properties properly set).
           */
        }
      else
        {
          handle_block_uevent_for_modules (provider, action, device);
          handle_block_uevent_for_mdraid (provider, action, device);
          handle_block_uevent_for_drive (provider, action, device);
          handle_block_uevent_for_block (provider, action, device);
        }
    }

  if (g_strcmp0 (action, "add") != 0)
    {
      /* Possibly need to clean up */
      udisks_state_check (udisks_daemon_get_state (udisks_provider_get_daemon (UDISKS_PROVIDER (provider))));
    }
}

/* called without lock held */
static void
udisks_linux_provider_handle_uevent (UDisksLinuxProvider *provider,
                                     const gchar         *action,
                                     UDisksLinuxDevice   *device)
{
  const gchar *subsystem;

  G_LOCK (provider_lock);

  udisks_debug ("uevent %s %s",
                action,
                g_udev_device_get_sysfs_path (device->udev_device));

  subsystem = g_udev_device_get_subsystem (device->udev_device);
  if (g_strcmp0 (subsystem, "block") == 0 ||
      g_strcmp0 (subsystem, "nvme") == 0)
    {
      handle_block_uevent (provider, action, device);
    }

  G_UNLOCK (provider_lock);
}

/* ---------------------------------------------------------------------------------------------------- */

/* Runs in housekeeping thread - called without lock held */
static void
housekeeping_all_drives (UDisksLinuxProvider *provider,
                         guint                secs_since_last)
{
  GList *objects;
  GList *l;

  G_LOCK (provider_lock);
  objects = g_hash_table_get_values (provider->vpd_to_drive);
  g_list_foreach (objects, (GFunc) udisks_g_object_ref_foreach, NULL);
  G_UNLOCK (provider_lock);

  for (l = objects; l != NULL; l = l->next)
    {
      UDisksLinuxDriveObject *object = UDISKS_LINUX_DRIVE_OBJECT (l->data);
      GError *error;

      error = NULL;
      if (!udisks_linux_drive_object_housekeeping (object,
                                                   secs_since_last,
                                                   NULL, /* TODO: cancellable */
                                                   &error))
        {
          udisks_warning ("Error performing housekeeping for drive %s: %s (%s, %d)",
                          g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                          error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
    }

  g_list_free_full (objects, g_object_unref);
}

/* Runs in housekeeping thread - called without lock held */
static void
housekeeping_all_modules (UDisksLinuxProvider *provider,
                          guint                secs_since_last)
{
  GList *objects = NULL;
  GList *l;
  GHashTable *inst_table;
  GHashTableIter iter_modules, iter_inst;
  GDBusObjectSkeleton *inst;

  G_LOCK (provider_lock);
  g_hash_table_iter_init (&iter_modules, provider->module_objects);
  while (g_hash_table_iter_next (&iter_modules, NULL, (gpointer *) &inst_table))
    {
      g_hash_table_iter_init (&iter_inst, inst_table);
      while (g_hash_table_iter_next (&iter_inst, (gpointer *) &inst, NULL))
        objects = g_list_append (objects, g_object_ref (inst));
    }
  G_UNLOCK (provider_lock);

  for (l = objects; l != NULL; l = l->next)
    {
      UDisksModuleObject *object = UDISKS_MODULE_OBJECT (l->data);
      GError *error;

      error = NULL;
      if (! udisks_module_object_housekeeping (object,
                                               secs_since_last,
                                               NULL, /* TODO: cancellable */
                                               &error))
        {
          udisks_warning ("Error performing housekeeping for module object %s: %s (%s, %d)",
                          g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                          error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
        }
    }

  g_list_free_full (objects, g_object_unref);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
housekeeping_thread_func (GTask           *task,
                          gpointer         source_object,
                          gpointer         task_data,
                          GCancellable    *cancellable)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (source_object);
  guint secs_since_last;
  guint64 now;

  /* TODO: probably want some kind of timeout here to avoid faulty devices/drives blocking forever */

  secs_since_last = 0;
  now = time (NULL);
  if (provider->housekeeping_last > 0)
    secs_since_last = now - provider->housekeeping_last;
  provider->housekeeping_last = now;

  udisks_info ("Housekeeping initiated (%u seconds since last housekeeping)", secs_since_last);

  housekeeping_all_drives (provider, secs_since_last);
  housekeeping_all_modules (provider, secs_since_last);

  udisks_info ("Housekeeping complete");
  G_LOCK (provider_lock);
  provider->housekeeping_running = FALSE;
  G_UNLOCK (provider_lock);
  g_task_return_boolean (task, TRUE);
}

/* called from the main thread on start-up and every 10 minutes or so */
static gboolean
on_housekeeping_timeout (gpointer user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  GTask *task;

  G_LOCK (provider_lock);
  if (provider->housekeeping_running)
    goto out;
  provider->housekeeping_running = TRUE;
  task = g_task_new (provider, NULL, NULL, NULL);
  g_task_run_in_thread (task, housekeeping_thread_func);
  g_object_unref (task);

 out:
  G_UNLOCK (provider_lock);

  return TRUE; /* keep timeout around */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_block_objects (UDisksLinuxProvider *provider, const gchar *device_path)
{
  GList *objects;
  GList *l;

  G_LOCK (provider_lock);
  objects = g_hash_table_get_values (provider->sysfs_to_block);
  g_list_foreach (objects, (GFunc) udisks_g_object_ref_foreach, NULL);
  G_UNLOCK (provider_lock);

  for (l = objects; l != NULL; l = l->next)
    {
      UDisksLinuxBlockObject *object = UDISKS_LINUX_BLOCK_OBJECT (l->data);

      if (device_path == NULL)
        udisks_linux_block_object_uevent (object, "change", NULL);
      else
        {
          gchar *block_dev;
          gboolean match;

          block_dev = udisks_linux_block_object_get_device_file (object);
          match = g_strcmp0 (block_dev, device_path) == 0;
          g_free (block_dev);
          if (match)
            {
              udisks_linux_block_object_uevent (object, "change", NULL);
              break;
            }
        }
    }

  g_list_free_full (objects, g_object_unref);
}

/* fstab monitoring */
static void
mount_monitor_on_mountpoints_changed (GUnixMountMonitor *monitor,
                                      gpointer           user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  /* TODO: compare differences and only update relevant objects */
  update_block_objects (provider, NULL);
}

static void
crypttab_monitor_on_entry_added (UDisksCrypttabMonitor *monitor,
                                 UDisksCrypttabEntry   *entry,
                                 gpointer               user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_block_objects (provider, NULL);
}

static void
crypttab_monitor_on_entry_removed (UDisksCrypttabMonitor *monitor,
                                   UDisksCrypttabEntry   *entry,
                                   gpointer               user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_block_objects (provider, NULL);
}

static void
utab_monitor_on_entry_added (UDisksUtabMonitor *monitor,
                             UDisksUtabEntry   *entry,
                             gpointer           user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_block_objects (provider, udisks_utab_entry_get_source (entry));
}

static void
utab_monitor_on_entry_removed (UDisksUtabMonitor *monitor,
                               UDisksUtabEntry   *entry,
                               gpointer           user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_block_objects (provider, udisks_utab_entry_get_source (entry));
}
