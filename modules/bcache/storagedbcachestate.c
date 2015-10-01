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
#include "storagedbcachestate.h"

struct _StoragedBcacheState
{
  StoragedDaemon *daemon;
};

/**
 * storaged_bcache_state_new:
 * @daemon: A #StoragedDaemon instance.
 *
 * Initializes the #StoragedBcacheState structure that holds the global state
 * within Bcache plugin.
 *
 * Returns: (transfer full): A #StoragedBcacheState that must be freed with
 * storaged_bcache_state_free().
 */

StoragedBcacheState *
storaged_bcache_state_new (StoragedDaemon *daemon)
{
  StoragedBcacheState *state;

  state = g_malloc (sizeof (StoragedBcacheState));

  if (state)
    {
      state->daemon = daemon;
    }
  return state;

}

void
storaged_bcache_state_free (StoragedBcacheState* state)
{
  g_return_if_fail (state);

  g_free (state);
}
