/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2009 David Zeuthen <david@fubar.dk>
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

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <gio/gunixmounts.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gudev/gudev.h>
#include <atasmart.h>

#include "daemon.h"
#include "adapter.h"
#include "adapter-private.h"
#include "marshal.h"

/*--------------------------------------------------------------------------------------------------------------*/
#include "adapter-glue.h"

static void adapter_class_init (AdapterClass *klass);
static void adapter_init (Adapter *seat);
static void adapter_finalize (GObject *object);

static gboolean update_info (Adapter *adapter);

static void drain_pending_changes (Adapter *adapter,
                                   gboolean force_update);

enum
{
  PROP_0,
  PROP_NATIVE_PATH,

  PROP_VENDOR,
  PROP_MODEL,
  PROP_DRIVER,
  PROP_NUM_PORTS,
  PROP_FABRIC,
};

enum
{
  CHANGED_SIGNAL,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (Adapter, adapter, G_TYPE_OBJECT)

#define ADAPTER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TYPE_ADAPTER, AdapterPrivate))

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
  Adapter *adapter = ADAPTER (object);

  switch (prop_id)
    {
    case PROP_NATIVE_PATH:
      g_value_set_string (value, adapter->priv->native_path);
      break;

    case PROP_VENDOR:
      g_value_set_string (value, adapter->priv->vendor);
      break;

    case PROP_MODEL:
      g_value_set_string (value, adapter->priv->model);
      break;

    case PROP_DRIVER:
      g_value_set_string (value, adapter->priv->driver);
      break;

    case PROP_NUM_PORTS:
      g_value_set_uint (value, adapter->priv->num_ports);
      break;

    case PROP_FABRIC:
      g_value_set_string (value, adapter->priv->fabric);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
adapter_class_init (AdapterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = adapter_finalize;
  object_class->get_property = get_property;

  g_type_class_add_private (klass, sizeof(AdapterPrivate));

  signals[CHANGED_SIGNAL] = g_signal_new ("changed",
                                          G_OBJECT_CLASS_TYPE (klass),
                                          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                          0,
                                          NULL,
                                          NULL,
                                          g_cclosure_marshal_VOID__VOID,
                                          G_TYPE_NONE,
                                          0);

  dbus_g_object_type_install_info (TYPE_ADAPTER, &dbus_glib_adapter_object_info);

  g_object_class_install_property (object_class, PROP_NATIVE_PATH, g_param_spec_string ("native-path",
                                                                                        NULL,
                                                                                        NULL,
                                                                                        NULL,
                                                                                        G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_VENDOR, g_param_spec_string ("vendor",
                                                                                   NULL,
                                                                                   NULL,
                                                                                   NULL,
                                                                                   G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_MODEL, g_param_spec_string ("model",
                                                                                  NULL,
                                                                                  NULL,
                                                                                  NULL,
                                                                                  G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_DRIVER, g_param_spec_string ("driver",
                                                                                   NULL,
                                                                                   NULL,
                                                                                   NULL,
                                                                                   G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_NUM_PORTS, g_param_spec_uint ("num-ports",
                                                                                    NULL,
                                                                                    NULL,
                                                                                    0,
                                                                                    G_MAXUINT,
                                                                                    0,
                                                                                    G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_FABRIC, g_param_spec_string ("fabric",
                                                                                   NULL,
                                                                                   NULL,
                                                                                   NULL,
                                                                                   G_PARAM_READABLE));
}

static void
adapter_init (Adapter *adapter)
{
  adapter->priv = ADAPTER_GET_PRIVATE (adapter);
}

static void
adapter_finalize (GObject *object)
{
  Adapter *adapter;

  g_return_if_fail (object != NULL);
  g_return_if_fail (IS_ADAPTER (object));

  adapter = ADAPTER (object);
  g_return_if_fail (adapter->priv != NULL);

  /* g_debug ("finalizing %s", adapter->priv->native_path); */

  g_object_unref (adapter->priv->d);
  g_object_unref (adapter->priv->daemon);
  g_free (adapter->priv->object_path);

  g_free (adapter->priv->native_path);

  if (adapter->priv->emit_changed_idle_id > 0)
    g_source_remove (adapter->priv->emit_changed_idle_id);

  /* free properties */
  g_free (adapter->priv->vendor);
  g_free (adapter->priv->model);
  g_free (adapter->priv->driver);

  G_OBJECT_CLASS (adapter_parent_class)->finalize (object);
}

/**
 * compute_object_path:
 * @native_path: Either an absolute sysfs path or the basename
 *
 * Maps @native_path to the D-Bus object path for the adapter.
 *
 * Returns: A valid D-Bus object path. Free with g_free().
 */
static char *
compute_object_path (const char *native_path)
{
  const gchar *basename;
  GString *s;
  guint n;

  basename = strrchr (native_path, '/');
  if (basename != NULL)
    {
      basename++;
    }
  else
    {
      basename = native_path;
    }

  s = g_string_new ("/org/freedesktop/UDisks/adapters/");
  for (n = 0; basename[n] != '\0'; n++)
    {
      gint c = basename[n];

      /* D-Bus spec sez:
       *
       * Each element must only contain the ASCII characters "[A-Z][a-z][0-9]_"
       */
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
          g_string_append_c (s, c);
        }
      else
        {
          /* Escape bytes not in [A-Z][a-z][0-9] as _<hex-with-two-digits> */
          g_string_append_printf (s, "_%02x", c);
        }
    }

  return g_string_free (s, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
register_disks_adapter (Adapter *adapter)
{
  DBusConnection *connection;
  GError *error = NULL;

  adapter->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
  if (adapter->priv->system_bus_connection == NULL)
    {
      if (error != NULL)
        {
          g_critical ("error getting system bus: %s", error->message);
          g_error_free (error);
        }
      goto error;
    }
  connection = dbus_g_connection_get_connection (adapter->priv->system_bus_connection);

  adapter->priv->object_path = compute_object_path (adapter->priv->native_path);

  /* safety first */
  if (dbus_g_connection_lookup_g_object (adapter->priv->system_bus_connection, adapter->priv->object_path) != NULL)
    {
      g_error ("**** HACK: Wanting to register object at path `%s' but there is already an "
        "object there. This is an internal error in the daemon. Aborting.\n", adapter->priv->object_path);
    }

  dbus_g_connection_register_g_object (adapter->priv->system_bus_connection,
                                       adapter->priv->object_path,
                                       G_OBJECT (adapter));

  return TRUE;

 error:
  return FALSE;
}

void
adapter_removed (Adapter *adapter)
{
  adapter->priv->removed = TRUE;

  dbus_g_connection_unregister_g_object (adapter->priv->system_bus_connection, G_OBJECT (adapter));
  g_assert (dbus_g_connection_lookup_g_object (adapter->priv->system_bus_connection, adapter->priv->object_path)
      == NULL);
}

Adapter *
adapter_new (Daemon *daemon,
             GUdevDevice *d)
{
  Adapter *adapter;
  const char *native_path;

  adapter = NULL;
  native_path = g_udev_device_get_sysfs_path (d);

  adapter = ADAPTER (g_object_new (TYPE_ADAPTER, NULL));
  adapter->priv->d = g_object_ref (d);
  adapter->priv->daemon = g_object_ref (daemon);
  adapter->priv->native_path = g_strdup (native_path);

  if (!update_info (adapter))
    {
      g_object_unref (adapter);
      adapter = NULL;
      goto out;
    }

  if (!register_disks_adapter (ADAPTER (adapter)))
    {
      g_object_unref (adapter);
      adapter = NULL;
      goto out;
    }

  out:
  return adapter;
}

static void
drain_pending_changes (Adapter *adapter,
                       gboolean force_update)
{
  gboolean emit_changed;

  emit_changed = FALSE;

  /* the update-in-idle is set up if, and only if, there are pending changes - so
   * we should emit a 'change' event only if it is set up
   */
  if (adapter->priv->emit_changed_idle_id != 0)
    {
      g_source_remove (adapter->priv->emit_changed_idle_id);
      adapter->priv->emit_changed_idle_id = 0;
      emit_changed = TRUE;
    }

  if ((!adapter->priv->removed) && (emit_changed || force_update))
    {
      if (adapter->priv->object_path != NULL)
        {
          g_print ("**** EMITTING CHANGED for %s\n", adapter->priv->native_path);
          g_signal_emit_by_name (adapter, "changed");
          g_signal_emit_by_name (adapter->priv->daemon, "adapter-changed", adapter->priv->object_path);
        }
    }
}

/* called by the daemon on the 'change' uevent */
gboolean
adapter_changed (Adapter *adapter,
                 GUdevDevice *d,
                 gboolean synthesized)
{
  gboolean keep_adapter;

  g_object_unref (adapter->priv->d);
  adapter->priv->d = g_object_ref (d);

  keep_adapter = update_info (adapter);

  /* this 'change' event might prompt us to remove the adapter */
  if (!keep_adapter)
    goto out;

  /* no, it's good .. keep it.. and always force a 'change' signal if the event isn't synthesized */
  drain_pending_changes (adapter, !synthesized);

  out:
  return keep_adapter;
}

/* ---------------------------------------------------------------------------------------------------- */

const char *
adapter_local_get_object_path (Adapter *adapter)
{
  return adapter->priv->object_path;
}

const char *
adapter_local_get_native_path (Adapter *adapter)
{
  return adapter->priv->native_path;
}

const char *
adapter_local_get_driver (Adapter *adapter)
{
  return adapter->priv->driver;
}

const char *
adapter_local_get_fabric (Adapter *adapter)
{
  return adapter->priv->fabric;
}

/* ---------------------------------------------------------------------------------------------------- */

/* figure out the fabric and number of ports - this is a bit dicey/sketchy and involves
 * some heuristics - ideally drivers would export enough information here but that's
 * not the way things work today...
 */
static gboolean
update_info_fabric_and_num_ports (Adapter *adapter)
{
  gboolean ret;
  const gchar *fabric;
  GDir *dir;
  guint num_scsi_host_objects;
  guint num_ports;
  guint64 device_class;
  const gchar *driver;
  guint class;
  guint subclass;
  guint interface;
  gchar *scsi_host_name;
  gchar *s;
  gchar *s2;

  ret = FALSE;
  fabric = NULL;
  scsi_host_name = NULL;
  num_ports = 0;

  device_class = g_udev_device_get_sysfs_attr_as_uint64 (adapter->priv->d, "class");
  driver = g_udev_device_get_driver (adapter->priv->d);

  class = (device_class & 0xff0000) >> 16;
  subclass = (device_class & 0x00ff00) >> 8;
  interface = (device_class & 0x0000ff);

  /* count number of scsi_host objects - this is to detect whether we are dealing with
   * ATA - see comment in port.c:update_info_ata() for details about
   * the hack we use here and how to fix this
   */
  num_scsi_host_objects = 0;
  dir = g_dir_open (adapter_local_get_native_path (adapter), 0, NULL);
  if (dir != NULL)
    {
      const gchar *name;
      while ((name = g_dir_read_name (dir)) != NULL)
        {
          gint number;
          if (sscanf (name, "host%d", &number) != 1)
            continue;
          num_scsi_host_objects++;

          if (scsi_host_name == NULL)
            {
              scsi_host_name = g_strdup (name);
            }
        }

      g_dir_close (dir);
    }

  /* Don't bother if no driver is bound */
  if (num_scsi_host_objects == 0)
    goto out;

  /* First try to use driver name to determine if this is ATA */
  if (driver != NULL && g_str_has_prefix (driver, "pata_"))
    {
      fabric = "ata_pata";
      num_ports = num_scsi_host_objects;
    }
  else if (driver != NULL && (g_str_has_prefix (driver, "sata_") || g_strcmp0 (driver, "ahci") == 0))
    {
      fabric = "ata_sata";
      num_ports = num_scsi_host_objects;
    }
  else if (num_scsi_host_objects > 1)
    {
      /* we're definitely possibly (!) dealing with ATA */
      num_ports = num_scsi_host_objects;

      /* use PCI class to zero in - maybe we also want to use driver names? */
      fabric = "ata";
      if (subclass == 0x01 || subclass == 0x05)
        {
          fabric = "ata_pata";
        }
      else if (subclass == 0x06)
        {
          fabric = "ata_sata";
        }
    }
  else
    {
      /* Not ATA */
      if (subclass == 0x00)
        {
          fabric = "scsi";
        }
      else if (subclass == 0x07)
        {
          fabric = "scsi_sas";
        }

      /* SAS */
      if (scsi_host_name != NULL)
        {
          s = g_strdup_printf ("%s/%s/sas_host/%s",
                               adapter_local_get_native_path (adapter),
                               scsi_host_name,
                               scsi_host_name);
          if (g_file_test (s, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
            {
              fabric = "scsi_sas";

              s = g_strdup_printf ("%s/%s", adapter_local_get_native_path (adapter), scsi_host_name);
              /* Count number of phy objects in hostN/ */
              dir = g_dir_open (s, 0, NULL);
              if (dir != NULL)
                {
                  const gchar *name;
                  while ((name = g_dir_read_name (dir)) != NULL)
                    {
                      if (!g_str_has_prefix (name, "phy-"))
                        continue;
                      /* Check that it's really a sas_phy */
                      s2 = g_strdup_printf ("%s/%s/sas_phy", s, name);
                      if (g_file_test (s2, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
                        {
                          num_ports++;
                        }
                      g_free (s2);
                    }

                  if (scsi_host_name == NULL)
                    {
                      scsi_host_name = g_strdup (name);
                    }
                  g_dir_close (dir);
                }
            }
          g_free (s);
        }
    }

  ret = TRUE;

  adapter_set_fabric (adapter, fabric);
  adapter_set_num_ports (adapter, num_ports);

 out:
  g_free (scsi_host_name);
  return ret;
}

/**
 * update_info:
 * @adapter: the adapter
 *
 * Update information about the adapter.
 *
 * If one or more properties changed, the changes are scheduled to be emitted. Use
 * drain_pending_changes() to force emitting the pending changes (which is useful
 * before returning the result of an operation).
 *
 * Returns: #TRUE to keep (or add) the adapter; #FALSE to ignore (or remove) the adapter
 **/
static gboolean
update_info (Adapter *adapter)
{
  gboolean ret;
  guint64 device_class;
  gchar *vendor;
  gchar *model;
  const gchar *driver;

  ret = FALSE;
  vendor = NULL;
  model = NULL;

  /* Only care about Mass Storage Adapter devices */
  device_class = g_udev_device_get_sysfs_attr_as_uint64 (adapter->priv->d, "class");
  if (((device_class & 0xff0000) >> 16) != 0x01)
    goto out;

  driver = g_udev_device_get_driver (adapter->priv->d);

  g_print ("**** UPDATING %s\n", adapter->priv->native_path);

  vendor = g_strdup (g_udev_device_get_property (adapter->priv->d, "ID_VENDOR_FROM_DATABASE"));
  model = g_strdup (g_udev_device_get_property (adapter->priv->d, "ID_MODEL_FROM_DATABASE"));

  /* TODO: probably want subsystem vendor and model - for the adapters in my Thinkpad X61 (not T61!)
   * it looks like this
   *
   *  00:1f.1: vendor:        Intel Corporation"
   *           model:         82801HBM/HEM (ICH8M/ICH8M-E) IDE Adapter
   *           subsys_vendor: Lenovo
   *           subsys_model:  ThinkPad T61
   *
   *  00:1f.2: vendor:        Intel Corporation
   *           model:         82801HBM/HEM (ICH8M/ICH8M-E) SATA AHCI Adapter
   *           subsys_vendor: Lenovo
   *           subsys_model:  ThinkPad T61
   *
   * or maybe not...
   */

  /* TODO: we want some kind of "type" or "interconnect" for the
   * adapter - e.g. SATA/PATA/SAS/FC/iSCSI - also want version
   * (e.g. SATA1, SATA2) and speed (e.g. 150MB/s, 300MB/s)
   */

  /* TODO: want some kind of information about the number of ports - and for
   * each port the "type" of connector - e.g. PATA, SATA, eSATA, SAS,
   * SASx4 (wide lane), FC... and the role (initiator or target)
   */

  /* TODO: want to convey some kind of information about where the adapter
   * is located (express-card, pc-card, pci-slot, onboard)...
   */

  /* TODO: also, enclosure information (needs thought re SES-2 enclosure support) */

  if (vendor == NULL)
    {
      vendor = g_strdup_printf ("[vendor=0x%04x subsys=0x%04x]",
                                g_udev_device_get_sysfs_attr_as_int (adapter->priv->d, "vendor"),
                                g_udev_device_get_sysfs_attr_as_int (adapter->priv->d, "subsystem_vendor"));
    }
  if (model == NULL)
    {
      vendor = g_strdup_printf ("Storage Adapter [model=0x%04x subsys=0x%04x]",
                                g_udev_device_get_sysfs_attr_as_int (adapter->priv->d, "device"),
                                g_udev_device_get_sysfs_attr_as_int (adapter->priv->d, "subsystem_device"));
    }

  adapter_set_vendor (adapter, vendor);
  adapter_set_model (adapter, model);
  adapter_set_driver (adapter, driver);

  if (!update_info_fabric_and_num_ports (adapter))
    goto out;

  ret = TRUE;

 out:
  g_free (vendor);
  g_free (model);
  return ret;
}

