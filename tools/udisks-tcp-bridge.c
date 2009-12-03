/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2009 David Zeuthen <david@fubar.dk>
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
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

typedef struct
{
  DBusGConnection *bus;
  DBusServer *server;

  GList *connections;
} Bridge;

static void
bridge_free (Bridge *bridge)
{
  GList *l;

  for (l = bridge->connections; l != NULL; l = l->next)
    {
      DBusConnection *c = l->data;
      dbus_connection_close (c);
      dbus_connection_unref (c);
    }

  if (bridge->server != NULL)
    dbus_server_unref (bridge->server);
  if (bridge->bus != NULL)
    dbus_g_connection_unref (bridge->bus);

  g_free (bridge);
}

G_GNUC_UNUSED static void
print_message (DBusMessage *message)
{
    const gchar *message_type;
    switch (dbus_message_get_type (message))
      {
      case DBUS_MESSAGE_TYPE_METHOD_CALL:
        message_type = "method_call";
        break;

      case DBUS_MESSAGE_TYPE_METHOD_RETURN:
        message_type = "method_return";
        break;

      case DBUS_MESSAGE_TYPE_ERROR:
        message_type = "error";
        break;

      case DBUS_MESSAGE_TYPE_SIGNAL:
        message_type = "signal";
        break;

      case DBUS_MESSAGE_TYPE_INVALID:
        message_type = "invalid";
        break;

      default:
        message_type = "unknown";
        break;
      }

    g_print ("  type:         %s\n"
             "  sender:       %s\n"
             "  destination:  %s\n"
             "  path:         %s\n"
             "  interface:    %s\n"
             "  member:       %s\n",
             message_type,
             dbus_message_get_sender (message),
             dbus_message_get_destination (message),
             dbus_message_get_path (message),
             dbus_message_get_interface (message),
             dbus_message_get_member (message));
}

typedef struct
{
  DBusConnection *connection;
  DBusMessage *original_message;
} ForwardedMessage;

static void
forwarded_message_free (ForwardedMessage *fwd)
{
  dbus_connection_unref (fwd->connection);
  dbus_message_unref (fwd->original_message);
  g_free (fwd);
}

static void
on_forwarded_method_call_reply (DBusPendingCall *pending_call,
                                void            *user_data)
{
  ForwardedMessage *fwd = user_data;
  DBusMessage *reply;

  reply = dbus_pending_call_steal_reply (pending_call);

  //g_print ("Forwarding method call reply\n");
  //print_message (reply);

  dbus_message_set_reply_serial (reply, dbus_message_get_serial (fwd->original_message));
  dbus_connection_send (fwd->connection, reply, NULL);
  dbus_message_unref (reply);

  forwarded_message_free (fwd);
}

/* filter function for remote connections - used to forward method calls
 * for the org.freedesktop.UDisks
 */
static DBusHandlerResult
filter_function (DBusConnection *connection,
                 DBusMessage    *message,
                 void           *user_data)
{
  Bridge *bridge = user_data;
  DBusMessage *reply;

  /* Handle message synthezied by libdbus when the other end disconnects */
  if (dbus_message_is_signal (message, "org.freedesktop.DBus.Local", "Disconnected") &&
      dbus_message_get_destination (message) == NULL)
    {
      dbus_connection_close (connection);
      dbus_connection_unref (connection);
      g_warn_if_fail (g_list_find (bridge->connections, connection) != NULL);
      bridge->connections = g_list_remove (bridge->connections, connection);
    }

  /* Handle AddMatch/RemoveMatch methods for the message bus - we'll
   * guarantee forwarding of all signals from UDisks anyway
   *
   * (D-Bus libraries like dbus-glib are buggy in this way)
   */
  else if ((dbus_message_is_method_call (message, "org.freedesktop.DBus", "AddMatch") ||
            dbus_message_is_method_call (message, "org.freedesktop.DBus", "RemoveMatch")) &&
           g_strcmp0 (dbus_message_get_destination (message), "org.freedesktop.DBus") == 0)
    {
      reply = dbus_message_new_method_return (message);
      dbus_connection_send (connection, reply, NULL);
      dbus_message_unref (reply);
    }

  /* For the same reasons, also handle GetNameOwner on the bus for the
   * org.freedesktop.UDisks name - just return :42.0
   *
   * (D-Bus libraries like dbus-glib are buggy in this way)
   */
  else if (dbus_message_is_method_call (message, "org.freedesktop.DBus", "GetNameOwner") &&
           g_strcmp0 (dbus_message_get_destination (message), "org.freedesktop.DBus") == 0)
    {
      DBusMessageIter iter;
      const gchar *fake_unique_name;

      fake_unique_name = ":1.42";

      reply = dbus_message_new_method_return (message);
      dbus_message_iter_init_append (reply, &iter);
      dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &fake_unique_name);
      dbus_connection_send (connection, reply, NULL);
      dbus_message_unref (reply);
    }

  /* Forward only method call messages for the UDisks service */
  else if (g_strcmp0 (dbus_message_get_destination (message), "org.freedesktop.UDisks") == 0 &&
           dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_METHOD_CALL)
    {
      ForwardedMessage *fwd;
      DBusPendingCall *pending_call;

      fwd = g_new0 (ForwardedMessage, 1);
      fwd->connection = dbus_connection_ref (connection);
      fwd->original_message = dbus_message_ref (message);

      //g_print ("Forwarding method call\n");
      //print_message (message);

      dbus_connection_send_with_reply (dbus_g_connection_get_connection (bridge->bus),
                                       message,
                                       &pending_call,
                                       INT_MAX);

      dbus_pending_call_set_notify (pending_call,
                                    on_forwarded_method_call_reply,
                                    fwd,
                                    NULL);
    }

  /* Disconnect clients sending inappropriate messages */
  else
    {
      /* TODO: disconnect the client */
      //g_print ("Warning: dropping inappropriate message\n");
      //print_message (message);
    }

  return DBUS_HANDLER_RESULT_HANDLED;
}

