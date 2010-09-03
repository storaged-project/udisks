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

#include "udisksdaemon.h"

typedef struct _UDisksDaemonClass   UDisksDaemonClass;

struct _UDisksDaemon
{
  GObject parent_instance;
  GDBusConnection *connection;
  GDBusObjectManager *object_manager;
};

struct _UDisksDaemonClass
{
  GObjectClass parent_class;
};

enum
{
  PROP_0,
  PROP_CONNECTION,
  PROP_OBJECT_MANAGER
};

G_DEFINE_TYPE (UDisksDaemon, udisks_daemon, G_TYPE_OBJECT);

static void
udisks_daemon_finalize (GObject *object)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);

  g_object_unref (daemon->connection);

  if (G_OBJECT_CLASS (udisks_daemon_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_daemon_parent_class)->finalize (object);
}

static void
udisks_daemon_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, udisks_daemon_get_connection (daemon));
      break;

    case PROP_OBJECT_MANAGER:
      g_value_set_object (value, udisks_daemon_get_object_manager (daemon));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_daemon_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      g_assert (daemon->connection == NULL);
      daemon->connection = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_daemon_init (UDisksDaemon *daemon)
{
}

static void
udisks_daemon_constructed (GObject *object)
{
  UDisksDaemon *daemon = UDISKS_DAEMON (object);

  daemon->object_manager = g_dbus_object_manager_new (daemon->connection, "/org/freedesktop/UDisks");

  if (G_OBJECT_CLASS (udisks_daemon_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_daemon_parent_class)->constructed (object);
}


static void
udisks_daemon_class_init (UDisksDaemonClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_daemon_finalize;
  gobject_class->constructed  = udisks_daemon_constructed;
  gobject_class->set_property = udisks_daemon_set_property;
  gobject_class->get_property = udisks_daemon_get_property;

  /**
   * UDisksDaemon:connection:
   *
   * The #GDBusConnection the daemon is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CONNECTION,
                                   g_param_spec_object ("connection",
                                                        "Connection",
                                                        "The D-Bus connection the daemon is for",
                                                        G_TYPE_DBUS_CONNECTION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksDaemon:object-manager:
   *
   * The #GDBusObjectManager used by the daemon
   */
  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_MANAGER,
                                   g_param_spec_object ("object-manager",
                                                        "Object Manager",
                                                        "The D-Bus Object Manager used by the daemon",
                                                        G_TYPE_DBUS_OBJECT_MANAGER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_daemon_new:
 * @connection: A #GDBusConnection.
 *
 * Create a new daemon object for exporting objects on @connection.
 *
 * Returns: A #UDisksDaemon object. Free with g_object_unref().
 */
UDisksDaemon *
udisks_daemon_new (GDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  return UDISKS_DAEMON (g_object_new (UDISKS_TYPE_DAEMON,
                                      "connection", connection,
                                      NULL));
}

/**
 * udisks_daemon_get_connection:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the D-Bus connection used by @daemon.
 *
 * Returns: A #GDBusConnection. Do not free, the object is owned by @daemon.
 */
GDBusConnection *
udisks_daemon_get_connection (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->connection;
}

/**
 * udisks_daemon_get_object_manager:
 * @daemon: A #UDisksDaemon.
 *
 * Gets the D-Bus object manager used by @daemon.
 *
 * Returns: A #GDBusObjectManager. Do not free, the object is owned by @daemon.
 */
GDBusObjectManager *
udisks_daemon_get_object_manager (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return daemon->object_manager;
}

