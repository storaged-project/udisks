/* -*- mode: c; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * copyright (c) 2015 samikshan bairagya <sbairagy@redhat.com>
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license as published by
 * the free software foundation; either version 2 of the license, or
 * (at your option) any later version.
 *
 * this program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * merchantability or fitness for a particular purpose.  see the
 * gnu general public license for more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program; if not, write to the free software
 * foundation, inc., 51 franklin st, fifth floor, boston, ma  02110-1301  usa
 */

#include <glib.h>

#include <src/storagedlogging.h>
#include <src/storageddaemon.h>
#include <src/storagedmodulemanager.h>

#include "storagedglusterfsutils.h"
#include "storagedglusterfsstate.h"
#include "storagedglusterfsinfo.h"
#include "storagedlinuxglusterfsvolumeobject.h"


GVariant *volumes_names = NULL;

struct VariantReaderData {
  const GVariantType *type;
  void (*callback) (GVariant *result, GError *error, gpointer user_data);
  gpointer user_data;
  GIOChannel *output_channel;
  GString *output;
  gint output_watch;
};

static gboolean
variant_reader_child_output (GIOChannel *source,
                             GIOCondition condition,
                             gpointer user_data)
{
  storaged_debug ("variant_reader_child_output");
  struct VariantReaderData *data = user_data;
  guint8 buf[1024];
  gsize bytes_read;

  if (condition == G_IO_HUP)
    {
      storaged_debug ("BROKEN Pipe");
      g_io_channel_unref (data->output_channel);
      return FALSE;
    }

  g_io_channel_read_chars (source, (gchar *)buf, sizeof buf, &bytes_read, NULL);
  data->output = g_string_new_len (buf, bytes_read);
  return FALSE;
}

static void
variant_reader_watch_child (GPid     pid,
                            gint     status,
                            gpointer user_data)
{
  struct VariantReaderData *data = user_data;
  GVariant *result;
  gchar *buf;
  gsize buf_size;
  GError *error = NULL;

  if (!g_spawn_check_exit_status (status, &error))
    {
      storaged_warning ("Error occured while trying to get glusterfs volume information");
      data->callback (NULL, error, data->user_data);
      g_error_free (error);
    }
  else
    {
      if (g_io_channel_read_to_end (data->output_channel, &buf, &buf_size, NULL) == G_IO_STATUS_NORMAL)
        {
          storaged_notice ("Reading remaining gfsvol info");
          g_string_append_len (data->output, buf, buf_size);
          g_free(buf);
          storaged_debug ("GlusterFS vol info read: \n %s", data->output->str);
        } 
 
      result = g_variant_new_bytestring (data->output->str);
      data->callback (result, NULL, data->user_data);
      g_variant_unref (result);
    }

  storaged_debug ("Freeing stuff");
  g_io_channel_unref (data->output_channel);
  data->output_watch = NULL;

  g_spawn_close_pid (pid);
  g_string_free (data->output, TRUE);
  g_free (data);
   
}

GPid
storaged_glusterfs_spawn_for_variant (const gchar       **argv,
                                           const GVariantType *type,
                                           void (*callback) (GVariant *result,
                                                             GError *error,
                                                             gpointer user_data),
                                           gpointer user_data)
{
  storaged_debug ("storaged_glusterfs_spawn_for_variant");
  GError *error = NULL;                           
  struct VariantReaderData *data;                 
  GPid pid;
  gint output_fd;

  if (!g_spawn_async_with_pipes (NULL,            
                                 (gchar **)argv,  
                                 NULL,            
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,            
                                 &pid,            
                                 NULL,            
                                 &output_fd,
                                 NULL,            
                                 &error))         
    {
      callback (NULL, error, user_data);
      g_error_free (error);
      return;
    }

  data = g_new0 (struct VariantReaderData, 1); 

  data->type = type;
  data->callback = callback;
  data->user_data = user_data;


  data->output_channel = g_io_channel_unix_new (output_fd);
  data->output_watch = g_io_add_watch (data->output_channel,
                                       G_IO_IN | G_IO_HUP,
                                       variant_reader_child_output,
                                       data);                                                                                       

  g_child_watch_add (pid, variant_reader_watch_child, data);
  return pid;
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

static void
storaged_glusterfs_update_all_from_variant (GVariant *volume_all_info_xml,
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

void
storaged_glusterfs_volumes_update (StoragedDaemon *daemon)
{
  const gchar *args[] = { "/usr/sbin/gluster", "volume", "info", "all", "--xml", NULL };
  storaged_debug ("glusterfs_volumes_update");
  storaged_glusterfs_spawn_for_variant (args, G_VARIANT_TYPE("s"),
                                        storaged_glusterfs_update_all_from_variant, daemon);
}

