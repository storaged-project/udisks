/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
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

#include "udisksiscsidbusutil.h"

/**
 * udisks_object_get_iscsi_session:
 * @object: A #UDisksObject.
 *
 * Gets the #UDisksISCSISession instance for the D-Bus interface <link linkend="gdbus-interface-org-freedesktop-UDisks2-ISCSI-Session.top_of_page">org.freedesktop.UDisks2.ISCSI.Session</link> on @object, if any.
 *
 * Returns: (transfer full): A #UDisksISCSISession that must be freed with g_object_unref() or %NULL if @object does not implement the interface.
 */
UDisksISCSISession *
udisks_object_get_iscsi_session (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.ISCSI.Session");
  if (ret == NULL)
    return NULL;
  return UDISKS_ISCSI_SESSION (ret);
}

/**
 * udisks_object_peek_iscsi_session: (skip)
 * @object: A #UDisksObject.
 *
 * Like udisks_object_get_iscsi_session() but doesn't increase the reference count on the returned object.
 *
 * <warning>It is not safe to use the returned object if you are on another thread than the one where the #GDBusObjectManagerClient or #GDBusObjectManagerServer for @object is running.</warning>
 *
 * Returns: (transfer none): A #UDisksISCSISession or %NULL if @object does not implement the interface. Do not free the returned object, it is owned by @object.
 */
UDisksISCSISession *
udisks_object_peek_iscsi_session (UDisksObject *object)
{
  GDBusInterface *ret;
  ret = g_dbus_object_get_interface (G_DBUS_OBJECT (object), "org.freedesktop.UDisks2.ISCSI.Session");
  if (ret == NULL)
    return NULL;
  g_object_unref (ret);
  return UDISKS_ISCSI_SESSION (ret);
}
