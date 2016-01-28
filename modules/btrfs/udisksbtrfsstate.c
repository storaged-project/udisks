/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Peter Hatina <phatina@redhat.com>
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

#include "udisksbtrfsstate.h"

struct _UDisksBTRFSState
{
  UDisksDaemon *daemon;
};

/**
 * udisks_btrfs_state_new:
 * @daemon: A #UDisksDaemon instance.
 *
 * Initializes the #UDisksBTRFSState structure that holds the global state
 * within BTRFS plugin.
 *
 * Returns: (transfer full): A #UDisksBTRFSState that must be freed with
 * udisks_btrfs_state_free().
 */
UDisksBTRFSState *
udisks_btrfs_state_new (UDisksDaemon *daemon)
{
  UDisksBTRFSState *state;

  state = g_malloc0 (sizeof(UDisksBTRFSState));

  if (state)
    {
      /* Initialize members. */
      state->daemon = daemon;
    }

  return state;
}

void
udisks_btrfs_state_free (UDisksBTRFSState *state)
{
  g_return_if_fail (state);

  /* Free/Unref members. */

  g_free (state);
}
