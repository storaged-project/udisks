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

#include "storagedlogging.h"
#include "storageddaemon.h"
#include "storagedprovider.h"
#include "storagedlinuxprovider.h"
#include "storagedlinuxblockobject.h"
#include "storagedlinuxdriveobject.h"
#include "storagedlinuxmdraidobject.h"
#include "storagedlinuxmanager.h"
#include "storagedstate.h"
#include "storagedlinuxdevice.h"
#include "storagedmodulemanager.h"

#include <modules/storagedmoduleifacetypes.h>
#include <modules/storagedmoduleobject.h>


/**
 * SECTION:storagedlinuxprovider
 * @title: StoragedLinuxProvider
 * @short_description: Provides Linux-specific objects
 *
 * This object is used to add/remove Linux specific objects of type
 * #StoragedLinuxBlockObject, #StoragedLinuxDriveObject and #StoragedLinuxMDRaidObject.
 */

typedef struct _StoragedLinuxProviderClass   StoragedLinuxProviderClass;

/**
 * StoragedLinuxProvider:
 *
 * The #StoragedLinuxProvider structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _StoragedLinuxProvider
{
  StoragedProvider parent_instance;

  GUdevClient *gudev_client;
  GAsyncQueue *probe_request_queue;
  GThread *probe_request_thread;

  StoragedObjectSkeleton *manager_object;

  /* maps from sysfs path to StoragedLinuxBlockObject objects */
  GHashTable *sysfs_to_block;

  /* maps from VPD (serial, wwn) and sysfs_path to StoragedLinuxDriveObject instances */
  GHashTable *vpd_to_drive;
  GHashTable *sysfs_path_to_drive;

  /* maps from array UUID and sysfs_path to StoragedLinuxMDRaidObject instances */
  GHashTable *uuid_to_mdraid;
  GHashTable *sysfs_path_to_mdraid;
  GHashTable *sysfs_path_to_mdraid_members;

  /* maps from StoragedModuleObjectNewFuncs to nested hashtables containing object
   * skeleton instances as keys and GLists of consumed sysfs path as values */
  GHashTable *module_funcs_to_instances;

  GFileMonitor *etc_storaged_dir_monitor;

  /* set to TRUE only in the coldplug phase */
  gboolean coldplug;

  guint housekeeping_timeout;
  guint64 housekeeping_last;
  gboolean housekeeping_running;
};

G_LOCK_DEFINE_STATIC (provider_lock);

struct _StoragedLinuxProviderClass
{
  StoragedProviderClass parent_class;
};

static void storaged_linux_provider_handle_uevent (StoragedLinuxProvider *provider,
                                                   const gchar           *action,
                                                   StoragedLinuxDevice   *device);

static gboolean on_housekeeping_timeout (gpointer user_data);

static void fstab_monitor_on_entry_added (StoragedFstabMonitor *monitor,
                                          StoragedFstabEntry   *entry,
                                          gpointer              user_data);

static void fstab_monitor_on_entry_removed (StoragedFstabMonitor *monitor,
                                            StoragedFstabEntry   *entry,
                                            gpointer              user_data);

static void crypttab_monitor_on_entry_added (StoragedCrypttabMonitor *monitor,
                                             StoragedCrypttabEntry   *entry,
                                             gpointer                 user_data);

static void crypttab_monitor_on_entry_removed (StoragedCrypttabMonitor *monitor,
                                               StoragedCrypttabEntry   *entry,
                                               gpointer                 user_data);

static void on_etc_storaged_dir_monitor_changed (GFileMonitor     *monitor,
                                                 GFile            *file,
                                                 GFile            *other_file,
                                                 GFileMonitorEvent event_type,
                                                 gpointer          user_data);

gpointer probe_request_thread_func (gpointer user_data);

G_DEFINE_TYPE (StoragedLinuxProvider, storaged_linux_provider, STORAGED_TYPE_PROVIDER);

