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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define _GNU_SOURCE 1

/* ---------------------------------------------------------------------------------------------------- */
/* We might want these things to be configurable; for now they are hardcoded */

/* update ATA SMART every 30 minutes */
#define ATA_SMART_REFRESH_INTERVAL_SECONDS (30*60)

/* ---------------------------------------------------------------------------------------------------- */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <net/if_arp.h>
#include <fcntl.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gudev/gudev.h>

#include "daemon.h"
#include "device.h"
#include "device-private.h"
#include "adapter.h"
#include "adapter-private.h"
#include "expander.h"
#include "expander-private.h"
#include "port.h"
#include "port-private.h"
#include "mount-file.h"
#include "mount.h"
#include "mount-monitor.h"
#include "poller.h"
#include "inhibitor.h"

#include "daemon-glue.h"
#include "marshal.h"

#include "profile.h"

/*--------------------------------------------------------------------------------------------------------------*/

enum
  {
    PROP_0,
    PROP_DAEMON_VERSION,
    PROP_DAEMON_IS_INHIBITED,
    PROP_SUPPORTS_LUKS_DEVICES,
    PROP_KNOWN_FILESYSTEMS,
  };

enum
  {
    DEVICE_ADDED_SIGNAL,
    DEVICE_REMOVED_SIGNAL,
    DEVICE_CHANGED_SIGNAL,
    DEVICE_JOB_CHANGED_SIGNAL,
    ADAPTER_ADDED_SIGNAL,
    ADAPTER_REMOVED_SIGNAL,
    ADAPTER_CHANGED_SIGNAL,
    EXPANDER_ADDED_SIGNAL,
    EXPANDER_REMOVED_SIGNAL,
    EXPANDER_CHANGED_SIGNAL,
    PORT_ADDED_SIGNAL,
    PORT_REMOVED_SIGNAL,
    PORT_CHANGED_SIGNAL,
    LAST_SIGNAL,
  };

static guint signals[LAST_SIGNAL] = { 0 };

struct DaemonPrivate
{
  DBusGConnection *system_bus_connection;
  DBusGProxy *system_bus_proxy;

  PolkitAuthority *authority;

  GUdevClient *gudev_client;

  GIOChannel *mdstat_channel;

  GHashTable *map_dev_t_to_device;
  GHashTable *map_device_file_to_device;
  GHashTable *map_native_path_to_device;
  GHashTable *map_object_path_to_device;

  GHashTable *map_native_path_to_adapter;
  GHashTable *map_object_path_to_adapter;

  GHashTable *map_native_path_to_expander;
  GHashTable *map_object_path_to_expander;

  GHashTable *map_native_path_to_port;
  GHashTable *map_object_path_to_port;

  MountMonitor *mount_monitor;

  guint ata_smart_refresh_timer_id;
  guint ata_smart_cleanup_timer_id;

  GList *polling_inhibitors;

  GList *inhibitors;

  GList *spindown_inhibitors;
};

static void
daemon_class_init (DaemonClass *klass);
static void
daemon_init (Daemon *seat);
static void
daemon_finalize (GObject *object);

static void
daemon_polling_inhibitor_disconnected_cb (Inhibitor *inhibitor,
                                          Daemon *daemon);

static void
daemon_inhibitor_disconnected_cb (Inhibitor *inhibitor,
                                  Daemon *daemon);

G_DEFINE_TYPE (Daemon, daemon, G_TYPE_OBJECT)

#define DAEMON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TYPE_DAEMON, DaemonPrivate))

/*--------------------------------------------------------------------------------------------------------------*/

GQuark
error_quark (void)
{
  static GQuark ret = 0;

  if (ret == 0)
    {
      ret = g_quark_from_static_string ("udisks_error");
    }

  return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0)
    {
      static const GEnumValue values[] =
        {
          ENUM_ENTRY (ERROR_FAILED, "Failed"),
          ENUM_ENTRY (ERROR_PERMISSION_DENIED, "PermissionDenied"),
          ENUM_ENTRY (ERROR_INHIBITED, "Inhibited"),
          ENUM_ENTRY (ERROR_BUSY, "Busy"),
          ENUM_ENTRY (ERROR_CANCELLED, "Cancelled"),
          ENUM_ENTRY (ERROR_INVALID_OPTION, "InvalidOption"),
          ENUM_ENTRY (ERROR_NOT_SUPPORTED, "NotSupported"),
          ENUM_ENTRY (ERROR_ATA_SMART_WOULD_WAKEUP, "AtaSmartWouldWakeup"),
          ENUM_ENTRY (ERROR_FILESYSTEM_DRIVER_MISSING, "FilesystemDriverMissing"),
          ENUM_ENTRY (ERROR_FILESYSTEM_TOOLS_MISSING, "FilesystemToolsMissing"),
          { 0, 0, 0 }
        };
      g_assert (NUM_ERRORS == G_N_ELEMENTS (values) - 1);
      etype = g_enum_register_static ("Error", values);
    }
  return etype;
}

static GObject *
daemon_constructor (GType type,
                    guint n_construct_properties,
                    GObjectConstructParam *construct_properties)
{
  Daemon *daemon;
  DaemonClass *klass;

  klass = DAEMON_CLASS (g_type_class_peek (TYPE_DAEMON));

  daemon = DAEMON (G_OBJECT_CLASS (daemon_parent_class)->constructor (type,
                                                                      n_construct_properties,
                                                                      construct_properties));
  return G_OBJECT (daemon);
}

/*--------------------------------------------------------------------------------------------------------------*/

#define KNOWN_FILESYSTEMS_STRUCT_TYPE (dbus_g_type_get_struct ("GValueArray", \
                                                               G_TYPE_STRING, \
                                                               G_TYPE_STRING, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_UINT, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_BOOLEAN, \
                                                               G_TYPE_INVALID))

static const Filesystem known_file_systems[] =
  {
    {
      "vfat", /* id */
      "FAT", /* name */
      FALSE, /* supports_unix_owners */
      TRUE, /* can_mount */
      TRUE, /* can_create */
      254, /* max_label_len */
      TRUE, /* supports_label_rename */
      FALSE, /* supports_online_label_rename*/
      TRUE, /* supports_fsck */
      FALSE, /* supports_online_fsck */
      FALSE, /* supports_resize_enlarge */
      FALSE, /* supports_online_resize_enlarge */
      FALSE, /* supports_resize_shrink */
      FALSE, /* supports_online_resize_shrink */
    },
    {
      "ext2", /* id */
      "Linux Ext2", /* name */
      TRUE, /* supports_unix_owners */
      TRUE, /* can_mount */
      TRUE, /* can_create */
      16, /* max_label_len */
      TRUE, /* supports_label_rename */
      TRUE, /* supports_online_label_rename*/
      TRUE, /* supports_fsck */
      FALSE, /* supports_online_fsck */
      TRUE, /* supports_resize_enlarge */
      TRUE, /* supports_online_resize_enlarge */
      TRUE, /* supports_resize_shrink */
      TRUE, /* supports_online_resize_shrink */
    },
    {
      "ext3", /* id */
      "Linux Ext3", /* name */
      TRUE, /* supports_unix_owners */
      TRUE, /* can_mount */
      TRUE, /* can_create */
      16, /* max_label_len */
      TRUE, /* supports_label_rename */
      TRUE, /* supports_online_label_rename*/
      TRUE, /* supports_fsck */
      FALSE, /* supports_online_fsck */
      TRUE, /* supports_resize_enlarge */
      TRUE, /* supports_online_resize_enlarge */
      TRUE, /* supports_resize_shrink */
      TRUE, /* supports_online_resize_shrink */
    },
    {
      "ext4", /* id */
      "Linux Ext4", /* name */
      TRUE, /* supports_unix_owners */
      TRUE, /* can_mount */
      TRUE, /* can_create */
      16, /* max_label_len */
      TRUE, /* supports_label_rename */
      TRUE, /* supports_online_label_rename*/
      TRUE, /* supports_fsck */
      FALSE, /* supports_online_fsck */
      TRUE, /* supports_resize_enlarge */
      TRUE, /* supports_online_resize_enlarge */
      TRUE, /* supports_resize_shrink */
      TRUE, /* supports_online_resize_shrink */
    },
    {
      "xfs", /* id */
      "XFS", /* name */
      TRUE, /* supports_unix_owners */
      TRUE, /* can_mount */
      TRUE, /* can_create */
      12, /* max_label_len */
      TRUE, /* supports_label_rename */
      FALSE, /* supports_online_label_rename*/
      TRUE, /* supports_fsck */
      FALSE, /* supports_online_fsck */
      FALSE, /* supports_resize_enlarge */
      TRUE, /* supports_online_resize_enlarge */
      FALSE, /* supports_resize_shrink */
      FALSE, /* supports_online_resize_shrink */
    },
    {
      "reiserfs",     /* id */
      "ReiserFS",     /* name */
      TRUE,           /* supports_unix_owners */
      TRUE,           /* can_mount */
      TRUE,           /* can_create */
      16,             /* max_label_len */
      TRUE,           /* supports_label_rename */
      FALSE,          /* supports_online_label_rename*/
      TRUE,           /* supports_fsck */
      FALSE,          /* supports_online_fsck */
      TRUE,           /* supports_resize_enlarge */
      TRUE,           /* supports_online_resize_enlarge */
      TRUE,           /* supports_resize_shrink */
      FALSE,          /* supports_online_resize_shrink */
    },
    {
      "minix", /* id */
      "Minix", /* name */
      TRUE, /* supports_unix_owners */
      TRUE, /* can_mount */
      TRUE, /* can_create */
      0, /* max_label_len */
      FALSE, /* supports_label_rename */
      FALSE, /* supports_online_label_rename*/
      TRUE, /* supports_fsck */
      FALSE, /* supports_online_fsck */
      FALSE, /* supports_resize_enlarge */
      FALSE, /* supports_online_resize_enlarge */
      FALSE, /* supports_resize_shrink */
      FALSE, /* supports_online_resize_shrink */
    },
    {
      "ntfs", /* id */
      "NTFS", /* name */
      FALSE, /* supports_unix_owners */
      TRUE, /* can_mount */
      TRUE, /* can_create */
      128, /* max_label_len */
      TRUE, /* supports_label_rename */
      FALSE, /* supports_online_label_rename*/
      FALSE, /* supports_fsck (TODO: hmm.. ntfsck doesn't support -a yet?) */
      FALSE, /* supports_online_fsck */
      TRUE, /* supports_resize_enlarge */
      FALSE, /* supports_online_resize_enlarge */
      TRUE, /* supports_resize_shrink */
      FALSE, /* supports_online_resize_shrink */
    },
    {
      "swap", /* id */
      "Swap Space", /* name */
      FALSE, /* supports_unix_owners */
      FALSE, /* can_mount */
      TRUE, /* can_create */
      15, /* max_label_len */
      FALSE, /* supports_label_rename */
      FALSE, /* supports_online_label_rename*/
      FALSE, /* supports_fsck */
      FALSE, /* supports_online_fsck */
      FALSE, /* supports_resize_enlarge */
      FALSE, /* supports_online_resize_enlarge */
      FALSE, /* supports_resize_shrink */
      FALSE, /* supports_online_resize_shrink */
    }
  };

static const int num_known_file_systems = sizeof(known_file_systems) / sizeof(Filesystem);

