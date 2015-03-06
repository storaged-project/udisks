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

#include "storagedclient.h"
#include "storagederror.h"
#include "storaged-generated.h"
#include "storagedobjectinfo.h"

/* For __GNUC_PREREQ usage below */
#ifdef __GNUC__
# include <features.h>
#endif

/**
 * SECTION:storagedclient
 * @title: StoragedClient
 * @short_description: Utility routines for accessing the Storaged service
 *
 * #StoragedClient is used for accessing the Storaged service from a
 * client program.
 */

G_LOCK_DEFINE_STATIC (init_lock);

/**
 * StoragedClient:
 *
 * The #StoragedClient structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedClient
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
} StoragedClientClass;

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

static void maybe_emit_changed_now (StoragedClient *client);

static void init_interface_proxy (StoragedClient *client,
                                  GDBusProxy   *proxy);

static StoragedPartitionTypeInfo *storaged_partition_type_info_new (void);

G_DEFINE_TYPE_WITH_CODE (StoragedClient, storaged_client, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init)
                         );

static void
storaged_client_finalize (GObject *object)
{
  StoragedClient *client = STORAGED_CLIENT (object);

  if (client->changed_timeout_source != NULL)
    g_source_destroy (client->changed_timeout_source);

  if (client->initialization_error != NULL)
    g_error_free (client->initialization_error);

  /* might be NULL if failing early in the constructor */
  if (client->object_manager != NULL)
    {
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
    }

  if (client->context != NULL)
    g_main_context_unref (client->context);

  G_OBJECT_CLASS (storaged_client_parent_class)->finalize (object);
}

static void
storaged_client_init (StoragedClient *client)
{
  static volatile GQuark storaged_error_domain = 0;
  /* this will force associating errors in the STORAGED_ERROR error
   * domain with org.storaged.Storaged.Error.* errors via
   * g_dbus_error_register_error_domain().
   */
  storaged_error_domain = STORAGED_ERROR;
  storaged_error_domain; /* shut up -Wunused-but-set-variable */
}

