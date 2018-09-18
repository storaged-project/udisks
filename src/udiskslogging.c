/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2011 David Zeuthen <zeuthen@gmail.com>
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

#include "config.h"
#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <sys/syscall.h>

#include "udiskslogging.h"

/**
 * SECTION:udiskslogging
 * @title: Logging
 * @short_description: Logging Routines
 *
 * Logging routines.
 */

/**
 * udisks_log:
 * @level: A #UDisksLogLevel.
 * @function: Pass #G_STRFUNC here.
 * @location: Pass #G_STRLOC here.
 * @format: printf()-style format.
 * @...: Arguments for format.
 *
 * Low-level logging function used by udisks_debug() and other macros.
 */
void
udisks_log (UDisksLogLevel     level,
            const gchar       *function,
            const gchar       *location,
            const gchar       *format,
            ...)
{
  va_list var_args;
  gchar *message;

  va_start (var_args, format);
  message = g_strdup_vprintf (format, var_args);
  va_end (var_args);

#if GLIB_CHECK_VERSION(2, 50, 0)
  g_log_structured ("udisks", (GLogLevelFlags) level,
                    "MESSAGE", "%s", message, "THREAD_ID", "%d", (gint) syscall (SYS_gettid),
                    "CODE_FUNC", function, "CODE_FILE", location);
#else
  g_log ("udisks", level, "[%d]: %s [%s, %s()]", (gint) syscall (SYS_gettid), message, location, function);
#endif

  g_free (message);
}
