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
 * @action: uevent action, common values are <literal>add</literal>, <literal>change</literal> and <literal>remove</literal> or <literal>other</literal>
 * @device: A #UDisksLinuxDevice device object or %NULL if the device hasn't changed.
 * @keep: A return value whether to keep the object around or not.
 *
 * A #UDisksModuleObject method that is called by #UDisksLinuxBlockObject,
 * #UDisksLinuxDriveObject and #UDisksLinuxProvider to process a uevent on exported
 * module objects and interfaces and control their validity.
 *
 * Upon receiving a uevent the object implementing the #UDisksModuleObject interface
 * is responsible for processing updated information and indicate whether the @object
 * is still valid or not.
 *
 * This function may be called quite often and since uevent processing is currently
 * serialized by #UDisksLinuxProvider this method call should minimize its processing
 * time as much as possible.
 *
 * See related udisks_module_new_object(), udisks_module_new_block_object_interface()
 * and udisks_module_new_drive_object_interface() methods for information how uevent
 * routing is done and what effect the return values have.
 *
 * Set @keep to %FALSE if the object or interface should be unexported and removed,
 * %TRUE if the object or interface should be kept around. The return value of @keep
 * is ignored when the return value from this method is %FALSE. These return values
 * should align with the uevent @action, i.e. a @keep return value of %FALSE is
 * expected for a <literal>remove</literal> @action. Note that the <literal>remove</literal>
 * uevent is not always sent to block objects and the daemon may opt for direct
 * object destruction (for which the @object should be prepared to perform proper
 * cleanup within its destructor).
 *
 * Returns: %TRUE in case the uevent was processed, %FALSE when the @device is
 *          not applicable for the object or interface.
 *
 * Since: 2.0
 */
gboolean
udisks_module_object_process_uevent (UDisksModuleObject  *object,
                                     UDisksUeventAction   action,
                                     UDisksLinuxDevice   *device,
                                     gboolean            *keep)
{
  return UDISKS_MODULE_OBJECT_GET_IFACE (object)->process_uevent (object, action, device, keep);
}

/**
 * udisks_module_object_housekeeping:
 * @object: A #UDisksModuleObject.
 * @secs_since_last: Number of seconds since the last housekeeping or 0 if the first housekeeping ever.
 * @cancellable: A %GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * A #UDisksModuleObject method that is called periodically (every ten minutes or so)
 * by #UDisksLinuxProvider to perform module housekeeping tasks such as refreshing
 * <literal>ATA SMART</literal> data.
 *
 * The method runs in a dedicated thread and is allowed to perform
 * blocking I/O.
 *
 * Long-running tasks should periodically check @cancellable to see if
 * they have been cancelled.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 *
 * Since: 2.0
 */
gboolean
udisks_module_object_housekeeping (UDisksModuleObject  *object,
                                   guint                secs_since_last,
                                   GCancellable        *cancellable,
                                   GError             **error)
{
  return UDISKS_MODULE_OBJECT_GET_IFACE (object)->housekeeping (object, secs_since_last, cancellable, error);
}
