/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Samikshan Bairagya <sbairagy@redhat.com>
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

#include <src/storagedlogging.h>
#include "storagedglusterfsstate.h"

struct _StoragedGlusterFSState
{
  StoragedDaemon *daemon;

  /* maps from gluster volume name to StoragedLinuxGlusterFSVolumeObject instances. */
  GHashTable *name_to_glusterfs_volume;
  StoragedLinuxGlusterFSGlusterdObject *glusterd_obj;
};

/**
 * storaged_glusterfs_state_new:
 * @daemon: A #StoragedDaemon instance.
 *
 * Initializes the #StoragedGlusterFSState structure that holds the global state
 * within GlusterFS plugin.
 *
 * Returns: (transfer full): A #StoragedGlusterFSState that must be freed with
 * storaged_glusterfs_state_free().
 */
StoragedGlusterFSState *
storaged_glusterfs_state_new (StoragedDaemon *daemon)
{
  StoragedGlusterFSState *state;

  state = g_malloc0 (sizeof(StoragedGlusterFSState));

  if (state)
    {
      state->daemon = daemon;
      state->name_to_glusterfs_volume = g_hash_table_new_full (g_str_hash,
                                                               g_str_equal,
                                                               g_free,
                                                               (GDestroyNotify) g_object_unref);
      state->glusterd_obj = NULL;
    }

  return state;
}

void
storaged_glusterfs_state_free (StoragedGlusterFSState *state)
{
  g_return_if_fail (state);

  g_hash_table_unref(state->name_to_glusterfs_volume);

  g_free (state);
}

GHashTable *
storaged_glusterfs_state_get_name_to_glusterfs_volume (StoragedGlusterFSState *state)
{
  g_return_val_if_fail (state, NULL);
  return state->name_to_glusterfs_volume;
}

StoragedLinuxGlusterFSGlusterdObject *
storaged_glusterfs_state_get_glusterd (StoragedGlusterFSState *state)
{
  g_return_val_if_fail (state, NULL);
  return state->glusterd_obj;
}

void
storaged_glusterfs_state_set_glusterd (StoragedGlusterFSState               *state,
                                       StoragedLinuxGlusterFSGlusterdObject *object)
{
  g_return_if_fail (state);
  state->glusterd_obj = object;
}
