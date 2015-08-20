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

#include <modules/storagedmoduleiface.h>

#include <storaged/storaged-generated.h>
#include <src/storageddaemon.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlogging.h>
#include <src/storagedmodulemanager.h>

#include "storagedglusterfstypes.h"
#include "storagedglusterfsstate.h"
#include "storagedglusterfsutils.h"
#include "storagedglusterfsinfo.h"
#include "storagedlinuxmanagerglusterd.h"
#include "storagedlinuxglusterfsvolumeobject.h"

/* ---------------------------------------------------------------------------------------------------- */

gchar *
storaged_module_id (void)
{
  return g_strdup (GLUSTERFS_MODULE_NAME);
}

gpointer
storaged_module_init (StoragedDaemon *daemon)
{
  return storaged_glusterfs_state_new (daemon);
}

void
storaged_module_teardown (StoragedDaemon *daemon)
{
  StoragedModuleManager *manager = storaged_daemon_get_module_manager (daemon);
  StoragedGlusterFSState *state_pointer = (StoragedGlusterFSState *) \
                                       storaged_module_manager_get_module_state_pointer (manager,
                                                                                         GLUSTERFS_MODULE_NAME);

  storaged_glusterfs_state_free (state_pointer);
}

/* ---------------------------------------------------------------------------------------------------- */

static StoragedGlusterFSState *
get_module_state (StoragedDaemon *daemon)         
{
  StoragedGlusterFSState *state;
  StoragedModuleManager *manager;

  manager = storaged_daemon_get_module_manager (daemon);
  g_assert (manager != NULL);

  state = (StoragedGlusterFSState *) storaged_module_manager_get_module_state_pointer (manager, GLUSTERFS_MODULE_NAME);
  g_assert (state != NULL);   

  return state;
}

/* ---------------------------------------------------------------------------------------------------- */

StoragedModuleInterfaceInfo **
storaged_module_get_block_object_iface_setup_entries (void)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

StoragedModuleInterfaceInfo **
storaged_module_get_drive_object_iface_setup_entries (void)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
glusterfs_update_all_from_variant (GVariant *volume_all_info_xml,
                                   GError *error,
                                   gpointer user_data)
{
  StoragedGlusterFSState *state;
  StoragedDaemon *daemon = STORAGED_DAEMON (user_data);
  GDBusObjectManagerServer *manager;
  GVariantIter var_iter;
  GHashTableIter *gfsvol_name_iter;
  GVariant *gfs_volumes;
  gpointer key, value;
  const gchar *name;

  if (error != NULL)
    {
      storaged_warning ("GlusterFS plugin: %s", error->message);
      return;
    }

  manager = storaged_daemon_get_object_manager (daemon);
  state = get_module_state (daemon);
  storaged_notice ("Got variant");
  gfs_volumes = storaged_process_glusterfs_xml_info (g_variant_get_bytestring (volume_all_info_xml)); 

  storaged_notice ("Got GlusterFS volume names");
  /* Remove obsolete gluster volumes */
  g_hash_table_iter_init (&gfsvol_name_iter,
                          storaged_glusterfs_state_get_name_to_glusterfs_volume (state));
  storaged_notice ("Removing obsolete glusterfs volumes");
  while (g_hash_table_iter_next (&gfsvol_name_iter, &key, &value))
    {
      storaged_notice ("Checking gfsvol"); 
      const gchar *gfsvol;
      StoragedLinuxGlusterFSVolumeObject *volume;      
      gboolean found = FALSE;

      name = key;
      volume = value;

      g_variant_iter_init (&var_iter, gfs_volumes);
      while (g_variant_iter_next (&var_iter, "&s", &gfsvol))
        if (g_strcmp0 (gfsvol, name) == 0)
          {
            found = TRUE;
            break;
          }

      if (!found)
        {                                 
          storaged_linux_glusterfs_volume_object_destroy (volume);
          g_dbus_object_manager_server_unexport (manager,
                                                 g_dbus_object_get_object_path (G_DBUS_OBJECT (volume)));
          g_hash_table_iter_remove (&gfsvol_name_iter);
        }
    }

  /* Add or update glusterfs volumes */
  g_variant_iter_init (&var_iter, gfs_volumes);
  while (g_variant_iter_next (&var_iter, "&s", &name))
    {
      StoragedLinuxGlusterFSVolumeObject *volume;
      volume = g_hash_table_lookup (storaged_glusterfs_state_get_name_to_glusterfs_volume (state),
                                   name);

      if (volume == NULL)
        {
          volume = storaged_linux_glusterfs_volume_object_new (daemon, name);
          storaged_debug ("GLusterFS volume object created");
          g_hash_table_insert (storaged_glusterfs_state_get_name_to_glusterfs_volume (state),
                               g_strdup (name), volume);
          storaged_debug ("New volume \"%s\" added to glusterfs state hashtable", name);
        }
      storaged_linux_glusterfs_volume_object_update (volume);
      storaged_debug ("Hhshshs");
    }
}

static void
glusterfs_volumes_update (StoragedDaemon *daemon)
{
  const gchar *args[] = { "/usr/sbin/gluster", "volume", "info", "all", "--xml", NULL };
  storaged_debug ("glusterfs_volumes_update");
  storaged_glusterfs_spawn_for_variant (args, G_VARIANT_TYPE("s"),
                                        glusterfs_update_all_from_variant, daemon);
}

static GDBusObjectSkeleton *
glusterfs_object_new (StoragedDaemon *daemon)
{
  storaged_debug ("glusterfs_object_new");
  glusterfs_volumes_update (daemon);
  return NULL;
}


StoragedModuleObjectNewFunc *
storaged_module_get_object_new_funcs (void)
{
  StoragedModuleObjectNewFunc *funcs = NULL;

  funcs = g_new0 (StoragedModuleObjectNewFunc, 2);
  funcs[0] = &glusterfs_object_new;

  return funcs;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
new_manager_glusterd_iface (StoragedDaemon *daemon)
{
  StoragedLinuxManagerGlusterD *manager;

  manager = storaged_linux_manager_glusterd_new (daemon);

  return G_DBUS_INTERFACE_SKELETON (manager);
}

StoragedModuleNewManagerIfaceFunc *
storaged_module_get_new_manager_iface_funcs (void)
{
  StoragedModuleNewManagerIfaceFunc *funcs;

  funcs = g_new0 (StoragedModuleNewManagerIfaceFunc, 2);
  funcs[0] = &new_manager_glusterd_iface;

  return funcs;
}

