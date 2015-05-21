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

#ifndef __STORAGED_MODULE_IFACE_TYPES_H__
#define __STORAGED_MODULE_IFACE_TYPES_H__

#include <gio/gio.h>
#include <storaged/storaged.h>
#include <gudev/gudev.h>

#include <sys/types.h>

#include <src/storageddaemontypes.h>

G_BEGIN_DECLS


/**
 * StoragedModuleInterfaceInfo:
 * @has_func: A #StoragedObjectHasInterfaceFunc
 * @connect_func: A #StoragedObjectConnectInterfaceFunc
 * @update_func: A #StoragedObjectUpdateInterfaceFunc
 * @skeleton_type: A #GType of the instance that is created once @has_func succeeds.
 *
 * Structure containing interface setup functions used by modules for exporting
 * custom interfaces on existing block and drive objects.
 *
 * Event processing is solely done by #StoragedLinuxBlockObject and #StoragedLinuxDriveObject
 * themselves whose call the @has_func, @connect_func and @update_func respectively
 * as needed. Purpose of these member functions is to check whether the particular
 * #StoragedModuleInterfaceInfo record is applicable to the current device and
 * construct a new #GDBusInterface if so.
 *
 * See #StoragedObjectHasInterfaceFunc, #StoragedObjectConnectInterfaceFunc and
 * #StoragedObjectUpdateInterfaceFunc for detailed description and return values.
 */
struct _StoragedModuleInterfaceInfo
{
  StoragedObjectHasInterfaceFunc has_func;
  StoragedObjectConnectInterfaceFunc connect_func;
  StoragedObjectUpdateInterfaceFunc update_func;
  GType skeleton_type;
};

typedef struct _StoragedModuleInterfaceInfo StoragedModuleInterfaceInfo;

/**
 * StoragedModuleObjectNewFunc:
 * @daemon: A #StoragedDaemon instance.
 * @device: A #StoragedLinuxDevice device object.
 *
 * Function prototype that creates new #GDBusObjectSkeleton instance that
 * implements the #StoragedModuleObject interface.
 *
 * This is an another way of extending Storaged functionality. Objects in this
 * scope are meant to be of virtual kind and are pretty flexible - not
 * necessarily bound to any block device or perhaps representing a group of
 * resources. For illustration this kind of object may represent e.g. a RAID
 * array comprised of several block devices, all devices of the same kind such
 * as loop devices or any higher level representation of something else.
 *
 * This function may be called quite often, for nearly any uevent received.
 * It's done this way for broad flexibility and to give a chance to
 * #StoragedModuleObjectNewFunc functions to claim any device needed.
 *
 * Every #GDBusObjectSkeleton can claim one or more devices and Storaged
 * automatically manages uevent routing and instance lifecycle. A hierarchy of
 * claimed devices is maintained for the combination of particular module and
 * every #StoragedModuleObjectNewFunc - see below. This list lives in
 * #StoragedLinuxProvider and is strictly internal. Every module can provide
 * multiple #StoragedModuleObjectNewFunc functions for different kind of objects.
 *
 * Two scenarios are distinguished:
 *   1. The @device is already claimed by existing #GDBusObjectSkeleton
 *      instance for the current #StoragedModuleObjectNewFunc, it is guaranteed
 *      that only that instance will receive further uevents for the particular
 *      @device. This is done by calling the storaged_module_object_process_uevent()
 *      method of the #StoragedModuleObject interface. Depending on the returning
 *      value the device is either kept claimed by the object or removed. When
 *      last claimed device has been removed from the instance, it is
 *      automatically destroyed. In either case no further processing is done
 *      at this cycle to prevent creating new bogus instances for a @device
 *      that has just given up.
 *   2. In case the @device is not claimed by any existing #GDBusObjectSkeleton
 *      instance for the current #StoragedModuleObjectNewFunc. It now all depends
 *      on the return value of the function called. If it returns a new
 *      #GDBusObjectSkeleton instance, it also indicates to #StoragedLinuxProvider
 *      that it claims the device. The #StoragedLinuxProvider then registers the
 *      value with the current function and keeps track of it. If it returns
 *      %NULL, it inidcates that the current function is not interested in this
 *      kind of device.
 *
 * It's guaranteed that existing #GDBusObjectSkeleton instances will receive
 * uevents for devices they took and creating a new instance will take place
 * only if the event was not processed by any of them.
 *
 * Returns: A new #GDBusObjectSkeleton or %NULL when the module doesn't handle
 *          the device specified. Free with g_object_unref().
 */