const Filesystem *
daemon_local_get_fs_details (Daemon *daemon,
                             const gchar *filesystem_id)
{
  gint n;
  const Filesystem *ret;

  ret = NULL;

  for (n = 0; n < num_known_file_systems; n++)
    {
      if (strcmp (known_file_systems[n].id, filesystem_id) == 0)
        {
          ret = &known_file_systems[n];
          break;
        }
    }

  return ret;
}

static GPtrArray *
get_known_filesystems (Daemon *daemon)
{
  int n;
  GPtrArray *ret;

  ret = g_ptr_array_new ();
  for (n = 0; n < num_known_file_systems; n++)
    {
      GValue elem =
        { 0 };
      const Filesystem *fs = known_file_systems + n;

      g_value_init (&elem, KNOWN_FILESYSTEMS_STRUCT_TYPE);
      g_value_take_boxed (&elem, dbus_g_type_specialized_construct (KNOWN_FILESYSTEMS_STRUCT_TYPE));
      dbus_g_type_struct_set (&elem,
                              0,
                              fs->id,
                              1,
                              fs->name,
                              2,
                              fs->supports_unix_owners,
                              3,
                              fs->can_mount,
                              4,
                              fs->can_create,
                              5,
                              fs->max_label_len,
                              6,
                              fs->supports_label_rename,
                              7,
                              fs->supports_online_label_rename,
                              8,
                              fs->supports_fsck,
                              9,
                              fs->supports_online_fsck,
                              10,
                              fs->supports_resize_enlarge,
                              11,
                              fs->supports_online_resize_enlarge,
                              12,
                              fs->supports_resize_shrink,
                              13,
                              fs->supports_online_resize_shrink,
                              G_MAXUINT);
      g_ptr_array_add (ret, g_value_get_boxed (&elem));
    }

  return ret;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
  Daemon *daemon = DAEMON (object);
  GPtrArray *filesystems;

  switch (prop_id)
    {
    case PROP_DAEMON_VERSION:
      g_value_set_string (value, VERSION);
      break;

    case PROP_DAEMON_IS_INHIBITED:
      g_value_set_boolean (value, (daemon->priv->inhibitors != NULL));
      break;

    case PROP_SUPPORTS_LUKS_DEVICES:
      /* TODO: probably Linux only */
      g_value_set_boolean (value, TRUE);
      break;

    case PROP_KNOWN_FILESYSTEMS:
      filesystems = get_known_filesystems (daemon);
      g_value_set_boxed (value, filesystems);
      g_ptr_array_foreach (filesystems, (GFunc) g_value_array_free, NULL);
      g_ptr_array_free (filesystems, TRUE);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
daemon_class_init (DaemonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor = daemon_constructor;
  object_class->finalize = daemon_finalize;
  object_class->get_property = get_property;

  g_type_class_add_private (klass, sizeof(DaemonPrivate));

  signals[DEVICE_ADDED_SIGNAL] = g_signal_new ("device-added", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                               | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[DEVICE_REMOVED_SIGNAL] = g_signal_new ("device-removed", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                                 | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[DEVICE_CHANGED_SIGNAL] = g_signal_new ("device-changed", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                                 | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[DEVICE_JOB_CHANGED_SIGNAL] = g_signal_new ("device-job-changed",
                                                     G_OBJECT_CLASS_TYPE (klass),
                                                     G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                                     0,
                                                     NULL,
                                                     NULL,
                                                     marshal_VOID__BOXED_BOOLEAN_STRING_UINT_BOOLEAN_DOUBLE,
                                                     G_TYPE_NONE,
                                                     6,
                                                     DBUS_TYPE_G_OBJECT_PATH,
                                                     G_TYPE_BOOLEAN,
                                                     G_TYPE_STRING,
                                                     G_TYPE_UINT,
                                                     G_TYPE_BOOLEAN,
                                                     G_TYPE_DOUBLE);

  signals[ADAPTER_ADDED_SIGNAL] = g_signal_new ("adapter-added", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                                | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[ADAPTER_REMOVED_SIGNAL] = g_signal_new ("adapter-removed", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                                  | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[ADAPTER_CHANGED_SIGNAL] = g_signal_new ("adapter-changed", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                                  | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[EXPANDER_ADDED_SIGNAL] = g_signal_new ("expander-added", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                                 | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[EXPANDER_REMOVED_SIGNAL] = g_signal_new ("expander-removed", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                                   | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[EXPANDER_CHANGED_SIGNAL] = g_signal_new ("expander-changed", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                                   | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[PORT_ADDED_SIGNAL] = g_signal_new ("port-added", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                             | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[PORT_REMOVED_SIGNAL] = g_signal_new ("port-removed", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                               | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  signals[PORT_CHANGED_SIGNAL] = g_signal_new ("port-changed", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST
                                               | G_SIGNAL_DETAILED, 0, NULL, NULL, g_cclosure_marshal_VOID__BOXED, G_TYPE_NONE, 1, DBUS_TYPE_G_OBJECT_PATH);

  dbus_g_object_type_install_info (TYPE_DAEMON, &dbus_glib_daemon_object_info);

  dbus_g_error_domain_register (ERROR, "org.freedesktop.UDisks.Error", TYPE_ERROR);

  g_object_class_install_property (object_class, PROP_DAEMON_VERSION, g_param_spec_string ("daemon-version",
                                                                                           NULL,
                                                                                           NULL,
                                                                                           NULL,
                                                                                           G_PARAM_READABLE));

  g_object_class_install_property (object_class, PROP_DAEMON_IS_INHIBITED, g_param_spec_boolean ("daemon-is-inhibited",
                                                                                                 NULL,
                                                                                                 NULL,
                                                                                                 FALSE,
                                                                                                 G_PARAM_READABLE));

  g_object_class_install_property (object_class,
                                   PROP_SUPPORTS_LUKS_DEVICES,
                                   g_param_spec_boolean ("supports-luks-devices", NULL, NULL, FALSE, G_PARAM_READABLE));

  g_object_class_install_property (object_class,
                                   PROP_KNOWN_FILESYSTEMS,
                                   g_param_spec_boxed ("known-filesystems",
                                                       NULL,
                                                       NULL,
                                                       dbus_g_type_get_collection ("GPtrArray",
                                                                                   KNOWN_FILESYSTEMS_STRUCT_TYPE),
                                                       G_PARAM_READABLE));
}

static void
daemon_init (Daemon *daemon)
{
  daemon->priv = DAEMON_GET_PRIVATE (daemon);

  daemon->priv->map_dev_t_to_device = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
  daemon->priv->map_device_file_to_device = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  daemon->priv->map_native_path_to_device = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  daemon->priv->map_object_path_to_device = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  daemon->priv->map_native_path_to_adapter = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  daemon->priv->map_object_path_to_adapter = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  daemon->priv->map_native_path_to_expander = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  daemon->priv->map_object_path_to_expander = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

  daemon->priv->map_native_path_to_port = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  daemon->priv->map_object_path_to_port = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

static void
daemon_finalize (GObject *object)
{
  Daemon *daemon;
  GList *l;

  g_return_if_fail (object != NULL);
  g_return_if_fail (IS_DAEMON (object));

  daemon = DAEMON (object);

  g_return_if_fail (daemon->priv != NULL);

  if (daemon->priv->authority != NULL)
    g_object_unref (daemon->priv->authority);

  if (daemon->priv->system_bus_proxy != NULL)
    g_object_unref (daemon->priv->system_bus_proxy);

  if (daemon->priv->system_bus_connection != NULL)
    dbus_g_connection_unref (daemon->priv->system_bus_connection);

  if (daemon->priv->mdstat_channel != NULL)
    g_io_channel_unref (daemon->priv->mdstat_channel);

  if (daemon->priv->map_dev_t_to_device != NULL)
    {
      g_hash_table_unref (daemon->priv->map_dev_t_to_device);
    }
  if (daemon->priv->map_device_file_to_device != NULL)
    {
      g_hash_table_unref (daemon->priv->map_device_file_to_device);
    }

  if (daemon->priv->map_native_path_to_device != NULL)
    {
      g_hash_table_unref (daemon->priv->map_native_path_to_device);
    }
  if (daemon->priv->map_object_path_to_device != NULL)
    {
      g_hash_table_unref (daemon->priv->map_object_path_to_device);
    }

  if (daemon->priv->map_native_path_to_adapter != NULL)
    {
      g_hash_table_unref (daemon->priv->map_native_path_to_adapter);
    }
  if (daemon->priv->map_object_path_to_adapter != NULL)
    {
      g_hash_table_unref (daemon->priv->map_object_path_to_adapter);
    }

  if (daemon->priv->map_native_path_to_expander != NULL)
    {
      g_hash_table_unref (daemon->priv->map_native_path_to_expander);
    }
  if (daemon->priv->map_object_path_to_expander != NULL)
    {
      g_hash_table_unref (daemon->priv->map_object_path_to_expander);
    }

  if (daemon->priv->map_native_path_to_port != NULL)
    {
      g_hash_table_unref (daemon->priv->map_native_path_to_port);
    }
  if (daemon->priv->map_object_path_to_port != NULL)
    {
      g_hash_table_unref (daemon->priv->map_object_path_to_port);
    }

  if (daemon->priv->mount_monitor != NULL)
    {
      g_object_unref (daemon->priv->mount_monitor);
    }

  if (daemon->priv->gudev_client != NULL)
    {
      g_object_unref (daemon->priv->gudev_client);
    }

  if (daemon->priv->ata_smart_cleanup_timer_id > 0)
    {
      g_source_remove (daemon->priv->ata_smart_cleanup_timer_id);
    }

  if (daemon->priv->ata_smart_refresh_timer_id > 0)
    {
      g_source_remove (daemon->priv->ata_smart_refresh_timer_id);
    }

  for (l = daemon->priv->polling_inhibitors; l != NULL; l = l->next)
    {
      Inhibitor *inhibitor = INHIBITOR (l->data);
      g_signal_handlers_disconnect_by_func (inhibitor, daemon_polling_inhibitor_disconnected_cb, daemon);
      g_object_unref (inhibitor);
    }
  g_list_free (daemon->priv->polling_inhibitors);

  for (l = daemon->priv->inhibitors; l != NULL; l = l->next)
    {
      Inhibitor *inhibitor = INHIBITOR (l->data);
      g_signal_handlers_disconnect_by_func (inhibitor, daemon_inhibitor_disconnected_cb, daemon);
      g_object_unref (inhibitor);
    }
  g_list_free (daemon->priv->inhibitors);

  G_OBJECT_CLASS (daemon_parent_class)->finalize (object);
}

void
inhibitor_name_owner_changed (DBusMessage *message);

static DBusHandlerResult
_filter (DBusConnection *connection,
         DBusMessage *message,
         void *user_data)
{
  //Daemon *daemon = DAEMON (user_data);
  const char *interface;

  interface = dbus_message_get_interface (message);

  if (dbus_message_is_signal (message, DBUS_INTERFACE_DBUS, "NameOwnerChanged"))
    {
      /* for now, pass NameOwnerChanged to Inhibitor */
      inhibitor_name_owner_changed (message);
    }

  /* other filters might want to process this message too */
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
device_add (Daemon *daemon,
            GUdevDevice *d,
            gboolean emit_event);
static void
device_remove (Daemon *daemon,
               GUdevDevice *d);

/* ---------------------------------------------------------------------------------------------------- */

static void
pci_device_changed (Daemon *daemon,
                    GUdevDevice *d,
                    gboolean synthesized)
{
  Adapter *adapter;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  adapter = g_hash_table_lookup (daemon->priv->map_native_path_to_adapter, native_path);
  if (adapter != NULL)
    {
      gboolean keep_adapter;

      g_print ("**** pci CHANGING %s\n", native_path);

      /* The sysfs path ('move' uevent) may actually change so remove it and add
       * it back after processing. The kernel name will never change so the object
       * path will fortunately remain constant.
       */
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_native_path_to_adapter, adapter->priv->native_path));

      keep_adapter = adapter_changed (adapter, d, synthesized);

      g_assert (adapter_local_get_native_path (adapter) != NULL);
      g_assert (g_strcmp0 (native_path, adapter_local_get_native_path (adapter)) == 0);

      /* now add the things back to the global hashtables - it's important that we
       * do this *before* calling adapter_remove() - otherwise it will never remove
       * the adapter
       */
      g_hash_table_insert (daemon->priv->map_native_path_to_adapter,
                           g_strdup (adapter_local_get_native_path (adapter)),
                           g_object_ref (adapter));

      if (!keep_adapter)
        {
          g_print ("**** pci CHANGE TRIGGERED REMOVE %s\n", native_path);
          device_remove (daemon, d);
        }
      else
        {
          g_print ("**** pci CHANGED %s\n", native_path);
        }
    }
  else
    {
      g_print ("**** pci TREATING CHANGE AS ADD %s\n", native_path);
      device_add (daemon, d, TRUE);
    }
}

/* ------------------------------ */

static void
scsi_host_device_changed (Daemon *daemon,
                          GUdevDevice *d,
                          gboolean synthesized)
{
  Port *port;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  port = g_hash_table_lookup (daemon->priv->map_native_path_to_port, native_path);
  if (port != NULL)
    {
      gboolean keep_port;

      g_print ("**** scsi_host CHANGING %s\n", native_path);

      /* The sysfs path ('move' uevent) may actually change so remove it and add
       * it back after processing. The kernel name will never change so the object
       * path will fortunately remain constant.
       */
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_native_path_to_port, port->priv->native_path));

      keep_port = port_changed (port, d, synthesized);

      g_assert (port_local_get_native_path (port) != NULL);
      g_assert (g_strcmp0 (native_path, port_local_get_native_path (port)) == 0);

      /* now add the things back to the global hashtables - it's important that we
       * do this *before* calling port_remove() - otherwise it will never remove
       * the port
       */
      g_hash_table_insert (daemon->priv->map_native_path_to_port,
                           g_strdup (port_local_get_native_path (port)),
                           g_object_ref (port));

      if (!keep_port)
        {
          g_print ("**** scsi_host CHANGE TRIGGERED REMOVE %s\n", native_path);
          device_remove (daemon, d);
        }
      else
        {
          g_print ("**** scsi_host CHANGED %s\n", native_path);
        }
    }
  else
    {
      g_print ("**** scsi_host TREATING CHANGE AS ADD %s\n", native_path);
      device_add (daemon, d, TRUE);
    }
}

/* ------------------------------ */

static void
sas_phy_device_changed (Daemon *daemon,
                        GUdevDevice *d,
                        gboolean synthesized)
{
  Port *port;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  port = g_hash_table_lookup (daemon->priv->map_native_path_to_port, native_path);
  if (port != NULL)
    {
      gboolean keep_port;

      g_print ("**** sas_phy CHANGING %s\n", native_path);

      /* The sysfs path ('move' uevent) may actually change so remove it and add
       * it back after processing. The kernel name will never change so the object
       * path will fortunately remain constant.
       */
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_native_path_to_port, port->priv->native_path));

      keep_port = port_changed (port, d, synthesized);

      g_assert (port_local_get_native_path (port) != NULL);
      g_assert (g_strcmp0 (native_path, port_local_get_native_path (port)) == 0);

      /* now add the things back to the global hashtables - it's important that we
       * do this *before* calling port_remove() - otherwise it will never remove
       * the port
       */
      g_hash_table_insert (daemon->priv->map_native_path_to_port,
                           g_strdup (port_local_get_native_path (port)),
                           g_object_ref (port));

      if (!keep_port)
        {
          g_print ("**** sas_phy CHANGE TRIGGERED REMOVE %s\n", native_path);
          device_remove (daemon, d);
        }
      else
        {
          g_print ("**** sas_phy CHANGED %s\n", native_path);
        }
    }
  else
    {
      g_print ("**** sas_phy TREATING CHANGE AS ADD %s\n", native_path);
      device_add (daemon, d, TRUE);
    }
}

/* ------------------------------ */

static void
sas_expander_device_changed (Daemon *daemon,
                             GUdevDevice *d,
                             gboolean synthesized)
{
  Expander *expander;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  expander = g_hash_table_lookup (daemon->priv->map_native_path_to_expander, native_path);
  if (expander != NULL)
    {
      gboolean keep_expander;

      g_print ("**** sas_expander CHANGING %s\n", native_path);

      /* The sysfs path ('move' uevent) may actually change so remove it and add
       * it back after processing. The kernel name will never change so the object
       * path will fortunately remain constant.
       */
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_native_path_to_expander, expander->priv->native_path));

      keep_expander = expander_changed (expander, d, synthesized);

      g_assert (expander_local_get_native_path (expander) != NULL);
      g_assert (g_strcmp0 (native_path, expander_local_get_native_path (expander)) == 0);

      /* now add the things back to the global hashtables - it's important that we
       * do this *before* calling expander_remove() - otherwise it will never remove
       * the expander
       */
      g_hash_table_insert (daemon->priv->map_native_path_to_expander,
                           g_strdup (expander_local_get_native_path (expander)),
                           g_object_ref (expander));

      if (!keep_expander)
        {
          g_print ("**** sas_expander CHANGE TRIGGERED REMOVE %s\n", native_path);
          device_remove (daemon, d);
        }
      else
        {
          g_print ("**** sas_expander CHANGED %s\n", native_path);
        }
    }
  else
    {
      g_print ("**** sas_expander TREATING CHANGE AS ADD %s\n", native_path);
      device_add (daemon, d, TRUE);
    }
}

/* ------------------------------ */

static void
block_device_changed (Daemon *daemon,
                      GUdevDevice *d,
                      gboolean synthesized)
{
  Device *device;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  device = g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
  if (device != NULL)
    {
      gboolean keep_device;

      g_print ("**** CHANGING %s\n", native_path);

      /* The device file (udev rules) and/or sysfs path ('move' uevent) may actually change so
       * remove it and add it back after processing. The kernel name will never change so
       * the object path will fortunately remain constant.
       */
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_native_path_to_device, device->priv->native_path));
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_device_file_to_device, device->priv->device_file));

      keep_device = device_changed (device, d, synthesized);

      g_assert (device_local_get_device_file (device) != NULL);
      g_assert (device_local_get_native_path (device) != NULL);
      g_assert (g_strcmp0 (native_path, device_local_get_native_path (device)) == 0);

      /* now add the things back to the global hashtables - it's important that we
       * do this *before* calling device_remove() - otherwise it will never remove
       * the device
       */
      g_hash_table_insert (daemon->priv->map_device_file_to_device,
                           g_strdup (device_local_get_device_file (device)),
                           g_object_ref (device));
      g_hash_table_insert (daemon->priv->map_native_path_to_device,
                           g_strdup (device_local_get_native_path (device)),
                           g_object_ref (device));

      if (!keep_device)
        {
          g_print ("**** CHANGE TRIGGERED REMOVE %s\n", native_path);
          device_remove (daemon, d);
        }
      else
        {
          g_print ("**** CHANGED %s\n", native_path);

          daemon_local_update_poller (daemon);
          daemon_local_update_spindown (daemon);
        }
    }
  else
    {
      g_print ("**** TREATING CHANGE AS ADD %s\n", native_path);
      device_add (daemon, d, TRUE);
    }
}

static void
_device_changed (Daemon *daemon,
                 GUdevDevice *d,
                 gboolean synthesized)
{
  const gchar *subsystem;

  subsystem = g_udev_device_get_subsystem (d);
  if (g_strcmp0 (subsystem, "block") == 0)
    block_device_changed (daemon, d, synthesized);
  else if (g_strcmp0 (subsystem, "pci") == 0)
    pci_device_changed (daemon, d, synthesized);
  else if (g_strcmp0 (subsystem, "scsi_host") == 0)
    scsi_host_device_changed (daemon, d, synthesized);
  else if (g_strcmp0 (subsystem, "sas_phy") == 0)
    sas_phy_device_changed (daemon, d, synthesized);
  else if (g_strcmp0 (subsystem, "sas_expander") == 0)
    sas_expander_device_changed (daemon, d, synthesized);
  else
    g_warning ("Unhandled changed event from subsystem `%s'", subsystem);
}

void
daemon_local_synthesize_changed (Daemon *daemon, Device *device)
{
  g_object_ref (device->priv->d);
  _device_changed (daemon, device->priv->d, TRUE);
  g_object_unref (device->priv->d);
}

void
daemon_local_synthesize_changed_on_all_devices (Daemon *daemon)
{
  GHashTableIter hash_iter;
  Device *device;

  g_hash_table_iter_init (&hash_iter, daemon->priv->map_object_path_to_device);
  while (g_hash_table_iter_next (&hash_iter, NULL, (gpointer) & device))
    {
      daemon_local_synthesize_changed (daemon, device);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
pci_device_add (Daemon *daemon,
                GUdevDevice *d,
                gboolean emit_event)
{
  Adapter *adapter;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  adapter = g_hash_table_lookup (daemon->priv->map_native_path_to_adapter, native_path);
  if (adapter != NULL)
    {
      /* we already have the adapter; treat as change event */
      g_print ("**** pci TREATING ADD AS CHANGE %s\n", native_path);
      _device_changed (daemon, d, FALSE);
    }
  else
    {
      g_print ("**** pci ADDING %s\n", native_path);
      adapter = adapter_new (daemon, d);

      if (adapter != NULL)
        {
          /* assert that the adapter is fully loaded with info */
          g_assert (adapter_local_get_native_path (adapter) != NULL);
          g_assert (adapter_local_get_object_path (adapter) != NULL);
          g_assert (g_strcmp0 (native_path, adapter_local_get_native_path (adapter)) == 0);

          g_hash_table_insert (daemon->priv->map_native_path_to_adapter,
                               g_strdup (adapter_local_get_native_path (adapter)),
                               g_object_ref (adapter));
          g_hash_table_insert (daemon->priv->map_object_path_to_adapter,
                               g_strdup (adapter_local_get_object_path (adapter)),
                               g_object_ref (adapter));
          g_print ("**** pci ADDED %s\n", native_path);
          if (emit_event)
            {
              const char *object_path;
              object_path = adapter_local_get_object_path (adapter);
              g_print ("**** pci EMITTING ADDED for %s\n", adapter->priv->native_path);
              g_signal_emit (daemon, signals[ADAPTER_ADDED_SIGNAL], 0, object_path);
            }
        }
      else
        {
          g_print ("**** pci IGNORING ADD %s\n", native_path);
        }
    }
}

/* ------------------------------ */

static void
scsi_host_device_add (Daemon *daemon,
                      GUdevDevice *d,
                      gboolean emit_event)
{
  Port *port;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  port = g_hash_table_lookup (daemon->priv->map_native_path_to_port, native_path);
  if (port != NULL)
    {
      /* we already have the port; treat as change event */
      g_print ("**** scsi_host TREATING ADD AS CHANGE %s\n", native_path);
      _device_changed (daemon, d, FALSE);
    }
  else
    {
      g_print ("**** scsi_host ADDING %s\n", native_path);
      port = port_new (daemon, d);

      if (port != NULL)
        {
          /* assert that the port is fully loaded with info */
          g_assert (port_local_get_native_path (port) != NULL);
          g_assert (port_local_get_object_path (port) != NULL);
          g_assert (g_strcmp0 (native_path, port_local_get_native_path (port)) == 0);

          g_hash_table_insert (daemon->priv->map_native_path_to_port,
                               g_strdup (port_local_get_native_path (port)),
                               g_object_ref (port));
          g_hash_table_insert (daemon->priv->map_object_path_to_port,
                               g_strdup (port_local_get_object_path (port)),
                               g_object_ref (port));
          g_print ("**** scsi_host ADDED %s\n", native_path);
          if (emit_event)
            {
              const char *object_path;
              object_path = port_local_get_object_path (port);
              g_print ("**** scsi_host EMITTING ADDED for %s\n", port->priv->native_path);
              g_signal_emit (daemon, signals[PORT_ADDED_SIGNAL], 0, object_path);
            }
        }
      else
        {
          g_print ("**** scsi_host IGNORING ADD %s\n", native_path);
        }
    }
}

/* ------------------------------ */

static void
sas_phy_device_add (Daemon *daemon,
                    GUdevDevice *d,
                    gboolean emit_event)
{
  Port *port;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  port = g_hash_table_lookup (daemon->priv->map_native_path_to_port, native_path);
  if (port != NULL)
    {
      /* we already have the port; treat as change event */
      g_print ("**** sas_phy TREATING ADD AS CHANGE %s\n", native_path);
      _device_changed (daemon, d, FALSE);
    }
  else
    {
      g_print ("**** sas_phy ADDING %s\n", native_path);
      port = port_new (daemon, d);

      if (port != NULL)
        {
          /* assert that the port is fully loaded with info */
          g_assert (port_local_get_native_path (port) != NULL);
          g_assert (port_local_get_object_path (port) != NULL);
          g_assert (g_strcmp0 (native_path, port_local_get_native_path (port)) == 0);

          g_hash_table_insert (daemon->priv->map_native_path_to_port,
                               g_strdup (port_local_get_native_path (port)),
                               g_object_ref (port));
          g_hash_table_insert (daemon->priv->map_object_path_to_port,
                               g_strdup (port_local_get_object_path (port)),
                               g_object_ref (port));
          g_print ("**** sas_phy ADDED %s\n", native_path);
          if (emit_event)
            {
              const char *object_path;
              object_path = port_local_get_object_path (port);
              g_print ("**** sas_phy EMITTING ADDED for %s\n", port->priv->native_path);
              g_signal_emit (daemon, signals[PORT_ADDED_SIGNAL], 0, object_path);
            }
        }
      else
        {
          g_print ("**** sas_phy IGNORING ADD %s\n", native_path);
        }
    }
}

/* ------------------------------ */

static void
sas_expander_device_add (Daemon *daemon,
                         GUdevDevice *d,
                         gboolean emit_event)
{
  Expander *expander;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  expander = g_hash_table_lookup (daemon->priv->map_native_path_to_expander, native_path);
  if (expander != NULL)
    {
      /* we already have the expander; treat as change event */
      g_print ("**** sas_expander TREATING ADD AS CHANGE %s\n", native_path);
      _device_changed (daemon, d, FALSE);
    }
  else
    {
      g_print ("**** sas_expander ADDING %s\n", native_path);
      expander = expander_new (daemon, d);

      if (expander != NULL)
        {
          /* assert that the expander is fully loaded with info */
          g_assert (expander_local_get_native_path (expander) != NULL);
          g_assert (expander_local_get_object_path (expander) != NULL);
          g_assert (g_strcmp0 (native_path, expander_local_get_native_path (expander)) == 0);

          g_hash_table_insert (daemon->priv->map_native_path_to_expander,
                               g_strdup (expander_local_get_native_path (expander)),
                               g_object_ref (expander));
          g_hash_table_insert (daemon->priv->map_object_path_to_expander,
                               g_strdup (expander_local_get_object_path (expander)),
                               g_object_ref (expander));
          g_print ("**** sas_expander ADDED %s\n", native_path);
          if (emit_event)
            {
              const char *object_path;
              object_path = expander_local_get_object_path (expander);
              g_print ("**** sas_expander EMITTING ADDED for %s\n", expander->priv->native_path);
              g_signal_emit (daemon, signals[EXPANDER_ADDED_SIGNAL], 0, object_path);
            }
        }
      else
        {
          g_print ("**** sas_expander IGNORING ADD %s\n", native_path);
        }
    }
}

/* ------------------------------ */

static void
block_device_add (Daemon *daemon,
                  GUdevDevice *d,
                  gboolean emit_event)
{
  Device *device;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  device = g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
  if (device != NULL)
    {
      /* we already have the device; treat as change event */
      g_print ("**** TREATING ADD AS CHANGE %s\n", native_path);
      _device_changed (daemon, d, FALSE);
    }
  else
    {
      g_print ("**** ADDING %s\n", native_path);
      device = device_new (daemon, d);

      if (device != NULL)
        {
          /* assert that the device is fully loaded with info */
          g_assert (device_local_get_device_file (device) != NULL);
          g_assert (device_local_get_native_path (device) != NULL);
          g_assert (device_local_get_object_path (device) != NULL);
          g_assert (g_strcmp0 (native_path, device_local_get_native_path (device)) == 0);

          g_hash_table_insert (daemon->priv->map_dev_t_to_device,
                               GINT_TO_POINTER (device_local_get_dev (device)),
                               g_object_ref (device));
          g_hash_table_insert (daemon->priv->map_device_file_to_device,
                               g_strdup (device_local_get_device_file (device)),
                               g_object_ref (device));
          g_hash_table_insert (daemon->priv->map_native_path_to_device,
                               g_strdup (device_local_get_native_path (device)),
                               g_object_ref (device));
          g_hash_table_insert (daemon->priv->map_object_path_to_device,
                               g_strdup (device_local_get_object_path (device)),
                               g_object_ref (device));
          g_print ("**** ADDED %s\n", native_path);
          if (emit_event)
            {
              const char *object_path;
              object_path = device_local_get_object_path (device);
              g_print ("**** EMITTING ADDED for %s\n", device->priv->native_path);
              g_signal_emit (daemon, signals[DEVICE_ADDED_SIGNAL], 0, object_path);
            }
          daemon_local_update_poller (daemon);
          daemon_local_update_spindown (daemon);
        }
      else
        {
          g_print ("**** IGNORING ADD %s\n", native_path);
        }
    }
}

static void
device_add (Daemon *daemon,
            GUdevDevice *d,
            gboolean emit_event)
{
  const gchar *subsystem;

  subsystem = g_udev_device_get_subsystem (d);
  if (g_strcmp0 (subsystem, "block") == 0)
    block_device_add (daemon, d, emit_event);
  else if (g_strcmp0 (subsystem, "pci") == 0)
    pci_device_add (daemon, d, emit_event);
  else if (g_strcmp0 (subsystem, "scsi_host") == 0)
    scsi_host_device_add (daemon, d, emit_event);
  else if (g_strcmp0 (subsystem, "sas_phy") == 0)
    sas_phy_device_add (daemon, d, emit_event);
  else if (g_strcmp0 (subsystem, "sas_expander") == 0)
    sas_expander_device_add (daemon, d, emit_event);
  else
    g_warning ("Unhandled add event from subsystem `%s'", subsystem);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
pci_device_remove (Daemon *daemon,
                   GUdevDevice *d)
{
  Adapter *adapter;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  adapter = g_hash_table_lookup (daemon->priv->map_native_path_to_adapter, native_path);
  if (adapter == NULL)
    {
      g_print ("**** pci IGNORING REMOVE %s\n", native_path);
    }
  else
    {
      g_print ("**** pci REMOVING %s\n", native_path);

      g_warn_if_fail (g_strcmp0 (native_path, adapter->priv->native_path) == 0);

      g_hash_table_remove (daemon->priv->map_native_path_to_adapter, adapter->priv->native_path);
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_object_path_to_adapter, adapter->priv->object_path));

      g_print ("**** pci EMITTING REMOVED for %s\n", adapter->priv->native_path);
      g_signal_emit (daemon, signals[ADAPTER_REMOVED_SIGNAL], 0, adapter_local_get_object_path (adapter));

      adapter_removed (adapter);

      g_object_unref (adapter);
    }
}

/* ------------------------------ */

static void
scsi_host_device_remove (Daemon *daemon,
                         GUdevDevice *d)
{
  Port *port;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  port = g_hash_table_lookup (daemon->priv->map_native_path_to_port, native_path);
  if (port == NULL)
    {
      g_print ("**** scsi_host IGNORING REMOVE %s\n", native_path);
    }
  else
    {
      g_print ("**** scsi_host REMOVING %s\n", native_path);

      g_warn_if_fail (g_strcmp0 (native_path, port->priv->native_path) == 0);

      g_hash_table_remove (daemon->priv->map_native_path_to_port, port->priv->native_path);
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_object_path_to_port, port->priv->object_path));

      g_print ("**** scsi_host EMITTING REMOVED for %s\n", port->priv->native_path);
      g_signal_emit (daemon, signals[PORT_REMOVED_SIGNAL], 0, port_local_get_object_path (port));

      port_removed (port);

      g_object_unref (port);
    }
}

/* ------------------------------ */

static void
sas_phy_device_remove (Daemon *daemon,
                       GUdevDevice *d)
{
  Port *port;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  port = g_hash_table_lookup (daemon->priv->map_native_path_to_port, native_path);
  if (port == NULL)
    {
      g_print ("**** sas_phy IGNORING REMOVE %s\n", native_path);
    }
  else
    {
      g_print ("**** sas_phy REMOVING %s\n", native_path);

      g_warn_if_fail (g_strcmp0 (native_path, port->priv->native_path) == 0);

      g_hash_table_remove (daemon->priv->map_native_path_to_port, port->priv->native_path);
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_object_path_to_port, port->priv->object_path));

      g_print ("**** sas_phy EMITTING REMOVED for %s\n", port->priv->native_path);
      g_signal_emit (daemon, signals[PORT_REMOVED_SIGNAL], 0, port_local_get_object_path (port));

      port_removed (port);

      g_object_unref (port);
    }
}

