/*
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

#include <string.h>
#include "devkit-disks-adapter.h"
#include "devkit-disks-adapter-private.h"

static gboolean
emit_changed_idle_cb (gpointer data)
{
  DevkitDisksAdapter *adapter = DEVKIT_DISKS_ADAPTER (data);

  //g_debug ("XXX emitting 'changed' in idle");

  if (!adapter->priv->removed)
    {
      g_print ("**** EMITTING CHANGED for %s\n", adapter->priv->native_path);
      g_signal_emit_by_name (adapter->priv->daemon,
                             "adapter-changed",
                             adapter->priv->object_path);
      g_signal_emit_by_name (adapter, "changed");
    }
  adapter->priv->emit_changed_idle_id = 0;

  /* remove the idle source */
  return FALSE;
}

static void
emit_changed (DevkitDisksAdapter *adapter, const gchar *name)
{
  //g_debug ("property %s changed for %s", name, adapter->priv->adapter_file);

  if (adapter->priv->object_path != NULL)
    {
      /* schedule a 'changed' signal in idle if one hasn't been scheduled already */
      if (adapter->priv->emit_changed_idle_id == 0)
        {
          adapter->priv->emit_changed_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT,
                                                                    emit_changed_idle_cb,
                                                                    g_object_ref (adapter),
                                                                    (GDestroyNotify) g_object_unref);
        }
    }
}

void
devkit_disks_adapter_set_vendor (DevkitDisksAdapter *adapter, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (adapter->priv->vendor, value) != 0))
    {
      g_free (adapter->priv->vendor);
      adapter->priv->vendor = g_strdup (value);
      emit_changed (adapter, "vendor");
    }
}

void
devkit_disks_adapter_set_model (DevkitDisksAdapter *adapter, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (adapter->priv->model, value) != 0))
    {
      g_free (adapter->priv->model);
      adapter->priv->model = g_strdup (value);
      emit_changed (adapter, "model");
    }
}

void
devkit_disks_adapter_set_driver (DevkitDisksAdapter *adapter, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (adapter->priv->driver, value) != 0))
    {
      g_free (adapter->priv->driver);
      adapter->priv->driver = g_strdup (value);
      emit_changed (adapter, "driver");
    }
}

void
devkit_disks_adapter_set_num_ports (DevkitDisksAdapter *adapter, guint value)
{
  if (G_UNLIKELY (adapter->priv->num_ports != value))
    {
      adapter->priv->num_ports = value;
      emit_changed (adapter, "num_ports");
    }
}

void
devkit_disks_adapter_set_fabric (DevkitDisksAdapter *adapter, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (adapter->priv->fabric, value) != 0))
    {
      g_free (adapter->priv->fabric);
      adapter->priv->fabric = g_strdup (value);
      emit_changed (adapter, "fabric");
    }
}
