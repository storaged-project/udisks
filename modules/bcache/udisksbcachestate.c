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
#include "udisksbcachestate.h"

struct _UDisksBcacheState
{
  UDisksDaemon *daemon;
};

/**
 * udisks_bcache_state_new:
 * @daemon: A #UDisksDaemon instance.
 *
 * Initializes the #UDisksBcacheState structure that holds the global state
 * within Bcache plugin.
 *
 * Returns: (transfer full): A #UDisksBcacheState that must be freed with
 * udisks_bcache_state_free().
 */

UDisksBcacheState *
udisks_bcache_state_new (UDisksDaemon *daemon)
{
  UDisksBcacheState *state;

  state = g_malloc (sizeof (UDisksBcacheState));

  if (state)
    {
      state->daemon = daemon;
    }
  return state;

}

void
udisks_bcache_state_free (UDisksBcacheState* state)
{
  g_return_if_fail (state);

  g_free (state);
}
