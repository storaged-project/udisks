/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2010 Martin Pitt <martin.pitt@ubuntu.com>
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

#ifndef __PROFILE_H
#define __PROFILE_H

#include "config.h"

#include <unistd.h>
#include <glib.h>

/* Define a macro PROFILE(format, ...) which adds a trace point for profiling
 * startup speed. This executes an access() call to a fake file name 
 * "MARK: <description>" which can easily be evaluated with strace -t.
 *
 * http://people.gnome.org/~federico/news-2006-03.html#login-time-2 describes
 * how to turn these strace logs into a nice graph:
 *
 *   # strace -tttfo /tmp/trace src/udisks-daemon
 *   [...]
 *   $ plot-timeline.py -o /tmp/trace.png /tmp/trace
 *
 * If profiling is not enabled, this macro is a no-op.
 */

#ifdef PROFILING
# define PROFILE(format, ...) { \
        char *str = g_strdup_printf ("MARK: %s: " format, g_get_prgname(), ##__VA_ARGS__); \
        access (str, F_OK); \
        g_free (str); \
}
#else
# define PROFILE(...) {}
#endif

#endif /* __PROFILE_H */

