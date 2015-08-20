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

#include "storagedglusterfsutils.h"

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

