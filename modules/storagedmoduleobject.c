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
#include "storagedmoduleobject.h"


typedef StoragedModuleObjectIface StoragedModuleObjectInterface;
G_DEFINE_INTERFACE (StoragedModuleObject, storaged_module_object, G_TYPE_OBJECT);

static void
storaged_module_object_default_init (StoragedModuleObjectIface *iface)
{
}

/**
 * storaged_module_object_process_uevent:
 * @object: A #StoragedModuleObject.
 * @action: Uevent action, common values are "add", "changed" and "removed" or %NULL
 * @device: A #StoragedLinuxDevice device object or %NULL if the device hasn't changed.
 *
 * Virtual method that processes the uevent and updates all information on interfaces
 * on @object.
 *
 * See #StoragedModuleObjectNewFunc for detailed information on how to work with
 * the events.
 *
 * Returns: %FALSE if the object should be unexported and removed, %TRUE if the object
 *          has processed the information successfully and should be kept around.
 */
gboolean
storaged_module_object_process_uevent (StoragedModuleObject  *object,
                                       const gchar           *action,
                                       StoragedLinuxDevice   *device)
{
  return STORAGED_MODULE_OBJECT_GET_IFACE (object)->process_uevent (object, action, device);
}

/**
 * storaged_module_object_housekeeping:
 * @object: A #StoragedModuleObject.
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
storaged_module_object_housekeeping (StoragedModuleObject  *object,
                                     guint                  secs_since_last,
                                     GCancellable          *cancellable,
                                     GError               **error)
{
  return STORAGED_MODULE_OBJECT_GET_IFACE (object)->housekeeping (object, secs_since_last, cancellable, error);
}
