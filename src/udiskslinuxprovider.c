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

#include <string.h>

#include "udiskslogging.h"
#include "udisksdaemon.h"
#include "udisksprovider.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxblockobject.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxmdraidobject.h"
#include "udiskslinuxmanager.h"
#include "udisksstate.h"
#include "udiskslinuxdevice.h"
#include "udisksmodulemanager.h"
#include "udisksdaemonutil.h"

#include <modules/udisksmoduleifacetypes.h>
#include <modules/udisksmoduleobject.h>


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

  /* maps from UDisksModuleObjectNewFuncs to nested hashtables containing object
   * skeleton instances as keys and GLists of consumed sysfs path as values */
  GHashTable *module_funcs_to_instances;

  GFileMonitor *etc_udisks2_dir_monitor;

  /* Module interfaces list */
  GList *module_ifaces;

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
};

static void udisks_linux_provider_handle_uevent (UDisksLinuxProvider *provider,
                                                 const gchar         *action,
                                                 UDisksLinuxDevice   *device);

static gboolean on_housekeeping_timeout (gpointer user_data);

static void fstab_monitor_on_entry_added (UDisksFstabMonitor *monitor,
                                          UDisksFstabEntry   *entry,
                                          gpointer            user_data);

static void fstab_monitor_on_entry_removed (UDisksFstabMonitor *monitor,
                                            UDisksFstabEntry   *entry,
                                            gpointer            user_data);

static void crypttab_monitor_on_entry_added (UDisksCrypttabMonitor *monitor,
                                             UDisksCrypttabEntry   *entry,
                                             gpointer               user_data);

static void crypttab_monitor_on_entry_removed (UDisksCrypttabMonitor *monitor,
                                               UDisksCrypttabEntry   *entry,
                                               gpointer               user_data);

#ifdef HAVE_LIBMOUNT
static void utab_monitor_on_entry_added (UDisksUtabMonitor *monitor,
                                         UDisksUtabEntry   *entry,
                                         gpointer           user_data);

static void utab_monitor_on_entry_removed (UDisksUtabMonitor *monitor,
                                           UDisksUtabEntry   *entry,
                                           gpointer           user_data);
#endif

static void on_etc_udisks2_dir_monitor_changed (GFileMonitor     *monitor,
                                                GFile            *file,
                                                GFile            *other_file,
                                                GFileMonitorEvent event_type,
                                                gpointer          user_data);

gpointer probe_request_thread_func (gpointer user_data);

G_DEFINE_TYPE (UDisksLinuxProvider, udisks_linux_provider, UDISKS_TYPE_PROVIDER);