static void
storaged_client_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  StoragedClient *client = STORAGED_CLIENT (object);

  switch (prop_id)
    {
    case PROP_OBJECT_MANAGER:
      g_value_set_object (value, storaged_client_get_object_manager (client));
      break;

    case PROP_MANAGER:
      g_value_set_object (value, storaged_client_get_manager (client));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_client_class_init (StoragedClientClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_client_finalize;
  gobject_class->get_property = storaged_client_get_property;

  /**
   * StoragedClient:object-manager:
   *
   * The #GDBusObjectManager used by the #StoragedClient instance.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_MANAGER,
                                   g_param_spec_object ("object-manager",
                                                        "Object Manager",
                                                        "The GDBusObjectManager used by the StoragedClient",
                                                        G_TYPE_DBUS_OBJECT_MANAGER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedClient:manager:
   *
   * The #StoragedManager interface on the well-known
   * <literal>/org/storaged/Storaged/Manager</literal> object
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MANAGER,
                                   g_param_spec_object ("manager",
                                                        "Manager",
                                                        "The StoragedManager",
                                                        STORAGED_TYPE_MANAGER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedClient::changed:
   * @client: A #StoragedClient.
   *
   * This signal is emitted either when an object or interface is
   * added or removed a when property has changed. Additionally,
   * multiple received signals are coalesced into a single signal that
   * is rate-limited to fire at most every 100ms.
   *
   * Note that calling storaged_client_settle() will cause this signal
   * to fire if any changes are outstanding.
   *
   * For greater detail, connect to the
   * #GDBusObjectManager::object-added,
   * #GDBusObjectManager::object-removed,
   * #GDBusObjectManager::interface-added,
   * #GDBusObjectManager::interface-removed,
   * #GDBusObjectManagerClient::interface-proxy-properties-changed and
   * signals on the #StoragedClient:object-manager object.
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
 * storaged_client_new:
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Function that will be called when the result is ready.
 * @user_data: Data to pass to @callback.
 *
 * Asynchronously gets a #StoragedClient. When the operation is
 * finished, @callback will be invoked in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this method from.
 */
void
storaged_client_new (GCancellable        *cancellable,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
  g_async_initable_new_async (STORAGED_TYPE_CLIENT,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              callback,
                              user_data,
                              NULL);
}

/**
 * storaged_client_new_finish:
 * @res: A #GAsyncResult.
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with storaged_client_new().
 *
 * Returns: A #StoragedClient or %NULL if @error is set. Free with
 * g_object_unref() when done with it.
 */
StoragedClient *
storaged_client_new_finish (GAsyncResult        *res,
                            GError             **error)
{
  GObject *ret;
  GObject *source_object;
  source_object = g_async_result_get_source_object (res);
  ret = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, error);
  g_object_unref (source_object);
  if (ret != NULL)
    return STORAGED_CLIENT (ret);
  else
    return NULL;
}

/**
 * storaged_client_new_sync:
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: (allow-none): Return location for error or %NULL.
 *
 * Synchronously gets a #StoragedClient for the local system.
 *
 * Returns: A #StoragedClient or %NULL if @error is set. Free with
 * g_object_unref() when done with it.
 */
StoragedClient *
storaged_client_new_sync (GCancellable  *cancellable,
                          GError       **error)
{
  GInitable *ret;
  ret = g_initable_new (STORAGED_TYPE_CLIENT,
                        cancellable,
                        error,
                        NULL);
  if (ret != NULL)
    return STORAGED_CLIENT (ret);
  else
    return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  StoragedClient *client = STORAGED_CLIENT (initable);
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

  client->object_manager = storaged_object_manager_client_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                                            G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                                            "org.storaged.Storaged",
                                                                            "/org/storaged/Storaged",
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
 * storaged_client_get_object_manager:
 * @client: A #StoragedClient.
 *
 * Gets the #GDBusObjectManager used by @client.
 *
 * Returns: (transfer none): A #GDBusObjectManager. Do not free, the
 * instance is owned by @client.
 */
GDBusObjectManager *
storaged_client_get_object_manager (StoragedClient        *client)
{
  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  return client->object_manager;
}

/**
 * storaged_client_get_manager:
 * @client: A #StoragedClient.
 *
 * Gets the #StoragedManager interface on the well-known
 * <literal>/org/storaged/Storaged/Manager</literal> object.
 *
 * Returns: (transfer none): A #StoragedManager or %NULL if the storaged
 * daemon is not currently running. Do not free, the instance is owned
 * by @client.
 */
StoragedManager *
storaged_client_get_manager (StoragedClient *client)
{
  StoragedManager *ret = NULL;
  GDBusObject *obj;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);

  obj = g_dbus_object_manager_get_object (client->object_manager, "/org/storaged/Storaged/Manager");
  if (obj == NULL)
    goto out;

  ret = storaged_object_peek_manager (STORAGED_OBJECT (obj));
  g_object_unref (obj);

 out:
  return ret;
}

/**
 * storaged_client_settle:
 * @client: A #StoragedClient.
 *
 * Blocks until all pending D-Bus messages have been delivered. Also
 * emits the (rate-limited) #StoragedClient::changed signal if changes
 * are currently pending.
 *
 * This is useful in two situations: 1. when using synchronous method
 * calls since e.g. D-Bus signals received while waiting for the reply
 * are queued up and dispatched after the synchronous call ends; and
 * 2. when using asynchronous calls where the return value references
 * a newly created object (such as the <link
 * linkend="gdbus-method-org-storaged-Storaged-Manager.LoopSetup">Manager.LoopSetup()</link> method).
 */
void
storaged_client_settle (StoragedClient *client)
{
  while (g_main_context_iteration (client->context, FALSE /* may_block */))
    ;
  /* TODO: careful if on different thread... */
  maybe_emit_changed_now (client);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_client_get_object:
 * @client: A #StoragedClient.
 * @object_path: Object path.
 *
 * Convenience function for looking up an #StoragedObject for @object_path.
 *
 * Returns: (transfer full): A #StoragedObject corresponding to
 * @object_path or %NULL if not found. The returned object must be
 * freed with g_object_unref().
 */
StoragedObject *
storaged_client_get_object (StoragedClient  *client,
                            const gchar   *object_path)
{
  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  return (StoragedObject *) g_dbus_object_manager_get_object (client->object_manager, object_path);
}

/**
 * storaged_client_peek_object:
 * @client: A #StoragedClient.
 * @object_path: Object path.
 *
 * Like storaged_client_get_object() but doesn't increase the reference
 * count on the returned #StoragedObject.
 *
 * <warning>The returned object is only valid until removed so it is only safe to use this function on the thread where @client was constructed. Use storaged_client_get_object() if on another thread.</warning>
 *
 * Returns: (transfer none): A #StoragedObject corresponding to
 * @object_path or %NULL if not found.
 */
StoragedObject *
storaged_client_peek_object (StoragedClient  *client,
                             const gchar   *object_path)
{
  StoragedObject *ret;
  ret = storaged_client_get_object (client, object_path);
  if (ret != NULL)
    g_object_unref (ret);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_client_get_block_for_label:
 * @client: A #StoragedClient.
 * @label: The label.
 *
 * Gets all the #StoragedBlock instances with the given label, if any.
 *
 * Returns: (transfer full) (element-type StoragedBlock): A list of #StoragedBlock instances. The
 *   returned list should be freed with g_list_free() after each
 *   element has been freed with g_object_unref().
 */
GList *
storaged_client_get_block_for_label (StoragedClient        *client,
                                     const gchar         *label)
{
  GList *ret = NULL;
  GList *l, *object_proxies = NULL;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (label != NULL, NULL);

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;

      block = storaged_object_get_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (storaged_block_get_id_label (block), label) == 0)
        ret = g_list_prepend (ret, block);
      else
        g_object_unref (block);
    }

  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  ret = g_list_reverse (ret);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_client_get_block_for_uuid:
 * @client: A #StoragedClient.
 * @uuid: The uuid.
 *
 * Gets all the #StoragedBlock instances with the given uuid, if any.
 *
 * Returns: (transfer full) (element-type StoragedBlock): A list of #StoragedBlock instances. The
 *   returned list should be freed with g_list_free() after each
 *   element has been freed with g_object_unref().
 */
GList *
storaged_client_get_block_for_uuid (StoragedClient        *client,
                                    const gchar         *uuid)
{
  GList *ret = NULL;
  GList *l, *object_proxies = NULL;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (uuid != NULL, NULL);

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;

      block = storaged_object_get_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (storaged_block_get_id_uuid (block), uuid) == 0)
        ret = g_list_prepend (ret, block);
      else
        g_object_unref (block);
    }

  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  ret = g_list_reverse (ret);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* Note: The (type guint64) is a workaround for g-i mishandling dev_t, see
 * https://bugzilla.gnome.org/show_bug.cgi?id=584517 */

/**
 * storaged_client_get_block_for_dev:
 * @client: A #StoragedClient.
 * @block_device_number: (type guint64): A #dev_t to get a #StoragedBlock for.
 *
 * Gets the #StoragedBlock corresponding to @block_device_number, if any.
 *
 * Returns: (transfer full): A #StoragedBlock or %NULL if not found.
 */
StoragedBlock *
storaged_client_get_block_for_dev (StoragedClient *client,
                                   dev_t         block_device_number)
{
  StoragedBlock *ret = NULL;
  GList *l, *object_proxies = NULL;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;

      block = storaged_object_get_block (object);
      if (block == NULL)
        continue;

      if (storaged_block_get_device_number (block) == block_device_number)
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
get_top_level_blocks_for_drive (StoragedClient *client,
                                const gchar  *drive_object_path)
{
  GList *ret;
  GList *l;
  GList *object_proxies;
  GDBusObjectManager *object_manager;

  object_manager = storaged_client_get_object_manager (client);
  object_proxies = g_dbus_object_manager_get_objects (object_manager);

  ret = NULL;
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;
      StoragedPartition *partition;

      block = storaged_object_get_block (object);
      partition = storaged_object_peek_partition (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (storaged_block_get_drive (block), drive_object_path) == 0 &&
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
 * storaged_client_get_block_for_drive:
 * @client: A #StoragedClient.
 * @drive: A #StoragedDrive.
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
 * Returns: (transfer full): A #StoragedBlock or %NULL if the requested
 * kind of block device is not available - use g_object_unref() to
 * free the returned object.
 */
StoragedBlock *
storaged_client_get_block_for_drive (StoragedClient        *client,
                                     StoragedDrive         *drive,
                                     gboolean             get_physical)
{
  StoragedBlock *ret = NULL;
  GDBusObject *object;
  GList *blocks = NULL;
  GList *l;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_DRIVE (drive), NULL);

  object = g_dbus_interface_get_object (G_DBUS_INTERFACE (drive));
  if (object == NULL)
    goto out;

  blocks = get_top_level_blocks_for_drive (client, g_dbus_object_get_object_path (object));
  for (l = blocks; l != NULL; l = l->next)
    {
      StoragedBlock *block = storaged_object_peek_block (STORAGED_OBJECT (l->data));
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
 * storaged_client_get_drive_for_block:
 * @client: A #StoragedClient.
 * @block: A #StoragedBlock.
 *
 * Gets the #StoragedDrive that @block belongs to, if any.
 *
 * Returns: (transfer full): A #StoragedDrive or %NULL if there is no
 * #StoragedDrive for @block - free the returned object with
 * g_object_unref().
 */
StoragedDrive *
storaged_client_get_drive_for_block (StoragedClient  *client,
                                     StoragedBlock   *block)
{
  StoragedDrive *ret = NULL;
  GDBusObject *object;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_BLOCK (block), NULL);

  object = g_dbus_object_manager_get_object (client->object_manager, storaged_block_get_drive (block));
  if (object != NULL)
    ret = storaged_object_get_drive (STORAGED_OBJECT (object));
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_client_get_mdraid_for_block:
 * @client: A #StoragedClient.
 * @block: A #StoragedBlock.
 *
 * Gets the #StoragedMDRaid that @block is the block device for, if any.
 *
 * Returns: (transfer full): A #StoragedMDRaid or %NULL if there is no
 *   #StoragedMDRaid for @block or @block is not a MD-RAID block
 *   device. Free the returned object with g_object_unref().
 *
 * Since: 2.1
 */
StoragedMDRaid *
storaged_client_get_mdraid_for_block (StoragedClient  *client,
                                      StoragedBlock   *block)
{
  StoragedMDRaid *ret = NULL;
  GDBusObject *object;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_BLOCK (block), NULL);

  object = g_dbus_object_manager_get_object (client->object_manager, storaged_block_get_mdraid (block));
  if (object != NULL)
    ret = storaged_object_get_mdraid (STORAGED_OBJECT (object));
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_client_get_block_for_mdraid:
 * @client: A #StoragedClient.
 * @raid: A #StoragedMDRaid.
 *
 * Gets the RAID device (e.g. <filename>/dev/md0</filename>) for @raid.
 *
 * In the case of a <ulink
 * url="http://en.wikipedia.org/wiki/Split-brain_(computing)">split-brain
 * syndrome</ulink>, it is undefined which RAID device is
 * returned. For example this can happen if
 * <filename>/dev/sda</filename> and <filename>/dev/sdb</filename> are
 * components of a two-disk RAID-1 and <filename>/dev/md0</filename>
 * and <filename>/dev/md1</filename> are two degraded arrays, each one
 * using exactly one of the two devices. Use
 * storaged_client_get_all_blocks_for_mdraid() to get all RAID devices.
 *
 * Returns: (transfer full): A #StoragedBlock or %NULL if no RAID device is running.
 *
 * Since: 2.1
 */
StoragedBlock *
storaged_client_get_block_for_mdraid (StoragedClient *client,
                                      StoragedMDRaid *raid)
{
  StoragedBlock *ret = NULL;
  GList *l, *object_proxies = NULL;
  GDBusObject *raid_object;
  const gchar *raid_objpath;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_MDRAID (raid), NULL);

  raid_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (raid));
  if (raid_object == NULL)
    goto out;

  raid_objpath = g_dbus_object_get_object_path (raid_object);

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;

      block = storaged_object_get_block (object);
      if (block == NULL)
        continue;

      /* ignore partitions */
      if (storaged_object_peek_partition (object) != NULL)
        continue;

      if (g_strcmp0 (storaged_block_get_mdraid (block), raid_objpath) == 0)
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

/**
 * storaged_client_get_all_blocks_for_mdraid:
 * @client: A #StoragedClient.
 * @raid: A #StoragedMDRaid.
 *
 * Gets all RAID devices (e.g. <filename>/dev/md0</filename> and <filename>/dev/md1</filename>) for @raid.
 *
 * This is usually only useful in <ulink
 * url="http://en.wikipedia.org/wiki/Split-brain_(computing)">split-brain
 * situations</ulink> — see storaged_client_get_block_for_mdraid() for
 * an example — and is normally used only to convey the problem in an
 * user interface.
 *
 * Returns: (transfer full) (element-type StoragedBlock): A list of #StoragedBlock instances. The
 *   returned list should be freed with g_list_free() after each
 *   element has been freed with g_object_unref().
 *
 * Since: 2.1
 */
GList *
storaged_client_get_all_blocks_for_mdraid (StoragedClient *client,
                                           StoragedMDRaid *raid)
{
  GList *ret = NULL;
  GList *l, *object_proxies = NULL;
  GDBusObject *raid_object;
  const gchar *raid_objpath;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_MDRAID (raid), NULL);

  raid_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (raid));
  if (raid_object == NULL)
    goto out;

  raid_objpath = g_dbus_object_get_object_path (raid_object);

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;

      block = storaged_object_get_block (object);
      if (block == NULL)
        continue;

      /* ignore partitions */
      if (storaged_object_peek_partition (object) != NULL)
        continue;

      if (g_strcmp0 (storaged_block_get_mdraid (block), raid_objpath) == 0)
        {
          ret = g_list_prepend (ret, block);
        }
      else
        {
          g_object_unref (block);
        }
    }

 out:
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  ret = g_list_reverse (ret);
  return ret;
}

/**
 * storaged_client_get_members_for_mdraid:
 * @client: A #StoragedClient.
 * @raid: A #StoragedMDRaid.
 *
 * Gets the physical block devices that are part of @raid.
 *
 * Returns: (transfer full) (element-type StoragedBlock): A list of #StoragedBlock instances. The
 *   returned list should be freed with g_list_free() after each
 *   element has been freed with g_object_unref().
 *
 * Since: 2.1
 */
GList *
storaged_client_get_members_for_mdraid (StoragedClient *client,
                                        StoragedMDRaid *raid)
{
  GList *ret = NULL;
  GList *l, *object_proxies = NULL;
  GDBusObject *raid_object;
  const gchar *raid_objpath;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_MDRAID (raid), NULL);

  raid_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (raid));
  if (raid_object == NULL)
    goto out;

  raid_objpath = g_dbus_object_get_object_path (raid_object);

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedBlock *block;

      block = storaged_object_get_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (storaged_block_get_mdraid_member (block), raid_objpath) == 0)
        {
          ret = g_list_prepend (ret, block); /* adopts reference to block */
        }
      else
        {
          g_object_unref (block);
        }
    }

 out:
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_item (gchar **items_str,
          const gchar *item)
{
  gchar *orig = *items_str;
  if (*items_str == NULL)
    {
      *items_str = g_strdup (item);
      g_free (orig);
    }
  else
    {
      *items_str = g_strdup_printf ("%s, %s", orig, item);
      g_free (orig);
    }
}