static void
storaged_linux_provider_finalize (GObject *object)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (object);
  StoragedDaemon *daemon;

  /* stop the request thread and wait for it */
  g_async_queue_push (provider->probe_request_queue, (gpointer) 0xdeadbeef);
  g_thread_join (provider->probe_request_thread);
  g_async_queue_unref (provider->probe_request_queue);

  daemon = storaged_provider_get_daemon (STORAGED_PROVIDER (provider));

  if (provider->etc_storaged_dir_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (provider->etc_storaged_dir_monitor,
                                            G_CALLBACK (on_etc_storaged_dir_monitor_changed),
                                            provider);
      g_object_unref (provider->etc_storaged_dir_monitor);
    }

  g_hash_table_unref (provider->sysfs_to_block);
  g_hash_table_unref (provider->vpd_to_drive);
  g_hash_table_unref (provider->sysfs_path_to_drive);
  g_hash_table_unref (provider->uuid_to_mdraid);
  g_hash_table_unref (provider->sysfs_path_to_mdraid);
  g_hash_table_unref (provider->sysfs_path_to_mdraid_members);
  g_hash_table_unref (provider->module_funcs_to_instances);
  g_object_unref (provider->gudev_client);

  storaged_object_skeleton_set_manager (provider->manager_object, NULL);
  g_object_unref (provider->manager_object);

  if (provider->housekeeping_timeout > 0)
    g_source_remove (provider->housekeeping_timeout);

  g_signal_handlers_disconnect_by_func (storaged_daemon_get_fstab_monitor (daemon),
                                        G_CALLBACK (fstab_monitor_on_entry_added),
                                        provider);
  g_signal_handlers_disconnect_by_func (storaged_daemon_get_fstab_monitor (daemon),
                                        G_CALLBACK (fstab_monitor_on_entry_removed),
                                        provider);
  g_signal_handlers_disconnect_by_func (storaged_daemon_get_crypttab_monitor (daemon),
                                        G_CALLBACK (crypttab_monitor_on_entry_added),
                                        provider);
  g_signal_handlers_disconnect_by_func (storaged_daemon_get_crypttab_monitor (daemon),
                                        G_CALLBACK (crypttab_monitor_on_entry_removed),
                                        provider);

  if (G_OBJECT_CLASS (storaged_linux_provider_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_provider_parent_class)->finalize (object);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  StoragedLinuxProvider *provider;
  GUdevDevice *udev_device;
  StoragedLinuxDevice *storaged_device;
} ProbeRequest;

