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
#include "devkit-disks-controller.h"
#include "devkit-disks-controller-private.h"

static gboolean
emit_changed_idle_cb (gpointer data)
{
  DevkitDisksController *controller = DEVKIT_DISKS_CONTROLLER (data);

  //g_debug ("XXX emitting 'changed' in idle");

  if (!controller->priv->removed)
    {
      g_print ("**** EMITTING CHANGED for %s\n", controller->priv->native_path);
      g_signal_emit_by_name (controller->priv->daemon,
                             "controller-changed",
                             controller->priv->object_path);
      g_signal_emit_by_name (controller, "changed");
    }
  controller->priv->emit_changed_idle_id = 0;

  /* remove the idle source */
  return FALSE;
}

static void
emit_changed (DevkitDisksController *controller, const gchar *name)
{
  //g_debug ("property %s changed for %s", name, controller->priv->controller_file);

  if (controller->priv->object_path != NULL)
    {
      /* schedule a 'changed' signal in idle if one hasn't been scheduled already */
      if (controller->priv->emit_changed_idle_id == 0)
        {
          controller->priv->emit_changed_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT,
                                                                    emit_changed_idle_cb,
                                                                    g_object_ref (controller),
                                                                    (GDestroyNotify) g_object_unref);
        }
    }
}

void
devkit_disks_controller_set_vendor (DevkitDisksController *controller, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (controller->priv->vendor, value) != 0))
    {
      g_free (controller->priv->vendor);
      controller->priv->vendor = g_strdup (value);
      emit_changed (controller, "vendor");
    }
}

void
devkit_disks_controller_set_model (DevkitDisksController *controller, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (controller->priv->model, value) != 0))
    {
      g_free (controller->priv->model);
      controller->priv->model = g_strdup (value);
      emit_changed (controller, "model");
    }
}

void
devkit_disks_controller_set_driver (DevkitDisksController *controller, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (controller->priv->driver, value) != 0))
    {
      g_free (controller->priv->driver);
      controller->priv->driver = g_strdup (value);
      emit_changed (controller, "driver");
    }
}

void
devkit_disks_controller_set_num_ports (DevkitDisksController *controller, guint value)
{
  if (G_UNLIKELY (controller->priv->num_ports != value))
    {
      controller->priv->num_ports = value;
      emit_changed (controller, "num_ports");
    }
}

void
devkit_disks_controller_set_fabric (DevkitDisksController *controller, const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (controller->priv->fabric, value) != 0))
    {
      g_free (controller->priv->fabric);
      controller->priv->fabric = g_strdup (value);
      emit_changed (controller, "fabric");
    }
}
