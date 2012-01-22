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

#include "udiskserror.h"

/**
 * SECTION:udiskserror
 * @title: UDisksError
 * @short_description: Possible errors that can be returned
 *
 * Error codes and D-Bus errors.
 */

static const GDBusErrorEntry dbus_error_entries[] =
{
  {UDISKS_ERROR_FAILED,                       "org.freedesktop.UDisks2.Error.Failed"},
  {UDISKS_ERROR_CANCELLED,                    "org.freedesktop.UDisks2.Error.Cancelled"},
  {UDISKS_ERROR_ALREADY_CANCELLED,            "org.freedesktop.UDisks2.Error.AlreadyCancelled"},
  {UDISKS_ERROR_NOT_AUTHORIZED,               "org.freedesktop.UDisks2.Error.NotAuthorized"},
  {UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN,    "org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain"},
  {UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED,     "org.freedesktop.UDisks2.Error.NotAuthorizedDismissed"},
  {UDISKS_ERROR_ALREADY_MOUNTED,              "org.freedesktop.UDisks2.Error.AlreadyMounted"},
  {UDISKS_ERROR_NOT_MOUNTED,                  "org.freedesktop.UDisks2.Error.NotMounted"},
  {UDISKS_ERROR_OPTION_NOT_PERMITTED,         "org.freedesktop.UDisks2.Error.OptionNotPermitted"},
  {UDISKS_ERROR_MOUNTED_BY_OTHER_USER,        "org.freedesktop.UDisks2.Error.MountedByOtherUser"},
  {UDISKS_ERROR_ALREADY_UNMOUNTING,           "org.freedesktop.UDisks2.Error.AlreadyUnmounting"},
  {UDISKS_ERROR_NOT_SUPPORTED,                "org.freedesktop.UDisks2.Error.NotSupported"},
  {UDISKS_ERROR_TIMED_OUT,                    "org.freedesktop.UDisks2.Error.Timedout"},
  {UDISKS_ERROR_WOULD_WAKEUP,                 "org.freedesktop.UDisks2.Error.WouldWakeup"},
  {UDISKS_ERROR_DEVICE_BUSY,                  "org.freedesktop.UDisks2.Error.DeviceBusy"},
};

GQuark
udisks_error_quark (void)
{
  G_STATIC_ASSERT (G_N_ELEMENTS (dbus_error_entries) == UDISKS_ERROR_NUM_ENTRIES);
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("udisks-error-quark",
                                      &quark_volatile,
                                      dbus_error_entries,
                                      G_N_ELEMENTS (dbus_error_entries));
  return (GQuark) quark_volatile;
}
