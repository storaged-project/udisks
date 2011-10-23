/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 David Zeuthen <zeuthen@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "udisksclient.h"
#include "udiskserror.h"
#include "udisks-generated.h"

/**
 * SECTION:udisksclient
 * @title: UDisksClient
 * @short_description: UDisks Client
 *
 * #UDisksClient is used for accessing the UDisks service from a
 * client program.
 */

G_LOCK_DEFINE_STATIC (init_lock);

/**
 * UDisksClient:
 *
 * The #UDisksClient structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksClient
{
  GObject parent_instance;

  gboolean is_initialized;
  GError *initialization_error;

  GDBusObjectManager *object_manager;

  GMainContext *context;

  GSource *changed_timeout_source;
};

typedef struct
{
  GObjectClass parent_class;
} UDisksClientClass;

enum
{
  PROP_0,
  PROP_OBJECT_MANAGER,
  PROP_MANAGER
};

enum
{
  CHANGED_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void initable_iface_init       (GInitableIface      *initable_iface);
static void async_initable_iface_init (GAsyncInitableIface *async_initable_iface);

static void on_object_added (GDBusObjectManager  *manager,
                             GDBusObject         *object,
                             gpointer             user_data);

static void on_object_removed (GDBusObjectManager  *manager,
                               GDBusObject         *object,
                               gpointer             user_data);

static void on_interface_added (GDBusObjectManager  *manager,
                                GDBusObject         *object,
                                GDBusInterface      *interface,
                                gpointer             user_data);

static void on_interface_removed (GDBusObjectManager  *manager,
                                  GDBusObject         *object,
                                  GDBusInterface      *interface,
                                  gpointer             user_data);

static void on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                                   GDBusObjectProxy           *object_proxy,
                                                   GDBusProxy                 *interface_proxy,
                                                   GVariant                   *changed_properties,
                                                   const gchar *const         *invalidated_properties,
                                                   gpointer                    user_data);

static void init_interface_proxy (UDisksClient *client,
                                  GDBusProxy   *proxy);

G_DEFINE_TYPE_WITH_CODE (UDisksClient, udisks_client, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init)
                         );

static void
udisks_client_finalize (GObject *object)
{
  UDisksClient *client = UDISKS_CLIENT (object);

  if (client->changed_timeout_source != NULL)
    g_source_destroy (client->changed_timeout_source);

  if (client->initialization_error != NULL)
    g_error_free (client->initialization_error);

  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_object_added),
                                        client);
  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_object_removed),
                                        client);
  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_interface_added),
                                        client);
  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_interface_removed),
                                        client);
  g_signal_handlers_disconnect_by_func (client->object_manager,
                                        G_CALLBACK (on_interface_proxy_properties_changed),
                                        client);
  g_object_unref (client->object_manager);

  if (client->context != NULL)
    g_main_context_unref (client->context);

  G_OBJECT_CLASS (udisks_client_parent_class)->finalize (object);
}

static void
udisks_client_init (UDisksClient *client)
{
  static volatile GQuark udisks_error_domain = 0;
  /* this will force associating errors in the UDISKS_ERROR error domain
   * with org.freedesktop.UDisks.Error.* errors via g_dbus_error_register_error_domain().
   */
  udisks_error_domain = UDISKS_ERROR;
  udisks_error_domain; /* shut up -Wunused-but-set-variable */
}

static void
udisks_client_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  UDisksClient *client = UDISKS_CLIENT (object);

  switch (prop_id)
    {
    case PROP_OBJECT_MANAGER:
      g_value_set_object (value, udisks_client_get_object_manager (client));
      break;

    case PROP_MANAGER:
      g_value_set_object (value, udisks_client_get_manager (client));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_client_class_init (UDisksClientClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_client_finalize;
  gobject_class->get_property = udisks_client_get_property;

  /**
   * UDisksClient:object-manager:
   *
   * The #GDBusObjectManager used by the #UDisksClient instance.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_MANAGER,
                                   g_param_spec_object ("object-manager",
                                                        "Object Manager",
                                                        "The GDBusObjectManager used by the UDisksClient",
                                                        G_TYPE_DBUS_OBJECT_MANAGER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksClient:manager:
   *
   * The #UDisksManager interface on the well-known
   * <literal>/org/freedesktop/UDisks2/Manager</literal> object
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MANAGER,
                                   g_param_spec_object ("manager",
                                                        "Manager",
                                                        "The UDisksManager",
                                                        UDISKS_TYPE_MANAGER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksClient::changed:
   * @client: A #UDisksClient.
   *
   * This signal is emitted either when an object or interface is
   * added or removed a when property has changed. Additionally,
   * multiple received signals are coalesced into a single signal that
   * is rate-limited to fire at most every 100ms.
   *
   * For greater detail, connect to the
   * #GDBusObjectManager::object-added,
   * #GDBusObjectManager::object-removed,
   * #GDBusObjectManager::interface-added,
   * #GDBusObjectManager::interface-removed,
   * #GDBusObjectManagerClient::interface-proxy-properties-changed and
   * signals on the #UDisksClient:object-manager object.
   */
  signals[CHANGED_SIGNAL] = g_signal_new ("changed",
                                          G_OBJECT_CLASS_TYPE (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0, /* G_STRUCT_OFFSET */
                                          NULL, /* accu */
                                          NULL, /* accu data */
                                          g_cclosure_marshal_generic,
                                          G_TYPE_NONE,
                                          0);

}

/**
 * udisks_client_new:
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Function that will be called when the result is ready.
 * @user_data: Data to pass to @callback.
 *
 * Asynchronously gets a #UDisksClient. When the operation is
 * finished, @callback will be invoked in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this method from.
 */
void
udisks_client_new (GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  g_async_initable_new_async (UDISKS_TYPE_CLIENT,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              callback,
                              user_data,
                              NULL);
}

/**
 * udisks_client_new_finish:
 * @res: A #GAsyncResult.
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with udisks_client_new().
 *
 * Returns: A #UDisksClient or %NULL if @error is set. Free with
 * g_object_unref() when done with it.
 */
UDisksClient *
udisks_client_new_finish (GAsyncResult        *res,
                          GError             **error)
{
  GObject *ret;
  GObject *source_object;
  source_object = g_async_result_get_source_object (res);
  ret = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, error);
  g_object_unref (source_object);
  if (ret != NULL)
    return UDISKS_CLIENT (ret);
  else
    return NULL;
}

/**
 * udisks_client_new_sync:
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: (allow-none): Return location for error or %NULL.
 *
 * Synchronously gets a #UDisksClient for the local system.
 *
 * Returns: A #UDisksClient or %NULL if @error is set. Free with
 * g_object_unref() when done with it.
 */
UDisksClient *
udisks_client_new_sync (GCancellable  *cancellable,
                        GError       **error)
{
  GInitable *ret;
  ret = g_initable_new (UDISKS_TYPE_CLIENT,
                        cancellable,
                        error,
                        NULL);
  if (ret != NULL)
    return UDISKS_CLIENT (ret);
  else
    return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  UDisksClient *client = UDISKS_CLIENT (initable);
  gboolean ret;
  GList *objects, *l;
  GList *interfaces, *ll;

  ret = FALSE;

  /* This method needs to be idempotent to work with the singleton
   * pattern. See the docs for g_initable_init(). We implement this by
   * locking.
   */
  G_LOCK (init_lock);
  if (client->is_initialized)
    {
      if (client->object_manager != NULL)
        ret = TRUE;
      else
        g_assert (client->initialization_error != NULL);
      goto out;
    }
  g_assert (client->initialization_error == NULL);

  client->context = g_main_context_get_thread_default ();
  if (client->context != NULL)
    g_main_context_ref (client->context);

  client->object_manager = udisks_object_manager_client_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                          G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                                          "org.freedesktop.UDisks2",
                                                                          "/org/freedesktop/UDisks2",
                                                                          cancellable,
                                                                          &client->initialization_error);
  if (client->object_manager == NULL)
    goto out;

  /* init all proxies */
  objects = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      interfaces = g_dbus_object_get_interfaces (G_DBUS_OBJECT (l->data));
      for (ll = interfaces; ll != NULL; ll = ll->next)
        {
          init_interface_proxy (client, G_DBUS_PROXY (ll->data));
        }
      g_list_foreach (interfaces, (GFunc) g_object_unref, NULL);
      g_list_free (interfaces);
    }
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);

  g_signal_connect (client->object_manager,
                    "object-added",
                    G_CALLBACK (on_object_added),
                    client);
  g_signal_connect (client->object_manager,
                    "object-removed",
                    G_CALLBACK (on_object_removed),
                    client);
  g_signal_connect (client->object_manager,
                    "interface-added",
                    G_CALLBACK (on_interface_added),
                    client);
  g_signal_connect (client->object_manager,
                    "interface-removed",
                    G_CALLBACK (on_interface_removed),
                    client);
  g_signal_connect (client->object_manager,
                    "interface-proxy-properties-changed",
                    G_CALLBACK (on_interface_proxy_properties_changed),
                    client);

  ret = TRUE;

