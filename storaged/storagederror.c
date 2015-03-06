/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 David Zeuthen <zeuthen@gmail.com>
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
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "storagederror.h"

/**
 * SECTION:storagederror
 * @title: StoragedError
 * @short_description: Possible errors that can be returned
 *
 * Error codes and D-Bus errors.
 */

static const GDBusErrorEntry dbus_error_entries[] =
{
  {STORAGED_ERROR_FAILED,                       "org.storaged.Storaged.Error.Failed"},
  {STORAGED_ERROR_CANCELLED,                    "org.storaged.Storaged.Error.Cancelled"},
  {STORAGED_ERROR_ALREADY_CANCELLED,            "org.storaged.Storaged.Error.AlreadyCancelled"},
  {STORAGED_ERROR_NOT_AUTHORIZED,               "org.storaged.Storaged.Error.NotAuthorized"},
  {STORAGED_ERROR_NOT_AUTHORIZED_CAN_OBTAIN,    "org.storaged.Storaged.Error.NotAuthorizedCanObtain"},
  {STORAGED_ERROR_NOT_AUTHORIZED_DISMISSED,     "org.storaged.Storaged.Error.NotAuthorizedDismissed"},
  {STORAGED_ERROR_ALREADY_MOUNTED,              "org.storaged.Storaged.Error.AlreadyMounted"},
  {STORAGED_ERROR_NOT_MOUNTED,                  "org.storaged.Storaged.Error.NotMounted"},
  {STORAGED_ERROR_OPTION_NOT_PERMITTED,         "org.storaged.Storaged.Error.OptionNotPermitted"},
  {STORAGED_ERROR_MOUNTED_BY_OTHER_USER,        "org.storaged.Storaged.Error.MountedByOtherUser"},
  {STORAGED_ERROR_ALREADY_UNMOUNTING,           "org.storaged.Storaged.Error.AlreadyUnmounting"},
  {STORAGED_ERROR_NOT_SUPPORTED,                "org.storaged.Storaged.Error.NotSupported"},
  {STORAGED_ERROR_TIMED_OUT,                    "org.storaged.Storaged.Error.Timedout"},
  {STORAGED_ERROR_WOULD_WAKEUP,                 "org.storaged.Storaged.Error.WouldWakeup"},
  {STORAGED_ERROR_DEVICE_BUSY,                  "org.storaged.Storaged.Error.DeviceBusy"},
};

GQuark
storaged_error_quark (void)
{
  G_STATIC_ASSERT (G_N_ELEMENTS (dbus_error_entries) == STORAGED_ERROR_NUM_ENTRIES);
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("storaged-error-quark",
                                      &quark_volatile,
                                      dbus_error_entries,
                                      G_N_ELEMENTS (dbus_error_entries));
  return (GQuark) quark_volatile;
}