/**
 * storaged_client_get_partition_info:
 * @client: A #StoragedClient.
 * @partition: # #StoragedPartition.
 *
 * Gets information about @partition that is suitable to present in an
 * user interface in a single line of text.
 *
 * The returned string is localized and includes things like the
 * partition type, flags (if any) and name (if any).
 *
 * Returns: (transfer full): A string that should be freed with g_free().
 */
gchar *
storaged_client_get_partition_info (StoragedClient    *client,
                                    StoragedPartition *partition)
{
  gchar *ret = NULL;
  const gchar *type_str = NULL;
  gchar *flags_str = NULL;
  StoragedPartitionTable *table = NULL;
  guint64 flags;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_PARTITION (partition), NULL);

  table = storaged_client_get_partition_table (client, partition);
  if (table == NULL)
    goto out;

  flags = storaged_partition_get_flags (partition);
  if (g_strcmp0 (storaged_partition_table_get_type_ (table), "dos") == 0)
    {
      if (flags & 0x80)
        {
          /* Translators: Corresponds to the DOS/Master-Boot-Record "bootable" flag for a partition */
          add_item (&flags_str, C_("dos-part-flag", "Bootable"));
        }
    }
  else if (g_strcmp0 (storaged_partition_table_get_type_ (table), "gpt") == 0)
    {
      if (flags & (1ULL<<0))
        {
          /* Translators: Corresponds to the GPT "system" flag for a partition,
           * see http://en.wikipedia.org/wiki/GUID_Partition_Table
           */
          add_item (&flags_str, C_("gpt-part-flag", "System"));
        }
      if (flags & (1ULL<<2))
        {
          /* Translators: Corresponds to the GPT "legacy bios bootable" flag for a partition,
           * see http://en.wikipedia.org/wiki/GUID_Partition_Table
           */
          add_item (&flags_str, C_("gpt-part-flag", "Legacy BIOS Bootable"));
        }
      if (flags & (1ULL<<60))
        {
          /* Translators: Corresponds to the GPT "read-only" flag for a partition,
           * see http://en.wikipedia.org/wiki/GUID_Partition_Table
           */
          add_item (&flags_str, C_("gpt-part-flag", "Read-only"));
        }
      if (flags & (1ULL<<62))
        {
          /* Translators: Corresponds to the GPT "hidden" flag for a partition,
           * see http://en.wikipedia.org/wiki/GUID_Partition_Table
           */
          add_item (&flags_str, C_("gpt-part-flag", "Hidden"));
        }
      if (flags & (1ULL<<63))
        {
          /* Translators: Corresponds to the GPT "no automount" flag for a partition,
           * see http://en.wikipedia.org/wiki/GUID_Partition_Table
           */
          add_item (&flags_str, C_("gpt-part-flag", "No Automount"));
        }
    }

  type_str = storaged_client_get_partition_type_for_display (client,
                                                             storaged_partition_table_get_type_ (table),
                                                             storaged_partition_get_type_ (partition));
  if (type_str == NULL)
    type_str = storaged_partition_get_type_ (partition);

  if (flags_str != NULL)
    {
      /* Translators: Partition info. First %s is the type, second %s is a list of flags */
      ret = g_strdup_printf (C_("partition-info", "%s (%s)"), type_str, flags_str);
    }
  else
    {
      ret = g_strdup (type_str);
    }

  if (ret == NULL || strlen (ret) == 0)
    {
      g_free (ret);
      /* Translators: The Partition info when unknown */
      ret = g_strdup (C_("partition-info", "Unknown"));
    }

  g_free (flags_str);
  g_object_unref (table);
 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_client_get_cleartext_block:
 * @client: A #StoragedClient.
 * @block: A #StoragedBlock.
 *
 * If @block is an unlocked encrypted device, gets the cleartext device.
 *
 * Returns: (transfer full): A #StoragedBlock or %NULL. Free with
 * g_object_unref() when done with it.
 */
StoragedBlock *
storaged_client_get_cleartext_block (StoragedClient  *client,
                                     StoragedBlock   *block)
{
  StoragedBlock *ret = NULL;
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
      StoragedObject *iter_object = STORAGED_OBJECT (l->data);
      StoragedBlock *iter_block;

      iter_block = storaged_object_peek_block (iter_object);
      if (iter_block == NULL)
        continue;

      if (g_strcmp0 (storaged_block_get_crypto_backing_device (iter_block), object_path) == 0)
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
 * storaged_client_get_drive_siblings:
 * @client: A #StoragedClient
 * @drive: A #StoragedDrive.
 *
 * Gets all siblings for @drive.
 *
 * Returns: (transfer full) (element-type StoragedDrive): A list of #StoragedDrive instances. The
 *   returned list should be freed with g_list_free() after each element has been
 *   freed with g_object_unref().
 *
 * Since: 2.1
 */
GList *
storaged_client_get_drive_siblings  (StoragedClient  *client,
                                     StoragedDrive   *drive)
{
  GList *ret = NULL;
  const gchar *sibling_id = NULL;
  GList *l, *object_proxies = NULL;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_DRIVE (drive), NULL);

  sibling_id = storaged_drive_get_sibling_id (drive);
  if (sibling_id == NULL || strlen (sibling_id) == 0)
    goto out;

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedDrive *iter_drive;

      iter_drive = storaged_object_get_drive (object);
      if (iter_drive == NULL)
        continue;

      if (iter_drive != drive &&
          g_strcmp0 (storaged_drive_get_sibling_id (iter_drive), sibling_id) == 0)
        {
          ret = g_list_prepend (ret, g_object_ref (iter_drive));
        }

      g_object_unref (iter_drive);
    }
  ret = g_list_reverse (ret);
 out:
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_client_get_partitions:
 * @client: A #StoragedClient.
 * @table: A #StoragedPartitionTable.
 *
 * Gets all partitions of @table.
 *
 * Returns: (transfer full) (element-type StoragedPartition): A list of #StoragedPartition instances. The
 *   returned list should be freed with g_list_free() after each
 *   element has been freed with g_object_unref().
 */
GList *
storaged_client_get_partitions (StoragedClient         *client,
                                StoragedPartitionTable *table)
{
  GList *ret = NULL;
  GDBusObject *table_object;
  const gchar *table_object_path;
  GList *l, *object_proxies = NULL;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_PARTITION_TABLE (table), NULL);

  table_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (table));
  if (table_object == NULL)
    goto out;
  table_object_path = g_dbus_object_get_object_path (table_object);

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedPartition *partition;

      partition = storaged_object_get_partition (object);
      if (partition == NULL)
        continue;

      if (g_strcmp0 (storaged_partition_get_table (partition), table_object_path) == 0)
        ret = g_list_prepend (ret, g_object_ref (partition));

      g_object_unref (partition);
    }
  ret = g_list_reverse (ret);
 out:
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

/**
 * storaged_client_get_partition_table:
 * @client: A #StoragedClient.
 * @partition: A #StoragedPartition.
 *
 * Gets the #StoragedPartitionTable corresponding to @partition.
 *
 * Returns: (transfer full): A #StoragedPartitionTable. Free with g_object_unref().
 */
StoragedPartitionTable *
storaged_client_get_partition_table (StoragedClient     *client,
                                     StoragedPartition  *partition)
{
  StoragedPartitionTable *ret = NULL;
  StoragedObject *object;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_PARTITION (partition), NULL);

  object = storaged_client_get_object (client, storaged_partition_get_table (partition));
  if (object == NULL)
    goto out;

  ret = storaged_object_get_partition_table (object);
  g_object_unref (object);

 out:
  return ret;
}

/**
 * storaged_client_get_loop_for_block:
 * @client: A #StoragedClient.
 * @block: A #StoragedBlock.
 *
 * Gets the corresponding loop interface for @block.
 *
 * This only works if @block itself is a loop device or a partition of
 * a loop device.
 *
 * Returns: (transfer full): A #StoragedLoop or %NULL. Free with g_object_unref().
 */
StoragedLoop *
storaged_client_get_loop_for_block (StoragedClient  *client,
                                    StoragedBlock   *block)
{
  StoragedPartition *partition;
  StoragedObject *object = NULL;
  StoragedLoop *ret = NULL;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_BLOCK (block), NULL);

  object = (StoragedObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (block));
  if (object == NULL)
    goto out;

  ret = storaged_object_get_loop (object);
  if (ret != NULL)
    goto out;

  /* Could be we're a partition of a loop device */
  partition = storaged_object_get_partition (object);
  if (partition != NULL)
    {
      StoragedPartitionTable *partition_table;
      partition_table = storaged_client_get_partition_table (client, partition);
      if (partition_table != NULL)
        {
          StoragedObject *partition_table_object;
          partition_table_object = (StoragedObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (partition_table));
          if (partition_table_object != NULL)
            {
              ret = storaged_object_get_loop (STORAGED_OBJECT (partition_table_object));
              g_object_unref (partition_table_object);
            }
          g_object_unref (partition_table);
        }
      g_object_unref (partition);
    }

 out:
  g_clear_object (&object);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_client_get_jobs_for_object:
 * @client: A #StoragedClient.
 * @object: A #StoragedObject.
 *
 * Gets all the #StoragedJob instances that reference @object, if any.
 *
 * Returns: (transfer full) (element-type StoragedJob): A list of #StoragedJob instances. The
 *   returned list should be freed with g_list_free() after each
 *   element has been freed with g_object_unref().
 */