out:
  client->is_initialized = TRUE;
  if (!ret)
    {
      g_assert (client->initialization_error != NULL);
      g_propagate_error (error, g_error_copy (client->initialization_error));
    }
  G_UNLOCK (init_lock);
  return ret;
}

static void
initable_iface_init (GInitableIface      *initable_iface)
{
  initable_iface->init = initable_init;
}

static void
async_initable_iface_init (GAsyncInitableIface *async_initable_iface)
{
  /* Use default implementation (e.g. run GInitable code in a thread) */
}

/**
 * udisks_client_get_object_manager:
 * @client: A #UDisksClient.
 *
 * Gets the #GDBusObjectManager used by @client.
 *
 * Returns: (transfer none): A #GDBusObjectManager. Do not free, the
 * instance is owned by @client.
 */
GDBusObjectManager *
udisks_client_get_object_manager (UDisksClient        *client)
{
  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);
  return client->object_manager;
}

/**
 * udisks_client_get_manager:
 * @client: A #UDisksClient.
 *
 * Gets the #UDisksManager interface on the well-known
 * <literal>/org/freedesktop/UDisks2/Manager</literal> object.
 *
 * Returns: (transfer none): A #UDisksManager or %NULL if the udisksd
 * daemon is not currently running. Do not free, the instance is owned
 * by @client.
 */
UDisksManager *
udisks_client_get_manager (UDisksClient *client)
{
  UDisksManager *ret = NULL;
  GDBusObject *obj;

  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);

  obj = g_dbus_object_manager_get_object (client->object_manager, "/org/freedesktop/UDisks2/Manager");
  if (obj == NULL)
    goto out;

  ret = udisks_object_peek_manager (UDISKS_OBJECT (obj));
  g_object_unref (obj);

 out:
  return ret;
}

/**
 * udisks_client_settle:
 * @client: A #UDisksClient.
 *
 * Blocks until all pending D-Bus messages have been delivered.
 *
 * This is useful when using synchronous method calls since e.g. D-Bus
 * signals received while waiting for the reply are queued up and
 * dispatched after the synchronous call ends.
 */
void
udisks_client_settle (UDisksClient *client)
{
  while (g_main_context_iteration (client->context, FALSE /* may_block */))
    ;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_client_get_object:
 * @client: A #UDisksClient.
 * @object_path: Object path.
 *
 * Convenience function for looking up an #UDisksObject for @object_path.
 *
 * Returns: (transfer full): A #UDisksObject corresponding to
 * @object_path or %NULL if not found. The returned object must be
 * freed with g_object_unref().
 */
UDisksObject *
udisks_client_get_object (UDisksClient  *client,
                          const gchar   *object_path)
{
  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);
  return (UDisksObject *) g_dbus_object_manager_get_object (client->object_manager, object_path);
}

/**
 * udisks_client_peek_object:
 * @client: A #UDisksClient.
 * @object_path: Object path.
 *
 * Like udisks_client_get_object() but doesn't increase the reference
 * count on the returned #UDisksObject.
 *
 * <warning>The returned object is only valid until removed so it is only safe to use this function on the thread where @client was constructed. Use udisks_client_get_object() if on another thread.</warning>
 *
 * Returns: (transfer none): A #UDisksObject corresponding to
 * @object_path or %NULL if not found.
 */
UDisksObject *
udisks_client_peek_object (UDisksClient  *client,
                           const gchar   *object_path)
{
  UDisksObject *ret;
  ret = udisks_client_get_object (client, object_path);
  if (ret != NULL)
    g_object_unref (ret);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_client_get_block_for_dev:
 * @client: A #UDisksClient.
 * @block_device_number: A #dev_t to get a #UDisksBlock for.
 *
 * Gets the #UDisksBlock corresponding to @block_device_number, if any.
 *
 * Returns: (transfer full): A #UDisksBlock or %NULL if not found.
 */
UDisksBlock *
udisks_client_get_block_for_dev (UDisksClient *client,
                                 dev_t         block_device_number)
{
  UDisksBlock *ret = NULL;
  GList *l, *object_proxies = NULL;

  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_get_block (object);
      if (block == NULL)
        continue;

      if (makedev (udisks_block_get_major (block), udisks_block_get_minor (block)) == block_device_number)
        {
          ret = block;
          goto out;
        }
      g_object_unref (block);
    }

 out:
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static GList *
get_top_level_blocks_for_drive (UDisksClient *client,
                                const gchar  *drive_object_path)
{
  GList *ret;
  GList *l;
  GList *object_proxies;
  GDBusObjectManager *object_manager;

  object_manager = udisks_client_get_object_manager (client);
  object_proxies = g_dbus_object_manager_get_objects (object_manager);

  ret = NULL;
  for (l = object_proxies; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;
      UDisksPartition *partition;

      block = udisks_object_get_block (object);
      partition = udisks_object_peek_partition (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_drive (block), drive_object_path) == 0 &&
          partition == NULL)
        {
          ret = g_list_append (ret, g_object_ref (object));
        }
      g_object_unref (block);
    }
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

/**
 * udisks_client_get_block_for_drive:
 * @client: A #UDisksClient.
 * @drive: A #UDisksDrive.
 * @get_physical: %TRUE to get a physical device, %FALSE to get the logical device.
 *
 * Gets a block device corresponding to @drive. The returned block
 * device, if any, is for the whole disk drive, e.g. a partition block
 * device is never returned.
 *
 * Set @get_physical to %TRUE if you need a block device that you can
 * send low-level SCSI commands with (for multipath, this returns one
 * of the physical paths). Set it to %FALSE if you need a block device
 * that you can read/write data with (for multipath, this returns the
 * mapped device).
 *
 * Returns: (transfer full): A #UDisksBlock or %NULL if the requested
 * kind of block device is not available - use g_object_unref() to
 * free the returned object.
 */
UDisksBlock *
udisks_client_get_block_for_drive (UDisksClient        *client,
                                   UDisksDrive         *drive,
                                   gboolean             get_physical)
{
  UDisksBlock *ret = NULL;
  GDBusObject *object;
  GList *blocks = NULL;
  GList *l;

  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);
  g_return_val_if_fail (UDISKS_IS_DRIVE (drive), NULL);

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (drive));
  if (object == NULL)
    goto out;

  blocks = get_top_level_blocks_for_drive (client, g_dbus_object_get_object_path (object));
  for (l = blocks; l != NULL; l = l->next)
    {
      UDisksBlock *block = udisks_object_peek_block (UDISKS_OBJECT (l->data));
      if (block != NULL)
        {
          /* TODO: actually look at @get_physical */
          ret = g_object_ref (block);
          goto out;
        }
    }

 out:
  g_list_foreach (blocks, (GFunc) g_object_unref, NULL);
  g_list_free (blocks);
  return ret;
}