static void
probe_request_free (ProbeRequest *request)
{
  g_clear_object (&request->provider);
  g_clear_object (&request->udev_device);
  g_clear_object (&request->storaged_device);
  g_slice_free (ProbeRequest, request);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called in main thread with a processed ProbeRequest struct - see probe_request_thread_func() */
static gboolean
on_idle_with_probed_uevent (gpointer user_data)
{
  ProbeRequest *request = user_data;
  storaged_linux_provider_handle_uevent (request->provider,
                                         g_udev_device_get_action (request->udev_device),
                                         request->storaged_device);
  probe_request_free (request);
  return FALSE; /* remove source */
}

/* ---------------------------------------------------------------------------------------------------- */

gpointer
probe_request_thread_func (gpointer user_data)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (user_data);
  ProbeRequest *request;

  do
    {
      request = g_async_queue_pop (provider->probe_request_queue);

      /* used by _finalize() above to stop this thread - if received, we can
       * no longer use @provider
       */
      if (request == (gpointer) 0xdeadbeef)
        goto out;

      /* probe the device - this may take a while */
      request->storaged_device = storaged_linux_device_new_sync (request->udev_device);

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
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (user_data);
  ProbeRequest *request;

  request = g_slice_new0 (ProbeRequest);
  request->provider = g_object_ref (provider);
  request->udev_device = g_object_ref (device);

  /* process uevent in "probing-thread" */
  g_async_queue_push (provider->probe_request_queue, request);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_provider_init (StoragedLinuxProvider *provider)
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

  file = g_file_new_for_path (PACKAGE_SYSCONF_DIR "/storaged");
  provider->etc_storaged_dir_monitor = g_file_monitor_directory (file,
                                                                G_FILE_MONITOR_NONE,
                                                                NULL,
                                                                &error);
  if (provider->etc_storaged_dir_monitor != NULL)
    {
      g_signal_connect (provider->etc_storaged_dir_monitor,
                        "changed",
                        G_CALLBACK (on_etc_storaged_dir_monitor_changed),
                        provider);
    }
  else
    {
      storaged_warning ("Error monitoring directory %s: %s (%s, %d)",
                      PACKAGE_SYSCONF_DIR "/storaged",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  g_object_unref (file);

}

static void
update_drive_with_id (StoragedLinuxProvider *provider,
                      const gchar           *id)
{
  GHashTableIter iter;
  StoragedLinuxDriveObject *drive_object;

  /* TODO: could have a GHashTable from id to StoragedLinuxDriveObject */
  g_hash_table_iter_init (&iter, provider->sysfs_path_to_drive);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &drive_object))
    {
      StoragedDrive *drive = storaged_object_get_drive (STORAGED_OBJECT (drive_object));
      if (drive != NULL)
        {
          if (g_strcmp0 (storaged_drive_get_id (drive), id) == 0)
            {
              //storaged_debug ("synthesizing change event on drive with id %s", id);
              storaged_linux_drive_object_uevent (drive_object, "change", NULL);
            }
          g_object_unref (drive);
        }
    }
}

static void
on_etc_storaged_dir_monitor_changed (GFileMonitor     *monitor,
                                     GFile            *file,
                                     GFile            *other_file,
                                     GFileMonitorEvent event_type,
                                     gpointer          user_data)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (user_data);

  if (event_type == G_FILE_MONITOR_EVENT_CREATED ||
      event_type == G_FILE_MONITOR_EVENT_DELETED ||
      event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
      gchar *filename = g_file_get_basename (file);
      if (g_str_has_suffix (filename, ".conf"))
        {
          gchar *id;
          id = g_strndup (filename, strlen (filename) - strlen(".conf"));
          update_drive_with_id (provider, id);
          g_free (id);
        }
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
get_storaged_devices (StoragedLinuxProvider *provider)
{
  GList *devices;
  GList *storaged_devices;
  GList *l;

  devices = g_udev_client_query_by_subsystem (provider->gudev_client, "block");

  /* make sure we process sda before sdz and sdz before sdaa */
  devices = g_list_sort (devices, (GCompareFunc) udev_device_name_cmp);

  storaged_devices = NULL;
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      storaged_devices = g_list_prepend (storaged_devices, storaged_linux_device_new_sync (device));
    }
  storaged_devices = g_list_reverse (storaged_devices);
  g_list_free_full (devices, g_object_unref);

  return storaged_devices;
}

static void
do_coldplug (StoragedLinuxProvider *provider,
             GList                 *storaged_devices)
{
  GList *l;

  for (l = storaged_devices; l != NULL; l = l->next)
    {
      StoragedLinuxDevice *device = l->data;
      storaged_linux_provider_handle_uevent (provider, "add", device);
    }
}

static void
ensure_modules (StoragedLinuxProvider *provider)
{
  StoragedDaemon *daemon;
  StoragedModuleManager *module_manager;
  GDBusInterfaceSkeleton *iface;
  StoragedModuleNewManagerIfaceFunc new_manager_iface_func;
  GList *storaged_devices;
  GList *l;
  gboolean do_refresh = FALSE;

  daemon = storaged_provider_get_daemon (STORAGED_PROVIDER (provider));
  module_manager = storaged_daemon_get_module_manager (daemon);

  if (! storaged_module_manager_get_modules_available (module_manager))
    return;

  storaged_debug ("Modules loaded, attaching interfaces...");
  /* Attach additional interfaces from modules */
  l = storaged_module_manager_get_new_manager_iface_funcs (module_manager);
  for (; l != NULL; l = l->next)
    {
      new_manager_iface_func = l->data;
      iface = new_manager_iface_func (daemon);
      if (iface != NULL)
        {
          g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (provider->manager_object), iface);
          g_object_unref (iface);
          do_refresh = TRUE;
        }
    }

  if (do_refresh)
    {
      /* Perform coldplug */
      storaged_debug ("Performing coldplug...");

      storaged_devices = get_storaged_devices (provider);
      do_coldplug (provider, storaged_devices);
      g_list_free_full (storaged_devices, g_object_unref);

      storaged_debug ("Coldplug complete");
    }
}