/* ------------------------------ */

static void
sas_expander_device_remove (Daemon *daemon,
                            GUdevDevice *d)
{
  Expander *expander;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  expander = g_hash_table_lookup (daemon->priv->map_native_path_to_expander, native_path);
  if (expander == NULL)
    {
      g_print ("**** sas_expander IGNORING REMOVE %s\n", native_path);
    }
  else
    {
      g_print ("**** sas_expander REMOVING %s\n", native_path);

      g_warn_if_fail (g_strcmp0 (native_path, expander->priv->native_path) == 0);

      g_hash_table_remove (daemon->priv->map_native_path_to_expander, expander->priv->native_path);
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_object_path_to_expander, expander->priv->object_path));

      g_print ("**** sas_expander EMITTING REMOVED for %s\n", expander->priv->native_path);
      g_signal_emit (daemon, signals[EXPANDER_REMOVED_SIGNAL], 0, expander_local_get_object_path (expander));

      expander_removed (expander);

      g_object_unref (expander);
    }
}

/* ------------------------------ */

static void
block_device_remove (Daemon *daemon,
                     GUdevDevice *d)
{
  Device *device;
  const char *native_path;

  native_path = g_udev_device_get_sysfs_path (d);
  device = g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
  if (device == NULL)
    {
      g_print ("**** IGNORING REMOVE %s\n", native_path);
    }
  else
    {
      g_print ("**** REMOVING %s\n", native_path);

      g_warn_if_fail (g_strcmp0 (native_path, device->priv->native_path) == 0);

      g_hash_table_remove (daemon->priv->map_native_path_to_device, device->priv->native_path);
      /* Note that the created device file may actually disappear under certain
       * circumstances such as a 'change' event. In this case we discard the device
       * in update_info() and then we end up here.
       *
       * See https://bugs.freedesktop.org/show_bug.cgi?id=24264 for details.
       */
      if (device->priv->device_file != NULL)
        {
          g_hash_table_remove (daemon->priv->map_device_file_to_device, device->priv->device_file);
        }
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_object_path_to_device, device->priv->object_path));
      g_warn_if_fail (g_hash_table_remove (daemon->priv->map_dev_t_to_device, GINT_TO_POINTER (device->priv->dev)));

      g_print ("**** EMITTING REMOVED for %s\n", device->priv->native_path);
      g_signal_emit (daemon, signals[DEVICE_REMOVED_SIGNAL], 0, device_local_get_object_path (device));

      device_removed (device);

      g_object_unref (device);

      daemon_local_update_poller (daemon);
      daemon_local_update_spindown (daemon);
    }
}