/**
 * udisks_client_get_drive_for_block:
 * @client: A #UDisksClient.
 * @block: A #UDisksBlock.
 *
 * Gets the #UDisksDrive that @block belongs to, if any.
 *
 * Returns: (transfer full): A #UDisksDrive or %NULL if there is no
 * #UDisksDrive for @block - free the returned object with
 * g_object_unref().
 */
UDisksDrive *
udisks_client_get_drive_for_block (UDisksClient  *client,
                                   UDisksBlock   *block)
{
  UDisksDrive *ret = NULL;
  GDBusObject *object;

  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);
  g_return_val_if_fail (UDISKS_IS_BLOCK (block), NULL);

  object = g_dbus_object_manager_get_object (client->object_manager, udisks_block_get_drive (block));
  if (object != NULL)
    ret = udisks_object_get_drive (UDISKS_OBJECT (object));
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef enum
{
  DRIVE_TYPE_UNSET,
  DRIVE_TYPE_DISK,
  DRIVE_TYPE_CARD,
  DRIVE_TYPE_DISC
} DriveType;

static const struct
{
  const gchar *id;
  const gchar *media_name;
  const gchar *media_family;
  const gchar *media_icon;
  DriveType    media_type;
  const gchar *drive_icon;
} media_data[] =
{
  {"floppy",     N_("Floppy"),       N_("Floppy"), "media-floppy",      DRIVE_TYPE_DISK, "drive-removable-media-floppy"},
  {"floppy_zip", N_("Zip"),          N_("Zip"),    "media-floppy-jaz",  DRIVE_TYPE_DISK, "drive-removable-media-floppy-jaz"},
  {"floppy_jaz", N_("Jaz"),          N_("Jaz"),    "media-floppy-zip",  DRIVE_TYPE_DISK, "drive-removable-media-floppy-zip"},

  {"flash",      N_("Flash"),        N_("Flash"),        "media-flash",       DRIVE_TYPE_CARD, "drive-removable-media-flash"},
  {"flash_ms",   N_("MemoryStick"),  N_("MemoryStick"),  "media-flash-ms",    DRIVE_TYPE_CARD, "drive-removable-media-flash-ms"},
  {"flash_sm",   N_("SmartMedia"),   N_("SmartMedia"),   "media-flash-sm",    DRIVE_TYPE_CARD, "drive-removable-media-flash-sm"},
  {"flash_cf",   N_("CompactFlash"), N_("CompactFlash"), "media-flash-cf",    DRIVE_TYPE_CARD, "drive-removable-media-flash-cf"},
  {"flash_mmc",  N_("MMC"),          N_("SD"),           "media-flash-mmc",   DRIVE_TYPE_CARD, "drive-removable-media-flash-mmc"},
  {"flash_sd",   N_("SD"),           N_("SD"),           "media-flash-sd",    DRIVE_TYPE_CARD, "drive-removable-media-flash-sd"},
  {"flash_sdxc", N_("SDXC"),         N_("SD"),           "media-flash-sd-xc", DRIVE_TYPE_CARD, "drive-removable-media-flash-sd-xc"},
  {"flash_sdhc", N_("SDHC"),         N_("SD"),           "media-flash-sd-hc", DRIVE_TYPE_CARD, "drive-removable-media-flash-sd-hc"},

  {"optical_cd",             N_("CD-ROM"),    N_("CD"),      "media-optical-cd-rom",        DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_cd_r",           N_("CD-R"),      N_("CD"),      "media-optical-cd-r",          DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_cd_rw",          N_("CD-RW"),     N_("CD"),      "media-optical-cd-rw",         DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd",            N_("DVD"),       N_("DVD"),     "media-optical-dvd-rom",       DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_dvd_r",          N_("DVD-R"),     N_("DVD"),     "media-optical-dvd-r",         DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_rw",         N_("DVD-RW"),    N_("DVD"),     "media-optical-dvd-rw",        DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_ram",        N_("DVD-RAM"),   N_("DVD"),     "media-optical-dvd-ram",       DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_plus_r",     N_("DVD+R"),     N_("DVD"),     "media-optical-dvd-r-plus",    DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_plus_rw",    N_("DVD+RW"),    N_("DVD"),     "media-optical-dvd-rw-plus",   DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_plus_r_dl",  N_("DVD+R DL"),  N_("DVD"),     "media-optical-dvd-dl-r-plus", DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_dvd_plus_rw_dl", N_("DVD+RW DL"), N_("DVD"),     "media-optical-dvd-dl-r-plus", DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_bd",             N_("BD-ROM"),    N_("Blu-Ray"), "media-optical-bd-rom",        DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_bd_r",           N_("BD-R"),      N_("Blu-Ray"), "media-optical-bd-r",          DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_bd_re",          N_("BD-RE"),     N_("Blu-Ray"), "media-optical-bd-re",         DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_hddvd",          N_("HDDVD"),     N_("HDDVD"),   "media-optical-hddvd-rom",     DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_hddvd_r",        N_("HDDVD-R"),   N_("HDDVD"),   "media-optical-hddvd-r",       DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_hddvd_rw",       N_("HDDVD-RW"),  N_("HDDVD"),   "media-optical-hddvd-rw",      DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_mo",             N_("MO"),        N_("CD"),      "media-optical-mo",            DRIVE_TYPE_DISC, "drive-optical"},
  {"optical_mrw",            N_("MRW"),       N_("CD"),      "media-optical-mrw",           DRIVE_TYPE_DISC, "drive-optical-recorder"},
  {"optical_mrw_w",          N_("MRW-W"),     N_("CD"),      "media-optical-mrw-w",         DRIVE_TYPE_DISC, "drive-optical-recorder"},
};

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
strv_has (const gchar * const *haystack,
          const gchar          *needle)
{
  gboolean ret;
  guint n;

  ret = FALSE;

  for (n = 0; haystack != NULL && haystack[n] != NULL; n++)
    {
      if (g_strcmp0 (haystack[n], needle) == 0)
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  return ret;
}

/**
 * udisks_client_get_drive_info:
 * @client: A #UDisksClient.
 * @drive: A #UDisksDrive.
 * @out_name: (out) (allow-none): Return location for name or %NULL.
 * @out_description: (out) (allow-none): Return location for description or %NULL.
 * @out_drive_icon: (out) (allow-none): Return location for icon representing the drive or %NULL.
 * @out_media_description: (out) (allow-none): Return location for description of the media or %NULL.
 * @out_media_icon: (out) (allow-none): Return location for icon representing the media or %NULL.
 *
 * Gets information about a #UDisksDrive object that is suitable to
 * present in an user interface. The returned strings are localized.
 *
 * If there is no media in @drive, then @out_media_icon is set to the
 * same value as @out_drive_icon.
 *
 * If the @drive doesn't support removable media, then %NULL is always
 * returned for @out_media_description and @out_media_icon.
 *
 * If the <link linkend="gdbus-property-org-freedesktop-UDisks2-Block.HintName">HintName</link>
 * and/or
 * <link linkend="gdbus-property-org-freedesktop-UDisks2-Block.HintName">HintIconName</link>
 * properties on the block device for @drive are set (see <xref linkend="udisks.7"/>),
 * their values are returned in the drive and media
 * description and icon (e.g. @out_description, @out_drive_icon, @out_media_description and @out_media_icon).
 *
 * The returned data is best described by example:
 * <informaltable>
 *   <tgroup cols="6">
 *     <thead>
 *       <row>
 *         <entry>Device / Media</entry>
 *         <entry>name</entry>
 *         <entry>description</entry>
 *         <entry>icon</entry>
 *         <entry>media_description</entry>
 *         <entry>media_icon</entry>
 *       </row>
 *     </thead>
 *     <tbody>
 *       <row>
 *         <entry>Internal System Disk (Hard Disk)</entry>
 *         <entry>ST3320620AS</entry>
 *         <entry>320 GB Hard Disk</entry>
 *         <entry>drive-harddisk</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>Internal System Disk (Solid State)</entry>
 *         <entry>INTEL SSDSA2MH080G1GC</entry>
 *         <entry>80 GB Disk</entry>
 *         <entry>drive-harddisk</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (empty)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with CD-ROM data disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>CD-ROM Disc</entry>
 *         <entry>media-optical-cd-rom</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with mixed disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>Audio/Data CD-ROM Disc</entry>
 *         <entry>media-optical-cd-rom</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with audio disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>Audio Disc</entry>
 *         <entry>media-optical-cd-audio</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with DVD-ROM disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>DVD-ROM Disc</entry>
 *         <entry>media-optical-dvd-rom</entry>
 *       </row>
 *       <row>
 *         <entry>Optical Drive (with blank DVD-R disc)</entry>
 *         <entry>LITE-ON DVDRW SOHW-812S</entry>
 *         <entry>CD/DVD Drive</entry>
 *         <entry>drive-optical</entry>
 *         <entry>Blank DVD-R Disc</entry>
 *         <entry>media-optical-dvd-r</entry>
 *       </row>
 *       <row>
 *         <entry>External USB Hard Disk</entry>
 *         <entry>WD 2500JB External</entry>
 *         <entry>250 GB Hard Disk</entry>
 *         <entry>drive-harddisk-usb</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>USB Compact Flash Reader (without media)</entry>
 *         <entry>BELKIN USB 2 HS-CF</entry>
 *         <entry>Compact Flash Drive</entry>
 *         <entry>drive-removable-media-flash-cf</entry>
 *         <entry>NULL</entry>
 *         <entry>NULL</entry>
 *       </row>
 *       <row>
 *         <entry>USB Compact Flash Reader (with media)</entry>
 *         <entry>BELKIN USB 2 HS-CF</entry>
 *         <entry>Compact Flash Drive</entry>
 *         <entry>drive-removable-media-flash-cf</entry>
 *         <entry>Compact Flash media</entry>
 *         <entry>media-flash-cf</entry>
 *       </row>
 *     </tbody>
 *   </tgroup>
 * </informaltable>
 */
void
udisks_client_get_drive_info (UDisksClient  *client,
                              UDisksDrive   *drive,
                              gchar        **out_name,
                              gchar        **out_description,
                              GIcon        **out_icon,
                              gchar        **out_media_description,
                              GIcon        **out_media_icon)
{
  gchar *name;
  gchar *description;
  GIcon *icon;
  gchar *media_description;
  GIcon *media_icon;
  const gchar *vendor;
  const gchar *model;
  const gchar *media;
  const gchar *const *media_compat;
  gboolean removable;
  gboolean rotation_rate;
  guint64 size;
  gchar *size_str;
  guint n;
  GString *desc_str;
  DriveType desc_type;
  gchar *hyphenated_connection_bus;
  const gchar *connection_bus;
  UDisksBlock *block = NULL;

  g_return_if_fail (UDISKS_IS_DRIVE (drive));

  name = NULL;
  description = NULL;
  icon = NULL;
  media_description = NULL;
  media_icon = NULL;
  size_str = NULL;

  vendor = udisks_drive_get_vendor (drive);
  model = udisks_drive_get_model (drive);
  size = udisks_drive_get_size (drive);
  removable = udisks_drive_get_media_removable (drive);
  rotation_rate = udisks_drive_get_rotation_rate (drive);
  if (size > 0)
    size_str = udisks_client_get_size_for_display (client, size, FALSE, FALSE);
  media = udisks_drive_get_media (drive);
  media_compat = udisks_drive_get_media_compatibility (drive);
  connection_bus = udisks_drive_get_connection_bus (drive);
  if (strlen (connection_bus) > 0)
    hyphenated_connection_bus = g_strdup_printf ("-%s", connection_bus);
  else
    hyphenated_connection_bus = g_strdup ("");

  /* Name is easy - that's just "$vendor $model" */
  if (strlen (vendor) == 0)
    vendor = NULL;
  if (strlen (model) == 0)
    model = NULL;
  name = g_strdup_printf ("%s%s%s",
                          vendor != NULL ? vendor : "",
                          vendor != NULL ? " " : "",
                          model != NULL ? model : "");

  desc_type = DRIVE_TYPE_UNSET;
  desc_str = g_string_new (NULL);
  for (n = 0; n < G_N_ELEMENTS (media_data) - 1; n++)
    {
      /* media_compat */
      if (strv_has (media_compat, media_data[n].id))
        {
          if (icon == NULL)
            icon = g_themed_icon_new_with_default_fallbacks (media_data[n].drive_icon);
          if (strstr (desc_str->str, media_data[n].media_family) == NULL)
            {
              if (desc_str->len > 0)
                g_string_append (desc_str, "/");
              g_string_append (desc_str, _(media_data[n].media_family));
            }
          desc_type = media_data[n].media_type;
        }

      /* media */
      if (g_strcmp0 (media, media_data[n].id) == 0)
        {
          if (media_description == NULL)
            {
              switch (media_data[n].media_type)
                {
                case DRIVE_TYPE_UNSET:
                  g_assert_not_reached ();
                  break;
                case DRIVE_TYPE_DISK:
                  media_description = g_strdup_printf (_("%s Disk"), _(media_data[n].media_name));
                  break;
                case DRIVE_TYPE_CARD:
                  media_description = g_strdup_printf (_("%s Card"), _(media_data[n].media_name));
                  break;
                case DRIVE_TYPE_DISC:
                  media_description = g_strdup_printf (_("%s Disc"), _(media_data[n].media_name));
                  break;
                }
            }
          if (media_icon == NULL)
            media_icon = g_themed_icon_new_with_default_fallbacks (media_data[n].media_icon);
        }
    }

  switch (desc_type)
    {
    case DRIVE_TYPE_UNSET:
      if (removable)
        {
          if (size_str != NULL)
            {
              description = g_strdup_printf (_("%s Drive"), size_str);
            }
          else
            {
              description = g_strdup (_("Drive"));
            }
        }
      else
        {
          if (rotation_rate == 0)
            {
              if (size_str != NULL)
                {
                  description = g_strdup_printf (_("%s Disk"), size_str);
                }
              else
                {
                  description = g_strdup (_("Disk"));
                }
            }
          else
            {
              if (size_str != NULL)
                {
                  description = g_strdup_printf (_("%s Hard Disk"), size_str);
                }
              else
                {
                  description = g_strdup (_("Hard Disk"));
                }
            }
        }
      break;

    case DRIVE_TYPE_CARD:
      description = g_strdup_printf (_("%s Card Reader"), desc_str->str);
      break;

    case DRIVE_TYPE_DISK:
      /* explicit fall-through */
    case DRIVE_TYPE_DISC:
      description = g_strdup_printf (_("%s Drive"), desc_str->str);
      break;
    }
  g_string_free (desc_str, TRUE);

  if (media_icon == NULL)
    {
      gchar *s;
      if (removable)
        s = g_strdup_printf ("drive-removable-media%s", hyphenated_connection_bus);
      else
        s = g_strdup_printf ("drive-harddisk%s", hyphenated_connection_bus);
      media_icon = g_themed_icon_new_with_default_fallbacks (s);
      g_free (s);
    }
  if (icon == NULL)
    {
      gchar *s;
      if (removable)
        s = g_strdup_printf ("drive-removable-media%s", hyphenated_connection_bus);
      else
        s = g_strdup_printf ("drive-harddisk%s", hyphenated_connection_bus);
      icon = g_themed_icon_new_with_default_fallbacks (s);
      g_free (s);
    }

  /* prepend a qualifier to the media description, based on the disc state */
  if (udisks_drive_get_optical_blank (drive))
    {
      gchar *s;
      /* Translators: String used for a blank disc. The %s is the disc type e.g. "CD-RW Disc" */
      s = g_strdup_printf (_("Blank %s"), media_description);
      g_free (media_description);
      media_description = s;
    }
  else if (udisks_drive_get_optical_num_audio_tracks (drive) > 0 &&
           udisks_drive_get_optical_num_data_tracks (drive) > 0)
    {
      gchar *s;
      /* Translators: String used for a mixed disc. The %s is the disc type e.g. "CD-ROM Disc" */
      s = g_strdup_printf (_("Mixed %s"), media_description);
      g_free (media_description);
      media_description = s;
    }
  else if (udisks_drive_get_optical_num_audio_tracks (drive) > 0 &&
           udisks_drive_get_optical_num_data_tracks (drive) == 0)
    {
      gchar *s;
      /* Translators: String used for an audio disc. The %s is the disc type e.g. "CD-ROM Disc" */
      s = g_strdup_printf (_("Audio %s"), media_description);
      g_free (media_description);
      media_description = s;
    }

  /* Apply UDISKS_NAME and UDISKS_ICON_NAME hints, if available */
  block = udisks_client_get_block_for_drive (client, drive, TRUE);
  if (block != NULL)
    {
      const gchar *s;

      s = udisks_block_get_hint_name (block);
      if (s != NULL && strlen (s) > 0)
        {
          g_free (description);
          g_free (media_description);
          description = g_strdup (s);
          media_description = g_strdup (s);
        }

      s = udisks_block_get_hint_icon_name (block);
      if (s != NULL && strlen (s) > 0)
        {
          g_clear_object (&icon);
          g_clear_object (&media_icon);
          icon = g_themed_icon_new_with_default_fallbacks (s);
          media_icon = g_themed_icon_new_with_default_fallbacks (s);
        }
    }

  /* return values to caller */
  if (out_name != NULL)
    *out_name = name;
  else
    g_free (name);
  if (out_description != NULL)
    *out_description = description;
  else
    g_free (description);
  if (out_icon != NULL)
    *out_icon = icon;
  else if (icon != NULL)
    g_object_unref (icon);
  if (out_media_description != NULL)
    *out_media_description = media_description;
  else
    g_free (media_description);
  if (out_media_icon != NULL)
    *out_media_icon = media_icon;
  else if (media_icon != NULL)
    g_object_unref (media_icon);

  g_free (hyphenated_connection_bus);
  g_free (size_str);

  g_clear_object (&block);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_client_get_cleartext_block:
 * @client: A #UDisksClient.
 * @block: A #UDisksBlock.
 *
 * If @block is an unlocked encrypted device, gets the cleartext device.
 *
 * Returns: (transfer full): A #UDisksBlock or %NULL. Free with
 * g_object_unref() when done with it.
 */
UDisksBlock *
udisks_client_get_cleartext_block (UDisksClient  *client,
                                   UDisksBlock   *block)
{
  UDisksBlock *ret = NULL;
  GDBusObject *object;
  const gchar *object_path;
  GList *objects = NULL;
  GList *l;

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (block));
  if (object == NULL)
    goto out;

  object_path = g_dbus_object_get_object_path (object);
  objects = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *iter_object = UDISKS_OBJECT (l->data);
      UDisksBlock *iter_block;

      iter_block = udisks_object_peek_block (iter_object);
      if (iter_block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_crypto_backing_device (iter_block), object_path) == 0)
        {
          ret = g_object_ref (iter_block);
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_client_get_partition_table:
 * @client: A #UDisksClient.
 * @partition: A #UDisksPartition.
 *
 * Gets the #UDisksPartitionTable corresponding to @partition.
 *
 * Returns: (transfer full): A #UDisksPartitionTable. Free with g_object_unref().
 */
UDisksPartitionTable *
udisks_client_get_partition_table (UDisksClient     *client,
                                   UDisksPartition  *partition)
{
  UDisksPartitionTable *ret = NULL;
  UDisksObject *object;

  g_return_val_if_fail (UDISKS_IS_CLIENT (client), NULL);
  g_return_val_if_fail (UDISKS_IS_PARTITION (partition), NULL);

  object = udisks_client_get_object (client, udisks_partition_get_table (partition));
  if (object == NULL)
    goto out;

  ret = udisks_object_get_partition_table (object);
  g_object_unref (object);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_changed_timeout (gpointer user_data)
{
  UDisksClient *client = UDISKS_CLIENT (user_data);
  client->changed_timeout_source = NULL;
  g_signal_emit (client, signals[CHANGED_SIGNAL], 0);
  return FALSE; /* remove source */
}

static void
queue_changed (UDisksClient *client)
{
  if (client->changed_timeout_source != NULL)
    goto out;

  client->changed_timeout_source = g_timeout_source_new (100);
  g_source_set_callback (client->changed_timeout_source,
                         (GSourceFunc) on_changed_timeout,
                         client,
                         NULL); /* destroy notify */
  g_source_attach (client->changed_timeout_source, client->context);
  g_source_unref (client->changed_timeout_source);

 out:
  ;
}

static void
on_object_added (GDBusObjectManager  *manager,
                 GDBusObject         *object,
                 gpointer             user_data)
{
  UDisksClient *client = UDISKS_CLIENT (user_data);
  queue_changed (client);
}

static void
on_object_removed (GDBusObjectManager  *manager,
                   GDBusObject         *object,
                   gpointer             user_data)
{
  UDisksClient *client = UDISKS_CLIENT (user_data);
  queue_changed (client);
}

static void
init_interface_proxy (UDisksClient *client,
                      GDBusProxy   *proxy)
{
  /* disable method timeouts */
  g_dbus_proxy_set_default_timeout (proxy, G_MAXINT);
}

static void
on_interface_added (GDBusObjectManager  *manager,
                    GDBusObject         *object,
                    GDBusInterface      *interface,
                    gpointer             user_data)
{
  UDisksClient *client = UDISKS_CLIENT (user_data);

  init_interface_proxy (client, G_DBUS_PROXY (interface));

  queue_changed (client);
}

static void
on_interface_removed (GDBusObjectManager  *manager,
                      GDBusObject         *object,
                      GDBusInterface      *interface,
                      gpointer             user_data)
{
  UDisksClient *client = UDISKS_CLIENT (user_data);
  queue_changed (client);
}

static void
on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                       GDBusObjectProxy           *object_proxy,
                                       GDBusProxy                 *interface_proxy,
                                       GVariant                   *changed_properties,
                                       const gchar *const         *invalidated_properties,
                                       gpointer                    user_data)
{
  UDisksClient *client = UDISKS_CLIENT (user_data);
  queue_changed (client);
}

/* ---------------------------------------------------------------------------------------------------- */

#define KILOBYTE_FACTOR 1000.0
#define MEGABYTE_FACTOR (1000.0 * 1000.0)
#define GIGABYTE_FACTOR (1000.0 * 1000.0 * 1000.0)
#define TERABYTE_FACTOR (1000.0 * 1000.0 * 1000.0 * 1000.0)

#define KIBIBYTE_FACTOR 1024.0
#define MEBIBYTE_FACTOR (1024.0 * 1024.0)
#define GIBIBYTE_FACTOR (1024.0 * 1024.0 * 1024.0)
#define TEBIBYTE_FACTOR (1024.0 * 1024.0 * 1024.0 * 10242.0)

static char *
get_pow2_size (guint64 size)
{
  gchar *str;
  gdouble displayed_size;
  const gchar *unit;
  guint digits;

  if (size < MEBIBYTE_FACTOR)
    {
      displayed_size = (double) size / KIBIBYTE_FACTOR;
      unit = "KiB";
    }
  else if (size < GIBIBYTE_FACTOR)
    {
      displayed_size = (double) size / MEBIBYTE_FACTOR;
      unit = "MiB";
    }
  else if (size < TEBIBYTE_FACTOR)
    {
      displayed_size = (double) size / GIBIBYTE_FACTOR;
      unit = "GiB";
    }
  else
    {
      displayed_size = (double) size / TEBIBYTE_FACTOR;
      unit = "TiB";
    }

  if (displayed_size < 10.0)
    digits = 1;
  else
    digits = 0;

  str = g_strdup_printf ("%.*f %s", digits, displayed_size, unit);

  return str;
}

static char *
get_pow10_size (guint64 size)
{
  gchar *str;
  gdouble displayed_size;
  const gchar *unit;
  guint digits;

  if (size < MEGABYTE_FACTOR)
    {
      displayed_size = (double) size / KILOBYTE_FACTOR;
      unit = "KB";
    }
  else if (size < GIGABYTE_FACTOR)
    {
      displayed_size = (double) size / MEGABYTE_FACTOR;
      unit = "MB";
    }
  else if (size < TERABYTE_FACTOR)
    {
      displayed_size = (double) size / GIGABYTE_FACTOR;
      unit = "GB";
    }
  else
    {
      displayed_size = (double) size / TERABYTE_FACTOR;
      unit = "TB";
    }

  if (displayed_size < 10.0)
    digits = 1;
  else
    digits = 0;

  str = g_strdup_printf ("%.*f %s", digits, displayed_size, unit);

  return str;
}

/**
 * udisks_client_get_size_for_display:
 * @client: A #UDisksClient.
 * @size: Size in bytes
 * @use_pow2: Whether power-of-two units should be used instead of power-of-ten units.
 * @long_string: Whether to produce a long string.
 *
 * Utility function to get a human-readable string that represents @size.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
udisks_client_get_size_for_display (UDisksClient  *client,
                                    guint64        size,
                                    gboolean       use_pow2,
                                    gboolean       long_string)
{
  gchar *str;

  if (long_string)
    {
      gchar *size_str;
      size_str = g_strdup_printf ("%'" G_GINT64_FORMAT, size);
      if (use_pow2)
        {
          gchar *pow2_str;
          pow2_str = get_pow2_size (size);
          /* Translators: The first %s is the size in power-of-2 units, e.g. '64 KiB'
           * the second %s is the size as a number e.g. '65,536 bytes'
           */
          str = g_strdup_printf (_("%s (%s bytes)"), pow2_str, size_str);
          g_free (pow2_str);
        }
      else
        {
          gchar *pow10_str;
          pow10_str = get_pow10_size (size);
          /* Translators: The first %s is the size in power-of-10 units, e.g. '100 KB'
           * the second %s is the size as a number e.g. '100,000 bytes'
           */
          str = g_strdup_printf (_("%s (%s bytes)"), pow10_str, size_str);
          g_free (pow10_str);
        }

      g_free (size_str);
    }
  else
    {
      if (use_pow2)
        {
          str = get_pow2_size (size);
        }
      else
        {
          str = get_pow10_size (size);
        }
    }
  return str;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_client_get_media_compat_for_display:
 * @client: A #UDisksClient.
 * @media_compat: An array of media types.
 *
 * Gets a human-readable string of the media described by
 * @media_compat. The returned information is localized.
 *
 * Returns: A string that should be freed with g_free() or %NULL if
 * unknown.
 */
gchar *
udisks_client_get_media_compat_for_display (UDisksClient       *client,
                                            const gchar* const *media_compat)
{
  guint n;
  gboolean optical_cd;
  gboolean optical_dvd;
  gboolean optical_bd;
  gboolean optical_hddvd;
  GString *result;

  optical_cd = FALSE;
  optical_dvd = FALSE;
  optical_bd = FALSE;
  optical_hddvd = FALSE;

  result = g_string_new (NULL);

  for (n = 0; media_compat != NULL && media_compat[n] != NULL; n++)
    {
      const gchar *media_name;
      const gchar *media;

      media = media_compat[n];
      media_name = NULL;
      if (g_strcmp0 (media, "flash_cf") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("CompactFlash");
        }
      else if (g_strcmp0 (media, "flash_ms") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("MemoryStick");
        }
      else if (g_strcmp0 (media, "flash_sm") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("SmartMedia");
        }
      else if (g_strcmp0 (media, "flash_sd") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("SecureDigital");
        }
      else if (g_strcmp0 (media, "flash_sdhc") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("SD High Capacity");
        }
      else if (g_strcmp0 (media, "floppy") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("Floppy");
        }
      else if (g_strcmp0 (media, "floppy_zip") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("Zip");
        }
      else if (g_strcmp0 (media, "floppy_jaz") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("Jaz");
        }
      else if (g_str_has_prefix (media, "flash"))
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = _("Flash");
        }
      else if (g_str_has_prefix (media, "optical_cd"))
        {
          optical_cd = TRUE;
        }
      else if (g_str_has_prefix (media, "optical_dvd"))
        {
          optical_dvd = TRUE;
        }
      else if (g_str_has_prefix (media, "optical_bd"))
        {
          optical_bd = TRUE;
        }
      else if (g_str_has_prefix (media, "optical_hddvd"))
        {
          optical_hddvd = TRUE;
        }

      if (media_name != NULL)
        {
          if (result->len > 0)
            g_string_append_c (result, '/');
          g_string_append (result, media_name);
        }
    }

  if (optical_cd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, _("CD"));
    }
  if (optical_dvd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, _("DVD"));
    }
  if (optical_bd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, _("Blu-Ray"));
    }
  if (optical_hddvd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, _("HDDVD"));
    }

  if (result->len > 0)
    return g_string_free (result, FALSE);

  g_string_free (result, TRUE);
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