static void
storaged_linux_provider_start (StoragedProvider *_provider)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (_provider);
  StoragedDaemon *daemon;
  StoragedManager *manager;
  StoragedModuleManager *module_manager;
  GList *storaged_devices;
  guint n;

  provider->coldplug = TRUE;

  if (STORAGED_PROVIDER_CLASS (storaged_linux_provider_parent_class)->start != NULL)
    STORAGED_PROVIDER_CLASS (storaged_linux_provider_parent_class)->start (_provider);

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

  daemon = storaged_provider_get_daemon (STORAGED_PROVIDER (provider));

  provider->manager_object = storaged_object_skeleton_new ("/org/storaged/Storaged/Manager");
  manager = storaged_linux_manager_new (daemon);
  storaged_object_skeleton_set_manager (provider->manager_object, manager);
  g_object_unref (manager);

  module_manager = storaged_daemon_get_module_manager (daemon);
  g_signal_connect_swapped (module_manager, "notify::modules-ready", G_CALLBACK (ensure_modules), provider);
  ensure_modules (provider);

  g_dbus_object_manager_server_export (storaged_daemon_get_object_manager (daemon),
                                       G_DBUS_OBJECT_SKELETON (provider->manager_object));

  /* probe for extra data we don't get from udev */
  storaged_info ("Initialization (device probing)");
  storaged_devices = get_storaged_devices (provider);

  /* do two coldplug runs to handle dependencies between devices */
  for (n = 0; n < 2; n++)
    {
      storaged_info ("Initialization (coldplug %d/2)", n + 1);
      do_coldplug (provider, storaged_devices);
    }
  g_list_free_full (storaged_devices, g_object_unref);
  storaged_info ("Initialization complete");

  /* schedule housekeeping for every 10 minutes */
  provider->housekeeping_timeout = g_timeout_add_seconds (10*60,
                                                          on_housekeeping_timeout,
                                                          provider);
  /* ... and also do an initial run */
  on_housekeeping_timeout (provider);

  provider->coldplug = FALSE;

  /* update Block:Configuration whenever fstab or crypttab entries are added or removed */
  g_signal_connect (storaged_daemon_get_fstab_monitor (daemon),
                    "entry-added",
                    G_CALLBACK (fstab_monitor_on_entry_added),
                    provider);
  g_signal_connect (storaged_daemon_get_fstab_monitor (daemon),
                    "entry-removed",
                    G_CALLBACK (fstab_monitor_on_entry_removed),
                    provider);
  g_signal_connect (storaged_daemon_get_crypttab_monitor (daemon),
                    "entry-added",
                    G_CALLBACK (crypttab_monitor_on_entry_added),
                    provider);
  g_signal_connect (storaged_daemon_get_crypttab_monitor (daemon),
                    "entry-removed",
                    G_CALLBACK (crypttab_monitor_on_entry_removed),
                    provider);
}

static void
storaged_linux_provider_class_init (StoragedLinuxProviderClass *klass)
{
  GObjectClass *gobject_class;
  StoragedProviderClass *provider_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_linux_provider_finalize;

  provider_class        = STORAGED_PROVIDER_CLASS (klass);
  provider_class->start = storaged_linux_provider_start;
}

/**
 * storaged_linux_provider_new:
 * @daemon: A #StoragedDaemon.
 *
 * Create a new provider object for Linux-specific objects / functionality.
 *
 * Returns: A #StoragedLinuxProvider object. Free with g_object_unref().
 */
StoragedLinuxProvider *
storaged_linux_provider_new (StoragedDaemon  *daemon)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  return STORAGED_LINUX_PROVIDER (g_object_new (STORAGED_TYPE_LINUX_PROVIDER,
                                                "daemon", daemon,
                                                NULL));
}

