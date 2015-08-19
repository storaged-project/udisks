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

#include "storagedglusterfsutils.h"

GVariant *volumes_names = NULL;

struct VariantReaderData {
  const GVariantType *type;
  void (*callback) (GPid pid, GVariant *result, GError *error, gpointer user_data);
  gpointer user_data;
  GPid pid;
  GIOChannel *output_channel;
  GByteArray *output;
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

  g_io_channel_read_chars (source, (gchar *)buf, sizeof buf, &bytes_read, NULL);
  g_byte_array_append (data->output, buf, bytes_read);
  return TRUE;
}

static void
variant_reader_watch_child (GPid     pid,
                            gint     status,
                            gpointer user_data)
{
  struct VariantReaderData *data = user_data;
  guint8 *buf;
  gsize buf_size;
  GVariant *result;
  GError *error = NULL;

  data->pid = 0;

  if (!g_spawn_check_exit_status (status, &error))
    {
      data->callback (pid, NULL, error, data->user_data);
      g_error_free (error);
      g_byte_array_free (data->output, TRUE);
    }
  else
    {
      if (g_io_channel_read_to_end (data->output_channel, (gchar **)&buf, &buf_size, NULL) == G_IO_STATUS_NORMAL)
        {
          g_byte_array_append (data->output, buf, buf_size);
          g_free (buf);
        }

      result = g_variant_new_from_data (data->type,
                                        data->output->data,
                                        data->output->len,
                                        TRUE,
                                        g_free, NULL);
      g_byte_array_free (data->output, FALSE);
      data->callback (pid, result, NULL, data->user_data);
      g_variant_unref (result);
    }
}

static void
variant_reader_destroy (gpointer user_data)
{
  struct VariantReaderData *data = user_data;

  g_source_remove (data->output_watch);
  g_io_channel_unref (data->output_channel);
  g_free (data);
}

void storaged_glusterfs_spawn_for_variant (const gchar       **argv,
                                           const GVariantType *type,
                                           void (*callback) (GPid pid,
                                                             GVariant *result,
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
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 NULL,
                                 NULL,            
                                 &pid,            
                                 NULL,            
                                 &output_fd,
                                 NULL,            
                                 &error))         
    {                                             
      callback (0, NULL, error, user_data);
      g_error_free (error);
      return 0;
    }

  data = g_new0 (struct VariantReaderData, 1); 

  data->type = type;
  data->callback = callback;
  data->user_data = user_data;

  data->pid = pid;                                
  data->output = g_byte_array_new ();
  data->output_channel = g_io_channel_unix_new (output_fd);
  g_io_channel_set_encoding (data->output_channel, NULL, NULL);
  g_io_channel_set_flags (data->output_channel, G_IO_FLAG_NONBLOCK | G_IO_FLAG_IS_SEEKABLE, NULL);
  data->output_watch = g_io_add_watch (data->output_channel, G_IO_IN, variant_reader_child_output, data);                                                                                       

  g_child_watch_add_full (G_PRIORITY_DEFAULT_IDLE,
                          pid, variant_reader_watch_child, data, variant_reader_destroy);
  return pid;
}

/* ---------------------------------------------------------------------------------------------------- */