const static struct
{
  const gchar *usage;
  const gchar *type;
  const gchar *version;
  const gchar *long_name;
  const gchar *short_name;
} id_type[] =
{
  {"filesystem", "vfat",              "FAT12", N_("FAT (12-bit version)"),              N_("FAT")},
  {"filesystem", "vfat",              "FAT16", N_("FAT (16-bit version)"),              N_("FAT")},
  {"filesystem", "vfat",              "FAT32", N_("FAT (32-bit version)"),              N_("FAT")},
  {"filesystem", "ntfs",              "*",     N_("FAT (version %s)"),                  N_("FAT")},
  {"filesystem", "vfat",              NULL,    N_("FAT"),                               N_("FAT")},
  {"filesystem", "ntfs",              "*",     N_("NTFS (version %s)"),                 N_("NTFS")},
  {"filesystem", "ntfs",              NULL,    N_("NTFS"),                              N_("NTFS")},
  {"filesystem", "hfs",               NULL,    N_("HFS"),                               N_("HFS")},
  {"filesystem", "hfsplus",           NULL,    N_("HFS+"),                              N_("HFS+")},
  {"filesystem", "ext2",              "*",     N_("Ext2 (version %s)"),                 N_("Ext2")},
  {"filesystem", "ext2",              NULL,    N_("Ext2"),                              N_("Ext2")},
  {"filesystem", "ext3",              "*",     N_("Ext3 (version %s)"),                 N_("Ext3")},
  {"filesystem", "ext3",              NULL,    N_("Ext3"),                              N_("Ext3")},
  {"filesystem", "ext4",              "*",     N_("Ext4 (version %s)"),                 N_("Ext4")},
  {"filesystem", "ext4",              NULL,    N_("Ext4"),                              N_("Ext4")},
  {"filesystem", "jdb",               "*",     N_("Journal for Ext (version %s)"),      N_("JDB")},
  {"filesystem", "jdb",               "*",     N_("Journal for Ext"),                   N_("JDB")},
  {"filesystem", "xfs",               "*",     N_("XFS (version %s)"),                  N_("XFS")},
  {"filesystem", "xfs",               NULL,    N_("XFS"),                               N_("XFS")},
  {"filesystem", "iso9660",           "*",     N_("ISO 9660 (version %s)"),             N_("ISO9660")},
  {"filesystem", "iso9660",           NULL,    N_("ISO 9660"),                          N_("ISO9660")},
  {"filesystem", "udf",               "*",     N_("UDF (version %s)"),                  N_("UDF")},
  {"filesystem", "udf",               NULL,    N_("UDF"),                               N_("UDF")},
  {"other",      "swap",              "*",     N_("Swap (version %s)"),                 N_("Swap")},
  {"other",      "swap",              NULL,    N_("Swap"),                              N_("Swap")},
  {"raid",       "LVM2_member",       "*",     N_("LVM2 Physical Volume (%s)"),         N_("LVM2 PV")},
  {"raid",       "LVM2_member",       NULL,    N_("LVM2 Physical Volume"),              N_("LVM2 PV")},
  {"raid",       "linux_raid_member", "*",     N_("Software RAID Component (version %s)"), N_("MD Raid")},
  {"raid",       "linux_raid_member", NULL,    N_("Software RAID Component"),           N_("MD Raid")},
  {"raid",       "zfs_member",        "*",     N_("ZFS Device (ZPool version %s)"),     N_("ZFS (v%s)")},
  {"raid",       "zfs_member",        NULL,    N_("ZFS Device"),                        N_("ZFS")},
  {"crypto",     "crypto_LUKS",       "*",     N_("LUKS Encryption (version %s)"),      N_("LUKS")},
  {"crypto",     "crypto_LUKS",       NULL,    N_("LUKS Encryption"),                   N_("LUKS")},
  {NULL, NULL, NULL, NULL}
};

