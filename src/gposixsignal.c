/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/* GDBus - GLib D-Bus Library
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <unistd.h>
#include <sys/signalfd.h>
#include <signal.h>

#include "gposixsignal.h"

typedef struct
{
  GSource source;
  GPollFD pollfd;
  gint signum;
} _GPosixSignalSource;

static gboolean
_g_posix_signal_source_prepare (GSource  *_source,
                                gint     *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
_g_posix_signal_source_check (GSource  *_source)
{
  _GPosixSignalSource *source = (_GPosixSignalSource *) _source;
  return source->pollfd.revents != 0;
}

static gboolean
_g_posix_signal_source_dispatch (GSource     *_source,
                                 GSourceFunc  callback,
                                 gpointer     user_data)

{
  _GPosixSignalWatchFunc func = (_GPosixSignalWatchFunc) callback;
  g_warn_if_fail (func != NULL);
  return (*func) (user_data);
}

static void
_g_posix_signal_source_finalize (GSource *_source)
{
  _GPosixSignalSource *source = (_GPosixSignalSource *) _source;
  close (source->pollfd.fd);
}

static GSourceFuncs _g_posix_signal_source_funcs =
{
  _g_posix_signal_source_prepare,
  _g_posix_signal_source_check,
  _g_posix_signal_source_dispatch,
  _g_posix_signal_source_finalize
};

GSource *
_g_posix_signal_source_new (gint signum)
{
  sigset_t sigset;
  gint fd;
  GSource *_source;
  _GPosixSignalSource *source;

  _source = NULL;

  sigemptyset (&sigset);
  sigaddset (&sigset, signum);

  if (sigprocmask (SIG_BLOCK, &sigset, NULL) == -1)
    g_assert_not_reached ();

  fd = signalfd (-1, &sigset, SFD_NONBLOCK | SFD_CLOEXEC);

  _source = g_source_new (&_g_posix_signal_source_funcs, sizeof (_GPosixSignalSource));
  source = (_GPosixSignalSource *) _source;

  source->pollfd.fd = fd;
  source->pollfd.events = G_IO_IN;
  g_source_add_poll (_source, &source->pollfd);

  source->signum = signum;
  return _source;
}

guint
_g_posix_signal_watch_add (gint                   signum,
                           gint                   priority,
                           _GPosixSignalWatchFunc function,
                           gpointer               user_data,
                           GDestroyNotify         notify)
{
  GSource *source;
  guint id;

  g_return_val_if_fail (function != NULL, 0);

  source = _g_posix_signal_source_new (signum);
  if (priority != G_PRIORITY_DEFAULT_IDLE)
    g_source_set_priority (source, priority);
  g_source_set_callback (source, (GSourceFunc) function, user_data, notify);
  id = g_source_attach (source, NULL);
  g_source_unref (source);

  return id;
}
