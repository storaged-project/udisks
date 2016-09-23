/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2016 Red Hat Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Stef Walter <stefw@redhat.com>
 */

#include "config.h"

#include "storageddbus.h"

#include <glib.h>
#include <gio/gio.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <stdlib.h>

/*
 * Code from libsystemd documentation:
 *
 * Copyright 2014 Tom Gundersen
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

typedef struct SDEventSource {
  GSource source;
  GPollFD pollfd;
  sd_event *event;
} SDEventSource;

static gboolean
event_prepare (GSource *source,
               gint *timeout_)
{
  return sd_event_prepare (((SDEventSource *)source)->event) > 0;
}

static gboolean
event_check (GSource *source)
{
  return sd_event_wait (((SDEventSource *)source)->event, 0) > 0;
}

static gboolean
event_dispatch (GSource *source,
                GSourceFunc callback,
                gpointer user_data)
{
  return sd_event_dispatch (((SDEventSource *)source)->event) > 0;
}

static void
event_finalize (GSource *source)
{
  sd_event_unref (((SDEventSource *)source)->event);
}

static GSourceFuncs event_funcs = {
  .prepare = event_prepare,
  .check = event_check,
  .dispatch = event_dispatch,
  .finalize = event_finalize,
};

static void
connect_sd_event_glib_mainloop (void)
{
    SDEventSource *source;
    sd_event *event;

    if (sd_event_default (&event) < 0)
      g_return_if_reached ();

    source = (SDEventSource *)g_source_new(&event_funcs, sizeof (SDEventSource));

    source->event = sd_event_ref (event);
    source->pollfd.fd = sd_event_get_fd (event);
    source->pollfd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;

    g_source_add_poll ((GSource *)source, &source->pollfd);
    g_source_attach ((GSource *)source, NULL);
    g_source_unref ((GSource *)source);
}

static int
on_message_filter (sd_bus_message *message,
                   void *userdata,
                   sd_bus_error *ret_error)
{
  sd_bus *other = userdata;
  const sd_bus_error *error;
  const char *unique = NULL;
  uint8_t type;
  int r;


  if (sd_bus_message_is_method_call (message, "org.freedesktop.DBus", "Hello"))
    {
      r = sd_bus_get_unique_name (other, &unique);
      g_return_val_if_fail (r >= 0, r);

      r = sd_bus_reply_method_return (message, "s", unique);
      g_return_val_if_fail (r >= 0, r);

      return 1;
    }

#if 0
  r = sd_bus_message_get_type (message, &type);
  g_return_val_if_fail (r >= 0, r);

  error = sd_bus_message_get_error (message);
  if (error)
    {
      g_printerr ("passing message %p %d %s -> %s %s %s\n", userdata, (int)type,
                  sd_bus_message_get_sender (message),
                  sd_bus_message_get_destination (message),
                  error->name, error->message);
    }
  else
    {
      g_printerr ("passing message %p %d %s -> %s %s %s.%s\n", userdata, (int)type,
                  sd_bus_message_get_sender (message),
                  sd_bus_message_get_destination (message),
                  sd_bus_message_get_path (message),
                  sd_bus_message_get_interface (message),
                  sd_bus_message_get_member (message));
    }
#endif

  /*
   * The sd-bus code automatically bumps its latest serial number
   * reply cookie to be larger than the serial number in this
   * message. So thankfully we don't have to worry about the serial
   * numbers from GDBus and sd-bus overlapping.
   */

  r = sd_bus_send (other, message, NULL);
  if (r < 0)
    {
      g_critical ("Couldn't send message to GDBus sd-bus proxy: %s", g_strerror (-r));
      return r;
    }

  return 1;
}

static void
on_ready_get_result (GObject *source,
                     GAsyncResult *result,
                     gpointer user_data)
{
  GAsyncResult **retval = user_data;
  g_assert (retval != NULL);
  g_assert (*retval == NULL);
  *retval = g_object_ref (result);
}

