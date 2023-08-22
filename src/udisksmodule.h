/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2020 Tomas Bzatek <tbzatek@redhat.com>
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

#ifndef __UDISKS_MODULE_H__
#define __UDISKS_MODULE_H__

#include <glib-object.h>
#include <gio/gio.h>

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_MODULE              (udisks_module_get_type ())
#define UDISKS_MODULE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), UDISKS_TYPE_MODULE, UDisksModule))
#define UDISKS_MODULE_CLASS(k)          (G_TYPE_CHECK_CLASS_CAST((k), UDISKS_TYPE_MODULE, UDisksModuleClass))
#define UDISKS_MODULE_GET_CLASS(o)      (G_TYPE_INSTANCE_GET_CLASS ((o), UDISKS_TYPE_MODULE, UDisksModuleClass))
#define UDISKS_IS_MODULE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), UDISKS_TYPE_MODULE))
#define UDISKS_IS_MODULE_CLASS(k)       (G_TYPE_CHECK_CLASS_TYPE ((k), UDISKS_TYPE_MODULE))

typedef struct _UDisksModule UDisksModule;
typedef struct _UDisksModuleClass UDisksModuleClass;

/**
 * UDisksModule:
 *
 * The #UDisksModule structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksModule
{
  /*< private >*/
  GObject parent_instance;

  UDisksDaemon *daemon;
  gchar        *name;
};

/**
 * UDisksModuleIDFunc:
 *
 * Function prototype that is called by #UDisksModuleManager to get
 * unique module identifier. No initialization is supposed to be done
 * at this point.
 *
 * Returns: (transfer full): The module ID string. Free with g_free().
 *
 * Since: 2.0
 */
typedef gchar *(*UDisksModuleIDFunc) (void);

/**
 * UDisksModuleNewFunc:
 * @daemon: A #UDisksDaemon instance.
 * @cancellable: (nullable): A #GCancellable or %NULL
 * @error: Return location for error or %NULL.
 *
 * Function prototype that creates a new #UDisksModule instance. Module
 * initialization is done at this point. This is a failable method call
 * that properly reports module initialization failure.
 *
 * Returns: (transfer full): A #UDisksModule object or %NULL if @error is set.
 *                           Free with g_object_unref().
 *
 * Since: 2.9
 */
typedef UDisksModule* (*UDisksModuleNewFunc) (UDisksDaemon  *daemon,
                                              GCancellable  *cancellable,
                                              GError       **error);
/**
 * UDisksModuleClass:
 * @parent_class: The parent class.
 * @new_manager: Virtual function for udisks_module_new_manager(). The default implementation returns %NULL.
 * @new_object: Virtual function for udisks_module_new_object(). The default implementation returns %NULL.
 * @track_parent: Virtual function for udisks_module_track_parent(). The default implementation returns %NULL.
 * @get_block_object_interface_types: Virtual function for udisks_module_get_block_object_interface_types(). The default implementation returns %NULL.
 * @get_drive_object_interface_types: Virtual function for udisks_module_get_drive_object_interface_types(). The default implementation returns %NULL.
 * @new_block_object_interface: Virtual function for udisks_module_new_block_object_interface(). The default implementation returns %NULL.
 * @new_drive_object_interface: Virtual function for udisks_module_new_drive_object_interface(). The default implementation returns %NULL.
 * @handle_uevent: Virtual function for udisks_module_handle_uevent(). The default implementation returns %NULL.
 *
 * Class structure for #UDisksModule.
 */
struct _UDisksModuleClass
{
  GObjectClass parent_class;

  GDBusInterfaceSkeleton  * (*new_manager)                      (UDisksModule           *module);
  GDBusObjectSkeleton    ** (*new_object)                       (UDisksModule           *module,
                                                                 UDisksLinuxDevice      *device);
  gchar                   * (*track_parent)                     (UDisksModule           *module,
                                                                 const gchar            *path,
                                                                 gchar                 **uuid);
  GType                   * (*get_block_object_interface_types) (UDisksModule           *module);
  GType                   * (*get_drive_object_interface_types) (UDisksModule           *module);
  GDBusInterfaceSkeleton  * (*new_block_object_interface)       (UDisksModule           *module,
                                                                 UDisksLinuxBlockObject *object,
                                                                 GType                   interface_type);
  GDBusInterfaceSkeleton  * (*new_drive_object_interface)       (UDisksModule           *module,
                                                                 UDisksLinuxDriveObject *object,
                                                                 GType                   interface_type);
  void                      (*handle_uevent)                    (UDisksModule           *module,
                                                                 UDisksLinuxDevice      *device);
};



GType                    udisks_module_get_type                         (void) G_GNUC_CONST;

const gchar             *udisks_module_get_name                         (UDisksModule           *module);

UDisksDaemon            *udisks_module_get_daemon                       (UDisksModule           *module);

GDBusInterfaceSkeleton  *udisks_module_new_manager                      (UDisksModule           *module);
GDBusObjectSkeleton    **udisks_module_new_object                       (UDisksModule           *module,
                                                                         UDisksLinuxDevice      *device);
gchar                   *udisks_module_track_parent                     (UDisksModule           *module,
                                                                         const gchar            *path,
                                                                         gchar                 **uuid);
GType                   *udisks_module_get_block_object_interface_types (UDisksModule           *module);
GType                   *udisks_module_get_drive_object_interface_types (UDisksModule           *module);
GDBusInterfaceSkeleton  *udisks_module_new_block_object_interface       (UDisksModule           *module,
                                                                         UDisksLinuxBlockObject *object,
                                                                         GType                   interface_type);
GDBusInterfaceSkeleton  *udisks_module_new_drive_object_interface       (UDisksModule           *module,
                                                                         UDisksLinuxDriveObject *object,
                                                                         GType                   interface_type);
void                     udisks_module_handle_uevent                    (UDisksModule           *module,
                                                                         UDisksLinuxDevice      *device);


G_END_DECLS

#endif /* __UDISKS_MODULE_H__ */
