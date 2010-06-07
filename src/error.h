/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <david@fubar.dk>
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


#ifndef __ERROR_H__
#define __ERROR_H__

#include "types.h"

G_BEGIN_DECLS

typedef enum
{
  ERROR_FAILED,
  ERROR_PERMISSION_DENIED,
  ERROR_BUSY,
  ERROR_CANCELLED,
  ERROR_INHIBITED,
  ERROR_INVALID_OPTION,
  ERROR_NOT_SUPPORTED,
  ERROR_ATA_SMART_WOULD_WAKEUP,
  ERROR_FILESYSTEM_DRIVER_MISSING,
  ERROR_FILESYSTEM_TOOLS_MISSING
} Error;

#define ERROR (error_quark ())
GQuark error_quark (void);

G_END_DECLS

#endif /* __ERROR_H__ */