/**
 * udisks_client_get_id_for_display:
 * @client: A #UDisksClient.
 * @usage: Usage id e.g. "filesystem" or "crypto".
 * @type: Type e.g. "ext4" or "crypto_LUKS"
 * @version: Version.
 * @long_string: Whether to produce a long string.
 *
 * Gets a human readable localized string for @usage, @type and @version.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
udisks_client_get_id_for_display (UDisksClient *client,
                                  const gchar  *usage,
                                  const gchar  *type,
                                  const gchar  *version,
                                  gboolean      long_string)
{
  guint n;
  gchar *ret;

  ret = NULL;

  for (n = 0; id_type[n].usage != NULL; n++)
    {
      if (g_strcmp0 (id_type[n].usage, usage) == 0 &&
          g_strcmp0 (id_type[n].type, type) == 0)
        {
          if ((id_type[n].version == NULL && strlen (version) == 0))
            {
              if (long_string)
                ret = g_strdup (_(id_type[n].long_name));
              else
                ret = g_strdup (_(id_type[n].short_name));
              goto out;
            }
          else if ((g_strcmp0 (id_type[n].version, version) == 0 && strlen (version) > 0) ||
                   (g_strcmp0 (id_type[n].version, "*") == 0 && strlen (version) > 0))
            {
              if (long_string)
                ret = g_strdup_printf (_(id_type[n].long_name), version);
              else
                ret = g_strdup_printf (_(id_type[n].short_name), version);
              goto out;
            }
        }
    }

  if (long_string)
    {
      if (strlen (version) > 0)
        {
          /* Translators: Shown for unknown filesystem types.
           * First %s is the filesystem type, second %s is version.
           */
          ret = g_strdup_printf (_("Unknown (%s %s)"), type, version);
        }
      else
        {
          if (strlen (type) > 0)
            {
              /* Translators: Shown for unknown filesystem types.
               * First %s is the filesystem type.
               */
              ret = g_strdup_printf (_("Unknown (%s)"), type);
            }
          else
            {
              /* Translators: Shown for unknown filesystem types.
               */
              ret = g_strdup (_("Unknown"));
            }
        }
    }
  else
    {
      if (strlen (type) > 0)
        ret = g_strdup (type);
      else
        ret = g_strdup (_("Unknown"));
    }

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