static void
on_new_connection (DBusServer     *server,
                   DBusConnection *new_connection,
                   void           *user_data)
{
  Bridge *bridge = user_data;

  dbus_connection_set_allow_anonymous (new_connection, TRUE);
  dbus_connection_add_filter (new_connection,
                              filter_function,
                              bridge,
                              NULL);
  dbus_connection_setup_with_g_main (new_connection, NULL);
  bridge->connections = g_list_prepend (bridge->connections, dbus_connection_ref (new_connection));
}

/* filter function for local connections - used to forward signals from
 * org.freedesktop.UDisks to all the remote connections
 */
static DBusHandlerResult
bus_filter_function (DBusConnection *connection,
                     DBusMessage    *message,
                     void           *user_data)
{
  Bridge *bridge = user_data;

  /* Can't match on sender name because the bus rewrites it to a unique name - the
   * fact that we only add matches for org.freedesktop.UDisks means that we only
   * ever get signals from two sources - the bus and the UDisks server
   */
  if (dbus_message_get_type (message) == DBUS_MESSAGE_TYPE_SIGNAL &&
      g_strcmp0 (dbus_message_get_sender (message), "org.freedesktop.DBus") != 0)
    {
      GList *l;
      DBusMessage *rewritten_message;

      /* Rewrite the message so the sender is :1.42 as per the comment in filter_function
       * for handling the name
       */
      rewritten_message = dbus_message_copy (message);
      dbus_message_set_sender (rewritten_message, ":1.42");

      //g_print ("TODO: forward signal\n");
      //print_message (rewritten_message);

      for (l = bridge->connections; l != NULL; l = l->next)
        {
          DBusConnection *remote_connection = l->data;
          dbus_connection_send (remote_connection, rewritten_message, NULL);
        }

      dbus_message_unref (rewritten_message);
    }

  return DBUS_HANDLER_RESULT_HANDLED;
}

int
main (int argc, char **argv)
{
  gint ret;
  GError *error;
  GOptionContext *context;
  GMainLoop *loop;
  static gboolean port_number;
  DBusError dbus_error;
  gchar *server_address;
  Bridge *bridge;
  const gchar *auth_mechanisms[] = {"ANONYMOUS", NULL};
  static GOptionEntry entries[] =
    {
      { "port", 'p', 0, G_OPTION_ARG_INT, &port_number, "TCP port number to listen on", NULL },
      { NULL }
    };

  ret = 1;
  context = NULL;
  loop = NULL;
  server_address = NULL;
  bridge = NULL;

  g_type_init ();

  bridge = g_new0 (Bridge, 1);

  context = g_option_context_new ("udisks TCP/IP bridge");

  g_option_context_add_main_entries (context, entries, NULL);

  error = NULL;
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("Error parsing arguments: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  if (port_number == 0)
    {
      g_printerr ("Port not specified\n");
      goto out;
    }

  error = NULL;
  bridge->bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
  if (bridge->bus == NULL)
    {
      g_printerr ("Error connecting to the system bus: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  dbus_connection_add_filter (dbus_g_connection_get_connection (bridge->bus),
                              bus_filter_function,
                              bridge,
                              NULL);

  dbus_error_init (&dbus_error);
  dbus_bus_add_match (dbus_g_connection_get_connection (bridge->bus),
                      "type='signal',sender='org.freedesktop.UDisks'",
                      &dbus_error);
  if (dbus_error_is_set (&dbus_error))
    {
      g_printerr ("Error adding match rule: %s: %s\n",
                  dbus_error.name,
                  dbus_error.message);
      dbus_error_free (&dbus_error);
      goto out;
    }

  /* TODO: take host as parameter */
  server_address = g_strdup_printf ("tcp:host=0.0.0.0,port=%d", port_number);

  dbus_error_init (&dbus_error);
  bridge->server = dbus_server_listen (server_address, &dbus_error);
  if (bridge->server == NULL)
    {
      g_printerr ("Error listening to address `%s': %s: %s\n",
                  server_address,
                  dbus_error.name,
                  dbus_error.message);
      dbus_error_free (&dbus_error);
      goto out;
    }
  dbus_server_setup_with_g_main (bridge->server, NULL);

  dbus_server_set_new_connection_function (bridge->server,
                                           on_new_connection,
                                           bridge,
                                           NULL);

  /* Allow only anonymous auth */
  if (!dbus_server_set_auth_mechanisms (bridge->server, auth_mechanisms))
    {
      g_printerr ("Error setting auth mechanisms\n");
      g_error_free (error);
      goto out;
    }

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  ret = 0;

 out:
  g_free (server_address);
  if (context != NULL)
    g_option_context_free (context);
  if (loop != NULL)
    g_main_loop_unref (loop);
  if (bridge != NULL)
    bridge_free (bridge);

  return ret;
}