static void
device_remove (Daemon *daemon,
               GUdevDevice *d)
{
  const gchar *subsystem;

  subsystem = g_udev_device_get_subsystem (d);
  if (g_strcmp0 (subsystem, "block") == 0)
    block_device_remove (daemon, d);
  else if (g_strcmp0 (subsystem, "pci") == 0)
    pci_device_remove (daemon, d);
  else if (g_strcmp0 (subsystem, "scsi_host") == 0)
    scsi_host_device_remove (daemon, d);
  else if (g_strcmp0 (subsystem, "sas_phy") == 0)
    sas_phy_device_remove (daemon, d);
  else if (g_strcmp0 (subsystem, "sas_expander") == 0)
    sas_expander_device_remove (daemon, d);
  else
    g_warning ("Unhandled remove event from subsystem `%s'", subsystem);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_uevent (GUdevClient *client,
           const char *action,
           GUdevDevice *device,
           gpointer user_data)
{
  Daemon *daemon = DAEMON (user_data);

  if (strcmp (action, "add") == 0)
    {
      device_add (daemon, device, TRUE);
    }
  else if (strcmp (action, "remove") == 0)
    {
      device_remove (daemon, device);
    }
  else if (strcmp (action, "change") == 0)
    {
      _device_changed (daemon, device, FALSE);
    }
  else
    {
      g_print ("*** NOTE: unhandled action '%s' on %s\n", action, g_udev_device_get_sysfs_path (device));
    }
}

Device *
daemon_local_find_by_dev (Daemon *daemon,
                          dev_t dev)
{
  return g_hash_table_lookup (daemon->priv->map_dev_t_to_device, GINT_TO_POINTER (dev));
}

Device *
daemon_local_find_by_device_file (Daemon *daemon,
                                  const char *device_file)
{
  return g_hash_table_lookup (daemon->priv->map_device_file_to_device, device_file);
}

Device *
daemon_local_find_by_native_path (Daemon *daemon,
                                  const char *native_path)
{
  return g_hash_table_lookup (daemon->priv->map_native_path_to_device, native_path);
}

Device *
daemon_local_find_by_object_path (Daemon *daemon,
                                  const char *object_path)
{
  return g_hash_table_lookup (daemon->priv->map_object_path_to_device, object_path);
}

GList *
daemon_local_get_all_devices (Daemon *daemon)
{
  return g_hash_table_get_values (daemon->priv->map_object_path_to_device);
}

static void
mount_removed (MountMonitor *monitor,
               Mount *mount,
               gpointer user_data)
{
  Daemon *daemon = DAEMON (user_data);
  Device *device;

  device = g_hash_table_lookup (daemon->priv->map_dev_t_to_device, GINT_TO_POINTER (mount_get_dev (mount)));
  if (device != NULL)
    {
      g_print ("**** UNMOUNTED %s\n", device->priv->native_path);
      daemon_local_synthesize_changed (daemon, device);
    }
}

static void
mount_added (MountMonitor *monitor,
             Mount *mount,
             gpointer user_data)
{
  Daemon *daemon = DAEMON (user_data);
  Device *device;

  device = g_hash_table_lookup (daemon->priv->map_dev_t_to_device, GINT_TO_POINTER (mount_get_dev (mount)));
  if (device != NULL)
    {
      g_print ("**** MOUNTED %s\n", device->priv->native_path);
      daemon_local_synthesize_changed (daemon, device);
    }
}

static gboolean
mdstat_changed_event (GIOChannel *channel,
                      GIOCondition cond,
                      gpointer user_data)
{
  Daemon *daemon = DAEMON (user_data);
  GHashTableIter iter;
  char *str;
  gsize len;
  Device *device;
  char *native_path;
  GPtrArray *a;
  int n;

  if (cond & ~G_IO_PRI)
    goto out;

  if (g_io_channel_seek (channel, 0, G_SEEK_SET) != G_IO_ERROR_NONE)
    {
      g_warning ("Cannot seek in /proc/mdstat");
      goto out;
    }

  g_io_channel_read_to_end (channel, &str, &len, NULL);

  /* synthesize this as a change event on _all_ md devices; need to be careful; the change
   * event might remove the device and thus change the hash table (e.g. invalidate our iterator)
   */
  a = g_ptr_array_new ();
  g_hash_table_iter_init (&iter, daemon->priv->map_native_path_to_device);
  while (g_hash_table_iter_next (&iter, (gpointer *) &native_path, (gpointer *) &device))
    {
      if (device->priv->device_is_linux_md)
        {
          g_ptr_array_add (a, g_object_ref (device->priv->d));
        }
    }

  for (n = 0; n < (int) a->len; n++)
    {
      GUdevDevice *d = a->pdata[n];
      g_debug ("using change on /proc/mdstat to trigger change event on %s", native_path);
      _device_changed (daemon, d, FALSE);
      g_object_unref (d);
    }

  g_ptr_array_free (a, TRUE);

 out:
  return TRUE;
}

static gboolean
refresh_ata_smart_data (Daemon *daemon)
{
  Device *device;
  const char *native_path;
  GHashTableIter iter;

  g_hash_table_iter_init (&iter, daemon->priv->map_native_path_to_device);
  while (g_hash_table_iter_next (&iter, (gpointer *) &native_path, (gpointer *) &device))
    {
      if (device->priv->drive_ata_smart_is_available)
        {
          char *options[] =
            { "nowakeup", NULL };

          g_print ("**** Refreshing ATA SMART data for %s\n", native_path);

          device_drive_ata_smart_refresh_data (device, options, NULL);
        }
    }

  /* update in another N seconds */
  daemon->priv->ata_smart_refresh_timer_id = g_timeout_add_seconds (ATA_SMART_REFRESH_INTERVAL_SECONDS,
                                                                    (GSourceFunc) refresh_ata_smart_data,
                                                                    daemon);

  return FALSE;
}

static gboolean
register_disks_daemon (Daemon *daemon)
{
  DBusConnection *connection;
  DBusError dbus_error;
  GError *error = NULL;
  const char *subsystems[] =
    { "block", /* Disks and partitions */
      "pci", /* Storage adapters */
      "scsi_host", /* ATA ports are represented by scsi_host */
      "sas_phy", /* SAS PHYs are represented by sas_phy */
      "sas_expander", /* SAS Expanders */
      NULL };

  daemon->priv->authority = polkit_authority_get ();

  error = NULL;
  daemon->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
  if (daemon->priv->system_bus_connection == NULL)
    {
      if (error != NULL)
        {
          g_critical ("error getting system bus: %s", error->message);
          g_error_free (error);
        }
      goto error;
    }
  connection = dbus_g_connection_get_connection (daemon->priv->system_bus_connection);

  dbus_g_connection_register_g_object (daemon->priv->system_bus_connection,
                                       "/org/freedesktop/UDisks",
                                       G_OBJECT (daemon));

  daemon->priv->system_bus_proxy = dbus_g_proxy_new_for_name (daemon->priv->system_bus_connection,
                                                              DBUS_SERVICE_DBUS,
                                                              DBUS_PATH_DBUS,
                                                              DBUS_INTERFACE_DBUS);
  dbus_error_init (&dbus_error);

  /* need to listen to NameOwnerChanged */
  dbus_bus_add_match (connection,
                      "type='signal'"
                      ",interface='"DBUS_INTERFACE_DBUS"'"
                      ",sender='"DBUS_SERVICE_DBUS"'"
                      ",member='NameOwnerChanged'",
                      &dbus_error);

  if (dbus_error_is_set (&dbus_error))
    {
      g_warning ("Cannot add match rule: %s: %s", dbus_error.name, dbus_error.message);
      dbus_error_free (&dbus_error);
      goto error;
    }

  if (!dbus_connection_add_filter (connection, _filter, daemon, NULL))
    {
      g_warning ("Cannot add D-Bus filter: %s: %s", dbus_error.name, dbus_error.message);
      goto error;
    }

  /* listen to /proc/mdstat for md changes
   *
   * Linux 2.6.19 and onwards throws a POLLPRI event for every change
   *
   * TODO: Some people might have md as a module so if it's not there
   *       we need to set up a watch for it to appear when loaded and
   *       then poll it. Sigh.
   */
  daemon->priv->mdstat_channel = g_io_channel_new_file ("/proc/mdstat", "r", &error);
  if (daemon->priv->mdstat_channel != NULL)
    {
      g_io_add_watch (daemon->priv->mdstat_channel, G_IO_PRI, mdstat_changed_event, daemon);
    }
  else
    {
      g_warning ("No /proc/mdstat file: %s", error->message);
      g_error_free (error);
      error = NULL;
    }

  /* connect to udev */
  daemon->priv->gudev_client = g_udev_client_new (subsystems);
  g_signal_connect (daemon->priv->gudev_client, "uevent", G_CALLBACK (on_uevent), daemon);

  daemon->priv->mount_monitor = mount_monitor_new ();
  g_signal_connect (daemon->priv->mount_monitor, "mount-added", (GCallback) mount_added, daemon);
  g_signal_connect (daemon->priv->mount_monitor, "mount-removed", (GCallback) mount_removed, daemon);

  return TRUE;
 error:
  return FALSE;
}

Daemon *
daemon_new (void)
{
  Daemon *daemon;
  GList *devices;
  GList *l;
  Device *device;
  GHashTableIter device_iter;

  PROFILE ("daemon_new(): start");

  daemon = DAEMON (g_object_new (TYPE_DAEMON, NULL));

  PROFILE ("daemon_new(): register_disks_daemon");
  if (!register_disks_daemon (DAEMON (daemon)))
    {
      g_object_unref (daemon);
      goto error;
    }

  /* process storage adapters */
  PROFILE ("daemon_new(): storage adapters");
  devices = g_udev_client_query_by_subsystem (daemon->priv->gudev_client, "pci");
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = l->data;
      device_add (daemon, device, FALSE);
    }
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);

  /* process ATA ports */
  PROFILE ("daemon_new(): ATA ports");
  devices = g_udev_client_query_by_subsystem (daemon->priv->gudev_client, "scsi_host");
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = l->data;
      device_add (daemon, device, FALSE);
    }
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);

  /* process SAS Expanders */
  PROFILE ("daemon_new(): SAS Expanders");
  devices = g_udev_client_query_by_subsystem (daemon->priv->gudev_client, "sas_expander");
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = l->data;
      device_add (daemon, device, FALSE);
    }
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);

  /* process SAS PHYs */
  PROFILE ("daemon_new(): process SAS PHYs");
  devices = g_udev_client_query_by_subsystem (daemon->priv->gudev_client, "sas_phy");
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = l->data;
      device_add (daemon, device, FALSE);
    }
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);

  /* reprocess SAS expanders to get the right Ports associated
   *
   * TODO: ideally there would be a way to properly traverse a whole subtree using gudev
   * so we could visit everything in the proper order.
   */
  PROFILE ("daemon_new(): reprocess SAS expanders");
  devices = g_udev_client_query_by_subsystem (daemon->priv->gudev_client, "sas_expander");
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = l->data;
      device_add (daemon, device, FALSE);
    }
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);

  /* process block devices (disks and partitions) */
  PROFILE ("daemon_new(): block devices");
  devices = g_udev_client_query_by_subsystem (daemon->priv->gudev_client, "block");
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = l->data;
      device_add (daemon, device, FALSE);
    }
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);

  /* now refresh data for all devices just added to get slave/holder relationships
   * properly initialized
   */
  PROFILE ("daemon_new(): refresh");
  g_hash_table_iter_init (&device_iter, daemon->priv->map_object_path_to_device);
  while (g_hash_table_iter_next (&device_iter, NULL, (gpointer) & device))
    {
      daemon_local_synthesize_changed (daemon, device);
    }

  /* clean stale directories in /media as well as stale
   * entries in /var/lib/udisks/mtab
   */
  PROFILE ("daemon_new(): clean up stale locks and mount points");
  l = g_hash_table_get_values (daemon->priv->map_native_path_to_device);
  mount_file_clean_stale (l);
  g_list_free (l);

  /* set up timer for refreshing ATA SMART data - we don't want to refresh immediately because
   * when adding a device we also do this...
   */
  daemon->priv->ata_smart_refresh_timer_id = g_timeout_add_seconds (ATA_SMART_REFRESH_INTERVAL_SECONDS,
                                                                    (GSourceFunc) refresh_ata_smart_data,
                                                                    daemon);

  PROFILE ("daemon_new(): end");
  return daemon;

 error:
  PROFILE ("daemon_new(): existing with error");
  return NULL;
}