const static struct
{
  const gchar *scheme;
  const gchar *name;
} part_scheme[] =
{
  {"dos", N_("Master Boot Record")},
  {"gpt", N_("GUID Partition Table")},
  {"apm", N_("Apple Partition Map")},
  {NULL, NULL}
};

/**
 * udisks_client_get_part_scheme_for_display:
 * @client: A #UDisksClient.
 * @scheme: A partitioning scheme id.
 *
 * Gets a human readable localized string for @scheme.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
udisks_client_get_part_scheme_for_display (UDisksClient  *client,
                                           const gchar   *scheme)
{
  guint n;
  gchar *ret;

  for (n = 0; part_scheme[n].scheme != NULL; n++)
    {
      if (g_strcmp0 (part_scheme[n].scheme, scheme) == 0)
        {
          ret = g_strdup (_(part_scheme[n].name));
          goto out;
        }
    }

  /* Translators: Shown for unknown partitioning scheme.
   * First %s is the partition type scheme.
   */
  ret = g_strdup_printf (_("Unknown Scheme (%s)"), scheme);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

const static struct
{
  const gchar *scheme;
  const gchar *type;
  const gchar *name;
} part_type[] =
{
  /* see http://en.wikipedia.org/wiki/GUID_Partition_Table */

  /* Linux */
  {"gpt", "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", N_("Basic Data")}, /* same as ms bdp */
  {"gpt", "a19d880f-05fc-4d3b-a006-743f0f84911e", N_("Linux RAID")},
  {"gpt", "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f", N_("Linux Swap")},
  {"gpt", "e6d6d379-f507-44c2-a23c-238f2a3df928", N_("Linux LVM")},
  {"gpt", "8da63339-0007-60c0-c436-083ac8230908", N_("Linux Reserved")},
  /* Not associated with any OS */
  {"gpt", "024dee41-33e7-11d3-9d69-0008c781f39f", N_("MBR Partition Scheme")},
  {"gpt", "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", N_("EFI System")},
  {"gpt", "21686148-6449-6e6f-744e-656564454649", N_("BIOS Boot")},
  /* Microsoft */
  {"gpt", "e3c9e316-0b5c-4db8-817d-f92df00215ae", N_("Microsoft Reserved")},
  {"gpt", "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", N_("Microsoft Basic Data")}, /* same as Linux Basic Data */
  {"gpt", "5808c8aa-7e8f-42e0-85d2-e1e90434cfb3", N_("Microsoft LDM metadata")},
  {"gpt", "af9b60a0-1431-4f62-bc68-3311714a69ad", N_("Microsoft LDM data")},
  {"gpt", "de94bba4-06d1-4d40-a16a-bfd50179d6ac", N_("Microsoft Windows Recovery Environment")},
  /* HP-UX */
  {"gpt", "75894c1e-3aeb-11d3-b7c1-7b03a0000000", N_("HP-UX Data")},
  {"gpt", "e2a1e728-32e3-11d6-a682-7b03a0000000", N_("HP-UX Service")},
  /* FreeBSD */
  {"gpt", "83bd6b9d-7f41-11dc-be0b-001560b84f0f", N_("FreeBSD Boot")},
  {"gpt", "516e7cb4-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD Data")},
  {"gpt", "516e7cb5-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD Swap")},
  {"gpt", "516e7cb6-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD UFS")},
  {"gpt", "516e7cb8-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD Vinum")},
  {"gpt", "516e7cba-6ecf-11d6-8ff8-00022d09712b", N_("FreeBSD ZFS")},
  /* Solaris */
  {"gpt", "6a82cb45-1dd2-11b2-99a6-080020736631", N_("Solaris Boot")},
  {"gpt", "6a85cf4d-1dd2-11b2-99a6-080020736631", N_("Solaris Root")},
  {"gpt", "6a87c46f-1dd2-11b2-99a6-080020736631", N_("Solaris Swap")},
  {"gpt", "6a8b642b-1dd2-11b2-99a6-080020736631", N_("Solaris Backup")},
  {"gpt", "6a898cc3-1dd2-11b2-99a6-080020736631", N_("Solaris /usr")}, /* same as Apple ZFS */
  {"gpt", "6a8ef2e9-1dd2-11b2-99a6-080020736631", N_("Solaris /var")},
  {"gpt", "6a90ba39-1dd2-11b2-99a6-080020736631", N_("Solaris /home")},
  {"gpt", "6a9283a5-1dd2-11b2-99a6-080020736631", N_("Solaris Alternate Sector")},
  {"gpt", "6a945a3b-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved")},
  {"gpt", "6a9630d1-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved (2)")},
  {"gpt", "6a980767-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved (3)")},
  {"gpt", "6a96237f-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved (4)")},
  {"gpt", "6a8d2ac7-1dd2-11b2-99a6-080020736631", N_("Solaris Reserved (5)")},
  /* Apple OS X */
  {"gpt", "48465300-0000-11aa-aa11-00306543ecac", N_("Apple HFS/HFS+")},
  {"gpt", "55465300-0000-11aa-aa11-00306543ecac", N_("Apple UFS")},
  {"gpt", "6a898cc3-1dd2-11b2-99a6-080020736631", N_("Apple ZFS")}, /* same as Solaris /usr */
  {"gpt", "52414944-0000-11aa-aa11-00306543ecac", N_("Apple RAID")},
  {"gpt", "52414944-5f4f-11aa-aa11-00306543ecac", N_("Apple RAID (offline)")},
  {"gpt", "426f6f74-0000-11aa-aa11-00306543ecac", N_("Apple Boot")},
  {"gpt", "4c616265-6c00-11aa-aa11-00306543ecac", N_("Apple Label")},
  {"gpt", "5265636f-7665-11aa-aa11-00306543ecac", N_("Apple TV Recovery")},
  /* NetBSD */
  {"gpt", "49f48d32-b10e-11dc-b99b-0019d1879648", N_("NetBSD Swap")},
  {"gpt", "49f48d5a-b10e-11dc-b99b-0019d1879648", N_("NetBSD FFS")},
  {"gpt", "49f48d82-b10e-11dc-b99b-0019d1879648", N_("NetBSD LFS")},
  {"gpt", "49f48daa-b10e-11dc-b99b-0019d1879648", N_("NetBSD RAID")},
  {"gpt", "2db519c4-b10f-11dc-b99b-0019d1879648", N_("NetBSD Concatenated")},
  {"gpt", "2db519ec-b10f-11dc-b99b-0019d1879648", N_("NetBSD Encrypted")},

  /* see http://developer.apple.com/documentation/mac/devices/devices-126.html
   *     http://lists.apple.com/archives/Darwin-drivers/2003/May/msg00021.html */
  {"apm", "Apple_Unix_SVR2", N_("Apple UFS")},
  {"apm", "Apple_HFS", N_("Apple HFS/HFS")},
  {"apm", "Apple_partition_map", N_("Apple Partition Map")},
  {"apm", "Apple_Free", N_("Unused")},
  {"apm", "Apple_Scratch", N_("Empty")},
  {"apm", "Apple_Driver", N_("Driver")},
  {"apm", "Apple_Driver43", N_("Driver 4.3")},
  {"apm", "Apple_PRODOS", N_("ProDOS file system")},
  {"apm", "DOS_FAT_12", N_("FAT 12")},
  {"apm", "DOS_FAT_16", N_("FAT 16")},
  {"apm", "DOS_FAT_32", N_("FAT 32")},
  {"apm", "Windows_FAT_16", N_("FAT 16 (Windows)")},
  {"apm", "Windows_FAT_32", N_("FAT 32 (Windows)")},

  /* see http://www.win.tue.nl/~aeb/partitions/partition_types-1.html */
  {"dos", "0x00",  N_("Empty")},
  {"dos", "0x01",  N_("FAT12")},
  {"dos", "0x04",  N_("FAT16 <32M")},
  {"dos", "0x05",  N_("Extended")},
  {"dos", "0x06",  N_("FAT16")},
  {"dos", "0x07",  N_("HPFS/NTFS")},
  {"dos", "0x0b",  N_("W95 FAT32")},
  {"dos", "0x0c",  N_("W95 FAT32 (LBA)")},
  {"dos", "0x0e",  N_("W95 FAT16 (LBA)")},
  {"dos", "0x0f",  N_("W95 Ext d (LBA)")},
  {"dos", "0x10",  N_("OPUS")},
  {"dos", "0x11",  N_("Hidden FAT12")},
  {"dos", "0x12",  N_("Compaq diagnostics")},
  {"dos", "0x14",  N_("Hidden FAT16 <32M")},
  {"dos", "0x16",  N_("Hidden FAT16")},
  {"dos", "0x17",  N_("Hidden HPFS/NTFS")},
  {"dos", "0x1b",  N_("Hidden W95 FAT32")},
  {"dos", "0x1c",  N_("Hidden W95 FAT32 (LBA)")},
  {"dos", "0x1e",  N_("Hidden W95 FAT16 (LBA)")},
  {"dos", "0x3c",  N_("PartitionMagic")},
  {"dos", "0x81",  N_("Minix")}, /* cf. http://en.wikipedia.org/wiki/MINIX_file_system */
  {"dos", "0x82",  N_("Linux swap")},
  {"dos", "0x83",  N_("Linux")},
  {"dos", "0x84",  N_("Hibernation")},
  {"dos", "0x85",  N_("Linux Extended")},
  {"dos", "0x8e",  N_("Linux LVM")},
  {"dos", "0xa0",  N_("Hibernation")},
  {"dos", "0xa5",  N_("FreeBSD")},
  {"dos", "0xa6",  N_("OpenBSD")},
  {"dos", "0xa8",  N_("Mac OS X")},
  {"dos", "0xaf",  N_("Mac OS X")},
  {"dos", "0xbe",  N_("Solaris boot")},
  {"dos", "0xbf",  N_("Solaris")},
  {"dos", "0xeb",  N_("BeOS BFS")},
  {"dos", "0xec",  N_("SkyOS SkyFS")},
  {"dos", "0xee",  N_("EFI GPT")},
  {"dos", "0xef",  N_("EFI (FAT-12/16/32)")},
  {"dos", "0xfd",  N_("Linux RAID auto")},
  {NULL,  NULL, NULL}
};

