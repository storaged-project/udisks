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

#include "storagediscsistate.h"

struct _StoragedISCSIState
{
  StoragedDaemon *daemon;

  struct libiscsi_context *iscsi_ctx;
};

/**
 * storaged_iscsi_state_new:
 * @daemon: A #StoragedDaemon instance.
 *
 * Initializes the #StoragedISCSIState structure that holds the global state
 * within ISCSI plugin.
 *
 * Returns: (transfer full): A #StoragedISCSIState that must be freed with
 * storaged_iscsi_state_free().
 */
StoragedISCSIState *
storaged_iscsi_state_new (StoragedDaemon *daemon)
{
  StoragedISCSIState *state;

  state = g_malloc0 (sizeof(StoragedISCSIState));

  if (state)
    {
      /* Initialize members. */
      state->daemon = daemon;

      state->iscsi_ctx = libiscsi_init ();
    }

  return state;
}

void
storaged_iscsi_state_free (StoragedISCSIState *state)
{
  g_return_if_fail (state);

  /* Free/Unref members. */
  if (state->iscsi_ctx)
    libiscsi_cleanup (state->iscsi_ctx);

  g_free (state);
}

struct libiscsi_context *
storaged_iscsi_state_get_libiscsi_context (StoragedISCSIState *state)
{
  g_return_val_if_fail (state, NULL);
  return state->iscsi_ctx;
}
