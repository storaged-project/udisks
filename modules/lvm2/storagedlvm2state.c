/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014 Tomas Bzatek <tbzatek@redhat.com>
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

#include <src/storageddaemontypes.h>
#include "storagedlvm2state.h"

struct _StoragedLVM2State
{
  StoragedDaemon *daemon;

  /* maps from volume group name to StoragedLinuxVolumeGroupObject instances. */
  GHashTable *name_to_volume_group;

  gint lvm_delayed_update_id;
  gboolean coldplug_done;
};

/**
 * storaged_lvm2_state_new:
 * @daemon: A #StoragedDaemon instance.
 *
 * Initializes the #StoragedLVM2State structure that holds global state within the LVM2 plugin.
 *
 * Returns: (transfer full): A #StoragedLVM2State that must be freed with storaged_lvm2_state_free().
 */
StoragedLVM2State *
storaged_lvm2_state_new (StoragedDaemon *daemon)
{
  StoragedLVM2State *state;

  state = g_malloc0 (sizeof (StoragedLVM2State));

  state->daemon = daemon;
  state->name_to_volume_group = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       (GDestroyNotify) g_object_unref);
  state->coldplug_done = FALSE;

  return state;
}

/**
 * storaged_lvm2_state_free:
 * @state: A #StoragedLVM2State.
 *
 * Properly frees any memory initialized by the structure.
 */
void
storaged_lvm2_state_free (StoragedLVM2State *state)
{
  g_assert (state != NULL);

  g_hash_table_unref (state->name_to_volume_group);

  g_free (state);
}

GHashTable *
storaged_lvm2_state_get_name_to_volume_group (StoragedLVM2State *state)
{
  g_assert (state != NULL);

  return state->name_to_volume_group;
}

gint
storaged_lvm2_state_get_lvm_delayed_update_id (StoragedLVM2State *state)
{
  g_assert (state != NULL);

  return state->lvm_delayed_update_id;
}

gboolean
storaged_lvm2_state_get_coldplug_done (StoragedLVM2State *state)
{
  g_assert (state != NULL);

  return state->coldplug_done;
}

void
storaged_lvm2_state_set_lvm_delayed_update_id (StoragedLVM2State *state,
                                               gint               id)
{
  g_assert (state != NULL);

  state->lvm_delayed_update_id = id;
}

void
storaged_lvm2_state_set_coldplug_done (StoragedLVM2State *state,
                                       gboolean           coldplug_done)
{
  g_assert (state != NULL);

  state->coldplug_done = coldplug_done;
}
