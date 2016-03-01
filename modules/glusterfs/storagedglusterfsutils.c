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

#include "config.h"

#include <glib.h>

#include <src/storagedlogging.h>
#include <src/storageddaemon.h>
#include <src/storagedmodulemanager.h>

#include "storagedglusterfsutils.h"
#include "storagedglusterfsstate.h"
#include "storagedglusterfsinfo.h"
#include "storagedlinuxglusterfsvolumeobject.h"
#include "storagedlinuxglusterfsbrickobject.h"
#include "storagedlinuxglusterfsglusterdobject.h"

const gchar *glusterfs_policy_action_id = "org.storaged.Storaged.glusterfs.manage-glusterfs";
GVariant *volumes_names = NULL;

struct VariantReaderData
{
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
  struct VariantReaderData *data = user_data;
  guint8 buf[1024];
  gsize bytes_read;

  if (condition == G_IO_HUP)
    {
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
          g_string_append_len (data->output, buf, buf_size);
          g_free(buf);
        }

      result = g_variant_new_bytestring (data->output->str);
      data->callback (result, NULL, data->user_data);
      g_variant_unref (result);
    }

  g_io_channel_unref (data->output_channel);
  /*data->output_watch = NULL;*/

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
  GError *error = NULL;
  struct VariantReaderData *data;
  GPid pid;
  gint output_fd;

  if (!g_spawn_async_with_pipes (NULL,
                                 (gchar **)argv,
                                 NULL,
                                 G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
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
      return 0;
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
  GHashTableIter gfsvol_name_iter;
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
  gfs_volumes = storaged_process_glusterfs_volume_info_all (g_variant_get_bytestring (volume_all_info_xml));

  /* Remove obsolete gluster volumes */
  g_hash_table_iter_init (&gfsvol_name_iter,
                          storaged_glusterfs_state_get_name_to_glusterfs_volume (state));
  while (g_hash_table_iter_next (&gfsvol_name_iter, &key, &value))
    {
      const gchar *gfsvol;
      StoragedLinuxGlusterFSVolumeObject *volume;
      GHashTableIter bricks_iter;
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
          /* First unexport dbus objects corresponding to the volume's bricks */
          g_hash_table_iter_init (&bricks_iter, volume->bricks);
          while (g_hash_table_iter_next (&bricks_iter, &key, &value))
            {
              StoragedLinuxGlusterFSBrickObject *brick_obj = value;
              g_dbus_object_manager_server_unexport (manager,
                                                     g_dbus_object_get_object_path (G_DBUS_OBJECT (brick_obj)));
              g_hash_table_iter_remove (&bricks_iter);
            }

          g_dbus_object_manager_server_unexport (manager,
                                                 g_dbus_object_get_object_path (G_DBUS_OBJECT (volume)));
          storaged_linux_glusterfs_volume_object_destroy (volume);
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
          g_hash_table_insert (storaged_glusterfs_state_get_name_to_glusterfs_volume (state),
                               g_strdup (name), volume);
          storaged_debug ("New volume \"%s\" added to glusterfs state hashtable", name);
        }
      storaged_linux_glusterfs_volume_object_update (volume);
    }
}

void
storaged_glusterfs_volumes_update (StoragedDaemon *daemon)
{
  const gchar *args[] = { "gluster", "volume", "info", "all", "--xml", NULL };
  storaged_glusterfs_spawn_for_variant (args, G_VARIANT_TYPE("s"),
                                        storaged_glusterfs_update_all_from_variant, daemon);
}

StoragedLinuxGlusterFSVolumeObject *
storaged_glusterfs_util_find_volume_object (StoragedDaemon *daemon,
                                            const gchar    *name)
{
  StoragedGlusterFSState *state;
  state = get_module_state (daemon);
  return g_hash_table_lookup (storaged_glusterfs_state_get_name_to_glusterfs_volume (state), name);
}

/****************************************************************************/

static GDBusProxy *
storaged_get_sddbus_proxy (const gchar *obj_path,
                           const gchar *interface_name)
{
  GError *error;
  GDBusProxy *proxy;

  error = NULL;
  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.freedesktop.systemd1",
                                         obj_path,
                                         interface_name,
                                         NULL,
                                         &error);
  return proxy;
}

static gchar *
storaged_get_path_for_service (const gchar *service_name)
{
  GDBusProxy *proxy;
  GVariant *path;
  GError *error;

  gchar *service_path;

  error = NULL;
  service_path = NULL;
  proxy = storaged_get_sddbus_proxy ("/org/freedesktop/systemd1",
                                     "org.freedesktop.systemd1.Manager");
  if (proxy == NULL)
    {
      storaged_error ("Error creating proxy for systemd dbus: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  path = g_dbus_proxy_call_sync (proxy,
                                 "GetUnit",
                                 g_variant_new ("(s)",
                                                service_name),
                                 G_DBUS_CALL_FLAGS_NONE,
                                 -1,
                                 NULL,
                                 &error);
  if (path == NULL)
    {
      storaged_error ("Error trying to find DBus object corresponding to service %s: %s\n",
                      service_name,
                      error->message);
      g_error_free (error);
      goto out;
    }

  g_assert (g_variant_n_children (path) == 1);
  service_path = g_variant_get_string (g_variant_get_child_value (path, 0), NULL);
  storaged_info ("Service path: %s", service_path);

out:
  if (proxy != NULL)
    g_object_unref (proxy);
  if (path != NULL)
    g_variant_unref (path);
  return service_path;
}

GVariant *
storaged_get_glusterd_info (void)
{
  /* Check for status of glusterd.service */
  GDBusProxy *proxy;
  const gchar *service_path;
  const gchar *load_state;
  const gchar *active_state;
  GVariantDict glusterd_info;

  service_path = storaged_get_path_for_service ("glusterd.service");

  if (service_path == NULL) /* glusterd.service is not yet loaded */
    return NULL;

  proxy = storaged_get_sddbus_proxy (service_path,
                                     "org.freedesktop.systemd1.Unit");
  if (proxy == NULL)
    return NULL;

  storaged_debug ("Object path: %s", g_dbus_proxy_get_object_path (proxy));

  load_state = g_variant_get_string (g_dbus_proxy_get_cached_property (proxy, "LoadState"), NULL);
  active_state = g_variant_get_string (g_dbus_proxy_get_cached_property (proxy, "ActiveState"), NULL);

  g_variant_dict_init (&glusterd_info, NULL);
  g_variant_dict_insert (&glusterd_info,
                         "LoadState",
                         "s",
                         load_state);
  g_variant_dict_insert (&glusterd_info,
                         "ActiveState",
                         "s",
                         active_state);

  if (proxy != NULL)
    g_object_unref (proxy);

  return g_variant_dict_end (&glusterd_info);
}

void
storaged_glusterfs_daemons_update (StoragedDaemon *daemon)
{
  StoragedLinuxGlusterFSGlusterdObject *glusterd_obj;
  StoragedGlusterFSState *state;

  state = get_module_state (daemon);
  glusterd_obj = storaged_glusterfs_state_get_glusterd (state);

  if (glusterd_obj == NULL)
    {
      glusterd_obj = storaged_linux_glusterfs_glusterd_object_new (daemon);
      storaged_glusterfs_state_set_glusterd (state, glusterd_obj);
    }
  g_return_if_fail (STORAGED_IS_LINUX_GLUSTERFS_GLUSTERD_OBJECT (glusterd_obj));
  storaged_linux_glusterfs_glusterd_object_update (glusterd_obj);
}

