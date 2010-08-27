/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
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

#include <gudev/gudev.h>

#include "linuxdaemon.h"
#include "linuxdevice.h"

struct _LinuxDaemonPrivate
{
  GDBusConnection *connection;

  GUdevClient *udev_client;

  GHashTable *map_sysfs_path_to_object;
};

enum
{
  PROP_0,
  PROP_CONNECTION,
};

/* ---------------------------------------------------------------------------------------------------- */

static void linux_daemon_coldplug (LinuxDaemon *daemon);

static void on_uevent (GUdevClient  *client,
                       const gchar  *action,
                       GUdevDevice  *device,
                       gpointer      user_data);

static void daemon_iface_init (DaemonIface *iface);

/* ---------------------------------------------------------------------------------------------------- */

G_DEFINE_TYPE_WITH_CODE (LinuxDaemon, linux_daemon, TYPE_DAEMON_STUB,
                         G_IMPLEMENT_INTERFACE (TYPE_DAEMON, daemon_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
linux_daemon_get_property (GObject      *object,
                           guint         prop_id,
                           GValue       *value,
                           GParamSpec   *pspec)
{
  LinuxDaemon *daemon = LINUX_DAEMON (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      g_value_set_object (value, daemon->priv->connection);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
linux_daemon_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  LinuxDaemon *daemon = LINUX_DAEMON (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      daemon->priv->connection = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
linux_daemon_finalize (GObject *object)
{
  LinuxDaemon *daemon = LINUX_DAEMON (object);

  g_hash_table_unref (daemon->priv->map_sysfs_path_to_object);
  g_object_unref (daemon->priv->connection);
  g_object_unref (daemon->priv->udev_client);

  if (G_OBJECT_CLASS (linux_daemon_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (linux_daemon_parent_class)->finalize (object);
}

static void
linux_daemon_init (LinuxDaemon *daemon)
{
  const gchar *subsystems[] = {"block", NULL};

  daemon->priv = G_TYPE_INSTANCE_GET_PRIVATE (daemon, TYPE_LINUX_DAEMON, LinuxDaemonPrivate);

  daemon->priv->map_sysfs_path_to_object = g_hash_table_new_full (g_str_hash,
                                                                  g_str_equal,
                                                                  g_free,
                                                                  g_object_unref);

  daemon->priv->udev_client = g_udev_client_new (subsystems);
  g_signal_connect (daemon->priv->udev_client,
                    "uevent",
                    G_CALLBACK (on_uevent),
                    daemon);
}

static void
linux_daemon_constructed (GObject *object)
{
  LinuxDaemon *daemon = LINUX_DAEMON (object);

  linux_daemon_coldplug (daemon);

  if (G_OBJECT_CLASS (linux_daemon_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (linux_daemon_parent_class)->constructed (object);
}

static void
linux_daemon_class_init (LinuxDaemonClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = linux_daemon_get_property;
  gobject_class->set_property = linux_daemon_set_property;
  gobject_class->finalize     = linux_daemon_finalize;
  gobject_class->constructed  = linux_daemon_constructed;

  g_object_class_install_property (gobject_class,
                                   PROP_CONNECTION,
                                   g_param_spec_object ("connection",
                                                        "Connection",
                                                        "The GDBusConnection to use",
                                                        G_TYPE_DBUS_CONNECTION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));


  g_type_class_add_private (klass, sizeof (LinuxDaemonPrivate));
}

/* ---------------------------------------------------------------------------------------------------- */

/* returns TRUE if a change was made */
static gboolean
maybe_export_unexport_object (LinuxDaemon *daemon,
                              LinuxDevice *device,
                              gboolean     visible)
{
  guint daemon_export_id;
  gboolean ret;

  ret = FALSE;

  daemon_export_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (device), "daemon-export-id"));
  if (visible)
    {
      if (daemon_export_id == 0)
        {
          GError *error;
          guint id;
          error = NULL;
          id = g_dbus_interface_register_object (G_DBUS_INTERFACE (device),
                                                 daemon->priv->connection,
                                                 linux_device_get_object_path (device),
                                                 &error);
          if (id == 0)
            {
              g_printerr ("Error registering object: %s\n",
                          error->message);
              g_error_free (error);
            }
          g_object_set_data (G_OBJECT (device), "daemon-export-id", GUINT_TO_POINTER (id));
          ret = TRUE;
          g_print ("registered object path `%s'\n", linux_device_get_object_path (device));
        }
      else
        {
          /* all good, is already exported */
        }
    }
  else
    {
      if (daemon_export_id > 0)
        {
          g_warn_if_fail (g_dbus_connection_unregister_object (daemon->priv->connection, daemon_export_id));
          g_object_set_data (G_OBJECT (device), "daemon-export-id", GUINT_TO_POINTER (0));
          ret = TRUE;
          g_print ("unregistered object path `%s'\n", linux_device_get_object_path (device));
        }
      else
        {
          /* all good, wasn't previously exported */
        }
    }

  return ret;
}

static void
emit_added (LinuxDaemon  *daemon,
            LinuxDevice  *device)
{
  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
  g_variant_builder_add (&builder,
                         "{s@a{sv}}",
                         device_interface_info ()->name,
                         g_dbus_interface_get_properties (G_DBUS_INTERFACE (device)));
  daemon_emit_device_added (DAEMON (daemon),
                            linux_device_get_object_path (device),
                            g_variant_builder_end (&builder));
}

static void
emit_removed (LinuxDaemon  *daemon,
              LinuxDevice  *device)
{
  daemon_emit_device_removed (DAEMON (daemon), linux_device_get_object_path (device));
}

static gboolean
on_properties_changed_emitted (LinuxDevice         *exported_object,
                               GVariant            *changed_properties,
                               const gchar* const  *invalidated_properties,
                               gpointer             user_data)
{
  LinuxDaemon *daemon = LINUX_DAEMON (user_data);
  GVariantBuilder builder;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sa{sv}}"));
  g_variant_builder_add (&builder,
                         "{s@a{sv}}",
                         device_interface_info ()->name,
                         changed_properties);
  daemon_emit_device_changed (DAEMON (daemon),
                              linux_device_get_object_path (LINUX_DEVICE (exported_object)),
                              g_variant_builder_end (&builder));
  return FALSE; /* don't consume the signal */
}

static void
handle_device_uevent (LinuxDaemon  *daemon,
                      const gchar  *action,
                      GUdevDevice  *udev_device)
{
  LinuxDevice *device;
  const gchar *sysfs_path;

  sysfs_path = g_udev_device_get_sysfs_path (udev_device);
  if (g_strcmp0 (action, "remove") == 0)
    {
      device = g_hash_table_lookup (daemon->priv->map_sysfs_path_to_object, sysfs_path);
      if (device != NULL)
        {
          if (maybe_export_unexport_object (daemon, device, FALSE))
            emit_removed (daemon, device);

          g_warn_if_fail (g_signal_handlers_disconnect_by_func (device,
                                                                G_CALLBACK (on_properties_changed_emitted),
                                                                daemon) == 1);

          g_hash_table_remove (daemon->priv->map_sysfs_path_to_object, sysfs_path);
          g_print ("removed object with sysfs path `%s'\n", sysfs_path);
        }
    }
  else
    {
      device = g_hash_table_lookup (daemon->priv->map_sysfs_path_to_object, sysfs_path);
      if (device != NULL)
        {
          gboolean visible;

          linux_device_set_udev_device (device, udev_device);
          linux_device_update (device);
          visible = linux_device_get_visible (device);
          if (maybe_export_unexport_object (daemon, device, visible))
            {
              if (visible)
                emit_added (daemon, device);
              else
                emit_removed (daemon, device);
            }
          g_print ("handled %s uevent for object with sysfs path `%s'\n", action, sysfs_path);
        }
      else
        {
          const gchar *object_path;
          gboolean visible;

          device = linux_device_new (udev_device);
          object_path = linux_device_get_object_path (device);
          visible = linux_device_get_visible (device);

          g_signal_connect (device,
                            "g-properties-changed-emitted",
                            G_CALLBACK (on_properties_changed_emitted),
                            daemon);

          g_hash_table_insert (daemon->priv->map_sysfs_path_to_object,
                               g_strdup (sysfs_path),
                               device);

          if (maybe_export_unexport_object (daemon, device, visible))
            emit_added (daemon, device);

          /* TODO: connect to notify:visible to handle changes */

          g_print ("added object with sysfs path `%s'\n", sysfs_path);
        }
    }
}

/* Called on startup */
static void
linux_daemon_coldplug (LinuxDaemon *daemon)
{
  GList *devices;
  GList *l;

  /* TODO: maybe do two loops to properly handle dependency SNAFU? */
  devices = g_udev_client_query_by_subsystem (daemon->priv->udev_client, "block");
  for (l = devices; l != NULL; l = l->next)
    handle_device_uevent (daemon, "add", G_UDEV_DEVICE (l->data));
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);
}

/* Called when an uevent happens on a device */
static void
on_uevent (GUdevClient  *client,
           const gchar  *action,
           GUdevDevice  *device,
           gpointer      user_data)
{
  LinuxDaemon *daemon = LINUX_DAEMON (user_data);
  handle_device_uevent (daemon, action, device);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_enumerate_device_files (Daemon                *_daemon,
                               GDBusMethodInvocation *invocation)
{
  /* LinuxDaemon *daemon = LINUX_DAEMON (_daemon); */
  const gchar *ret[] = {"/dev/sda", "/dev/sda1", NULL};

  daemon_complete_enumerate_device_files (_daemon, invocation, ret);

  return TRUE;
}

static void
daemon_iface_init (DaemonIface *iface)
{
  iface->handle_enumerate_device_files = handle_enumerate_device_files;
}

/* ---------------------------------------------------------------------------------------------------- */
