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

#include <libiscsi.h>

#include "udisksiscsistate.h"

struct _UDisksISCSIState
{
  UDisksDaemon *daemon;

  GMutex libiscsi_mutex;
  struct libiscsi_context *iscsi_ctx;
};

/**
 * udisks_iscsi_state_new:
 * @daemon: A #UDisksDaemon instance.
 *
 * Initializes the #UDisksISCSIState structure that holds the global state
 * within ISCSI plugin.
 *
 * Returns: (transfer full): A #UDisksISCSIState that must be freed with
 * udisks_iscsi_state_free().
 */
UDisksISCSIState *
udisks_iscsi_state_new (UDisksDaemon *daemon)
{
  UDisksISCSIState *state;

  state = g_malloc0 (sizeof(UDisksISCSIState));

  if (state)
    {
      /* Initialize members. */
      state->daemon = daemon;

      g_mutex_init (&state->libiscsi_mutex);
      state->iscsi_ctx = libiscsi_init ();
    }

  return state;
}

void
udisks_iscsi_state_free (UDisksISCSIState *state)
{
  g_return_if_fail (state);

  /* Free/Unref members. */
  if (state->iscsi_ctx)
    libiscsi_cleanup (state->iscsi_ctx);

  g_free (state);
}

void
udisks_iscsi_state_lock_libiscsi_context (UDisksISCSIState *state)
{
  g_mutex_lock (&state->libiscsi_mutex);
}

void
udisks_iscsi_state_unlock_libiscsi_context (UDisksISCSIState *state)
{
  g_mutex_unlock (&state->libiscsi_mutex);
}

struct libiscsi_context *
udisks_iscsi_state_get_libiscsi_context (UDisksISCSIState *state)
{
  g_return_val_if_fail (state, NULL);
  return state->iscsi_ctx;
}