GList *
storaged_client_get_jobs_for_object (StoragedClient  *client,
                                     StoragedObject  *object)
{
  GList *ret = NULL;
  const gchar *object_path;
  GList *l, *object_proxies = NULL;

  /* TODO: this is probably slow. Can optimize by maintaining a hash-table from object path to StoragedJob* */

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);
  g_return_val_if_fail (STORAGED_IS_OBJECT (object), NULL);

  object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));

  object_proxies = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      StoragedObject *job_object = STORAGED_OBJECT (l->data);
      StoragedJob *job;

      job = storaged_object_get_job (job_object);
      if (job != NULL)
        {
          const gchar *const *object_paths;
          guint n;
          object_paths = storaged_job_get_objects (job);
          for (n = 0; object_paths != NULL && object_paths[n] != NULL; n++)
            {
              if (g_strcmp0 (object_paths[n], object_path) == 0)
                ret = g_list_prepend (ret, g_object_ref (job));
            }
          g_object_unref (job);
        }
    }
  ret = g_list_reverse (ret);

  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
maybe_emit_changed_now (StoragedClient *client)
{
  if (client->changed_timeout_source == NULL)
    goto out;

  g_source_destroy (client->changed_timeout_source);
  client->changed_timeout_source = NULL;

  g_signal_emit (client, signals[CHANGED_SIGNAL], 0);

 out:
  ;
}


static gboolean
on_changed_timeout (gpointer user_data)
{
  StoragedClient *client = STORAGED_CLIENT (user_data);
  client->changed_timeout_source = NULL;
  g_signal_emit (client, signals[CHANGED_SIGNAL], 0);
  return FALSE; /* remove source */
}

/**
 * storaged_client_queue_changed:
 * @client: A #StoragedClient.
 *
 * Queues up a #StoragedClient::changed signal and rate-limit it. See
 * the documentation for the #StoragedClient::changed property for more
 * information.
 *
 * Since: 2.1
 */
void
storaged_client_queue_changed (StoragedClient *client)
{
  g_return_if_fail (STORAGED_IS_CLIENT (client));

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
  StoragedClient *client = STORAGED_CLIENT (user_data);
  GList *interfaces, *l;

  interfaces = g_dbus_object_get_interfaces (object);
  for (l = interfaces; l != NULL; l = l->next)
    {
      init_interface_proxy (client, G_DBUS_PROXY (l->data));
    }
  g_list_foreach (interfaces, (GFunc) g_object_unref, NULL);
  g_list_free (interfaces);

  storaged_client_queue_changed (client);
}

static void
on_object_removed (GDBusObjectManager  *manager,
                   GDBusObject         *object,
                   gpointer             user_data)
{
  StoragedClient *client = STORAGED_CLIENT (user_data);
  storaged_client_queue_changed (client);
}

static void
init_interface_proxy (StoragedClient *client,
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
  StoragedClient *client = STORAGED_CLIENT (user_data);

  init_interface_proxy (client, G_DBUS_PROXY (interface));

  storaged_client_queue_changed (client);
}

static void
on_interface_removed (GDBusObjectManager  *manager,
                      GDBusObject         *object,
                      GDBusInterface      *interface,
                      gpointer             user_data)
{
  StoragedClient *client = STORAGED_CLIENT (user_data);
  storaged_client_queue_changed (client);
}

static void
on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                       GDBusObjectProxy           *object_proxy,
                                       GDBusProxy                 *interface_proxy,
                                       GVariant                   *changed_properties,
                                       const gchar *const         *invalidated_properties,
                                       gpointer                    user_data)
{
  StoragedClient *client = STORAGED_CLIENT (user_data);
  storaged_client_queue_changed (client);
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
      /* Translators: SI prefix and standard unit symbol, translate cautiously (or not at all) */
      unit = C_("byte-size-pow2", "KiB");
    }
  else if (size < GIBIBYTE_FACTOR)
    {
      displayed_size = (double) size / MEBIBYTE_FACTOR;
      /* Translators: SI prefix and standard unit symbol, translate cautiously (or not at all) */
      unit = C_("byte-size-pow2", "MiB");
    }
  else if (size < TEBIBYTE_FACTOR)
    {
      displayed_size = (double) size / GIBIBYTE_FACTOR;
      /* Translators: SI prefix and standard unit symbol, translate cautiously (or not at all) */
      unit = C_("byte-size-pow2", "GiB");
    }
  else
    {
      displayed_size = (double) size / TEBIBYTE_FACTOR;
      /* Translators: SI prefix and standard unit symbol, translate cautiously (or not at all) */
      unit = C_("byte-size-pow2", "TiB");
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
      /* Translators: SI prefix and standard unit symbol, translate cautiously (or not at all) */
      unit = C_("byte-size-pow10", "KB");
    }
  else if (size < GIGABYTE_FACTOR)
    {
      displayed_size = (double) size / MEGABYTE_FACTOR;
      /* Translators: SI prefix and standard unit symbol, translate cautiously (or not at all) */
      unit = C_("byte-size-pow10", "MB");
    }
  else if (size < TERABYTE_FACTOR)
    {
      displayed_size = (double) size / GIGABYTE_FACTOR;
      /* Translators: SI prefix and standard unit symbol, translate cautiously (or not at all) */
      unit = C_("byte-size-pow10", "GB");
    }
  else
    {
      displayed_size = (double) size / TERABYTE_FACTOR;
      /* Translators: SI prefix and standard unit symbol, translate cautiously (or not at all) */
      unit = C_("byte-size-pow10", "TB");
    }

  if (displayed_size < 10.0)
    digits = 1;
  else
    digits = 0;

  str = g_strdup_printf ("%.*f %s", digits, displayed_size, unit);

  return str;
}

