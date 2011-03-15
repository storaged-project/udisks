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

/*
 * dbus-send --system --print-reply --dest=org.freedesktop.UDisks /org/freedesktop/UDisks/expanders/expander_2d7_3a0 org.freedesktop.DBus.Properties.GetAll string:org.freedesktop.UDisks.Expander
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
#include <stdlib.h>

#include "daemon.h"
#include "expander.h"
#include "expander-private.h"
#include "marshal.h"

#include "port.h"
#include "adapter.h"

/*--------------------------------------------------------------------------------------------------------------*/
#include "expander-glue.h"

static void
expander_class_init (ExpanderClass *klass);
static void
expander_init (Expander *seat);
static void
expander_finalize (GObject *object);

static gboolean
update_info (Expander *expander);

static void
drain_pending_changes (Expander *expander,
                       gboolean force_update);

enum
  {
    PROP_0,
    PROP_NATIVE_PATH,

    PROP_VENDOR,
    PROP_MODEL,
    PROP_REVISION,
    PROP_NUM_PORTS,
    PROP_UPSTREAM_PORTS,
    PROP_ADAPTER
  };

enum
  {
    CHANGED_SIGNAL,
    LAST_SIGNAL,
  };

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (Expander, expander, G_TYPE_OBJECT)

#define EXPANDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TYPE_EXPANDER, ExpanderPrivate))

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
  Expander *expander = EXPANDER (object);

  switch (prop_id)
    {
    case PROP_NATIVE_PATH:
      g_value_set_string (value, expander->priv->native_path);
      break;

    case PROP_VENDOR:
      g_value_set_string (value, expander->priv->vendor);
      break;

    case PROP_MODEL:
      g_value_set_string (value, expander->priv->model);
      break;

    case PROP_REVISION:
      g_value_set_string (value, expander->priv->revision);
      break;

    case PROP_NUM_PORTS:
      g_value_set_uint (value, expander->priv->num_ports);
      break;

    case PROP_UPSTREAM_PORTS:
      g_value_set_boxed (value, expander->priv->upstream_ports);
      break;

    case PROP_ADAPTER:
      if (expander->priv->adapter == NULL)
        g_value_set_boxed (value, "/");
      else
        g_value_set_boxed (value, expander->priv->adapter);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
expander_class_init (ExpanderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = expander_finalize;
  object_class->get_property = get_property;

  g_type_class_add_private (klass, sizeof(ExpanderPrivate));

  signals[CHANGED_SIGNAL] = g_signal_new ("changed",
                                          G_OBJECT_CLASS_TYPE (klass),
                                          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                          0,
                                          NULL,
                                          NULL,
                                          g_cclosure_marshal_VOID__VOID,
                                          G_TYPE_NONE,
                                          0);

  dbus_g_object_type_install_info (TYPE_EXPANDER, &dbus_glib_expander_object_info);

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
  g_object_class_install_property (object_class, PROP_REVISION, g_param_spec_string ("revision",
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
  g_object_class_install_property (object_class,
                                   PROP_UPSTREAM_PORTS,
                                   g_param_spec_boxed ("upstream-ports",
                                                       NULL,
                                                       NULL,
                                                       dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
                                                       G_PARAM_READABLE));
  g_object_class_install_property (object_class, PROP_ADAPTER, g_param_spec_boxed ("adapter",
                                                                                   NULL,
                                                                                   NULL,
                                                                                   DBUS_TYPE_G_OBJECT_PATH,
                                                                                   G_PARAM_READABLE));
}

static void
expander_init (Expander *expander)
{
  expander->priv = EXPANDER_GET_PRIVATE (expander);
  expander->priv->upstream_ports = g_ptr_array_new ();
}

static void
expander_finalize (GObject *object)
{
  Expander *expander;

  g_return_if_fail (object != NULL);
  g_return_if_fail (IS_EXPANDER (object));

  expander = EXPANDER (object);
  g_return_if_fail (expander->priv != NULL);

  /* g_debug ("finalizing %s", expander->priv->native_path); */

  g_object_unref (expander->priv->d);
  g_object_unref (expander->priv->daemon);
  g_free (expander->priv->object_path);
  g_free (expander->priv->native_path);
  g_free (expander->priv->native_path_for_sysfs_prefix);

  if (expander->priv->emit_changed_idle_id > 0)
    g_source_remove (expander->priv->emit_changed_idle_id);

  /* free properties */
  g_free (expander->priv->vendor);
  g_free (expander->priv->model);
  g_free (expander->priv->revision);
  g_ptr_array_foreach (expander->priv->upstream_ports, (GFunc) g_free, NULL);
  g_ptr_array_free (expander->priv->upstream_ports, TRUE);
  g_free (expander->priv->adapter);

  G_OBJECT_CLASS (expander_parent_class)->finalize (object);
}

/**
 * compute_object_path:
 * @native_path: Either an absolute sysfs path or the basename
 *
 * Maps @native_path to the D-Bus object path for the expander.
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

  s = g_string_new ("/org/freedesktop/UDisks/expanders/");
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
register_disks_expander (Expander *expander)
{
  GError *error = NULL;

  expander->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
  if (expander->priv->system_bus_connection == NULL)
    {
      if (error != NULL)
        {
          g_critical ("error getting system bus: %s", error->message);
          g_error_free (error);
        }
      goto error;
    }

  expander->priv->object_path = compute_object_path (expander->priv->native_path);

  /* safety first */
  if (dbus_g_connection_lookup_g_object (expander->priv->system_bus_connection, expander->priv->object_path) != NULL)
    {
      g_error ("**** HACK: Wanting to register object at path `%s' but there is already an "
               "object there. This is an internal error in the daemon. Aborting.\n",
               expander->priv->object_path);
    }

  dbus_g_connection_register_g_object (expander->priv->system_bus_connection,
                                       expander->priv->object_path,
                                       G_OBJECT (expander));

  return TRUE;

 error:
  return FALSE;
}

void
expander_removed (Expander *expander)
{
  expander->priv->removed = TRUE;

  dbus_g_connection_unregister_g_object (expander->priv->system_bus_connection, G_OBJECT (expander));
  g_assert (dbus_g_connection_lookup_g_object (expander->priv->system_bus_connection, expander->priv->object_path) == NULL);
}

Expander *
expander_new (Daemon *daemon,
              GUdevDevice *d)
{
  Expander *expander;
  const char *native_path;

  expander = NULL;
  native_path = g_udev_device_get_sysfs_path (d);

  expander = EXPANDER (g_object_new (TYPE_EXPANDER, NULL));
  expander->priv->d = g_object_ref (d);
  expander->priv->daemon = g_object_ref (daemon);
  expander->priv->native_path = g_strdup (native_path);

  if (!update_info (expander))
    {
      g_object_unref (expander);
      expander = NULL;
      goto out;
    }

  if (!register_disks_expander (EXPANDER (expander)))
    {
      g_object_unref (expander);
      expander = NULL;
      goto out;
    }

 out:
  return expander;
}

static void
drain_pending_changes (Expander *expander,
                       gboolean force_update)
{
  gboolean emit_changed;

  emit_changed = FALSE;

  /* the update-in-idle is set up if, and only if, there are pending changes - so
   * we should emit a 'change' event only if it is set up
   */
  if (expander->priv->emit_changed_idle_id != 0)
    {
      g_source_remove (expander->priv->emit_changed_idle_id);
      expander->priv->emit_changed_idle_id = 0;
      emit_changed = TRUE;
    }

  if ((!expander->priv->removed) && (emit_changed || force_update))
    {
      if (expander->priv->object_path != NULL)
        {
          g_print ("**** EMITTING CHANGED for %s\n", expander->priv->native_path);
          g_signal_emit_by_name (expander, "changed");
          g_signal_emit_by_name (expander->priv->daemon, "expander-changed", expander->priv->object_path);
        }
    }
}

/* called by the daemon on the 'change' uevent */
gboolean
expander_changed (Expander *expander,
                  GUdevDevice *d,
                  gboolean synthesized)
{
  gboolean keep_expander;

  g_object_unref (expander->priv->d);
  expander->priv->d = g_object_ref (d);

  keep_expander = update_info (expander);

  /* this 'change' event might prompt us to remove the expander */
  if (!keep_expander)
    goto out;

  /* no, it's good .. keep it.. and always force a 'change' signal if the event isn't synthesized */
  drain_pending_changes (expander, !synthesized);

 out:
  return keep_expander;
}

/* ---------------------------------------------------------------------------------------------------- */

const char *
expander_local_get_object_path (Expander *expander)
{
  return expander->priv->object_path;
}

const char *
expander_local_get_native_path (Expander *expander)
{
  return expander->priv->native_path;
}

gboolean
local_expander_encloses_native_path (Expander *expander,
                                     const gchar *native_path)
{
  gboolean ret;
  ret = g_str_has_prefix (native_path, expander->priv->native_path_for_sysfs_prefix);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static char *
sysfs_resolve_link (const char *sysfs_path,
                    const char *name)
{
  char *full_path;
  char link_path[PATH_MAX];
  char resolved_path[PATH_MAX];
  ssize_t num;
  gboolean found_it;

  found_it = FALSE;

  full_path = g_build_filename (sysfs_path, name, NULL);

  //g_debug ("name='%s'", name);
  //g_debug ("full_path='%s'", full_path);
  num = readlink (full_path, link_path, sizeof(link_path) - 1);
  if (num != -1)
    {
      char *absolute_path;

      link_path[num] = '\0';

      //g_debug ("link_path='%s'", link_path);
      absolute_path = g_build_filename (sysfs_path, link_path, NULL);
      //g_debug ("absolute_path='%s'", absolute_path);
      if (realpath (absolute_path, resolved_path) != NULL)
        {
          //g_debug ("resolved_path='%s'", resolved_path);
          found_it = TRUE;
        }
      g_free (absolute_path);
    }
  g_free (full_path);

  if (found_it)
    return g_strdup (resolved_path);
  else
    return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * update_info:
 * @expander: the expander
 *
 * Update information about the expander.
 *
 * If one or more properties changed, the changes are scheduled to be emitted. Use
 * drain_pending_changes() to force emitting the pending changes (which is useful
 * before returning the result of an operation).
 *
 * Returns: #TRUE to keep (or add) the expander; #FALSE to ignore (or remove) the expander
 **/
static gboolean
update_info (Expander *expander)
{
  Adapter *adapter;
  gboolean ret;
  GList *ports;
  GList *l;
  GPtrArray *p;
  gchar *s;
  GDir *dir;
  guint num_ports;
  const gchar *vendor;
  const gchar *model;
  const gchar *revision;

  /* NOTE: Only sas expanders are supported for now */

  ret = FALSE;
  num_ports = 0;

  /* First, figure out prefix used for matching the sysfs devices below the expander */
  if (expander->priv->native_path_for_sysfs_prefix == NULL)
    {
      expander->priv->native_path_for_sysfs_prefix = sysfs_resolve_link (g_udev_device_get_sysfs_path (expander->priv->d), "device");
      if (expander->priv->native_path_for_sysfs_prefix == NULL)
        {
          g_warning ("Unable to resolve 'device' symlink for %s", g_udev_device_get_sysfs_path (expander->priv->d));
          goto out;
        }
    }

  /* Set adapter */
  adapter = daemon_local_find_enclosing_adapter (expander->priv->daemon, expander->priv->native_path);
  if (adapter == NULL)
    goto out;
  expander_set_adapter (expander, adapter_local_get_object_path (adapter));

  /* Figure out the upstream ports for the expander */
  ports = daemon_local_find_enclosing_ports (expander->priv->daemon, expander->priv->native_path_for_sysfs_prefix);
  p = g_ptr_array_new ();
  for (l = ports; l != NULL; l = l->next)
    {
      Port *port = PORT (l->data);
      g_ptr_array_add (p, (gpointer) port_local_get_object_path (port));
    }
  g_ptr_array_add (p, NULL);
  expander_set_upstream_ports (expander, (GStrv) p->pdata);
  g_ptr_array_unref (p);
  g_list_free (ports);

  /* Count the number of ports (e.g. PHYs) */
  dir = g_dir_open (expander->priv->native_path_for_sysfs_prefix, 0, NULL);
  if (dir != NULL)
    {
      const gchar *name;
      while ((name = g_dir_read_name (dir)) != NULL)
        {
          if (!g_str_has_prefix (name, "phy-"))
            continue;
          /* Check that it's really a sas_phy */
          s = g_strdup_printf ("%s/%s/sas_phy", expander->priv->native_path_for_sysfs_prefix, name);
          if (g_file_test (s, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
            {
              num_ports++;
            }
          g_free (s);
        }
      g_dir_close (dir);
    }
  expander_set_num_ports (expander, num_ports);

  vendor = g_udev_device_get_property (expander->priv->d, "ID_VENDOR");
  model = g_udev_device_get_property (expander->priv->d, "ID_MODEL");
  revision = g_udev_device_get_property (expander->priv->d, "ID_REVISION");
  expander_set_vendor (expander, vendor);
  expander_set_model (expander, model);
  expander_set_revision (expander, revision);

  ret = TRUE;

 out:
  return ret;
}