MountMonitor *
daemon_local_get_mount_monitor (Daemon *daemon)
{
  return daemon->priv->mount_monitor;
}

/*--------------------------------------------------------------------------------------------------------------*/

static gboolean
throw_error (DBusGMethodInvocation *context,
             int error_code,
             const char *format,
             ...)
{
  GError *error;
  va_list args;
  char *message;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  if (context != NULL)
    {
      error = g_error_new (ERROR, error_code, "%s", message);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
  else
    {
      /* error from a daemon-internal method call */
      g_warning ("%s", message);
    }
  g_free (message);
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
daemon_local_get_uid (Daemon *daemon,
                      uid_t *out_uid,
                      DBusGMethodInvocation *context)
{
  gchar *sender;
  DBusError dbus_error;
  DBusConnection *connection;

  /* context can be NULL for things called by the daemon itself e.g. ATA SMART refresh */
  if (context == NULL)
    {
      *out_uid = 0;
      goto out;
    }

  /* TODO: right now this is synchronous and slow; when we switch to a better D-Bus
   *       binding a'la EggDBus there will be a utility class (with caching) where we
   *       can get this from
   */

  sender = dbus_g_method_get_sender (context);
  connection = dbus_g_connection_get_connection (daemon->priv->system_bus_connection);
  dbus_error_init (&dbus_error);
  *out_uid = dbus_bus_get_unix_user (connection, sender, &dbus_error);
  if (dbus_error_is_set (&dbus_error))
    {
      *out_uid = 0;
      g_warning ("Cannot get uid for sender %s: %s: %s", sender, dbus_error.name, dbus_error.message);
      dbus_error_free (&dbus_error);
    }
  g_free (sender);

 out:
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

void
daemon_local_update_poller (Daemon *daemon)
{
  GHashTableIter hash_iter;
  Device *device;
  GList *devices_to_poll;

  devices_to_poll = NULL;

  g_hash_table_iter_init (&hash_iter, daemon->priv->map_object_path_to_device);
  while (g_hash_table_iter_next (&hash_iter, NULL, (gpointer) & device))
    {
      if (device->priv->device_is_media_change_detected && device->priv->device_is_media_change_detection_polling)
        devices_to_poll = g_list_prepend (devices_to_poll, device);
    }

  poller_set_devices (devices_to_poll);

  g_list_free (devices_to_poll);
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct
{
  gchar *action_id;
  CheckAuthCallback check_auth_callback;
  DBusGMethodInvocation *context;
  Daemon *daemon;
  Device *device;

  GCancellable *cancellable;
  guint num_user_data;
  gpointer *user_data_elements;
  GDestroyNotify *user_data_notifiers;

  Inhibitor *caller;
} CheckAuthData;

/* invoked when device is removed during authorization check */
static void
lca_device_went_away (gpointer user_data,
                      GObject *where_the_object_was)
{
  CheckAuthData *data = user_data;

  g_object_weak_unref (G_OBJECT (data->device), lca_device_went_away, data);
  data->device = NULL;

  /* this will trigger lca_check_authorization_callback() */
  g_cancellable_cancel (data->cancellable);
}

/* invoked when caller disconnects during authorization check */
static void
lca_caller_disconnected_cb (Inhibitor *inhibitor,
                            gpointer user_data)
{
  CheckAuthData *data = user_data;

  /* this will trigger lca_check_authorization_callback() */
  g_cancellable_cancel (data->cancellable);
}

static void
check_auth_data_free (CheckAuthData *data)
{
  guint n;

  g_free (data->action_id);
  g_object_unref (data->daemon);
  if (data->device != NULL)
    g_object_weak_unref (G_OBJECT (data->device), lca_device_went_away, data);
  g_object_unref (data->cancellable);
  for (n = 0; n < data->num_user_data; n++)
    {
      if (data->user_data_notifiers[n] != NULL)
        data->user_data_notifiers[n] (data->user_data_elements[n]);
    }
  g_free (data->user_data_elements);
  g_free (data->user_data_notifiers);
  if (data->caller != NULL)
    g_object_unref (data->caller);
  g_free (data);
}

static void
lca_check_authorization_callback (PolkitAuthority *authority,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
  CheckAuthData *data = user_data;
  PolkitAuthorizationResult *result;
  GError *error;
  gboolean is_authorized;

  is_authorized = FALSE;

  error = NULL;
  result = polkit_authority_check_authorization_finish (authority, res, &error);
  if (error != NULL)
    {
      throw_error (data->context, ERROR_PERMISSION_DENIED, "Not Authorized: %s", error->message);
      g_error_free (error);
    }
  else
    {
      if (polkit_authorization_result_get_is_authorized (result))
        {
          is_authorized = TRUE;
        }
      else if (polkit_authorization_result_get_is_challenge (result))
        {
          throw_error (data->context, ERROR_PERMISSION_DENIED, "Authentication is required");
        }
      else
        {
          throw_error (data->context, ERROR_PERMISSION_DENIED, "Not Authorized");
        }
      g_object_unref (result);
    }

  if (is_authorized)
    {
      data->check_auth_callback (data->daemon,
                                 data->device,
                                 data->context,
                                 data->action_id,
                                 data->num_user_data,
                                 data->user_data_elements);
    }

  check_auth_data_free (data);
}

/* num_user_data param is followed by @num_user_data (gpointer, GDestroyNotify) pairs.. */
void
daemon_local_check_auth (Daemon *daemon,
                         Device *device,
                         const gchar *action_id,
                         const gchar *operation,
                         gboolean allow_user_interaction,
                         CheckAuthCallback check_auth_callback,
                         DBusGMethodInvocation *context,
                         guint num_user_data,
                         ...)
{
  CheckAuthData *data;
  va_list va_args;
  guint n;

  data = g_new0 (CheckAuthData, 1);
  data->action_id = g_strdup (action_id);
  data->check_auth_callback = check_auth_callback;
  data->context = context;
  data->daemon = g_object_ref (daemon);
  data->device = device;
  if (device != NULL)
    g_object_weak_ref (G_OBJECT (device), lca_device_went_away, data);

  data->cancellable = g_cancellable_new ();
  data->num_user_data = num_user_data;
  data->user_data_elements = g_new0 (gpointer, num_user_data);
  data->user_data_notifiers = g_new0 (GDestroyNotify, num_user_data);

  va_start (va_args, num_user_data);
  for (n = 0; n < num_user_data; n++)
    {
      data->user_data_elements[n] = va_arg (va_args, gpointer);
      data->user_data_notifiers[n] = va_arg (va_args, GDestroyNotify);
    }
  va_end (va_args);

  if (daemon_local_is_inhibited (daemon))
    {
      throw_error (data->context, ERROR_INHIBITED, "Daemon is inhibited");
      check_auth_data_free (data);

    }
  else if (action_id != NULL)
    {
      PolkitSubject *subject;
      PolkitDetails *details;
      PolkitCheckAuthorizationFlags flags;
      gchar partition_number_buf[32];

      /* Set details - see polkit-action-lookup.c for where
       * these key/value pairs are used
       */
      details = polkit_details_new ();
      if (operation != NULL)
        {
          polkit_details_insert (details, "operation", (gpointer) operation);
        }
      if (device != NULL)
        {
          Device *drive;

          polkit_details_insert (details, "unix-device", device->priv->device_file);
          if (device->priv->device_file_by_id->len > 0)
            polkit_details_insert (details, "unix-device-by-id", device->priv->device_file_by_id->pdata[0]);
          if (device->priv->device_file_by_path->len > 0)
            polkit_details_insert (details, "unix-device-by-path", device->priv->device_file_by_path->pdata[0]);

          if (device->priv->device_is_drive)
            {
              drive = device;
            }
          else if (device->priv->device_is_partition)
            {
              polkit_details_insert (details, "is-partition", "1");
              g_snprintf (partition_number_buf, sizeof partition_number_buf, "%d", device->priv->partition_number);
              polkit_details_insert (details, "partition-number", partition_number_buf);
              drive = daemon_local_find_by_object_path (device->priv->daemon, device->priv->partition_slave);
            }
          else
            {
              drive = NULL;
            }

          if (drive != NULL)
            {
              polkit_details_insert (details, "drive-unix-device", drive->priv->device_file);
              if (drive->priv->device_file_by_id->len > 0)
                polkit_details_insert (details, "drive-unix-device-by-id", drive->priv->device_file_by_id->pdata[0]);
              if (drive->priv->device_file_by_path->len > 0)
                polkit_details_insert (details, "drive-unix-device-by-path", drive->priv->device_file_by_path->pdata[0]);
              if (drive->priv->drive_vendor != NULL)
                polkit_details_insert (details, "drive-vendor", drive->priv->drive_vendor);
              if (drive->priv->drive_model != NULL)
                polkit_details_insert (details, "drive-model", drive->priv->drive_model);
              if (drive->priv->drive_revision != NULL)
                polkit_details_insert (details, "drive-revision", drive->priv->drive_revision);
              if (drive->priv->drive_serial != NULL)
                polkit_details_insert (details, "drive-serial", drive->priv->drive_serial);
              if (drive->priv->drive_connection_interface != NULL)
                polkit_details_insert (details, "drive-connection-interface", drive->priv->drive_connection_interface);
            }
        }

      subject = polkit_system_bus_name_new (dbus_g_method_get_sender (context));

      data->caller = inhibitor_new (context);
      g_signal_connect (data->caller, "disconnected", G_CALLBACK (lca_caller_disconnected_cb), data);

      flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE;
      if (allow_user_interaction)
        flags |= POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;
      polkit_authority_check_authorization (daemon->priv->authority,
                                            subject,
                                            action_id,
                                            details,
                                            flags,
                                            data->cancellable,
                                            (GAsyncReadyCallback) lca_check_authorization_callback,
                                            data);

      g_object_unref (subject);
      g_object_unref (details);
    }
  else
    {
      data->check_auth_callback (data->daemon,
                                 data->device,
                                 data->context,
                                 data->action_id,
                                 data->num_user_data,
                                 data->user_data_elements);
      check_auth_data_free (data);
    }
}

/*--------------------------------------------------------------------------------------------------------------*/

#define SYSFS_BLOCK_STAT_MAX_SIZE 256

static void
disk_set_standby_timeout_child_watch_cb (GPid pid,
                                         gint status,
                                         gpointer user_data)
{
  Device *device = DEVICE (user_data);

  if (WIFEXITED (status) && WEXITSTATUS (status) == 0)
    {
      g_print ("**** NOTE: standby helper for %s completed successfully\n", device->priv->device_file);
    }
  else
    {
      g_warning ("standby helper for %s failed with exit code %d (if_exited=%d)\n",
                 device->priv->device_file,
                 WEXITSTATUS (status),
                 WIFEXITED (status));
    }

  g_object_unref (device);
}

static void
disk_set_standby_timeout (Device *device)
{
  GError *error;
  GPid pid;
  gint value;
  gchar *argv[5] =
    { "hdparm", "-S", NULL, /* argv[2]: timeout value */
      NULL, /* argv[3]: device_file */
      NULL };

  if (device->priv->spindown_timeout == 0)
    {
      value = 0;
    }
  else if (device->priv->spindown_timeout <= 240 * 5)
    {
      /* 1...240 are blocks of 5 secs */
      value = device->priv->spindown_timeout / 5;
    }
  else if (device->priv->spindown_timeout <= (5 * 60 + 30) * 60)
    {
      /* 241...251 are blocks of 30 minutes */
      value = device->priv->spindown_timeout / (30 * 60) + 240;
      if (value == 240)
        value = 241;
    }
  else
    {
      /* so max timeout is 5.5 hours (252, 253, 244, 255 are vendor-specific / uninteresting) */
      value = 251;
    }

  argv[2] = g_strdup_printf ("%d", value);
  argv[3] = device->priv->device_file;

  error = NULL;
  if (!g_spawn_async_with_pipes (NULL,
                                 argv,
                                 NULL,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,
                                 &pid,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &error))
    {
      g_warning ("Error launching %s: %s", argv[0], error->message);
      g_error_free (error);
      goto out;
    }

  g_child_watch_add (pid, disk_set_standby_timeout_child_watch_cb, g_object_ref (device));

 out:
  g_free (argv[2]);
}

void
daemon_local_update_spindown (Daemon *daemon)
{
  GHashTableIter hash_iter;
  Device *device;
  GList *l;

  g_hash_table_iter_init (&hash_iter, daemon->priv->map_object_path_to_device);
  while (g_hash_table_iter_next (&hash_iter, NULL, (gpointer) & device))
    {
      gint spindown_timeout;

      if (!device->priv->device_is_drive || !device->priv->drive_can_spindown)
        continue;

      spindown_timeout = 0;
      if (device->priv->spindown_inhibitors == NULL && daemon->priv->spindown_inhibitors == NULL)
        {
          /* no inhibitors */
        }
      else
        {

          spindown_timeout = G_MAXINT;

          /* first go through all inhibitors on the device */
          for (l = device->priv->spindown_inhibitors; l != NULL; l = l->next)
            {
              Inhibitor *inhibitor = INHIBITOR (l->data);
              gint spindown_timeout_inhibitor;

              spindown_timeout_inhibitor = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (inhibitor),
                                                                               "spindown-timeout-seconds"));
              g_warn_if_fail (spindown_timeout_inhibitor > 0);

              if (spindown_timeout_inhibitor < spindown_timeout)
                spindown_timeout = spindown_timeout_inhibitor;
            }

          /* then all inhibitors on the daemon */
          for (l = daemon->priv->spindown_inhibitors; l != NULL; l = l->next)
            {
              Inhibitor *inhibitor = INHIBITOR (l->data);
              gint spindown_timeout_inhibitor;

              spindown_timeout_inhibitor = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (inhibitor),
                                                                               "spindown-timeout-seconds"));
              g_warn_if_fail (spindown_timeout_inhibitor > 0);

              if (spindown_timeout_inhibitor < spindown_timeout)
                spindown_timeout = spindown_timeout_inhibitor;
            }
        }

      if (device->priv->spindown_timeout != spindown_timeout)
        {
          device->priv->spindown_timeout = spindown_timeout;
          /* just assume this always works... */
          disk_set_standby_timeout (device);
        }
    }
}