/**
 * storaged_client_get_size_for_display:
 * @client: A #StoragedClient.
 * @size: Size in bytes
 * @use_pow2: Whether power-of-two units should be used instead of power-of-ten units.
 * @long_string: Whether to produce a long string.
 *
 * Utility function to get a human-readable string that represents @size.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
storaged_client_get_size_for_display (StoragedClient  *client,
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
           * the second %s is the size as a number e.g. '65,536' (always > 1)
           */
          str = g_strdup_printf (C_("byte-size-pow2", "%s (%s bytes)"), pow2_str, size_str);
          g_free (pow2_str);
        }
      else
        {
          gchar *pow10_str;
          pow10_str = get_pow10_size (size);
          /* Translators: The first %s is the size in power-of-10 units, e.g. '100 kB'
           * the second %s is the size as a number e.g. '100,000' (always > 1)
           */
          str = g_strdup_printf (C_("byte-size-pow10", "%s (%s bytes)"), pow10_str, size_str);
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
 * storaged_client_get_media_compat_for_display:
 * @client: A #StoragedClient.
 * @media_compat: An array of media types.
 *
 * Gets a human-readable string of the media described by
 * @media_compat. The returned information is localized.
 *
 * Returns: A string that should be freed with g_free() or %NULL if
 * unknown.
 */
gchar *
storaged_client_get_media_compat_for_display (StoragedClient       *client,
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
          media_name = C_("media", "CompactFlash");
        }
      else if (g_strcmp0 (media, "flash_ms") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = C_("media", "MemoryStick");
        }
      else if (g_strcmp0 (media, "flash_sm") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = C_("media", "SmartMedia");
        }
      else if (g_strcmp0 (media, "flash_sd") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = C_("media", "SecureDigital");
        }
      else if (g_strcmp0 (media, "flash_sdhc") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = C_("media", "SD High Capacity");
        }
      else if (g_strcmp0 (media, "floppy") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = C_("media", "Floppy");
        }
      else if (g_strcmp0 (media, "floppy_zip") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = C_("media", "Zip");
        }
      else if (g_strcmp0 (media, "floppy_jaz") == 0)
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = C_("media", "Jaz");
        }
      else if (g_str_has_prefix (media, "flash"))
        {
          /* Translators: This word is used to describe the media inserted into a device */
          media_name = C_("media", "Flash");
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
      g_string_append (result, C_("disc-type", "CD"));
    }
  if (optical_dvd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, C_("disc-type", "DVD"));
    }
  if (optical_bd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, C_("disc-type", "Blu-Ray"));
    }
  if (optical_hddvd)
    {
      if (result->len > 0)
        g_string_append_c (result, '/');
      /* Translators: This word is used to describe the optical disc type, it may appear
       * in a slash-separated list e.g. 'CD/DVD/Blu-Ray'
       */
      g_string_append (result, C_("disc-type", "HDDVD"));
    }

  if (result->len > 0)
    return g_string_free (result, FALSE);

  g_string_free (result, TRUE);
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static const struct
{
  const gchar *usage;
  const gchar *type;
  const gchar *version;
  const gchar *long_name;
  const gchar *short_name;
} id_type[] =
{
  {"filesystem", "vfat",              "FAT12", NC_("fs-type", "FAT (12-bit version)"),              NC_("fs-type", "FAT")},
  {"filesystem", "vfat",              "FAT16", NC_("fs-type", "FAT (16-bit version)"),              NC_("fs-type", "FAT")},
  {"filesystem", "vfat",              "FAT32", NC_("fs-type", "FAT (32-bit version)"),              NC_("fs-type", "FAT")},
  {"filesystem", "vfat",              "*",     NC_("fs-type", "FAT (version %s)"),                  NC_("fs-type", "FAT")},
  {"filesystem", "vfat",              NULL,    NC_("fs-type", "FAT"),                               NC_("fs-type", "FAT")},
  {"filesystem", "ntfs",              "*",     NC_("fs-type", "NTFS (version %s)"),                 NC_("fs-type", "NTFS")},
  {"filesystem", "ntfs",              NULL,    NC_("fs-type", "NTFS"),                              NC_("fs-type", "NTFS")},
  {"filesystem", "hfs",               NULL,    NC_("fs-type", "HFS"),                               NC_("fs-type", "HFS")},
  {"filesystem", "hfsplus",           NULL,    NC_("fs-type", "HFS+"),                              NC_("fs-type", "HFS+")},
  {"filesystem", "ext2",              "*",     NC_("fs-type", "Ext2 (version %s)"),                 NC_("fs-type", "Ext2")},
  {"filesystem", "ext2",              NULL,    NC_("fs-type", "Ext2"),                              NC_("fs-type", "Ext2")},
  {"filesystem", "ext3",              "*",     NC_("fs-type", "Ext3 (version %s)"),                 NC_("fs-type", "Ext3")},
  {"filesystem", "ext3",              NULL,    NC_("fs-type", "Ext3"),                              NC_("fs-type", "Ext3")},
  {"filesystem", "ext4",              "*",     NC_("fs-type", "Ext4 (version %s)"),                 NC_("fs-type", "Ext4")},
  {"filesystem", "ext4",              NULL,    NC_("fs-type", "Ext4"),                              NC_("fs-type", "Ext4")},
  {"filesystem", "jdb",               "*",     NC_("fs-type", "Journal for Ext (version %s)"),      NC_("fs-type", "JDB")},
  {"filesystem", "jdb",               "*",     NC_("fs-type", "Journal for Ext"),                   NC_("fs-type", "JDB")},
  {"filesystem", "xfs",               "*",     NC_("fs-type", "XFS (version %s)"),                  NC_("fs-type", "XFS")},
  {"filesystem", "xfs",               NULL,    NC_("fs-type", "XFS"),                               NC_("fs-type", "XFS")},
  /* TODO: No ID_FS_VERSION yet for btrfs... */
  {"filesystem", "btrfs",             NULL,    NC_("fs-type", "Btrfs"),                             NC_("fs-type", "Btrfs")},
  {"filesystem", "iso9660",           "*",     NC_("fs-type", "ISO 9660 (version %s)"),             NC_("fs-type", "ISO9660")},
  {"filesystem", "iso9660",           NULL,    NC_("fs-type", "ISO 9660"),                          NC_("fs-type", "ISO9660")},
  {"filesystem", "udf",               "*",     NC_("fs-type", "UDF (version %s)"),                  NC_("fs-type", "UDF")},
  {"filesystem", "udf",               NULL,    NC_("fs-type", "UDF"),                               NC_("fs-type", "UDF")},
  {"filesystem", "exfat",             NULL,    NC_("fs-type", "exFAT"),                             NC_("fs-type", "exFAT")},
  {"filesystem", "exfat",             "*",     NC_("fs-type", "exFAT (version %s)"),                NC_("fs-type", "exFAT")},
  {"other",      "swap",              "*",     NC_("fs-type", "Swap (version %s)"),                 NC_("fs-type", "Swap")},
  {"other",      "swap",              NULL,    NC_("fs-type", "Swap"),                              NC_("fs-type", "Swap")},
  {"raid",       "LVM2_member",       "*",     NC_("fs-type", "LVM2 Physical Volume (%s)"),         NC_("fs-type", "LVM2 PV")},
  {"raid",       "LVM2_member",       NULL,    NC_("fs-type", "LVM2 Physical Volume"),              NC_("fs-type", "LVM2 PV")},
  {"raid",       "linux_raid_member", "*",     NC_("fs-type", "Linux RAID Member (version %s)"),    NC_("fs-type", "Linux RAID Member")},
  {"raid",       "linux_raid_member", NULL,    NC_("fs-type", "Linux RAID Member"),                 NC_("fs-type", "Linux RAID Member")},
  {"raid",       "zfs_member",        "*",     NC_("fs-type", "ZFS Device (ZPool version %s)"),     NC_("fs-type", "ZFS (v%s)")},
  {"raid",       "zfs_member",        NULL,    NC_("fs-type", "ZFS Device"),                        NC_("fs-type", "ZFS")},
  {"raid",       "isw_raid_member",   "*",     NC_("fs-type", "Intel Rapid Storage Technology enterprise RAID Member (version %s)"), NC_("fs-type", "Intel RSTe RAID Member (%s)")},
  {"raid",       "isw_raid_member",   NULL,    NC_("fs-type", "Intel Rapid Storage Technology enterprise RAID Member"),          NC_("fs-type", "Intel RSTe RAID Member")},
  {"crypto",     "crypto_LUKS",       "*",     NC_("fs-type", "LUKS Encryption (version %s)"),      NC_("fs-type", "LUKS")},
  {"crypto",     "crypto_LUKS",       NULL,    NC_("fs-type", "LUKS Encryption"),                   NC_("fs-type", "LUKS")},
  {"filesystem", "VMFS",              "*",     NC_("fs-type", "VMFS (version %s)"),                 NC_("fs-type", "VMFS (v%s)")},
  {"filesystem", "VMFS",              NULL,    NC_("fs-type", "VMFS"),                              NC_("fs-type", "VMFS")},
  {"raid",       "VMFS_volume_member", "*",    NC_("fs-type", "VMFS Volume Member (version %s)"),   NC_("fs-type", "VMFS Member (v%s)")},
  {"raid",       "VMFS_volume_member", NULL,   NC_("fs-type", "VMFS Volume Member"),                NC_("fs-type", "VMFS Member")},
  {NULL, NULL, NULL, NULL}
};

/**
 * storaged_client_get_id_for_display:
 * @client: A #StoragedClient.
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
storaged_client_get_id_for_display (StoragedClient *client,
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
                ret = g_strdup (g_dpgettext2 (GETTEXT_PACKAGE, "fs-type", id_type[n].long_name));
              else
                ret = g_strdup (g_dpgettext2 (GETTEXT_PACKAGE, "fs-type", id_type[n].short_name));
              goto out;
            }
          else if ((g_strcmp0 (id_type[n].version, version) == 0 && strlen (version) > 0) ||
                   (g_strcmp0 (id_type[n].version, "*") == 0 && strlen (version) > 0))
            {
              /* we know better than the compiler here */
#if defined( __GNUC_PREREQ) || defined(__clang__)
# if __GNUC_PREREQ(4,6) || __clang__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wformat-nonliteral"
# endif
#endif
              if (long_string)
                ret = g_strdup_printf (g_dpgettext2 (GETTEXT_PACKAGE, "fs-type", id_type[n].long_name), version);
              else
                ret = g_strdup_printf (g_dpgettext2 (GETTEXT_PACKAGE, "fs-type", id_type[n].short_name), version);
              goto out;
#if defined( __GNUC_PREREQ) || defined(__clang__)
# if __GNUC_PREREQ(4,6) || __clang__
#  pragma GCC diagnostic pop
# endif
#endif
            }
        }
    }

  if (long_string)
    {
      if (strlen (version) > 0)
        {
          /* Translators: Shown for unknown filesystem types.
           * First %s is the raw filesystem type obtained from udev, second %s is version.
           */
          ret = g_strdup_printf (C_("fs-type", "Unknown (%s %s)"), type, version);
        }
      else
        {
          if (strlen (type) > 0)
            {
              /* Translators: Shown for unknown filesystem types.
               * First %s is the raw filesystem type obtained from udev.
               */
              ret = g_strdup_printf (C_("fs-type", "Unknown (%s)"), type);
            }
          else
            {
              /* Translators: Shown for unknown filesystem types.
               */
              ret = g_strdup (C_("fs-type", "Unknown"));
            }
        }
    }
  else
    {
      if (strlen (type) > 0)
        {
          ret = g_strdup (type);
        }
      else
        {
          /* Translators: Shown for unknown filesystem types.
           */
          ret = g_strdup (C_("fs-type", "Unknown"));
        }
    }

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static const struct
{
  const gchar *type;
  const gchar *name;
} known_partition_table_types[] =
{
  /* Translators: name of partition table format */
  {"dos", N_("Master Boot Record")},
  /* Translators: name of partition table format */
  {"gpt", N_("GUID Partition Table")},
  /* Translators: name of partition table format */
  {"apm", N_("Apple Partition Map")},
  {NULL, NULL}
};

/**
 * storaged_client_get_partition_table_type_for_display:
 * @client: A #StoragedClient.
 * @partition_table_type: A partition table type e.g. 'dos' or 'gpt'.
 *
 * Gets a human readable localized string for @partition_table_type.
 *
 * Returns: A description of @partition_table_type or %NULL.
 */
