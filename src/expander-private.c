/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
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
#include "expander.h"
#include "expander-private.h"

static gboolean
emit_changed_idle_cb (gpointer data)
{
  Expander *expander = EXPANDER (data);

  //g_debug ("XXX emitting 'changed' in idle");

  if (!expander->priv->removed)
    {
      g_print ("**** EMITTING CHANGED for %s\n", expander->priv->native_path);
      g_signal_emit_by_name (expander->priv->daemon, "expander-changed", expander->priv->object_path);
      g_signal_emit_by_name (expander, "changed");
    }
  expander->priv->emit_changed_idle_id = 0;

  /* remove the idle source */
  return FALSE;
}

static void
emit_changed (Expander *expander,
              const gchar *name)
{
  //g_debug ("property %s changed for %s", name, expander->priv->expander_file);

  if (expander->priv->object_path != NULL)
    {
      /* schedule a 'changed' signal in idle if one hasn't been scheduled already */
      if (expander->priv->emit_changed_idle_id == 0)
        {
          expander->priv->emit_changed_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT,
                                                                  emit_changed_idle_cb,
                                                                  g_object_ref (expander),
                                                                  (GDestroyNotify) g_object_unref);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ptr_str_array_equals_strv (GPtrArray *a,
                           GStrv b)
{
  guint n;
  guint b_len;

  if (a->len == 0 && b == NULL)
    return TRUE;

  b_len = (b != NULL ? g_strv_length (b) : 0);

  if (a->len != b_len)
    return FALSE;

  for (n = 0; n < a->len; n++)
    {
      if (g_strcmp0 ((gchar *) a->pdata[n], b[n]) != 0)
        return FALSE;
    }

  return TRUE;
}

static void
ptr_str_array_free (GPtrArray *p)
{
  g_ptr_array_foreach (p, (GFunc) g_free, NULL);
  g_ptr_array_free (p, TRUE);
}

static GPtrArray *
ptr_str_array_from_strv (GStrv s)
{
  GPtrArray *ret;
  guint n;

  ret = g_ptr_array_new ();
  for (n = 0; s != NULL && s[n] != NULL; n++)
    g_ptr_array_add (ret, g_strdup (s[n]));

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

void
expander_set_vendor (Expander *expander,
                     const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (expander->priv->vendor, value) != 0))
    {
      g_free (expander->priv->vendor);
      expander->priv->vendor = g_strdup (value);
      emit_changed (expander, "vendor");
    }
}

void
expander_set_model (Expander *expander,
                    const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (expander->priv->model, value) != 0))
    {
      g_free (expander->priv->model);
      expander->priv->model = g_strdup (value);
      emit_changed (expander, "model");
    }
}

void
expander_set_revision (Expander *expander,
                       const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (expander->priv->revision, value) != 0))
    {
      g_free (expander->priv->revision);
      expander->priv->revision = g_strdup (value);
      emit_changed (expander, "revision");
    }
}

void
expander_set_num_ports (Expander *expander,
                        guint value)
{
  if (G_UNLIKELY (expander->priv->num_ports != value))
    {
      expander->priv->num_ports = value;
      emit_changed (expander, "num_ports");
    }
}

void
expander_set_upstream_ports (Expander *expander,
                             GStrv value)
{
  if (G_UNLIKELY (!ptr_str_array_equals_strv (expander->priv->upstream_ports, value)))
    {
      ptr_str_array_free (expander->priv->upstream_ports);
      expander->priv->upstream_ports = ptr_str_array_from_strv (value);
      emit_changed (expander, "upstream_ports");
    }
}

void
expander_set_adapter (Expander *expander,
                      const gchar *value)
{
  if (G_UNLIKELY (g_strcmp0 (expander->priv->adapter, value) != 0))
    {
      g_free (expander->priv->adapter);
      expander->priv->adapter = g_strdup (value);
      emit_changed (expander, "adapter");
    }
}
