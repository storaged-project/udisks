/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Dominika Hodovska <dhodovsk@redhat.com>
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
 */

#include "config.h"
#include "udiskszramstate.h"

struct _UDisksZRAMState
{
  UDisksDaemon *daemon;
};

/**
 * udisks_zram_state_new:
 * @daemon: A #UDisksDaemon instance.
 *
 * Initializes the #UDisksZRAMState structure that holds the global state
 * within ZRAM plugin.
 *
 * Returns: (transfer full): A #UDisksZRAMState that must be freed with
 * udisks_zram_state_free().
 */

UDisksZRAMState *
udisks_zram_state_new (UDisksDaemon *daemon)
{
  UDisksZRAMState *state;

  state = g_malloc (sizeof (UDisksZRAMState));

  if (state)
    {
      state->daemon = daemon;
    }
  return state;

}

void
udisks_zram_state_free (UDisksZRAMState* state)
{
  g_return_if_fail (state);

  g_free (state);
}
