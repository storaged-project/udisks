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

#include <src/udisksdaemontypes.h>
#include "udiskslvm2state.h"

struct _UDisksLVM2State
{
  UDisksDaemon *daemon;

  /* maps from volume group name to UDisksLinuxVolumeGroupObject instances. */
  GHashTable *name_to_volume_group;

  gint lvm_delayed_update_id;
  gboolean coldplug_done;
};

/**
 * udisks_lvm2_state_new:
 * @daemon: A #UDisksDaemon instance.
 *
 * Initializes the #UDisksLVM2State structure that holds global state within the LVM2 plugin.
 *
 * Returns: (transfer full): A #UDisksLVM2State that must be freed with udisks_lvm2_state_free().
 */
UDisksLVM2State *
udisks_lvm2_state_new (UDisksDaemon *daemon)
{
  UDisksLVM2State *state;

  state = g_malloc0 (sizeof (UDisksLVM2State));

  state->daemon = daemon;
  state->name_to_volume_group = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       (GDestroyNotify) g_object_unref);
  state->coldplug_done = FALSE;

  return state;
}

/**
 * udisks_lvm2_state_free:
 * @state: A #UDisksLVM2State.
 *
 * Properly frees any memory initialized by the structure.
 */
void
udisks_lvm2_state_free (UDisksLVM2State *state)
{
  g_assert (state != NULL);

  g_hash_table_unref (state->name_to_volume_group);

  g_free (state);
}

GHashTable *
udisks_lvm2_state_get_name_to_volume_group (UDisksLVM2State *state)
{
  g_assert (state != NULL);

  return state->name_to_volume_group;
}

gint
udisks_lvm2_state_get_lvm_delayed_update_id (UDisksLVM2State *state)
{
  g_assert (state != NULL);

  return state->lvm_delayed_update_id;
}

gboolean
udisks_lvm2_state_get_coldplug_done (UDisksLVM2State *state)
{
  g_assert (state != NULL);

  return state->coldplug_done;
}

void
udisks_lvm2_state_set_lvm_delayed_update_id (UDisksLVM2State *state,
                                             gint             id)
{
  g_assert (state != NULL);

  state->lvm_delayed_update_id = id;
}

void
udisks_lvm2_state_set_coldplug_done (UDisksLVM2State *state,
                                     gboolean         coldplug_done)
{
  g_assert (state != NULL);

  state->coldplug_done = coldplug_done;
}