/*--------------------------------------------------------------------------------------------------------------*/

Adapter *
daemon_local_find_enclosing_adapter (Daemon *daemon,
                                     const gchar *native_path)
{
  GHashTableIter iter;
  const gchar *adapter_native_path;
  Adapter *adapter;
  Adapter *ret;

  ret = NULL;

  g_hash_table_iter_init (&iter, daemon->priv->map_native_path_to_adapter);
  while (g_hash_table_iter_next (&iter, (gpointer) & adapter_native_path, (gpointer) & adapter))
    {
      if (g_str_has_prefix (native_path, adapter_native_path))
        {
          ret = adapter;
          break;
        }
    }

  return ret;
}

Expander *
daemon_local_find_enclosing_expander (Daemon *daemon,
                                      const gchar *native_path)
{
  GHashTableIter iter;
  Expander *expander;
  Expander *ret;

  ret = NULL;

  g_hash_table_iter_init (&iter, daemon->priv->map_native_path_to_expander);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer) & expander))
    {
      if (local_expander_encloses_native_path (expander, native_path))
        {
          ret = expander;
          break;
        }
    }

  return ret;
}

GList *
daemon_local_find_enclosing_ports (Daemon *daemon,
                                   const gchar *native_path)
{
  GHashTableIter iter;
  Port *port;
  GList *ret;

  ret = NULL;

  g_hash_table_iter_init (&iter, daemon->priv->map_native_path_to_port);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer) & port))
    {
      if (local_port_encloses_native_path (port, native_path))
        {
          ret = g_list_append (ret, port);
        }
    }

  return ret;
}

