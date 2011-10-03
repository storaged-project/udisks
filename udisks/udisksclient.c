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
#include "udisksutil.h"
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

      block = udisks_object_get_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_drive (block), drive_object_path) == 0 &&
          !udisks_block_get_part_entry (block))
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
    size_str = udisks_util_get_size_for_display (size, FALSE, FALSE);
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
on_interface_added (GDBusObjectManager  *manager,
                    GDBusObject         *object,
                    GDBusInterface      *interface,
                    gpointer             user_data)
{
  UDisksClient *client = UDISKS_CLIENT (user_data);
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