const gchar *
storaged_client_get_partition_table_type_for_display (StoragedClient  *client,
                                                    const gchar   *partition_table_type)
{
  const gchar *ret = NULL;
  guint n;

  for (n = 0; known_partition_table_types[n].type != NULL; n++)
    {
      if (g_strcmp0 (known_partition_table_types[n].type, partition_table_type) == 0)
        {
          ret = _(known_partition_table_types[n].name);
          goto out;
        }
    }

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static const struct
{
  const gchar *type;
  const gchar *subtype;
  const gchar *name;
} known_partition_table_subtypes[] =
{
  /* Translators: name of partition table format */
  {"dos", "generic",   NC_("partition-subtype", "Generic")},
  {"dos", "linux",     NC_("partition-subtype", "Linux")},
  {"dos", "microsoft", NC_("partition-subtype", "Windows")},
  {"dos", "other",     NC_("partition-subtype", "Other")},

  {"gpt", "generic",   NC_("partition-subtype", "Generic")},
  {"gpt", "linux",     NC_("partition-subtype", "Linux")},
  {"gpt", "microsoft", NC_("partition-subtype", "Windows")},
  {"gpt", "apple",     NC_("partition-subtype", "Mac OS X")},
  {"gpt", "other",     NC_("partition-subtype", "Other")},

  {"apm", "apple",     NC_("partition-subtype", "Mac OS X")},
  {"apm", "microsoft", NC_("partition-subtype", "Windows")},
  {NULL, NULL}
};

/**
 * storaged_client_get_partition_table_subtype_for_display:
 * @client: A #StoragedClient.
 * @partition_table_type: A partition table type e.g. 'dos' or 'gpt'.
 * @partition_table_subtype: A partition table type e.g. 'dos' or 'gpt'.
 *
 * Gets a human readable localized string for @partition_table_type and @partition_table_subtype.
 *
 * Returns: A description of @partition_table_type and @partition_table_subtype or %NULL.
 */
const gchar *
storaged_client_get_partition_table_subtype_for_display (StoragedClient  *client,
                                                         const gchar   *partition_table_type,
                                                         const gchar   *partition_table_subtype)
{
  const gchar *ret = NULL;
  guint n;

  for (n = 0; known_partition_table_subtypes[n].type != NULL; n++)
    {
      if (g_strcmp0 (known_partition_table_subtypes[n].type,    partition_table_type) == 0 &&
          g_strcmp0 (known_partition_table_subtypes[n].subtype, partition_table_subtype) == 0)
        {
          ret = g_dpgettext2 (GETTEXT_PACKAGE, "partition-subtype", known_partition_table_subtypes[n].name);
          goto out;
        }
    }

 out:
  return ret;
}

/**
 * storaged_client_get_partition_table_subtypes:
 * @client: A #StoragedClient.
 * @partition_table_type: A partition table type e.g. 'dos' or 'gpt'.
 *
 * Gets all known subtypes for @partition_table_type.
 *
 * Returns: (transfer container): A %NULL-terminated array of
 * strings. Only the container should be freed with g_free().
 */
const gchar **
storaged_client_get_partition_table_subtypes (StoragedClient   *client,
                                              const gchar    *partition_table_type)
{
  guint n;
  GPtrArray *p;

  p = g_ptr_array_new();
  for (n = 0; known_partition_table_subtypes[n].type != NULL; n++)
    {
      if (g_strcmp0 (known_partition_table_subtypes[n].type, partition_table_type) == 0)
        {
          g_ptr_array_add (p, (gpointer) known_partition_table_subtypes[n].subtype);
        }
    }
  g_ptr_array_add (p, NULL);

  return (const gchar **) g_ptr_array_free (p, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

/* shorthand */
#define F_SWAP   STORAGED_PARTITION_TYPE_INFO_FLAGS_SWAP
#define F_RAID   STORAGED_PARTITION_TYPE_INFO_FLAGS_RAID
#define F_HIDDEN STORAGED_PARTITION_TYPE_INFO_FLAGS_HIDDEN
#define F_CONLY  STORAGED_PARTITION_TYPE_INFO_FLAGS_CREATE_ONLY
#define F_SYSTEM STORAGED_PARTITION_TYPE_INFO_FLAGS_SYSTEM

static const struct
{
  const gchar *table_type;
  const gchar *table_subtype;
  const gchar *type;
  const gchar *name;
  StoragedPartitionTypeInfoFlags flags;
} known_partition_types[] =
{
  /* see http://en.wikipedia.org/wiki/GUID_Partition_Table */

  /* Not associated with any OS */
  {"gpt", "generic",   "024dee41-33e7-11d3-9d69-0008c781f39f", NC_("part-type", "MBR Partition Scheme"), F_SYSTEM},
  {"gpt", "generic",   "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", NC_("part-type", "EFI System"), F_SYSTEM},
  {"gpt", "generic",   "21686148-6449-6e6f-744e-656564454649", NC_("part-type", "BIOS Boot"), F_SYSTEM},
  /* This is also defined in the Apple and Solaris section */
  {"gpt", "generic",   "6a898cc3-1dd2-11b2-99a6-080020736631", NC_("part-type", "ZFS"), 0},
  /* Extended Boot Partition, see http://www.freedesktop.org/wiki/Specifications/BootLoaderSpec/ */
  {"gpt", "generic",   "bc13c2ff-59e6-4262-a352-b275fd6f7172", NC_("part-type", "Extended Boot Partition"), 0},
  /* Discoverable Linux Partitions, see http://www.freedesktop.org/wiki/Specifications/DiscoverablePartitionsSpec */
  {"gpt", "linux",     "44479540-f297-41b2-9af7-d131d5f0458a", NC_("part-type", "Linux Root Partition (x86)"), 0},
  {"gpt", "linux",     "4f68bce3-e8cd-4db1-96e7-fbcaf984b709", NC_("part-type", "Linux Root Partition (x86_64)"), 0},
  {"gpt", "linux",     "933ac7e1-2eb4-4f13-b844-0e14e2aef915", NC_("part-type", "Linux Home Partition"), 0},
  {"gpt", "linux",     "3b8f8425-20e0-4f3b-907f-1a25a76f98e8", NC_("part-type", "Linux Server Data Partition"), 0},
  /* Linux */
  {"gpt", "linux",     "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f", NC_("part-type", "Linux Swap"), F_SWAP},
  {"gpt", "linux",     "0fc63daf-8483-4772-8e79-3d69d8477de4", NC_("part-type", "Linux Filesystem"), 0},
  {"gpt", "linux",     "a19d880f-05fc-4d3b-a006-743f0f84911e", NC_("part-type", "Linux RAID"), F_RAID},
  {"gpt", "linux",     "e6d6d379-f507-44c2-a23c-238f2a3df928", NC_("part-type", "Linux LVM"), F_RAID},
  {"gpt", "linux",     "8da63339-0007-60c0-c436-083ac8230908", NC_("part-type", "Linux Reserved"), 0},
  /* Microsoft */
  {"gpt", "microsoft", "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", NC_("part-type", "Basic Data"), 0},
  {"gpt", "microsoft", "e3c9e316-0b5c-4db8-817d-f92df00215ae", NC_("part-type", "Microsoft Reserved"), 0},
  {"gpt", "microsoft", "5808c8aa-7e8f-42e0-85d2-e1e90434cfb3", NC_("part-type", "Microsoft LDM metadata"), 0},
  {"gpt", "microsoft", "af9b60a0-1431-4f62-bc68-3311714a69ad", NC_("part-type", "Microsoft LDM data"), 0},
  {"gpt", "microsoft", "de94bba4-06d1-4d40-a16a-bfd50179d6ac", NC_("part-type", "Microsoft Windows Recovery Environment"), 0},
  /* Apple OS X */
  {"gpt", "apple",     "48465300-0000-11aa-aa11-00306543ecac", NC_("part-type", "Apple HFS/HFS+"), 0},
  {"gpt", "apple",     "55465300-0000-11aa-aa11-00306543ecac", NC_("part-type", "Apple UFS"), 0},
  {"gpt", "apple",     "6a898cc3-1dd2-11b2-99a6-080020736631", NC_("part-type", "Apple ZFS"), 0}, /* same as ZFS */
  {"gpt", "apple",     "52414944-0000-11aa-aa11-00306543ecac", NC_("part-type", "Apple RAID"), F_RAID},
  {"gpt", "apple",     "52414944-5f4f-11aa-aa11-00306543ecac", NC_("part-type", "Apple RAID (offline)"), F_RAID},
  {"gpt", "apple",     "426f6f74-0000-11aa-aa11-00306543ecac", NC_("part-type", "Apple Boot"), F_SYSTEM},
  {"gpt", "apple",     "4c616265-6c00-11aa-aa11-00306543ecac", NC_("part-type", "Apple Label"), 0},
  {"gpt", "apple",     "5265636f-7665-11aa-aa11-00306543ecac", NC_("part-type", "Apple TV Recovery"), F_SYSTEM},
  {"gpt", "apple",     "53746f72-6167-11aa-aa11-00306543ecac", NC_("part-type", "Apple Core Storage"), F_RAID},
  /* HP-UX */
  {"gpt", "other",     "75894c1e-3aeb-11d3-b7c1-7b03a0000000", NC_("part-type", "HP-UX Data"), 0},
  {"gpt", "other",     "e2a1e728-32e3-11d6-a682-7b03a0000000", NC_("part-type", "HP-UX Service"), 0},
  /* FreeBSD */
  {"gpt", "other",     "83bd6b9d-7f41-11dc-be0b-001560b84f0f", NC_("part-type", "FreeBSD Boot"), 0},
  {"gpt", "other",     "516e7cb4-6ecf-11d6-8ff8-00022d09712b", NC_("part-type", "FreeBSD Data"), 0},
  {"gpt", "other",     "516e7cb5-6ecf-11d6-8ff8-00022d09712b", NC_("part-type", "FreeBSD Swap"), F_SWAP},
  {"gpt", "other",     "516e7cb6-6ecf-11d6-8ff8-00022d09712b", NC_("part-type", "FreeBSD UFS"), 0},
  {"gpt", "other",     "516e7cb8-6ecf-11d6-8ff8-00022d09712b", NC_("part-type", "FreeBSD Vinum"), F_RAID},
  {"gpt", "other",     "516e7cba-6ecf-11d6-8ff8-00022d09712b", NC_("part-type", "FreeBSD ZFS"), 0},
  /* Solaris */
  {"gpt", "other",     "6a82cb45-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Boot"), 0},
  {"gpt", "other",     "6a85cf4d-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Root"), 0},
  {"gpt", "other",     "6a87c46f-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Swap"), F_SWAP},
  {"gpt", "other",     "6a8b642b-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Backup"), 0},
  {"gpt", "other",     "6a898cc3-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris /usr"), 0}, /* same as ZFS */
  {"gpt", "other",     "6a8ef2e9-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris /var"), 0},
  {"gpt", "other",     "6a90ba39-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris /home"), 0},
  {"gpt", "other",     "6a9283a5-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Alternate Sector"), 0},
  {"gpt", "other",     "6a945a3b-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Reserved"), 0},
  {"gpt", "other",     "6a9630d1-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Reserved (2)"), 0},
  {"gpt", "other",     "6a980767-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Reserved (3)"), 0},
  {"gpt", "other",     "6a96237f-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Reserved (4)"), 0},
  {"gpt", "other",     "6a8d2ac7-1dd2-11b2-99a6-080020736631", NC_("part-type", "Solaris Reserved (5)"), 0},
  /* NetBSD */
  {"gpt", "other",     "49f48d32-b10e-11dc-b99b-0019d1879648", NC_("part-type", "NetBSD Swap"), F_SWAP},
  {"gpt", "other",     "49f48d5a-b10e-11dc-b99b-0019d1879648", NC_("part-type", "NetBSD FFS"), 0},
  {"gpt", "other",     "49f48d82-b10e-11dc-b99b-0019d1879648", NC_("part-type", "NetBSD LFS"), 0},
  {"gpt", "other",     "49f48daa-b10e-11dc-b99b-0019d1879648", NC_("part-type", "NetBSD RAID"), F_RAID},
  {"gpt", "other",     "2db519c4-b10f-11dc-b99b-0019d1879648", NC_("part-type", "NetBSD Concatenated"), 0},
  {"gpt", "other",     "2db519ec-b10f-11dc-b99b-0019d1879648", NC_("part-type", "NetBSD Encrypted"), 0},
  /* VMWare, see http://blogs.vmware.com/vsphere/2011/08/vsphere-50-storage-features-part-7-gpt.html */
  {"gpt", "other",     "aa31e02a-400f-11db-9590-000c2911d1b8", NC_("part-type", "VMWare VMFS"), 0},
  {"gpt", "other",     "9d275380-40ad-11db-bf97-000c2911d1b8", NC_("part-type", "VMWare vmkcore"), 0},
  /* ChromeOS, see http://www.chromium.org/chromium-os/chromiumos-design-docs/disk-format */
  {"gpt", "other",     "cab6e88e-abf3-4102-a07a-d4bb9be3c1d3", NC_("part-type", "ChromeOS Firmware"), 0},
  {"gpt", "other",     "fe3a2a5d-4f32-41a7-b725-accc3285a309", NC_("part-type", "ChromeOS Kernel"), 0},
  {"gpt", "other",     "3cb8e202-3b7e-47dd-8a3c-7ff2a13cfcec", NC_("part-type", "ChromeOS Root Filesystem"), 0},
  {"gpt", "other",     "2e0a753d-9e48-43b0-8337-b15192cb1b5e", NC_("part-type", "ChromeOS Reserved"), 0},
  /* Intel Partition Types */
  /*     FFS = Fast Flash Standby, aka Intel Rapid start  */
  /*     http://downloadmirror.intel.com/22647/eng/Intel%20Rapid%20Start%20Technology%20Deployment%20Guide%20v1.0.pdf */
  {"gpt", "other",     "d3bfe2de-3daf-11df-ba40-e3a556d89593", NC_("part-type", "Intel FFS Reserved"), 0},

  /* see http://developer.apple.com/documentation/mac/devices/devices-126.html
   *     http://lists.apple.com/archives/Darwin-drivers/2003/May/msg00021.html */
  {"apm", "apple",     "Apple_Unix_SVR2", NC_("part-type", "Apple UFS"), 0},
  {"apm", "apple",     "Apple_HFS", NC_("part-type", "Apple HFS/HFS"), 0},
  {"apm", "apple",     "Apple_partition_map", NC_("part-type", "Apple Partition Map"), 0},
  {"apm", "apple",     "Apple_Free", NC_("part-type", "Unused"), 0},
  {"apm", "apple",     "Apple_Scratch", NC_("part-type", "Empty"), 0},
  {"apm", "apple",     "Apple_Driver", NC_("part-type", "Driver"), 0},
  {"apm", "apple",     "Apple_Driver43", NC_("part-type", "Driver 4.3"), 0},
  {"apm", "apple",     "Apple_PRODOS", NC_("part-type", "ProDOS file system"), 0},
  {"apm", "microsoft", "DOS_FAT_12", NC_("part-type", "FAT 12"), 0},
  {"apm", "microsoft", "DOS_FAT_16", NC_("part-type", "FAT 16"), 0},
  {"apm", "microsoft", "DOS_FAT_32", NC_("part-type", "FAT 32"), 0},
  {"apm", "microsoft", "Windows_FAT_16", NC_("part-type", "FAT 16 (Windows)"), 0},
  {"apm", "microsoft", "Windows_FAT_32", NC_("part-type", "FAT 32 (Windows)"), 0},

  /* see http://www.win.tue.nl/~aeb/partitions/partition_types-1.html */
  {"dos", "generic",   "0x05",  NC_("part-type", "Extended"), F_CONLY},
  {"dos", "generic",   "0xee",  NC_("part-type", "EFI GPT"), F_SYSTEM},
  {"dos", "generic",   "0xef",  NC_("part-type", "EFI (FAT-12/16/32)"), F_SYSTEM},
  {"dos", "linux",     "0x82",  NC_("part-type", "Linux swap"), F_SWAP},
  {"dos", "linux",     "0x83",  NC_("part-type", "Linux"), 0},
  {"dos", "linux",     "0x85",  NC_("part-type", "Linux Extended"), F_CONLY},
  {"dos", "linux",     "0x8e",  NC_("part-type", "Linux LVM"), F_RAID},
  {"dos", "linux",     "0xfd",  NC_("part-type", "Linux RAID auto"), F_RAID},
  {"dos", "microsoft", "0x01",  NC_("part-type", "FAT12"), 0},
  {"dos", "microsoft", "0x04",  NC_("part-type", "FAT16 <32M"), 0},
  {"dos", "microsoft", "0x06",  NC_("part-type", "FAT16"), 0},
  {"dos", "microsoft", "0x07",  NC_("part-type", "HPFS/NTFS"), 0},
  {"dos", "microsoft", "0x0b",  NC_("part-type", "W95 FAT32"), 0},
  {"dos", "microsoft", "0x0c",  NC_("part-type", "W95 FAT32 (LBA)"), 0},
  {"dos", "microsoft", "0x0e",  NC_("part-type", "W95 FAT16 (LBA)"), 0},
  {"dos", "microsoft", "0x0f",  NC_("part-type", "W95 Ext d (LBA)"), F_CONLY},
  {"dos", "microsoft", "0x11",  NC_("part-type", "Hidden FAT12"), F_HIDDEN},
  {"dos", "microsoft", "0x14",  NC_("part-type", "Hidden FAT16 <32M"), F_HIDDEN},
  {"dos", "microsoft", "0x16",  NC_("part-type", "Hidden FAT16"), F_HIDDEN},
  {"dos", "microsoft", "0x17",  NC_("part-type", "Hidden HPFS/NTFS"), F_HIDDEN},
  {"dos", "microsoft", "0x1b",  NC_("part-type", "Hidden W95 FAT32"), F_HIDDEN},
  {"dos", "microsoft", "0x1c",  NC_("part-type", "Hidden W95 FAT32 (LBA)"), F_HIDDEN},
  {"dos", "microsoft", "0x1e",  NC_("part-type", "Hidden W95 FAT16 (LBA)"), F_HIDDEN},
  {"dos", "other",     "0x10",  NC_("part-type", "OPUS"), 0},
  {"dos", "other",     "0x12",  NC_("part-type", "Compaq diagnostics"), 0},
  {"dos", "other",     "0x3c",  NC_("part-type", "PartitionMagic"), 0},
  {"dos", "other",     "0x81",  NC_("part-type", "Minix"), 0}, /* cf. http://en.wikipedia.org/wiki/MINIX_file_system */
  {"dos", "other",     "0x84",  NC_("part-type", "Hibernation"), 0},
  {"dos", "other",     "0xa0",  NC_("part-type", "Hibernation"), 0},
  {"dos", "other",     "0xa5",  NC_("part-type", "FreeBSD"), 0},
  {"dos", "other",     "0xa6",  NC_("part-type", "OpenBSD"), 0},
  {"dos", "other",     "0xa8",  NC_("part-type", "Mac OS X"), 0},
  {"dos", "other",     "0xaf",  NC_("part-type", "Mac OS X"), 0},
  {"dos", "other",     "0xbe",  NC_("part-type", "Solaris boot"), 0},
  {"dos", "other",     "0xbf",  NC_("part-type", "Solaris"), 0},
  {"dos", "other",     "0xeb",  NC_("part-type", "BeOS BFS"), 0},
  {"dos", "other",     "0xec",  NC_("part-type", "SkyOS SkyFS"), 0},
  {NULL,  NULL, NULL}
};

