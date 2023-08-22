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

#include <config.h>
#include "udisksmodule.h"

#include "udisksdaemon.h"

/**
 * SECTION:UDisksModule
 * @title: UDisksModule
 * @short_description: Daemon module
 *
 * ## UDisks module design # {#udisks-module-design}
 *
 * #UDisksModule is a stateful object that represents a daemon module. It is supposed
 * to hold arbitrary runtime data and perform proper initialization and cleanup within
 * its constructor and destructor. Once initialized by #UDisksModuleManager the instance
 * is usually kept around until the daemon exits. Although proper module unloading
 * is not currently implemented the object destructor may be actually called in some
 * cases.
 *
 * Derived #UDisksModule object is supposed to implement failable initialization
 * and return proper error that the #UDisksModuleManager would propagate further
 * up the stack. Modules are free to use failable initialization for checking runtime
 * dependencies such as additional config files and fail if misconfigured.
 *
 * ## UDisks module naming conventions # {#udisks-module-naming}
 *
 * Every module must implement and export two symbols that are used as entry points: <link linkend="UDisksModuleIDFunc"><function>udisks_module_id()</function></link>
 * and <link linkend="UDisksModuleNewFunc"><function>udisks_module_ID_new()</function></link>
 * where <literal>ID</literal> is a string returned by <link linkend="UDisksModuleIDFunc"><function>udisks_module_id()</function></link>.
 * This identification string is subsequently used at several places - primarily
 * serves as an unique and user readable module identifier (e.g. <literal>lvm2</literal>)
 * passed in as an argument to the <link linkend="gdbus-method-org-freedesktop-UDisks2-Manager.EnableModule">org.freedesktop.UDisks2.Manager.EnableModule()</link>
 * method call.
 *
 * Physically modules are essentially regular shared objects (<literal>.so</literal>)
 * that are loaded from <filename>$(libdir)/udisks2/modules</filename> directory
 * (typically <filename>/usr/lib/udisks2/modules</filename>). No extra service or
 * config files are needed, however a specific file naming of <filename>libudisks2_<emphasis>ID</emphasis>.so</filename>
 * is required.
 *
 * ## Module API # {#udisks-modular-api}
 *
 * Other than the two entry points described in last paragraph the rest of the daemon
 * to module interaction is done via #UDisksModule class methods over an instance
 * created by the <link linkend="UDisksModuleNewFunc"><function>udisks_module_ID_new()</function></link>
 * constructor. Please see particular #UDisksModule methods for detailed description
 * of each way of extending the daemon functionality. Most methods are pretty
 * straightforward with the exception of extra drive and block object interfaces.
 *
 * It's important to provide udisks_module_get_block_object_interface_types() and
 * udisks_module_new_block_object_interface() methods (or <literal>drive</literal>
 * respectively) always in pairs as the #UDisksLinuxBlockObject and #UDisksLinuxDriveObject
 * machinery needs to register available interface skeleton types first and subsequently
 * create target interfaces for each specified type and route uevents onto. There
 * can be only one extra interface of a given type on a single #UDisksLinuxBlockObject
 * or #UDisksLinuxDriveObject object.
 *
 * In case of an existing interface for a particular type uevents are routed through
 * the udisks_module_object_process_uevent() method of a #UDisksModuleObject interface
 * that the newly created #GDBusInterfaceSkeleton interface has to implement. This
 * call is supposed to process updated information and indicate via the return @keep
 * argument whether the particular interface is valid or should be removed from
 * the object.
 *
 * In case no #GDBusInterfaceSkeleton interface of a given type is attached on the
 * particular object, udisks_module_new_block_object_interface() or
 * udisks_module_new_drive_object_interface() methods respectively are called
 * in attempt to create new one. These methods are supposed to check whether the
 * interface type is applicable for the current object and return %NULL if not.
 *
 * Exposing independent module objects on the master UDisks object manager as another
 * way of daemon extensibility works in a similar way - please see udisks_module_new_object()
 * for detailed description.
 */

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_NAME,
};