static gboolean
open_proxied_connection (sd_bus *system,
                         sd_bus **proxy,
                         GDBusConnection **connection)
{
  sd_bus_slot *filter_in;
  sd_bus_slot *filter_out;
  sd_id128_t server_id;
  GDBusConnectionFlags flags;
  GAsyncResult *result = NULL;
  GSocket *socket = NULL;
  GIOStream *io = NULL;
  GError *error = NULL;
  int fds[2] = { -1, -1 };
  gboolean ret = FALSE;
  sd_bus *bus;
  int r;

  if (socketpair (PF_UNIX, SOCK_STREAM, 0, fds) < 0)
    {
      g_critical ("Couldn't create socket pair: %s", g_strerror (errno));
      goto out;
    }

  r = sd_bus_get_bus_id (system, &server_id);
  if (r < 0)
    {
      g_critical ("Couldn't connect to system bus: %s", g_strerror (-r));
      goto out;
    }

  /* Open an sd_bus for this FD */
  if (sd_bus_new (&bus) < 0)
    g_return_val_if_reached (FALSE);
  if (sd_bus_set_fd (bus, fds[0], fds[0]) < 0)
    g_return_val_if_reached (FALSE);
  fds[0] = -1; /* Claimed */
  if (sd_bus_set_server (bus, 1, server_id) < 0)
    g_return_val_if_reached (FALSE);
  if (sd_bus_set_anonymous (bus, 1) < 0)
    g_return_val_if_reached (FALSE);
  if (sd_bus_attach_event (bus, NULL, SD_EVENT_PRIORITY_NORMAL) < 0)
    g_return_val_if_reached (FALSE);
  r = sd_bus_start (bus);
  if (r < 0)
    {
      g_critical ("Couldn't start proxy bus: %s", g_strerror (r));
      goto out;
    }

  if (sd_bus_add_filter (system, &filter_in, on_message_filter, bus) < 0)
    g_return_val_if_reached (FALSE);
  if (sd_bus_add_filter (bus, &filter_out, on_message_filter, system) < 0)
    g_return_val_if_reached (FALSE);

  socket = g_socket_new_from_fd (fds[1], &error);
  if (!socket)
    {
      g_critical ("Couldn't create socket from fd: %s", error->message);
      g_clear_error (&error);
      goto out;
    }

  fds[1] = -1; /* Claimed */

  io = G_IO_STREAM (g_socket_connection_factory_create_connection (socket));
  flags = G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION;
  g_dbus_connection_new (io, NULL, flags, NULL, NULL, on_ready_get_result, &result);

  /*
   * We are actually a sync function, but we spin the main loop to allow
   * the GDBus connection and sd_bus proxy to start communicating properly.
   */

  while (result == NULL)
    g_main_context_iteration (NULL, TRUE);

  *connection = g_dbus_connection_new_finish (result, &error);
  if (*connection)
    {
      ret = TRUE;
      *proxy = bus;
      bus = NULL;
    }
  else
    {
      g_critical ("Couldn't create proxy GDBus to sd-bus connection: %s", error->message);
      g_clear_error (&error);
    }

out:
  if (fds[0] >= 0)
    close (fds[0]);
  if (fds[1] >= 0)
    close (fds[1]);
  g_clear_object (&result);
  g_clear_object (&socket);
  g_clear_object (&io);

  if (bus)
    sd_bus_unref (bus);

  return ret;
}

static gboolean
open_system_bus (sd_bus **system)
{
  int r;

  g_assert (system != NULL);

  r = sd_bus_default_system (system);
  if (r < 0)
    {
      g_critical ("Couldn't open system DBus bus: %s", g_strerror (-r));
      return FALSE;
    }

  if (sd_bus_attach_event (*system, NULL, SD_EVENT_PRIORITY_NORMAL) < 0)
    g_return_val_if_reached (FALSE);

  return TRUE;
}

gboolean
storaged_dbus_initialize (sd_bus **system,
                          GDBusConnection **connection)
{
  GDBusConnection *conn = NULL;
  sd_bus *proxy = NULL;
  sd_bus *bus = NULL;
  gboolean ret = FALSE;

  g_assert (system != NULL);
  g_assert (connection != NULL);

  connect_sd_event_glib_mainloop ();

  if (!open_system_bus (&bus))
    goto out;

  if (!open_proxied_connection (bus, &proxy, &conn))
    goto out;

  g_object_set_data_full (G_OBJECT (conn), "system-sd-bus",
                          sd_bus_ref (bus), (GDestroyNotify)sd_bus_unref);
  g_object_set_data_full (G_OBJECT (conn), "proxy-sd-bus",
                          sd_bus_ref (proxy), (GDestroyNotify)sd_bus_unref);

  *system = sd_bus_ref (bus);
  *connection = g_object_ref (conn);
  ret = TRUE;

out:
  g_clear_object (&conn);
  if (proxy)
    sd_bus_unref (proxy);
  if (bus)
    sd_bus_unref (bus);
  return ret;
}