typedef GDBusObjectSkeleton* (*StoragedModuleObjectNewFunc) (StoragedDaemon      *daemon,
                                                             StoragedLinuxDevice *device);

/**
 * StoragedModuleNewManagerIfaceFunc:
 * @daemon: A #StoragedDaemon instance.
 *
 * Function prototype that creates new #GDBusInterfaceSkeleton instance
 * carrying an additional D-Bus interface to be exported on the Storaged manager
 * object (on the "/org/storaged/Storaged/Manager" path). It is a fairly
 * simple stateless object not related to any device and serves the purpose of
 * performing general tasks or creating new resources.
 *
 * Returns: A new #GDBusInterfaceSkeleton. Free with g_object_unref().
 */
typedef GDBusInterfaceSkeleton* (*StoragedModuleNewManagerIfaceFunc) (StoragedDaemon *daemon);


/**
 * StoragedModuleIDFunc:
 *
 * This function is called by #StoragedModuleManager which stores the pointer
 * returned in a module state hashtable under the ID returned as the @module_id
 * argument.
 *
 * Returns: The module ID string.
 */
typedef gchar *(*StoragedModuleIDFunc) (void);

/**
 * StoragedModuleInitFunc:
 * @daemon: A #StoragedDaemon instance.
 *
 * Function prototype that is called upon module initialization. Its purpose is
 * to perform internal initialization and allocate memory that is then used e.g.
 * for saving state. See the storaged_module_manager_get_module_state_pointer()
 * method for how to work with state pointers.
 *
 * Note that since modules unloading is not supported, the memory returned gets
 * never freed.
 *
 * Corresponds with the storaged_module_init() module symbol.
 * Used internally by #StoragedModuleManager.
 *
 * Returns: Pointer to an opaque memory or %NULL when module doesn't need to save
 *          its state.
 */
typedef gpointer (*StoragedModuleInitFunc) (StoragedDaemon *daemon);

/**
 * StoragedModuleIfaceSetupFunc:
 *
 * Type declaration of a module setup entry function.
 *
 * Corresponds with the storaged_module_get_block_object_iface_setup_entries() and
 * storaged_module_get_drive_object_iface_setup_entries() module symbols.
 * Used internally by #StoragedModuleManager.
 *
 * Returns: An array of pointers to the #StoragedModuleInterfaceInfo structs. Free with g_free().
 */
typedef StoragedModuleInterfaceInfo ** (*StoragedModuleIfaceSetupFunc) (void);

/**
 * StoragedModuleObjectNewSetupFunc:
 *
 * Type declaration of a module setup entry function.
 *
 * Corresponds with the storaged_module_get_object_new_funcs() module symbol.
 * Used internally by #StoragedModuleManager.
 *
 * Returns: An array of pointers to the #StoragedModuleObjectNewFunc functions. Free with g_free().
 */
typedef StoragedModuleObjectNewFunc * (*StoragedModuleObjectNewSetupFunc) (void);

/**
 * StoragedModuleNewManagerIfaceSetupFunc:
 *
 * Type declaration of a module setup entry function.
 *
 * Corresponds with the storaged_module_get_new_manager_iface_funcs() module symbol.
 * Used internally by #StoragedModuleManager.
 *
 * Returns: An array of pointers to the #StoragedModuleNewManagerIfaceFunc functions. Free with g_free().
 */
typedef StoragedModuleNewManagerIfaceFunc * (*StoragedModuleNewManagerIfaceSetupFunc) (void);


G_END_DECLS

#endif /* __STORAGED_MODULE_IFACE_TYPES_H__ */
