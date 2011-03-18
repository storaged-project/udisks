/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#if !defined (__UDISKS_INSIDE_UDISKS_H__) && !defined (UDISKS_COMPILATION)
#error "Only <udisks/udisks.h> can be included directly."
#endif

#ifndef __UDISKS_ENUMS_H__
#define __UDISKS_ENUMS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * UDisksError:
 * @UDISKS_ERROR_FAILED: The operation failed.
 * @UDISKS_ERROR_CANCELLED: The operation was cancelled.
 * @UDISKS_ERROR_ALREADY_CANCELLED: The operation has already been cancelled.
 * @UDISKS_ERROR_NOT_AUTHORIZED: Not authorized to perform the requested operation.
 * @UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN: Like %UDISKS_ERROR_NOT_AUTHORIZED but authorization can be obtained through e.g. authentication.
 * @UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED: Like %UDISKS_ERROR_NOT_AUTHORIZED but an authentication was shown and the user dimissed it.
 * @UDISKS_ERROR_ALREADY_MOUNTED: The device is already mounted.
 * @UDISKS_ERROR_NOT_MOUNTED: The device is not mounted.
 * @UDISKS_ERROR_OPTION_NOT_PERMITTED: Not permitted to use the requested option.
 * @UDISKS_ERROR_MOUNTED_BY_OTHER_USER: The device is mounted by another user.
 * @UDISKS_ERROR_ALREADY_UNMOUNTING: The device is already unmounting.
 * @UDISKS_ERROR_NOT_SUPPORTED: The operation is not supported due to missing driver/tool support.
 *
 * Error codes for the #UDISKS_ERROR error domain and the
 * corresponding D-Bus error names.
 */
typedef enum
{
  UDISKS_ERROR_FAILED,                     /* org.freedesktop.UDisks.Error.Failed */
  UDISKS_ERROR_CANCELLED,                  /* org.freedesktop.UDisks.Error.Cancelled */
  UDISKS_ERROR_ALREADY_CANCELLED,          /* org.freedesktop.UDisks.Error.AlreadyCancelled */
  UDISKS_ERROR_NOT_AUTHORIZED,             /* org.freedesktop.UDisks.Error.NotAuthorized */
  UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN,  /* org.freedesktop.UDisks.Error.NotAuthorizedCanObtain */
  UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED,   /* org.freedesktop.UDisks.Error.NotAuthorizedDismissed */
  UDISKS_ERROR_ALREADY_MOUNTED,            /* org.freedesktop.UDisks.Error.AlreadyMounted */
  UDISKS_ERROR_NOT_MOUNTED,                /* org.freedesktop.UDisks.Error.NotMounted */
  UDISKS_ERROR_OPTION_NOT_PERMITTED,       /* org.freedesktop.UDisks.Error.OptionNotPermitted */
  UDISKS_ERROR_MOUNTED_BY_OTHER_USER,      /* org.freedesktop.UDisks.Error.MountedByOtherUser */
  UDISKS_ERROR_ALREADY_UNMOUNTING,         /* org.freedesktop.UDisks.Error.AlreadyUnmounting */
  UDISKS_ERROR_NOT_SUPPORTED               /* org.freedesktop.UDisks.Error.NotSupported */
} UDisksError;

#define UDISKS_ERROR_NUM_ENTRIES  (UDISKS_ERROR_NOT_SUPPORTED + 1)

G_END_DECLS

#endif /* __UDISKS_ENUMS_H__ */
