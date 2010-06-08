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

#ifndef ___G_POSIX_SIGNAL_H__
#define ___G_POSIX_SIGNAL_H__

#include <glib.h>

G_BEGIN_DECLS

typedef gboolean (*_GPosixSignalWatchFunc) (gpointer user_data);

GSource *_g_posix_signal_source_new (gint signum);

guint _g_posix_signal_watch_add (gint                   signum,
                                 gint                   priority,
                                 _GPosixSignalWatchFunc function,
                                 gpointer               user_data,
                                 GDestroyNotify         notify);

G_END_DECLS

#endif /* ___G_POSIX_SIGNAL_H__ */
