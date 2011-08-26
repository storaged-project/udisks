
#include <gio/gio.h>
#include "testutil.h"

typedef struct
{
  GMainLoop *loop;
  gboolean   timed_out;
} PropertyNotifyData;

static void
on_property_notify (GObject    *object,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
  PropertyNotifyData *data = user_data;
  g_main_loop_quit (data->loop);
}

static gboolean
on_property_notify_timeout (gpointer user_data)
{
  PropertyNotifyData *data = user_data;
  data->timed_out = TRUE;
  g_main_loop_quit (data->loop);
  return TRUE;
}

gboolean
_g_assert_property_notify_run (gpointer     object,
                               const gchar *property_name)
{
  gchar *s;
  gulong handler_id;
  guint timeout_id;
  PropertyNotifyData data;

  data.loop = g_main_loop_new (g_main_context_get_thread_default (), FALSE);
  data.timed_out = FALSE;
  s = g_strdup_printf ("notify::%s", property_name);
  handler_id = g_signal_connect (object,
                                 s,
                                 G_CALLBACK (on_property_notify),
                                 &data);
  g_free (s);
  timeout_id = g_timeout_add (5 * 1000,
                              on_property_notify_timeout,
                              &data);
  g_main_loop_run (data.loop);
  g_signal_handler_disconnect (object, handler_id);
  g_source_remove (timeout_id);
  g_main_loop_unref (data.loop);

  return data.timed_out;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GMainLoop *loop;
  gboolean   timed_out;
} SignalReceivedData;

static void
on_signal_received (gpointer user_data)
{
  SignalReceivedData *data = user_data;
  g_main_loop_quit (data->loop);
}

static gboolean
on_signal_received_timeout (gpointer user_data)
{
  SignalReceivedData *data = user_data;
  data->timed_out = TRUE;
  g_main_loop_quit (data->loop);
  return TRUE;
}

gboolean
_g_assert_signal_received_run (gpointer     object,
                               const gchar *signal_name,
                               GCallback    callback,
                               gpointer     user_data)
{
  gulong handler_id;
  gulong caller_handler_id;
  guint timeout_id;
  SignalReceivedData data;

  data.loop = g_main_loop_new (g_main_context_get_thread_default (), FALSE);
  data.timed_out = FALSE;
  caller_handler_id = 0;
  if (callback != NULL)
    caller_handler_id = g_signal_connect (object,
                                          signal_name,
                                          G_CALLBACK (callback),
                                          user_data);
  handler_id = g_signal_connect_swapped (object,
                                         signal_name,
                                         G_CALLBACK (on_signal_received),
                                         &data);
  timeout_id = g_timeout_add (5 * 1000,
                              on_signal_received_timeout,
                              &data);
  g_main_loop_run (data.loop);
  g_signal_handler_disconnect (object, handler_id);
  if (caller_handler_id != 0)
    g_signal_handler_disconnect (object, caller_handler_id);
  g_source_remove (timeout_id);
  g_main_loop_unref (data.loop);

  return data.timed_out;
}