/**
 * storaged_client_get_partition_type_infos:
 * @client: A #StoragedClient.
 * @partition_table_type: A partition table type e.g. 'dos' or 'gpt'.
 * @partition_table_subtype: (allow-none): A partition table subtype or %NULL to get all known types.
 *
 * Gets information about all known partition types for @partition_table_type and @partition_table_subtype.
 *
 * Returns: (transfer full) (element-type StoragedPartitionTypeInfo): A list of
 *   #StoragedPartitionTypeInfo instances. The returned list should be freed
 *   with g_list_free() after freeing each element with storaged_partition_type_info_free().
 */
GList *
storaged_client_get_partition_type_infos (StoragedClient   *client,
                                          const gchar    *partition_table_type,
                                          const gchar    *partition_table_subtype)
{
  GList *ret = NULL;
  guint n;

  for (n = 0; known_partition_types[n].name != NULL; n++)
    {
      if (g_strcmp0 (known_partition_types[n].table_type, partition_table_type) == 0 &&
          (partition_table_subtype == NULL ||
           g_strcmp0 (known_partition_types[n].table_subtype, partition_table_subtype) == 0))
        {
          StoragedPartitionTypeInfo *info = storaged_partition_type_info_new ();
          info->table_type    = known_partition_types[n].table_type;
          info->table_subtype = known_partition_types[n].table_subtype;
          info->type          = known_partition_types[n].type;
          info->flags         = known_partition_types[n].flags;
          ret = g_list_prepend (ret, info);
        }
    }
  ret = g_list_reverse (ret);
  return ret;
}

