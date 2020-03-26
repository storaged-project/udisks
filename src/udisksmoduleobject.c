/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014 Tomas Bzatek <tbzatek@redhat.com>
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

#include <config.h>
#include "udisksmoduleobject.h"


typedef UDisksModuleObjectIface UDisksModuleObjectInterface;
G_DEFINE_INTERFACE (UDisksModuleObject, udisks_module_object, G_TYPE_OBJECT);

static void
udisks_module_object_default_init (UDisksModuleObjectIface *iface)
{
}

/**
 * udisks_module_object_process_uevent:
 * @object: A #UDisksModuleObject.
 * @action: Uevent action, common values are "add", "changed" and "removed" or %NULL
 * @device: A #UDisksLinuxDevice device object or %NULL if the device hasn't changed.
 *
 * Virtual method that processes the uevent and updates all information on interfaces
 * on @object.
 *
 * See #UDisksModuleObjectNewFunc for detailed information on how to work with
 * the events.
 *
 * Returns: %FALSE if the object should be unexported and removed, %TRUE if the object
 *          has processed the information successfully and should be kept around.
 */
gboolean
udisks_module_object_process_uevent (UDisksModuleObject  *object,
                                     const gchar         *action,
                                     UDisksLinuxDevice   *device)
{
  return UDISKS_MODULE_OBJECT_GET_IFACE (object)->process_uevent (object, action, device);
}

/**
 * udisks_module_object_housekeeping:
 * @object: A #UDisksModuleObject.
 * @secs_since_last: Number of seconds since the last housekeeping or 0 if the first housekeeping ever.
 * @cancellable: A %GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Virtual method that is called periodically (every ten minutes or so)
 * to perform housekeeping tasks such as refreshing ATA SMART data.
 *
 * The function runs in a dedicated thread and is allowed to perform
 * blocking I/O.
 *
 * Long-running tasks should periodically check @cancellable to see if
 * they have been cancelled.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
udisks_module_object_housekeeping (UDisksModuleObject  *object,
                                   guint                secs_since_last,
                                   GCancellable        *cancellable,
                                   GError             **error)
{
  return UDISKS_MODULE_OBJECT_GET_IFACE (object)->housekeeping (object, secs_since_last, cancellable, error);
}
