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

#include "udiskslvm2state.h"


/**
 * udisks_lvm2_state_new:
 *
 * Initializes the #UDisksLVM2State structure that holds global state within the LVM2 plugin.
 *
 * Returns: (transfer full): A #UDisksLVM2State that must be freed with udisks_lvm2_state_free().
 */
UDisksLVM2State *
udisks_lvm2_state_new (void)
{
  UDisksLVM2State *state;

  state = g_malloc0 (sizeof (UDisksLVM2State));

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