/**
 * storaged_client_get_partition_type_for_display:
 * @client: A #StoragedClient.
 * @partition_table_type: A partitioning type e.g. 'dos' or 'gpt'.
 * @partition_type: A partition type.
 *
 * Gets a human readable localized string for @partiton_table_type and @partition_type.
 *
 * Returns: A description of @partition_type or %NULL if unknown.
 */
const gchar *
storaged_client_get_partition_type_for_display (StoragedClient  *client,
                                                const gchar   *partition_table_type,
                                                const gchar   *partition_type)
{
  const gchar *ret = NULL;
  guint n;

  for (n = 0; known_partition_types[n].name != NULL; n++)
    {
      if (g_strcmp0 (known_partition_types[n].table_type, partition_table_type) == 0 &&
          g_strcmp0 (known_partition_types[n].type, partition_type) == 0)
        {
          ret = g_dpgettext2 (GETTEXT_PACKAGE, "part-type", known_partition_types[n].name);
          goto out;
        }
    }

 out:
  return ret;
}

/**
 * storaged_client_get_partition_type_and_subtype_for_display:
 * @client: A #StoragedClient.
 * @partition_table_type: A partitioning type e.g. 'dos' or 'gpt'.
 * @partition_table_subtype: A partitioning subtype or %NULL.
 * @partition_type: A partition type.
 *
 * Like storaged_client_get_partition_type_for_display() but also takes
 * the partition table subtype into account, if available. This is
 * useful in scenarios where different subtypes is using the same
 * partition type.
 *
 * Returns: A description of @partition_type or %NULL if unknown.
 *
 * Since: 2.1.1
 */
const gchar *
storaged_client_get_partition_type_and_subtype_for_display (StoragedClient  *client,
                                                            const gchar   *partition_table_type,
                                                            const gchar   *partition_table_subtype,
                                                            const gchar   *partition_type)
{
  const gchar *ret = NULL;
  guint n;

  for (n = 0; known_partition_types[n].name != NULL; n++)
    {
      if (g_strcmp0 (known_partition_types[n].table_type, partition_table_type) == 0 &&
          g_strcmp0 (known_partition_types[n].type, partition_type) == 0)
        {
          if (partition_table_subtype != NULL &&
              g_strcmp0 (known_partition_types[n].table_subtype, partition_table_subtype) != 0)
            continue;
          ret = g_dpgettext2 (GETTEXT_PACKAGE, "part-type", known_partition_types[n].name);
          goto out;
        }
    }

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_client_get_job_description:
 * @client: A #StoragedClient.
 * @job: A #StoragedJob.
 *
 * Gets a human-readable and localized text string describing the
 * operation of @job.
 *
 * For known job types, see the documentation for the
 * <link linkend="gdbus-property-org-storaged-Storaged-Job.Operation">Job:Operation</link>
 * D-Bus property.
 *
 * Returns: A string that should be freed with g_free().
 */
gchar *
storaged_client_get_job_description (StoragedClient   *client,
                                     StoragedJob      *job)
{
  static gsize once = 0;
  static GHashTable *hash = NULL;
  const gchar *operation = NULL;
  gchar *ret = NULL;

  g_return_val_if_fail (STORAGED_IS_CLIENT (client), NULL);

  if (g_once_init_enter (&once))
    {
      hash = g_hash_table_new (g_str_hash, g_str_equal);
      g_hash_table_insert (hash, (gpointer) "ata-smart-selftest",   (gpointer) C_("job", "SMART self-test"));
      g_hash_table_insert (hash, (gpointer) "drive-eject",          (gpointer) C_("job", "Ejecting Medium"));
      g_hash_table_insert (hash, (gpointer) "encrypted-unlock",     (gpointer) C_("job", "Unlocking Device"));
      g_hash_table_insert (hash, (gpointer) "encrypted-lock",       (gpointer) C_("job", "Locking Device"));
      g_hash_table_insert (hash, (gpointer) "encrypted-modify",     (gpointer) C_("job", "Modifying Encrypted Device"));
      g_hash_table_insert (hash, (gpointer) "swapspace-start",      (gpointer) C_("job", "Starting Swap Device"));
      g_hash_table_insert (hash, (gpointer) "swapspace-stop",       (gpointer) C_("job", "Stopping Swap Device"));
      g_hash_table_insert (hash, (gpointer) "filesystem-mount",     (gpointer) C_("job", "Mounting Filesystem"));
      g_hash_table_insert (hash, (gpointer) "filesystem-unmount",   (gpointer) C_("job", "Unmounting Filesystem"));
      g_hash_table_insert (hash, (gpointer) "filesystem-modify",    (gpointer) C_("job", "Modifying Filesystem"));
      g_hash_table_insert (hash, (gpointer) "format-erase",         (gpointer) C_("job", "Erasing Device"));
      g_hash_table_insert (hash, (gpointer) "format-mkfs",          (gpointer) C_("job", "Creating Filesystem"));
      g_hash_table_insert (hash, (gpointer) "loop-setup",           (gpointer) C_("job", "Setting Up Loop Device"));
      g_hash_table_insert (hash, (gpointer) "partition-modify",     (gpointer) C_("job", "Modifying Partition"));
      g_hash_table_insert (hash, (gpointer) "partition-delete",     (gpointer) C_("job", "Deleting Partition"));
      g_hash_table_insert (hash, (gpointer) "partition-create",     (gpointer) C_("job", "Creating Partition"));
      g_hash_table_insert (hash, (gpointer) "cleanup",              (gpointer) C_("job", "Cleaning Up"));
      g_hash_table_insert (hash, (gpointer) "ata-secure-erase",     (gpointer) C_("job", "ATA Secure Erase"));
      g_hash_table_insert (hash, (gpointer) "ata-enhanced-secure-erase", (gpointer) C_("job", "ATA Enhanced Secure Erase"));
      g_hash_table_insert (hash, (gpointer) "md-raid-stop",         (gpointer) C_("job", "Stopping RAID Array"));
      g_hash_table_insert (hash, (gpointer) "md-raid-start",        (gpointer) C_("job", "Starting RAID Array"));
      g_hash_table_insert (hash, (gpointer) "md-raid-fault-device", (gpointer) C_("job", "Marking Device as Faulty"));
      g_hash_table_insert (hash, (gpointer) "md-raid-remove-device",(gpointer) C_("job", "Removing Device from Array"));
      g_hash_table_insert (hash, (gpointer) "md-raid-add-device",   (gpointer) C_("job", "Adding Device to Array"));
      g_hash_table_insert (hash, (gpointer) "md-raid-set-bitmap",   (gpointer) C_("job", "Setting Write-Intent Bitmap"));
      g_hash_table_insert (hash, (gpointer) "md-raid-create",       (gpointer) C_("job", "Creating RAID Array"));
      g_once_init_leave (&once, (gsize) 1);
    }

  operation = storaged_job_get_operation (job);
  if (operation != NULL)
    ret = g_strdup (g_hash_table_lookup (hash, operation));
  if (ret == NULL)
    ret = g_strdup_printf (C_("unknown-job", "Unknown (%s)"), storaged_job_get_operation (job));

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static StoragedPartitionTypeInfo *
storaged_partition_type_info_new (void)
{
  StoragedPartitionTypeInfo *ret;
  ret = g_slice_new0 (StoragedPartitionTypeInfo);
  return ret;
}

static StoragedPartitionTypeInfo *
storaged_partition_type_info_copy (StoragedPartitionTypeInfo  *info)
{
  StoragedPartitionTypeInfo *ret;
  ret = storaged_partition_type_info_new ();
  memcpy (ret, info, sizeof (StoragedPartitionTypeInfo));
  return ret;
}

/**
 * storaged_partition_type_info_free:
 * @info: A #StoragedPartitionTypeInfo.
 *
 * Frees @info.
 */
void
storaged_partition_type_info_free (StoragedPartitionTypeInfo  *info)
{
  g_slice_free (StoragedPartitionTypeInfo, info);
}

G_DEFINE_BOXED_TYPE (StoragedPartitionTypeInfo, storaged_partition_type_info, storaged_partition_type_info_copy, storaged_partition_type_info_free);
