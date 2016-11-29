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

#ifndef __UDISKS_LOGGING_H__
#define __UDISKS_LOGGING_H__

#include "udisksdaemontypes.h"
#include "config.h"

G_BEGIN_DECLS

void udisks_log (UDisksLogLevel   level,
                 const gchar     *function,
                 const gchar     *location,
                 const gchar     *format,
                 ...) G_GNUC_PRINTF (4, 5);

/**
 * udisks_debug:
 * @args...: printf()-style format string and arguments
 *
 * Logging macro for %UDISKS_LOG_LEVEL_DEBUG.
 * Warning: ONLY WORKS WHEN COMPILED WITH --enable-debug
 *
 * See #UDisksLogLevel for more details.
 */
#ifdef DEBUG
#define udisks_debug(args...)   udisks_log(UDISKS_LOG_LEVEL_DEBUG, G_STRFUNC, G_STRLOC, args)
#else
#define udisks_debug(args...)
#endif

/**
 * udisks_info:
 * @args...: printf()-style format string and arguments
 *
 * Logging macro for %UDISKS_LOG_LEVEL_INFO.
 * Warning: ONLY WORKS WHEN COMPILED WITH --enable-debug
 *
 * See #UDisksLogLevel for more details.
 */
#ifdef DEBUG
#define udisks_info(args...)    udisks_log(UDISKS_LOG_LEVEL_INFO, G_STRFUNC, G_STRLOC, args)
#else
#define udisks_info(args...)
#endif

/**
 * udisks_notice:
 * @args...: printf()-style format string and arguments
 *
 * Logging macro for %UDISKS_LOG_LEVEL_MESSAGE.
 *
 * See #UDisksLogLevel for more details.
 */
#define udisks_notice(args...)    udisks_log(UDISKS_LOG_LEVEL_MESSAGE, G_STRFUNC, G_STRLOC, args)

/**
 * udisks_message:
 * @args...: printf()-style format string and arguments
 *
 * Logging macro for %UDISKS_LOG_LEVEL_MESSAGE.
 *
 * See #UDisksLogLevel for more details.
 */
#define udisks_message(args...)    udisks_log(UDISKS_LOG_LEVEL_MESSAGE, G_STRFUNC, G_STRLOC, args)

/**
 * udisks_warning:
 * @args...: printf()-style format string and arguments
 *
 * Logging macro for %UDISKS_LOG_LEVEL_WARNING.
 *
 * See #UDisksLogLevel for more details.
 */
#define udisks_warning(args...) udisks_log(UDISKS_LOG_LEVEL_WARNING, G_STRFUNC, G_STRLOC, args)

/**
 * udisks_critical:
 * @args...: printf()-style format string and arguments
 *
 * Logging macro for %UDISKS_LOG_LEVEL_CRITICAL.
 *
 * See #UDisksLogLevel for more details.
 */
#define udisks_critical(args...)   udisks_log(UDISKS_LOG_LEVEL_CRITICAL, G_STRFUNC, G_STRLOC, args)

/**
 * udisks_error:
 * @args...: printf()-style format string and arguments
 *
 * Logging macro for %UDISKS_LOG_LEVEL_ERROR.
 * Warning: THIS ALSO TERMINATES THE PROCESS with the abort() call!
 *
 * See #UDisksLogLevel for more details.
 */
#define udisks_error(args...)   udisks_log(UDISKS_LOG_LEVEL_ERROR, G_STRFUNC, G_STRLOC, args)


G_END_DECLS

#endif /* __UDISKS_LOGGING_H__ */