static void
udisks_linux_provider_finalize (GObject *object)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (object);
  UDisksDaemon *daemon;

  /* stop the request thread and wait for it */
  g_async_queue_push (provider->probe_request_queue, (gpointer) 0xdeadbeef);
  g_thread_join (provider->probe_request_thread);
  g_thread_unref (provider->probe_request_thread);
  g_async_queue_unref (provider->probe_request_queue);

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));

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
  g_hash_table_unref (provider->module_funcs_to_instances);
  g_object_unref (provider->gudev_client);

  g_list_free (provider->module_ifaces);

  udisks_object_skeleton_set_manager (provider->manager_object, NULL);
  g_object_unref (provider->manager_object);

  if (provider->housekeeping_timeout > 0)
    g_source_remove (provider->housekeeping_timeout);

  g_signal_handlers_disconnect_by_func (udisks_daemon_get_fstab_monitor (daemon),
                                        G_CALLBACK (fstab_monitor_on_entry_added),
                                        provider);
  g_signal_handlers_disconnect_by_func (udisks_daemon_get_fstab_monitor (daemon),
                                        G_CALLBACK (fstab_monitor_on_entry_removed),
                                        provider);
  g_signal_handlers_disconnect_by_func (udisks_daemon_get_crypttab_monitor (daemon),
                                        G_CALLBACK (crypttab_monitor_on_entry_added),
                                        provider);
  g_signal_handlers_disconnect_by_func (udisks_daemon_get_crypttab_monitor (daemon),
                                        G_CALLBACK (crypttab_monitor_on_entry_removed),
                                        provider);

  if (G_OBJECT_CLASS (udisks_linux_provider_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_provider_parent_class)->finalize (object);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksLinuxProvider *provider;
  GUdevDevice *udev_device;
  UDisksLinuxDevice *udisks_device;
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
  probe_request_free (request);
  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

gpointer
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
      for (n_tries=5; !dev_initialized && (n_tries > 0); n_tries--)
      {
        g_usleep (100000);      /* microseconds */
        dev_initialized = g_udev_device_get_is_initialized (request->udev_device);
      }

      /* probe the device - this may take a while */
      request->udisks_device = udisks_linux_device_new_sync (request->udev_device);

      /* now that we've probed the device, post the request back to the main thread */
      g_idle_add (on_idle_with_probed_uevent, request);
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

  request = g_slice_new0 (ProbeRequest);
  request->provider = g_object_ref (provider);
  request->udev_device = g_object_ref (device);

  /* process uevent in "probing-thread" */
  g_async_queue_push (provider->probe_request_queue, request);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_provider_init (UDisksLinuxProvider *provider)
{
  const gchar *subsystems[] = {"block", "iscsi_connection", "scsi", NULL};
  GFile *file;
  GError *error = NULL;

  /* get ourselves an udev client */
  provider->gudev_client = g_udev_client_new (subsystems);

  g_signal_connect (provider->gudev_client,
                    "uevent",
                    G_CALLBACK (on_uevent),
                    provider);

  provider->probe_request_queue = g_async_queue_new ();
  provider->probe_request_thread = g_thread_new ("probing-thread",
                                                 probe_request_thread_func,
                                                 provider);

  file = g_file_new_for_path (PACKAGE_SYSCONF_DIR "/udisks2");
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
                      PACKAGE_SYSCONF_DIR "/udisks2",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  g_object_unref (file);

  provider->module_ifaces = NULL;
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

  /* make sure we process sda before sdz and sdz before sdaa */
  devices = g_list_sort (devices, (GCompareFunc) udev_device_name_cmp);

  udisks_devices = NULL;
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      if (!g_udev_device_get_is_initialized (device))
        continue;
      udisks_devices = g_list_prepend (udisks_devices, udisks_linux_device_new_sync (device));
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
ensure_modules (UDisksLinuxProvider *provider)
{
  UDisksDaemon *daemon;
  UDisksModuleManager *module_manager;
  GDBusInterfaceSkeleton *iface;
  UDisksModuleNewManagerIfaceFunc new_manager_iface_func;
  GList *udisks_devices;
  GList *l;
  gboolean do_refresh = FALSE;
  gboolean loaded;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  module_manager = udisks_daemon_get_module_manager (daemon);

  loaded = udisks_module_manager_get_modules_available (module_manager);

  if (loaded)
    {
      /* Attach additional interfaces from modules. */
      udisks_debug ("Modules loaded, attaching interfaces...");

      l = udisks_module_manager_get_new_manager_iface_funcs (module_manager);
      for (; l != NULL; l = l->next)
        {
          new_manager_iface_func = l->data;
          iface = new_manager_iface_func (daemon);
          if (iface != NULL)
            {
              g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (provider->manager_object), iface);
              g_object_unref (iface);
              do_refresh = TRUE;

              provider->module_ifaces = g_list_append (provider->module_ifaces, iface);
            }
        }
    }
  else
    {
      /* Detach additional interfaces from modules. */
      udisks_debug ("Modules unloading, detaching interfaces...");

      l = provider->module_ifaces;
      for (; l != NULL; l = l->next)
        {
          iface = l->data;
          g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (provider->manager_object), iface);

          udisks_debug ("Interface removed");
        }
      g_list_free (provider->module_ifaces);
      provider->module_ifaces = NULL;

      /* Finish module unloading. */
      udisks_module_manager_unload_modules (module_manager);

      do_refresh = TRUE;
    }

  if (do_refresh)
    {
      /* Perform coldplug */
      udisks_debug ("Performing coldplug...");

      udisks_devices = get_udisks_devices (provider);
      do_coldplug (provider, udisks_devices);
      g_list_free_full (udisks_devices, g_object_unref);

      udisks_debug ("Coldplug complete");
    }
}