/**
 * storaged_linux_provider_get_udev_client:
 * @provider: A #StoragedLinuxProvider.
 *
 * Gets the #GUdevClient used by @provider.
 *
 * Returns: A #GUdevClient owned by @provider. Do not free.
 */
GUdevClient *
storaged_linux_provider_get_udev_client (StoragedLinuxProvider *provider)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_PROVIDER (provider), NULL);
  return provider->gudev_client;
}


/**
 * storaged_linux_provider_get_coldplug:
 * @provider: A #StoragedLinuxProvider.
 *
 * Gets whether @provider is in the coldplug phase.
 *
 * Returns: %TRUE if in the coldplug phase, %FALSE otherwise.
 **/
gboolean
storaged_linux_provider_get_coldplug (StoragedLinuxProvider *provider)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_PROVIDER (provider), FALSE);
  return provider->coldplug;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
perform_initial_housekeeping_for_drive (GIOSchedulerJob *job,
                                        GCancellable    *cancellable,
                                        gpointer         user_data)
{
  StoragedLinuxDriveObject *object = STORAGED_LINUX_DRIVE_OBJECT (user_data);
  GError *error;

  error = NULL;
  if (!storaged_linux_drive_object_housekeeping (object, 0,
                                                 NULL, /* TODO: cancellable */
                                                 &error))
    {
      storaged_warning ("Error performing initial housekeeping for drive %s: %s (%s, %d)",
                        g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                        error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
    }
  return FALSE; /* job is complete */
}

/* ---------------------------------------------------------------------------------------------------- */

/* called with lock held */

static void
maybe_remove_mdraid_object (StoragedLinuxProvider     *provider,
                            StoragedLinuxMDRaidObject *object)
{
  gchar *object_uuid = NULL;
  StoragedDaemon *daemon = NULL;

  /* remove the object only if there are no devices left */
  if (storaged_linux_mdraid_object_have_devices (object))
    goto out;

  daemon = storaged_provider_get_daemon (STORAGED_PROVIDER (provider));

  object_uuid = g_strdup (storaged_linux_mdraid_object_get_uuid (object));
  g_dbus_object_manager_server_unexport (storaged_daemon_get_object_manager (daemon),
                                         g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
  g_warn_if_fail (g_hash_table_remove (provider->uuid_to_mdraid, object_uuid));

 out:
  g_free (object_uuid);
}

static void
handle_block_uevent_for_mdraid_with_uuid (StoragedLinuxProvider *provider,
                                          const gchar           *action,
                                          StoragedLinuxDevice   *device,
                                          const gchar           *uuid,
                                          gboolean               is_member)
{
  StoragedLinuxMDRaidObject *object;
  StoragedDaemon *daemon;
  const gchar *sysfs_path;

  daemon = storaged_provider_get_daemon (STORAGED_PROVIDER (provider));
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  /* if uuid is NULL or bogus, consider it a remove event */
  if (uuid == NULL || g_strcmp0 (uuid, "00000000:00000000:00000000:00000000") == 0)
    action = "remove";

  if (g_strcmp0 (action, "remove") == 0)
    {
      /* first check if this device was a member */
      object = g_hash_table_lookup (provider->sysfs_path_to_mdraid_members, sysfs_path);
      if (object != NULL)
        {
          storaged_linux_mdraid_object_uevent (object, action, device, TRUE /* is_member */);
          g_warn_if_fail (g_hash_table_remove (provider->sysfs_path_to_mdraid_members, sysfs_path));
          maybe_remove_mdraid_object (provider, object);
        }

      /* then check if the device was the raid device */
      object = g_hash_table_lookup (provider->sysfs_path_to_mdraid, sysfs_path);
      if (object != NULL)
        {
          storaged_linux_mdraid_object_uevent (object, action, device, FALSE /* is_member */);
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
          storaged_linux_mdraid_object_uevent (object, action, device, is_member);
        }
      else
        {
          object = storaged_linux_mdraid_object_new (daemon, uuid);
          storaged_linux_mdraid_object_uevent (object, action, device, is_member);
          g_dbus_object_manager_server_export_uniquely (storaged_daemon_get_object_manager (daemon),
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
handle_block_uevent_for_mdraid (StoragedLinuxProvider *provider,
                                const gchar           *action,
                                StoragedLinuxDevice   *device)
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
  uuid = g_udev_device_get_property (device->udev_device, "STORAGED_MD_UUID");
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
handle_block_uevent_for_drive (StoragedLinuxProvider *provider,
                               const gchar           *action,
                               StoragedLinuxDevice   *device)
{
  StoragedLinuxDriveObject *object;
  StoragedDaemon *daemon;
  const gchar *sysfs_path;
  gchar *vpd;

  vpd = NULL;
  daemon = storaged_provider_get_daemon (STORAGED_PROVIDER (provider));
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  if (g_strcmp0 (action, "remove") == 0)
    {
      object = g_hash_table_lookup (provider->sysfs_path_to_drive, sysfs_path);
      if (object != NULL)
        {
          GList *devices;

          storaged_linux_drive_object_uevent (object, action, device);

          g_warn_if_fail (g_hash_table_remove (provider->sysfs_path_to_drive, sysfs_path));

          devices = storaged_linux_drive_object_get_devices (object);
          if (devices == NULL)
            {
              const gchar *existing_vpd;
              existing_vpd = g_object_get_data (G_OBJECT (object), "x-vpd");
              g_dbus_object_manager_server_unexport (storaged_daemon_get_object_manager (daemon),
                                                     g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
              g_warn_if_fail (g_hash_table_remove (provider->vpd_to_drive, existing_vpd));
            }
          g_list_foreach (devices, (GFunc) g_object_unref, NULL);
          g_list_free (devices);
        }
    }
  else
    {
      if (!storaged_linux_drive_object_should_include_device (provider->gudev_client, device, &vpd))
        goto out;

      if (vpd == NULL)
        {
          storaged_debug ("Ignoring block device %s with no serial or WWN",
                          g_udev_device_get_sysfs_path (device->udev_device));
          goto out;
        }
      object = g_hash_table_lookup (provider->vpd_to_drive, vpd);
      if (object != NULL)
        {
          if (g_hash_table_lookup (provider->sysfs_path_to_drive, sysfs_path) == NULL)
            g_hash_table_insert (provider->sysfs_path_to_drive, g_strdup (sysfs_path), object);
          storaged_linux_drive_object_uevent (object, action, device);
        }
      else
        {
          object = storaged_linux_drive_object_new (daemon, device);
          if (object != NULL)
            {
              g_object_set_data_full (G_OBJECT (object), "x-vpd", g_strdup (vpd), g_free);
              g_dbus_object_manager_server_export_uniquely (storaged_daemon_get_object_manager (daemon),
                                                            G_DBUS_OBJECT_SKELETON (object));
              g_hash_table_insert (provider->vpd_to_drive, g_strdup (vpd), object);
              g_hash_table_insert (provider->sysfs_path_to_drive, g_strdup (sysfs_path), object);

              /* schedule initial housekeeping for the drive unless coldplugging */
              if (!provider->coldplug)
                {
                  g_io_scheduler_push_job (perform_initial_housekeeping_for_drive,
                                           g_object_ref (object),
                                           (GDestroyNotify) g_object_unref,
                                           G_PRIORITY_DEFAULT,
                                           NULL);
                }
            }
        }
    }

 out:
  g_free (vpd);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called with lock held */
static void
handle_block_uevent_for_block (StoragedLinuxProvider *provider,
                               const gchar           *action,
                               StoragedLinuxDevice   *device)
{
  const gchar *sysfs_path;
  StoragedLinuxBlockObject *object;
  StoragedDaemon *daemon;

  daemon = storaged_provider_get_daemon (STORAGED_PROVIDER (provider));
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  if (g_strcmp0 (action, "remove") == 0)
    {
      object = g_hash_table_lookup (provider->sysfs_to_block, sysfs_path);
      if (object != NULL)
        {
          g_dbus_object_manager_server_unexport (storaged_daemon_get_object_manager (daemon),
                                                 g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
          g_warn_if_fail (g_hash_table_remove (provider->sysfs_to_block, sysfs_path));
        }
    }
  else
    {
      object = g_hash_table_lookup (provider->sysfs_to_block, sysfs_path);
      if (object != NULL)
        {
          storaged_linux_block_object_uevent (object, action, device);
        }
      else
        {
          object = storaged_linux_block_object_new (daemon, device);
          g_dbus_object_manager_server_export_uniquely (storaged_daemon_get_object_manager (daemon),
                                                        G_DBUS_OBJECT_SKELETON (object));
          g_hash_table_insert (provider->sysfs_to_block, g_strdup (sysfs_path), object);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/* called with lock held */
static void
handle_block_uevent_for_modules (StoragedLinuxProvider *provider,
                                 const gchar           *action,
                                 StoragedLinuxDevice   *device)
{
  const gchar *sysfs_path;
  GDBusObjectSkeleton *object;
  StoragedDaemon *daemon;
  StoragedModuleManager *module_manager;
  GList *new_funcs, *l, *ll;
  StoragedModuleObjectNewFunc module_object_new_func;
  GHashTable *inst_table;
  GHashTableIter iter;
  gboolean handled;
  GHashTable *inst_sysfs_paths;
  GList *instances_to_remove;
  GList *funcs_to_remove = NULL;

  daemon = storaged_provider_get_daemon (STORAGED_PROVIDER (provider));
  module_manager = storaged_daemon_get_module_manager (daemon);
  if (! storaged_module_manager_get_modules_available (module_manager))
    return;

  new_funcs = storaged_module_manager_get_module_object_new_funcs (module_manager);

  /* The object hierarchy is as follows:
   *
   *   provider->module_funcs_to_instances:
   *      key: a StoragedModuleObjectNewFunc (pointer)
   *      value: nested hashtable
   *          key: a StoragedObjectSkeleton instance implementing the StoragedModuleObject interface
   *          value: nested hashtable
   *              key: sysfs path attached to the StoragedObjectSkeleton instance
   *              value: -- no values, just keys
   */

  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);

  /* The following algorithm brings some guarantees to existing instances:
   *  - every instance can claim one or more devices (sysfs paths)
   *  - existing instances are asked first and only when none is interested in claiming the device
   *    a new instance for the current StoragedModuleObjectNewFunc is attempted to be created
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
              if (storaged_module_object_process_uevent (STORAGED_MODULE_OBJECT (object), action, device))
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
                  g_dbus_object_manager_server_unexport (storaged_daemon_get_object_manager (daemon),
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

      /* no instance claimed or no instance was interested in this sysfs path, try creating new instance for the current StoragedModuleObjectNewFunc */
      if (! handled)
        {
          object = module_object_new_func (daemon, device);
          if (object != NULL)
            {
              g_dbus_object_manager_server_export_uniquely (storaged_daemon_get_object_manager (daemon),
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
handle_block_uevent (StoragedLinuxProvider *provider,
                     const gchar           *action,
                     StoragedLinuxDevice   *device)
{
  /* We use the sysfs block device for all of
   *
   *  - StoragedLinuxDriveObject
   *  - StoragedLinuxMDRaidObject
   *  - StoragedLinuxBlockObject
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
      storaged_state_check (storaged_daemon_get_state (storaged_provider_get_daemon (STORAGED_PROVIDER (provider))));
    }
}

/* called without lock held */
static void
storaged_linux_provider_handle_uevent (StoragedLinuxProvider *provider,
                                       const gchar           *action,
                                       StoragedLinuxDevice   *device)
{
  const gchar *subsystem;

  G_LOCK (provider_lock);

  storaged_debug ("uevent %s %s",
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
housekeeping_all_drives (StoragedLinuxProvider *provider,
                         guint                  secs_since_last)
{
  GList *objects;
  GList *l;

  G_LOCK (provider_lock);
  objects = g_hash_table_get_values (provider->vpd_to_drive);
  g_list_foreach (objects, (GFunc) g_object_ref, NULL);
  G_UNLOCK (provider_lock);

  for (l = objects; l != NULL; l = l->next)
    {
      StoragedLinuxDriveObject *object = STORAGED_LINUX_DRIVE_OBJECT (l->data);
      GError *error;

      error = NULL;
      if (!storaged_linux_drive_object_housekeeping (object,
                                                     secs_since_last,
                                                     NULL, /* TODO: cancellable */
                                                     &error))
        {
          storaged_warning ("Error performing housekeeping for drive %s: %s (%s, %d)",
                            g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                            error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
        }
    }

  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
}

/* Runs in housekeeping thread - called without lock held */
static void
housekeeping_all_modules (StoragedLinuxProvider *provider,
                          guint                  secs_since_last)
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
      StoragedModuleObject *object = STORAGED_MODULE_OBJECT (l->data);
      GError *error;

      error = NULL;
      if (! storaged_module_object_housekeeping (object,
                                                 secs_since_last,
                                                 NULL, /* TODO: cancellable */
                                                 &error))
        {
          storaged_warning ("Error performing housekeeping for module object %s: %s (%s, %d)",
                            g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                            error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
        }
    }

  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
housekeeping_thread_func (GIOSchedulerJob *job,
                          GCancellable    *cancellable,
                          gpointer         user_data)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (user_data);
  guint secs_since_last;
  guint64 now;

  /* TODO: probably want some kind of timeout here to avoid faulty devices/drives blocking forever */

  secs_since_last = 0;
  now = time (NULL);
  if (provider->housekeeping_last > 0)
    secs_since_last = now - provider->housekeeping_last;
  provider->housekeeping_last = now;

  storaged_info ("Housekeeping initiated (%d seconds since last housekeeping)", secs_since_last);

  housekeeping_all_drives (provider, secs_since_last);
  housekeeping_all_modules (provider, secs_since_last);

  storaged_info ("Housekeeping complete");
  G_LOCK (provider_lock);
  provider->housekeeping_running = FALSE;
  G_UNLOCK (provider_lock);

  return FALSE; /* job is complete */
}

/* called from the main thread on start-up and every 10 minutes or so */
static gboolean
on_housekeeping_timeout (gpointer user_data)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (user_data);

  G_LOCK (provider_lock);
  if (provider->housekeeping_running)
    goto out;
  provider->housekeeping_running = TRUE;
  g_io_scheduler_push_job (housekeeping_thread_func,
                           g_object_ref (provider),
                           (GDestroyNotify) g_object_unref,
                           G_PRIORITY_DEFAULT,
                           NULL);
 out:
  G_UNLOCK (provider_lock);

  return TRUE; /* keep timeout around */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_all_block_objects (StoragedLinuxProvider *provider)
{
  GList *objects;
  GList *l;

  G_LOCK (provider_lock);
  objects = g_hash_table_get_values (provider->sysfs_to_block);
  g_list_foreach (objects, (GFunc) g_object_ref, NULL);
  G_UNLOCK (provider_lock);

  for (l = objects; l != NULL; l = l->next)
    {
      StoragedLinuxBlockObject *object = STORAGED_LINUX_BLOCK_OBJECT (l->data);
      storaged_linux_block_object_uevent (object, "change", NULL);
    }

  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
}

static void
fstab_monitor_on_entry_added (StoragedFstabMonitor *monitor,
                              StoragedFstabEntry   *entry,
                              gpointer              user_data)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}

static void
fstab_monitor_on_entry_removed (StoragedFstabMonitor *monitor,
                                StoragedFstabEntry   *entry,
                                gpointer              user_data)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}

static void
crypttab_monitor_on_entry_added (StoragedCrypttabMonitor *monitor,
                                 StoragedCrypttabEntry   *entry,
                                 gpointer                 user_data)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}

static void
crypttab_monitor_on_entry_removed (StoragedCrypttabMonitor *monitor,
                                   StoragedCrypttabEntry   *entry,
                                   gpointer                 user_data)
{
  StoragedLinuxProvider *provider = STORAGED_LINUX_PROVIDER (user_data);
  update_all_block_objects (provider);
}