G_DEFINE_TYPE (UDisksModule, udisks_module, G_TYPE_OBJECT)


static void
udisks_module_finalize (GObject *object)
{
  UDisksModule *module = UDISKS_MODULE (object);

  g_free (module->name);

  if (G_OBJECT_CLASS (udisks_module_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_module_parent_class)->finalize (object);
}

static void
udisks_module_init (UDisksModule *module)
{
}

static void
udisks_module_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_module_parent_class)->constructed != NULL)
    (*G_OBJECT_CLASS (udisks_module_parent_class)->constructed) (object);
}

static void
udisks_module_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  UDisksModule *module = UDISKS_MODULE (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_module_get_daemon (module));
      break;

    case PROP_NAME:
      g_value_set_string (value, udisks_module_get_name (module));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_module_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  UDisksModule *module = UDISKS_MODULE (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (module->daemon == NULL);
      /* We don't take a reference to the daemon */
      module->daemon = g_value_get_object (value);
      break;

    case PROP_NAME:
      g_assert (module->name == NULL);
      module->name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton  *
udisks_module_new_manager_default (UDisksModule *module)
{
  return NULL;
}

static GDBusObjectSkeleton **
udisks_module_new_object_default (UDisksModule      *module,
                                  UDisksLinuxDevice *device)
{
  return NULL;
}

static gchar *
udisks_module_track_parent_default (UDisksModule  *module,
                                    const gchar   *path,
                                    gchar        **uuid)
{
  return NULL;
}

static GType *
udisks_module_get_block_object_interface_types_default (UDisksModule *module)
{
  return NULL;
}

static GType *
udisks_module_get_drive_object_interface_types_default (UDisksModule *module)
{
  return NULL;
}

static GDBusInterfaceSkeleton *
udisks_module_new_block_object_interface_default (UDisksModule           *module,
                                                  UDisksLinuxBlockObject *object,
                                                  GType                   interface_type)
{
  return NULL;
}

static GDBusInterfaceSkeleton *
udisks_module_new_drive_object_interface_default (UDisksModule           *module,
                                                  UDisksLinuxDriveObject *object,
                                                  GType                   interface_type)
{
  return NULL;
}

static void
udisks_module_handle_uevent_default (UDisksModule      *module,
                                     UDisksLinuxDevice *device)
{
  return;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_module_class_init (UDisksModuleClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize     = udisks_module_finalize;
  gobject_class->constructed  = udisks_module_constructed;
  gobject_class->get_property = udisks_module_get_property;
  gobject_class->set_property = udisks_module_set_property;

  klass->new_manager                      = udisks_module_new_manager_default;
  klass->new_object                       = udisks_module_new_object_default;
  klass->track_parent                     = udisks_module_track_parent_default;
  klass->get_block_object_interface_types = udisks_module_get_block_object_interface_types_default;
  klass->get_drive_object_interface_types = udisks_module_get_drive_object_interface_types_default;
  klass->new_block_object_interface       = udisks_module_new_block_object_interface_default;
  klass->new_drive_object_interface       = udisks_module_new_drive_object_interface_default;
  klass->handle_uevent                    = udisks_module_handle_uevent_default;

  /**
   * UDisksModule:daemon:
   *
   * The #UDisksDaemon for the object.
   *
   * Since: 2.9.0
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksModule:name:
   *
   * Name of the module.
   *
   * Since: 2.9.0
   */
  g_object_class_install_property (gobject_class,
                                   PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "Name of the module",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * udisks_module_get_name:
 * @module: A #UDisksModule.
 *
 * Gets the name of the @module.
 *
 * Returns: (transfer none): A module name string. Do not free, the string is owned by @module.
 *
 * Since: 2.9.0
 */
const gchar *
udisks_module_get_name (UDisksModule *module)
{
  g_return_val_if_fail (UDISKS_IS_MODULE (module), NULL);
  return module->name;
}

/**
 * udisks_module_get_daemon:
 * @module: A #UDisksModule.
 *
 * Gets the daemon used by @module.
 *
 * Returns: (transfer none): A #UDisksDaemon. Do not free, the object is owned by @module.
 *
 * Since: 2.9.0
 */
UDisksDaemon *
udisks_module_get_daemon (UDisksModule *module)
{
  g_return_val_if_fail (UDISKS_IS_MODULE (module), NULL);
  return module->daemon;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_module_new_manager:
 * @module: A #UDisksModule.
 *
 * Creates a new #GDBusInterfaceSkeleton instance carrying an additional D-Bus interface
 * to be exported on the #UDisksManager object (at the <filename>/org/freedesktop/UDisks2/Manager</filename>
 * path). It is a fairly simple stateless object not related to any device and serves
 * the purpose of performing general tasks or creating new resources. Only a single
 * manager interface can be provided by each module.
 *
 * Returns: (transfer full) (nullable): A new #GDBusInterfaceSkeleton. Free with g_object_unref().
 *
 * Since: 2.9.0
 */
GDBusInterfaceSkeleton *
udisks_module_new_manager (UDisksModule *module)
{
  g_return_val_if_fail (UDISKS_IS_MODULE (module), NULL);

  return UDISKS_MODULE_GET_CLASS (module)->new_manager (module);
}

/**
 * udisks_module_new_object:
 * @module: A #UDisksModule.
 * @device: A #UDisksLinuxDevice device object.
 *
 * Creates one or more #GDBusObjectSkeleton objects that implement the #UDisksModuleObject
 * interface. Multiple objects may be returned by this method call, e.g. in case
 * more than one object type is needed in order to represent a particular feature.
 *
 * Objects are exported by #UDisksLinuxProvider on the master object manager under
 * the <filename>/org/freedesktop/UDisks2</filename> path just like regular block
 * and drive objects. This allows to create brand new object types fully handled
 * by modules and providing custom interfaces. Objects in this scope are meant to be
 * of virtual kind and are pretty flexible in this regard - not necessarily bound
 * to any specific block device or drive. Perhaps even representing a group of resources.
 * For illustration this kind of object may represent a RAID array comprised of several
 * block devices, devices of the same kind such as loop devices or any higher level
 * representation of something else.
 *
 * Note that it's not currently possible to share module objects across multiple
 * modules with the intention to attach extra interfaces on a foreign module object.
 * In such case each module needs to export its own unique object, no matter if
 * they share or represent similar kind of resource.
 *
 * This method may be called quite often, for nearly any uevent received. It's done
 * this way for broad flexibility and to give module objects a chance to claim any
 * device needed.
 *
 * Module objects are supposed to maintain internal list of claimed devices and track
 * their validity, i.e. indicate removal only after all tracked devices are gone.
 * Every module object may claim one or more devices. #UDisksLinuxProvider essentially
 * provides uevent routing and guarantees that existing objects are asked first to
 * consider a claim of the @device before new object is attempted to be created.
 * This works always in the scope of a particular module, i.e. existing module objects
 * and their claims are always considered separately for each module.
 *
 * The uevent routing works as follows:
 *   1. Existing module objects are asked first to process the uevent for a particular
 *      @device via the udisks_module_object_process_uevent() method on the
 *      #UDisksModuleObject interface. The method return value and the @keep argument
 *      control the claim:
 *        * method return value of %FALSE means the object doesn't currently hold
 *          the claim of the @device and is not interested of making new one. The
 *          return value of @keep is ignored in this case.
 *        * method return value of %TRUE and the @keep return value of %FALSE indicates
 *          the object is not valid anymore and should be unexported from the object
 *          manager.
 *        * method return value of %TRUE and the @keep return value of %TRUE indicates
 *          the object has processed the updated information and remains valid.
 *
 *   2. In case the @device has not been claimed by any existing module object, meaning
 *      all the udisks_module_object_process_uevent() method calls from previous step
 *      returned %FALSE, only then a new object is attempted to be created via this
 *      udisks_module_new_object() method call. If there was a claim release in
 *      the previous step, no attempt to create new object is made to prevent creating
 *      bogus objects for recently released devices.
 *
 * Returns: (element-type GDBusObjectSkeleton) (array zero-terminated=1) (nullable) (transfer full):
 *          NULL-terminated array of new #GDBusObjectSkeleton objects or %NULL when
 *          the module is not interested in the @device.
 *
 * Since: 2.9.0
 */
GDBusObjectSkeleton **
udisks_module_new_object (UDisksModule      *module,
                          UDisksLinuxDevice *device)
{
  g_return_val_if_fail (UDISKS_IS_MODULE (module), NULL);

  return UDISKS_MODULE_GET_CLASS (module)->new_object (module, device);
}

/**
 * udisks_module_track_parent:
 * @module: A #UDisksModule.
 * @path: object path of a child to find parent of
 * @uuid: a pointer to return parent UUID string
 *
 * Finds a parent block device and returns its object path and UUID.
 * If the return value is %NULL, the value of @uuid has not been changed.
 * Related to udisks_daemon_get_parent_for_tracking().
 *
 * Returns: (transfer full) (nullable): object path of the parent device. Free with g_free().
 *
 * Since: 2.9.0
 */
gchar *
udisks_module_track_parent (UDisksModule  *module,
                            const gchar   *path,
                            gchar        **uuid)
{
  g_return_val_if_fail (UDISKS_IS_MODULE (module), NULL);

  return UDISKS_MODULE_GET_CLASS (module)->track_parent (module, path, uuid);
}

/**
 * udisks_module_get_block_object_interface_types:
 * @module: A #UDisksModule.
 *
 * Gets an array of interface skeleton #GType types the module provides as additional
 * interfaces for the #UDisksLinuxBlockObject. This list is subsequently used by
 * #UDisksLinuxBlockObject to track available interfaces and to create new ones via
 * udisks_module_new_block_object_interface().
 *
 * Returns: (element-type GType) (array zero-terminated=1) (nullable) (transfer none):
 *          A NULL-terminated array of #GType types or %NULL when the module doesn't
 *          handle block object interfaces. Do not free, the data belongs to the module.
 *
 * Since: 2.9.0
 */
GType *
udisks_module_get_block_object_interface_types (UDisksModule *module)
{
  g_return_val_if_fail (UDISKS_IS_MODULE (module), NULL);

  return UDISKS_MODULE_GET_CLASS (module)->get_block_object_interface_types (module);
}

/**
 * udisks_module_get_drive_object_interface_types:
 * @module: A #UDisksModule.
 *
 * Gets an array of interface skeleton #GType types the module provides as additional
 * interfaces for the #UDisksLinuxDriveObject. This list is subsequently used by
 * #UDisksLinuxDriveObject to track available interfaces and to create new ones via
 * udisks_module_new_drive_object_interface().
 *
 * Returns: (element-type GType) (array zero-terminated=1) (nullable) (transfer none):
 *          A NULL-terminated array of #GType types or %NULL when the module doesn't
 *          handle drive object interfaces. Do not free, the data belongs to the module.
 *
 * Since: 2.9.0
 */
GType *
udisks_module_get_drive_object_interface_types (UDisksModule *module)
{
  g_return_val_if_fail (UDISKS_IS_MODULE (module), NULL);

  return UDISKS_MODULE_GET_CLASS (module)->get_drive_object_interface_types (module);
}

/**
 * udisks_module_new_block_object_interface:
 * @module: A #UDisksModule.
 * @object: A #UDisksLinuxBlockObject.
 * @interface_type: A #GType of the desired new interface skeleton.
 *
 * Tries to create a new #GDBusInterfaceSkeleton instance of type @interface_type
 * that is supposed to be attached on the block @object. This method call is also
 * supposed to check whether the desired @interface_type is applicable for
 * the current @object and return %NULL if it's not. The returned instance must
 * implement the #UDisksModuleObject interface with the udisks_module_object_process_uevent()
 * method that is used to process uevents and controls whether the interface should
 * be removed or not.
 *
 * <note>Note that it is important not to take reference to @object to avoid circular
 * references. The returned #GDBusInterfaceSkeleton will be exported on the @object
 * and unexported when no longer valid (typically as a result of a <emphasis>remove</emphasis>
 * uevent). The returned object is responsible to perform cleanup in its destructor
 * as it's not generally guaranteed the <emphasis>remove</emphasis> uevent will be
 * sent prior to that.</note>
 *
 * Returns: (transfer full) (nullable): A new #GDBusInterfaceSkeleton instance or
 *           %NULL when not applicable for the @object. Free with g_object_unref().
 *
 * Since: 2.9.0
 */
GDBusInterfaceSkeleton *
udisks_module_new_block_object_interface (UDisksModule           *module,
                                          UDisksLinuxBlockObject *object,
                                          GType                   interface_type)
{
  g_return_val_if_fail (UDISKS_IS_MODULE (module), NULL);

  return UDISKS_MODULE_GET_CLASS (module)->new_block_object_interface (module, object, interface_type);
}

/**
 * udisks_module_new_drive_object_interface:
 * @module: A #UDisksModule.
 * @object: A #UDisksLinuxDriveObject.
 * @interface_type: A #GType of the desired new interface skeleton.
 *
 * Tries to create a new #GDBusInterfaceSkeleton instance of type @interface_type
 * that is supposed to be attached on the drive @object. This method call is also
 * supposed to check whether the desired @interface_type is applicable for
 * the current @object and return %NULL if it's not. The returned instance must
 * implement the #UDisksModuleObject interface with the udisks_module_object_process_uevent()
 * method that is used to process uevents and controls whether the interface should
 * be removed or not.
 *
 * <note>Note that it is important not to take reference to @object to avoid circular
 * references. The returned #GDBusInterfaceSkeleton will be exported on the @object
 * and unexported when no longer valid (typically as a result of a <emphasis>remove</emphasis>
 * uevent). The returned object is responsible to perform cleanup in its destructor
 * as it's not generally guaranteed the <emphasis>remove</emphasis> uevent will be
 * sent prior to that.</note>
 *
 * Returns: (transfer full) (nullable): A new #GDBusInterfaceSkeleton instance or
 *           %NULL when not applicable for the @object. Free with g_object_unref().
 *
 * Since: 2.9.0
 */
GDBusInterfaceSkeleton *
udisks_module_new_drive_object_interface (UDisksModule           *module,
                                          UDisksLinuxDriveObject *object,
                                          GType                   interface_type)
{
  g_return_val_if_fail (UDISKS_IS_MODULE (module), NULL);

  return UDISKS_MODULE_GET_CLASS (module)->new_drive_object_interface (module, object, interface_type);
}

/**
 * udisks_module_handle_uevent:
 * @module: A #UDisksModule.
 * @device: A #UDisksLinuxDevice device object.
 *
 * This is a generic uevent processing handler for special cases where
 * none of the regular interface methods really fit the needs of the module.
 * Every single uevent is routed this way and care must be taken
 * to minimize the processing time as it's run sync in the main thread.
 *
 * Added for the LVM2 module as a temporary workaround. Subject
 * to removal in the future.
 *
 * Since: 2.11.0
 */
void
udisks_module_handle_uevent (UDisksModule      *module,
                             UDisksLinuxDevice *device)
{
  g_return_if_fail (UDISKS_IS_MODULE (module));

  UDISKS_MODULE_GET_CLASS (module)->handle_uevent (module, device);
}