/**
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
  GDir *etc_dir;
  GError *error;
  const gchar *filename;
  GVariant *tmp_bool;
  gboolean suspending;

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

  etc_dir = g_dir_open (PACKAGE_SYSCONF_DIR "/udisks2", 0, &error);
  if (!etc_dir)
    {
      udisks_warning ("Error reading directory %s: %s (%s, %d)",
                      PACKAGE_SYSCONF_DIR "/udisks2",
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
  provider->module_funcs_to_instances = g_hash_table_new_full (g_direct_hash,
                                                               g_direct_equal,
                                                               NULL,
                                                               (GDestroyNotify) g_hash_table_unref);

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));

  provider->manager_object = udisks_object_skeleton_new ("/org/freedesktop/UDisks2/Manager");
  manager = udisks_linux_manager_new (daemon);
  udisks_object_skeleton_set_manager (provider->manager_object, manager);
  g_object_unref (manager);

  module_manager = udisks_daemon_get_module_manager (daemon);
  g_signal_connect_swapped (module_manager, "notify::modules-ready", G_CALLBACK (ensure_modules), provider);
  ensure_modules (provider);

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
  g_signal_connect (udisks_daemon_get_fstab_monitor (daemon),
                    "entry-added",
                    G_CALLBACK (fstab_monitor_on_entry_added),
                    provider);
  g_signal_connect (udisks_daemon_get_fstab_monitor (daemon),
                    "entry-removed",
                    G_CALLBACK (fstab_monitor_on_entry_removed),
                    provider);
  g_signal_connect (udisks_daemon_get_crypttab_monitor (daemon),
                    "entry-added",
                    G_CALLBACK (crypttab_monitor_on_entry_added),
                    provider);
  g_signal_connect (udisks_daemon_get_crypttab_monitor (daemon),
                    "entry-removed",
                    G_CALLBACK (crypttab_monitor_on_entry_removed),
                    provider);
#ifdef HAVE_LIBMOUNT
  g_signal_connect (udisks_daemon_get_utab_monitor (daemon),
                    "entry-added",
                    G_CALLBACK (utab_monitor_on_entry_added),
                    provider);
  g_signal_connect (udisks_daemon_get_utab_monitor (daemon),
                    "entry-removed",
                    G_CALLBACK (utab_monitor_on_entry_removed),
                    provider);
#endif

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
  gobject_class->finalize     = udisks_linux_provider_finalize;

  provider_class        = UDISKS_PROVIDER_CLASS (klass);
  provider_class->start = udisks_linux_provider_start;
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
  UDisksLinuxDriveObject *object = UDISKS_LINUX_DRIVE_OBJECT (task_data);
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
  if (! uuid)
    uuid = g_udev_device_get_property (device->udev_device, "STORAGED_MD_UUID");

  member_uuid = g_udev_device_get_property (device->udev_device, "UDISKS_MD_MEMBER_UUID");
  if (! member_uuid)
    member_uuid = g_udev_device_get_property (device->udev_device, "STORAGED_MD_MEMBER_UUID");

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
                      task = g_task_new (NULL, NULL, NULL, NULL);
                      g_task_set_task_data (task, g_object_ref (object), NULL);
                      g_task_run_in_thread (task, perform_initial_housekeeping_for_drive);
                      g_object_unref (task);
                    }
                }
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

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  if (g_strcmp0 (action, "remove") == 0)
    {
      object = g_hash_table_lookup (provider->sysfs_to_block, sysfs_path);
      if (object != NULL)
        {
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
  const gchar *sysfs_path;
  GDBusObjectSkeleton *object;
  UDisksDaemon *daemon;
  UDisksModuleManager *module_manager;
  GList *new_funcs, *l, *ll;
  UDisksModuleObjectNewFunc module_object_new_func;
  GHashTable *inst_table;
  GHashTableIter iter;
  gboolean handled;
  GHashTable *inst_sysfs_paths;
  GList *instances_to_remove;
  GList *funcs_to_remove = NULL;

  daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  module_manager = udisks_daemon_get_module_manager (daemon);
  if (! udisks_module_manager_get_modules_available (module_manager))
    return;

  new_funcs = udisks_module_manager_get_module_object_new_funcs (module_manager);

  /* The object hierarchy is as follows:
   *
   *   provider->module_funcs_to_instances:
   *      key: a UDisksModuleObjectNewFunc (pointer)
   *      value: nested hashtable
   *          key: a UDisksObjectSkeleton instance implementing the UDisksModuleObject interface
   *          value: nested hashtable
   *              key: sysfs path attached to the UDisksObjectSkeleton instance
   *              value: -- no values, just keys
   */

  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  /* The following algorithm brings some guarantees to existing instances:
   *  - every instance can claim one or more devices (sysfs paths)
   *  - existing instances are asked first and only when none is interested in claiming the device
   *    a new instance for the current UDisksModuleObjectNewFunc is attempted to be created
   */

  for (l = new_funcs; l; l = l->next)
    {
      handled = FALSE;
      instances_to_remove = NULL;
      module_object_new_func = l->data;
      inst_table = g_hash_table_lookup (provider->module_funcs_to_instances, module_object_new_func);
      if (inst_table)
        {
          /* First try existing instances and ask them to process the uevent */
          g_hash_table_iter_init (&iter, inst_table);
          while (g_hash_table_iter_next (&iter, (gpointer *) &object, (gpointer *) &inst_sysfs_paths))
            {
              if (udisks_module_object_process_uevent (UDISKS_MODULE_OBJECT (object), action, device))
                {
                  handled = TRUE;
                  if (g_hash_table_contains (inst_sysfs_paths, sysfs_path))
                    {
                      /* sysfs paths match, the object has processed the event just fine */
                    }
                  else
                    {
                      /* sysfs paths don't match yet the foreign instance is interested in claiming the device */
                      g_hash_table_add (inst_sysfs_paths, g_strdup (sysfs_path));
                    }
                }
              else
                {
                  if (g_hash_table_contains (inst_sysfs_paths, sysfs_path))
                    {
                      /* sysfs paths match, the object has indicated it's no longer interested in the current sysfs path */
                      g_warn_if_fail (g_hash_table_remove (inst_sysfs_paths, sysfs_path));
                      if (g_hash_table_size (inst_sysfs_paths) == 0)
                        {
                          /* no more sysfs paths, queue for removal */
                          instances_to_remove = g_list_append (instances_to_remove, object);
                        }
                      handled = TRUE;
                     }
                  else
                    {
                      /* the instance is not interested in claiming this device */
                      ;
                    }
                }
            }

          /* Remove empty instances */
          if (instances_to_remove != NULL)
            {
              for (ll = instances_to_remove; ll; ll = ll->next)
                {
                  object = ll->data;
                  g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (daemon),
                                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
                  g_warn_if_fail (g_hash_table_remove (inst_table, object));
                }
              if (g_hash_table_size (inst_table) == 0)
                {
                  /* no more instances, queue for removal */
                  funcs_to_remove = g_list_append (funcs_to_remove, module_object_new_func);
                  inst_table = NULL;
                }
              g_list_free (instances_to_remove);
            }
        }

      /* no instance claimed or no instance was interested in this sysfs path, try creating new instance for the current UDisksModuleObjectNewFunc */
      if (! handled)
        {
          object = module_object_new_func (daemon, device);
          if (object != NULL)
            {
              g_dbus_object_manager_server_export_uniquely (udisks_daemon_get_object_manager (daemon),
                                                            G_DBUS_OBJECT_SKELETON (object));
              inst_sysfs_paths = g_hash_table_new_full (g_str_hash,
                                                        g_str_equal,
                                                        (GDestroyNotify) g_free,
                                                        NULL);
              g_hash_table_add (inst_sysfs_paths, g_strdup (sysfs_path));
              if (inst_table == NULL)
                {
                  inst_table = g_hash_table_new_full (g_direct_hash,
                                                      g_direct_equal,
                                                      (GDestroyNotify) g_object_unref,
                                                      (GDestroyNotify) g_hash_table_unref);
                  g_hash_table_insert (provider->module_funcs_to_instances, module_object_new_func, inst_table);
                }
              g_hash_table_insert (inst_table, object, inst_sysfs_paths);
            }
        }
    }

  /* Remove empty funcs */
  if (funcs_to_remove != NULL)
    {
      for (ll = funcs_to_remove; ll; ll = ll->next)
        g_warn_if_fail (g_hash_table_remove (provider->module_funcs_to_instances, ll->data));
      g_list_free (funcs_to_remove);
    }
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
  if (g_strcmp0 (subsystem, "block") == 0)
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
  GHashTableIter iter_funcs, iter_inst;
  GDBusObjectSkeleton *inst;

  G_LOCK (provider_lock);
  g_hash_table_iter_init (&iter_funcs, provider->module_funcs_to_instances);
  while (g_hash_table_iter_next (&iter_funcs, NULL, (gpointer *) &inst_table))
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
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (task_data);
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
  task = g_task_new (NULL, NULL, NULL, NULL);
  g_task_set_task_data (task, g_object_ref (provider), NULL);
  g_task_run_in_thread (task, housekeeping_thread_func);
  g_object_unref (task);

 out:
  G_UNLOCK (provider_lock);

  return TRUE; /* keep timeout around */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_all_block_objects (UDisksLinuxProvider *provider)
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
      udisks_linux_block_object_uevent (object, "change", NULL);
    }

  g_list_free_full (objects, g_object_unref);
}

static void
fstab_monitor_on_entry_added (UDisksFstabMonitor *monitor,
                              UDisksFstabEntry   *entry,
                              gpointer            user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}

static void
fstab_monitor_on_entry_removed (UDisksFstabMonitor *monitor,
                                UDisksFstabEntry   *entry,
                                gpointer            user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}

static void
crypttab_monitor_on_entry_added (UDisksCrypttabMonitor *monitor,
                                 UDisksCrypttabEntry   *entry,
                                 gpointer               user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}

static void
crypttab_monitor_on_entry_removed (UDisksCrypttabMonitor *monitor,
                                   UDisksCrypttabEntry   *entry,
                                   gpointer               user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}

#ifdef HAVE_LIBMOUNT
static void
utab_monitor_on_entry_added (UDisksUtabMonitor *monitor,
                             UDisksUtabEntry   *entry,
                             gpointer           user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}

static void
utab_monitor_on_entry_removed (UDisksUtabMonitor *monitor,
                               UDisksUtabEntry   *entry,
                               gpointer           user_data)
{
  UDisksLinuxProvider *provider = UDISKS_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}
#endif
