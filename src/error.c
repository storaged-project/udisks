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

#include "config.h"

#include "error.h"

static const GDBusErrorEntry error_entries[] =
{
  {ERROR_FAILED, "org.freedesktop.UDisks.Error.Failed"},
  {ERROR_PERMISSION_DENIED, "org.freedesktop.UDisks.Error.PermissionDenied"},
  {ERROR_BUSY, "org.freedesktop.UDisks.Error.Busy"},
  {ERROR_CANCELLED, "org.freedesktop.UDisks.Error.Cancelled"},
  {ERROR_INHIBITED, "org.freedesktop.UDisks.Error.Inhibited"},
  {ERROR_INVALID_OPTION, "org.freedesktop.UDisks.Error.InvalidOption"},
  {ERROR_NOT_SUPPORTED, "org.freedesktop.UDisks.Error.NotSupported"},
  {ERROR_ATA_SMART_WOULD_WAKEUP, "org.freedesktop.UDisks.Error.AtaSmartWouldWakeup"},
  {ERROR_FILESYSTEM_DRIVER_MISSING, "org.freedesktop.UDisks.Error.FilesystemDriverMissing"},
  {ERROR_FILESYSTEM_TOOLS_MISSING, "org.freedesktop.UDisks.Error.FilesystemToolsMissing"}
};

GQuark
error_quark (void)
{
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("udisks-error-quark",
                                      &quark_volatile,
                                      error_entries,
                                      G_N_ELEMENTS (error_entries));
  G_STATIC_ASSERT (G_N_ELEMENTS (error_entries) - 1 == ERROR_FILESYSTEM_TOOLS_MISSING);
  return (GQuark) quark_volatile;
}