/*--------------------------------------------------------------------------------------------------------------*/
/* exported methods */

static void
enumerate_cb (gpointer key,
              gpointer value,
              gpointer user_data)
{
  Device *device = DEVICE (value);
  GPtrArray *object_paths = user_data;
  g_ptr_array_add (object_paths, g_strdup (device_local_get_object_path (device)));
}

gboolean
daemon_enumerate_devices (Daemon *daemon,
                          DBusGMethodInvocation *context)
{
  GPtrArray *object_paths;

  /* TODO: enumerate in the right order wrt. dm/md..
   *
   * see also gdu_pool_new() in src/gdu-pool.c in g-d-u
   */

  object_paths = g_ptr_array_new ();
  g_hash_table_foreach (daemon->priv->map_native_path_to_device, enumerate_cb, object_paths);
  dbus_g_method_return (context, object_paths);
  g_ptr_array_foreach (object_paths, (GFunc) g_free, NULL);
  g_ptr_array_free (object_paths, TRUE);
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
enumerate_adapter_cb (gpointer key,
                      gpointer value,
                      gpointer user_data)
{
  Adapter *adapter = ADAPTER (value);
  GPtrArray *object_paths = user_data;
  g_ptr_array_add (object_paths, g_strdup (adapter_local_get_object_path (adapter)));
}

/* dbus-send --system --print-reply --dest=org.freedesktop.UDisks /org/freedesktop/UDisks org.freedesktop.UDisks.EnumerateAdapters
 */
gboolean
daemon_enumerate_adapters (Daemon *daemon,
                           DBusGMethodInvocation *context)
{
  GPtrArray *object_paths;

  object_paths = g_ptr_array_new ();
  g_hash_table_foreach (daemon->priv->map_native_path_to_adapter, enumerate_adapter_cb, object_paths);
  dbus_g_method_return (context, object_paths);
  g_ptr_array_foreach (object_paths, (GFunc) g_free, NULL);
  g_ptr_array_free (object_paths, TRUE);
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
enumerate_expander_cb (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  Expander *expander = EXPANDER (value);
  GPtrArray *object_paths = user_data;
  g_ptr_array_add (object_paths, g_strdup (expander_local_get_object_path (expander)));
}

/* dbus-send --system --print-reply --dest=org.freedesktop.UDisks /org/freedesktop/UDisks org.freedesktop.UDisks.EnumerateExpanders
 */
gboolean
daemon_enumerate_expanders (Daemon *daemon,
                            DBusGMethodInvocation *context)
{
  GPtrArray *object_paths;

  object_paths = g_ptr_array_new ();
  g_hash_table_foreach (daemon->priv->map_native_path_to_expander, enumerate_expander_cb, object_paths);
  dbus_g_method_return (context, object_paths);
  g_ptr_array_foreach (object_paths, (GFunc) g_free, NULL);
  g_ptr_array_free (object_paths, TRUE);
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
enumerate_port_cb (gpointer key,
                   gpointer value,
                   gpointer user_data)
{
  Port *port = PORT (value);
  GPtrArray *object_paths = user_data;
  g_ptr_array_add (object_paths, g_strdup (port_local_get_object_path (port)));
}

/* dbus-send --system --print-reply --dest=org.freedesktop.UDisks /org/freedesktop/UDisks org.freedesktop.UDisks.EnumeratePorts
 */
gboolean
daemon_enumerate_ports (Daemon *daemon,
                        DBusGMethodInvocation *context)
{
  GPtrArray *object_paths;

  object_paths = g_ptr_array_new ();
  g_hash_table_foreach (daemon->priv->map_native_path_to_port, enumerate_port_cb, object_paths);
  dbus_g_method_return (context, object_paths);
  g_ptr_array_foreach (object_paths, (GFunc) g_free, NULL);
  g_ptr_array_free (object_paths, TRUE);
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
enumerate_device_files_cb (gpointer key,
                           gpointer value,
                           gpointer user_data)
{
  Device *device = DEVICE (value);
  GPtrArray *device_files = user_data;
  guint n;

  g_ptr_array_add (device_files, g_strdup (device_local_get_device_file (device)));

  for (n = 0; n < device->priv->device_file_by_id->len; n++)
    {
      g_ptr_array_add (device_files, g_strdup (((gchar **) device->priv->device_file_by_id->pdata)[n]));
    }

  for (n = 0; n < device->priv->device_file_by_path->len; n++)
    {
      g_ptr_array_add (device_files, g_strdup (((gchar **) device->priv->device_file_by_path->pdata)[n]));
    }
}

gboolean
daemon_enumerate_device_files (Daemon *daemon,
                               DBusGMethodInvocation *context)
{
  GPtrArray *device_files;

  device_files = g_ptr_array_new ();
  g_hash_table_foreach (daemon->priv->map_native_path_to_device, enumerate_device_files_cb, device_files);
  g_ptr_array_add (device_files, NULL);
  dbus_g_method_return (context, device_files->pdata);
  g_ptr_array_foreach (device_files, (GFunc) g_free, NULL);
  g_ptr_array_free (device_files, TRUE);
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
daemon_find_device_by_device_file (Daemon *daemon,
                                   const char *device_file,
                                   DBusGMethodInvocation *context)
{
  const char *object_path;
  Device *device;
  gchar canonical_device_file[PATH_MAX];

  if (realpath (device_file, canonical_device_file) != NULL)
      device = daemon_local_find_by_device_file (daemon, canonical_device_file);
  else
      /* Hm, not an existing device? Let's try with the original file name */
      device = daemon_local_find_by_device_file (daemon, device_file);

  object_path = NULL;

  if (device != NULL)
    {
      object_path = device_local_get_object_path (device);
      dbus_g_method_return (context, object_path);
    }
  else
    {
      throw_error (context, ERROR_FAILED, "No such device");
    }

  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
daemon_find_device_by_major_minor (Daemon *daemon,
                                   gint64 major,
                                   gint64 minor,
                                   DBusGMethodInvocation *context)
{
  const char *object_path;
  Device *device;
  dev_t dev;

  dev = makedev (major, minor);

  object_path = NULL;

  device = daemon_local_find_by_dev (daemon, dev);
  if (device != NULL)
    {
      object_path = device_local_get_object_path (device);
      dbus_g_method_return (context, object_path);
    }
  else
    {
      throw_error (context, ERROR_FAILED, "No such device");
    }

  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
daemon_polling_inhibitor_disconnected_cb (Inhibitor *inhibitor,
                                          Daemon *daemon)
{
  daemon->priv->polling_inhibitors = g_list_remove (daemon->priv->polling_inhibitors, inhibitor);
  g_signal_handlers_disconnect_by_func (inhibitor, daemon_polling_inhibitor_disconnected_cb, daemon);
  g_object_unref (inhibitor);

  daemon_local_synthesize_changed_on_all_devices (daemon);
  daemon_local_update_poller (daemon);
}

gboolean
daemon_local_has_polling_inhibitors (Daemon *daemon)
{
  return daemon->priv->polling_inhibitors != NULL;
}

static void
daemon_drive_inhibit_all_polling_authorized_cb (Daemon *daemon,
                                                Device *device,
                                                DBusGMethodInvocation *context,
                                                const gchar *action_id,
                                                guint num_user_data,
                                                gpointer *user_data_elements)
{
  gchar **options = user_data_elements[0];
  Inhibitor *inhibitor;
  guint n;

  for (n = 0; options[n] != NULL; n++)
    {
      const char *option = options[n];
      throw_error (context, ERROR_INVALID_OPTION, "Unknown option %s", option);
      goto out;
    }

  inhibitor = inhibitor_new (context);

  daemon->priv->polling_inhibitors = g_list_prepend (daemon->priv->polling_inhibitors, inhibitor);
  g_signal_connect (inhibitor, "disconnected", G_CALLBACK (daemon_polling_inhibitor_disconnected_cb), daemon);

  daemon_local_synthesize_changed_on_all_devices (daemon);
  daemon_local_update_poller (daemon);

  dbus_g_method_return (context, inhibitor_get_cookie (inhibitor));

 out:
  ;
}

gboolean
daemon_drive_inhibit_all_polling (Daemon *daemon,
                                  char **options,
                                  DBusGMethodInvocation *context)
{
  daemon_local_check_auth (daemon,
                           NULL,
                           "org.freedesktop.udisks.inhibit-polling",
                           "InhibitAllPolling",
                           TRUE,
                           daemon_drive_inhibit_all_polling_authorized_cb,
                           context,
                           1,
                           g_strdupv (options),
                           g_strfreev);
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
daemon_drive_uninhibit_all_polling (Daemon *daemon,
                                    char *cookie,
                                    DBusGMethodInvocation *context)
{
  const gchar *sender;
  Inhibitor *inhibitor;
  GList *l;

  sender = dbus_g_method_get_sender (context);

  inhibitor = NULL;
  for (l = daemon->priv->polling_inhibitors; l != NULL; l = l->next)
    {
      Inhibitor *i = INHIBITOR (l->data);

      if (g_strcmp0 (inhibitor_get_unique_dbus_name (i), sender) == 0 && g_strcmp0 (inhibitor_get_cookie (i), cookie)
          == 0)
        {
          inhibitor = i;
          break;
        }
    }

  if (inhibitor == NULL)
    {
      throw_error (context, ERROR_FAILED, "No such inhibitor");
      goto out;
    }

  daemon->priv->polling_inhibitors = g_list_remove (daemon->priv->polling_inhibitors, inhibitor);
  g_object_unref (inhibitor);

  daemon_local_synthesize_changed_on_all_devices (daemon);
  daemon_local_update_poller (daemon);

  dbus_g_method_return (context);

 out:
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
daemon_inhibitor_disconnected_cb (Inhibitor *inhibitor,
                                  Daemon *daemon)
{
  daemon->priv->inhibitors = g_list_remove (daemon->priv->inhibitors, inhibitor);
  g_signal_handlers_disconnect_by_func (inhibitor, daemon_inhibitor_disconnected_cb, daemon);
  g_object_unref (inhibitor);
}

gboolean
daemon_local_is_inhibited (Daemon *daemon)
{
  return daemon->priv->inhibitors != NULL;
}

gboolean
daemon_inhibit (Daemon *daemon,
                DBusGMethodInvocation *context)
{
  Inhibitor *inhibitor;
  uid_t uid;

  if (!daemon_local_get_uid (daemon, &uid, context))
    goto out;

  if (uid != 0)
    {
      throw_error (context, ERROR_FAILED, "Only uid 0 is authorized to inhibit the daemon");
      goto out;
    }

  inhibitor = inhibitor_new (context);

  daemon->priv->inhibitors = g_list_prepend (daemon->priv->inhibitors, inhibitor);
  g_signal_connect (inhibitor, "disconnected", G_CALLBACK (daemon_inhibitor_disconnected_cb), daemon);

  dbus_g_method_return (context, inhibitor_get_cookie (inhibitor));

 out:
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

gboolean
daemon_uninhibit (Daemon *daemon,
                  char *cookie,
                  DBusGMethodInvocation *context)
{
  const gchar *sender;
  Inhibitor *inhibitor;
  GList *l;

  sender = dbus_g_method_get_sender (context);

  inhibitor = NULL;
  for (l = daemon->priv->inhibitors; l != NULL; l = l->next)
    {
      Inhibitor *i = INHIBITOR (l->data);

      if (g_strcmp0 (inhibitor_get_unique_dbus_name (i), sender) == 0 && g_strcmp0 (inhibitor_get_cookie (i), cookie)
          == 0)
        {
          inhibitor = i;
          break;
        }
    }

  if (inhibitor == NULL)
    {
      throw_error (context, ERROR_FAILED, "No such inhibitor");
      goto out;
    }

  daemon->priv->inhibitors = g_list_remove (daemon->priv->inhibitors, inhibitor);
  g_object_unref (inhibitor);

  dbus_g_method_return (context);

 out:
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void
daemon_spindown_inhibitor_disconnected_cb (Inhibitor *inhibitor,
                                           Daemon *daemon)
{
  daemon->priv->spindown_inhibitors = g_list_remove (daemon->priv->spindown_inhibitors, inhibitor);
  g_signal_handlers_disconnect_by_func (inhibitor, daemon_spindown_inhibitor_disconnected_cb, daemon);
  g_object_unref (inhibitor);

  daemon_local_update_spindown (daemon);
}

static void
daemon_drive_set_all_spindown_timeouts_authorized_cb (Daemon *daemon,
                                                      Device *device,
                                                      DBusGMethodInvocation *context,
                                                      const gchar *action_id,
                                                      guint num_user_data,
                                                      gpointer *user_data_elements)
{
  gint timeout_seconds = GPOINTER_TO_INT (user_data_elements[0]);
  gchar **options = user_data_elements[1];
  Inhibitor *inhibitor;
  guint n;

  if (timeout_seconds < 1)
    {
      throw_error (context, ERROR_FAILED, "Timeout seconds must be at least 1");
      goto out;
    }

  for (n = 0; options[n] != NULL; n++)
    {
      const char *option = options[n];
      throw_error (context, ERROR_INVALID_OPTION, "Unknown option %s", option);
      goto out;
    }

  inhibitor = inhibitor_new (context);

  g_object_set_data (G_OBJECT (inhibitor), "spindown-timeout-seconds", GINT_TO_POINTER (timeout_seconds));

  daemon->priv->spindown_inhibitors = g_list_prepend (daemon->priv->spindown_inhibitors, inhibitor);
  g_signal_connect (inhibitor, "disconnected", G_CALLBACK (daemon_spindown_inhibitor_disconnected_cb), daemon);

  daemon_local_update_spindown (daemon);

  dbus_g_method_return (context, inhibitor_get_cookie (inhibitor));

 out:
  ;
}

gboolean
daemon_drive_set_all_spindown_timeouts (Daemon *daemon,
                                        int timeout_seconds,
                                        char **options,
                                        DBusGMethodInvocation *context)
{
  if (timeout_seconds < 1)
    {
      throw_error (context, ERROR_FAILED, "Timeout seconds must be at least 1");
      goto out;
    }

  daemon_local_check_auth (daemon,
                           NULL,
                           "org.freedesktop.udisks.drive-set-spindown",
                           "DriveSetAllSpindownTimeouts",
                           TRUE,
                           daemon_drive_set_all_spindown_timeouts_authorized_cb,
                           context,
                           2,
                           GINT_TO_POINTER (timeout_seconds),
                           NULL,
                           g_strdupv (options),
                           g_strfreev);

 out:
  return TRUE;
}

gboolean
daemon_drive_unset_all_spindown_timeouts (Daemon *daemon,
                                          char *cookie,
                                          DBusGMethodInvocation *context)
{
  const gchar *sender;
  Inhibitor *inhibitor;
  GList *l;

  sender = dbus_g_method_get_sender (context);

  inhibitor = NULL;
  for (l = daemon->priv->spindown_inhibitors; l != NULL; l = l->next)
    {
      Inhibitor *i = INHIBITOR (l->data);

      if (g_strcmp0 (inhibitor_get_unique_dbus_name (i), sender) == 0 && g_strcmp0 (inhibitor_get_cookie (i), cookie)
          == 0)
        {
          inhibitor = i;
          break;
        }
    }

  if (inhibitor == NULL)
    {
      throw_error (context, ERROR_FAILED, "No such spindown configurator");
      goto out;
    }

  daemon->priv->spindown_inhibitors = g_list_remove (daemon->priv->spindown_inhibitors, inhibitor);
  g_object_unref (inhibitor);

  daemon_local_update_spindown (daemon);

  dbus_g_method_return (context);

 out:
  return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/