/**
 * udisks_client_get_part_types_for_scheme:
 * @client: A #UDisksClient.
 * @scheme: A partitioning scheme id.
 *
 * Gets all known types for @scheme.
 *
 * Returns: (transfer container): A %NULL-terminated array of
 * strings. Only the container should be freed with g_free().
 */
const gchar **
udisks_client_get_part_types_for_scheme (UDisksClient   *client,
                                         const gchar    *scheme)
{
  guint n;
  GPtrArray *p;

  p = g_ptr_array_new();
  for (n = 0; part_type[n].name != NULL; n++)
    {
      if (g_strcmp0 (part_type[n].scheme, scheme) == 0)
        {
          g_ptr_array_add (p, (gpointer) part_type[n].type);
        }
    }
  g_ptr_array_add (p, NULL);

  return (const gchar **) g_ptr_array_free (p, FALSE);
}

/**
 * udisks_client_get_part_type_for_display:
 * @client: A #UDisksClient.
 * @scheme: A partitioning scheme id.
 * @type: A partition type.
 * @long_string: Whether to produce a long string.
 *
 * Gets a human readable localized string for @scheme and @type.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
udisks_client_get_part_type_for_display (UDisksClient  *client,
                                         const gchar   *scheme,
                                         const gchar   *type,
                                         gboolean       long_string)
{
  guint n;
  gchar *ret;

  for (n = 0; part_type[n].name != NULL; n++)
    {
      if (g_strcmp0 (part_type[n].scheme, scheme) == 0 &&
          g_strcmp0 (part_type[n].type, type) == 0)
        {
          if (long_string)
            {
              /* Translators: First %s is the detailed partition type (e.g. "FAT16 (0x16)") and
               * second %s is the partition type (e.g. 0x16)
               */
              ret = g_strdup_printf ("%s (%s)", _(part_type[n].name), type);
            }
          else
            {
              ret = g_strdup (_(part_type[n].name));
            }
          goto out;
        }
    }

  if (long_string)
    {
      /* Translators: Shown for unknown partition types.
       * First %s is the partition type.
       */
      ret = g_strdup_printf (_("Unknown (%s)"), type);
    }
  else
    {
      ret = g_strdup (_("Unknown"));
    }

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

