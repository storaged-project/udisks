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

#include "config.h"

#include <blockdev/blockdev.h>
#include <blockdev/vdo.h>

#include <src/udisksdaemon.h>
#include <src/udiskslogging.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodulemanager.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>
#include <src/udiskslinuxblockobject.h>

#include "udiskslinuxmodulevdo.h"
#include "udiskslinuxmanagervdo.h"
#include "udiskslinuxblockvdo.h"

/**
 * SECTION:udiskslinuxmodulevdo
 * @title: UDisksLinuxModuleVDO
 * @short_description: VDO module.
 *
 * The VDO module.
 */

/**
 * UDisksLinuxModuleVDO:
 *
 * The #UDisksLinuxModuleVDO structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _UDisksLinuxModuleVDO {
  UDisksModule parent_instance;
};

typedef struct _UDisksLinuxModuleVDOClass UDisksLinuxModuleVDOClass;

struct _UDisksLinuxModuleVDOClass {
  UDisksModuleClass parent_class;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxModuleVDO, udisks_linux_module_vdo, UDISKS_TYPE_MODULE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));


static void
udisks_linux_module_vdo_init (UDisksLinuxModuleVDO *module)
{
}

static void
udisks_linux_module_vdo_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_module_vdo_parent_class)->constructed)
    G_OBJECT_CLASS (udisks_linux_module_vdo_parent_class)->constructed (object);
}

static void
udisks_linux_module_vdo_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_module_vdo_parent_class)->finalize)
    G_OBJECT_CLASS (udisks_linux_module_vdo_parent_class)->finalize (object);
}

gchar *
udisks_module_id (void)
{
  return g_strdup (VDO_MODULE_NAME);
}

/**
 * udisks_module_vdo_new:
 * @daemon: A #UDisksDaemon.
 * @cancellable: (nullable): A #GCancellable or %NULL
 * @error: Return location for error or %NULL.
 *
 * Creates new #UDisksLinuxModuleVDO object.
 *
 * Returns: (transfer full) (type UDisksLinuxModuleVDO): A
 *   #UDisksLinuxModuleVDO object or %NULL if @error is set. Free
 *   with g_object_unref().
 */
UDisksModule *
udisks_module_vdo_new (UDisksDaemon  *daemon,
                        GCancellable  *cancellable,
                        GError       **error)
{
  GInitable *initable;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  initable = g_initable_new (UDISKS_TYPE_LINUX_MODULE_VDO,
                             cancellable,
                             error,
                             "daemon", daemon,
                             "name", VDO_MODULE_NAME,
                             NULL);

  if (initable == NULL)
    return NULL;
  else
    return UDISKS_MODULE (initable);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  /* NULL means no specific so_name (implementation) */
  BDPluginSpec vdo_plugin = { BD_PLUGIN_VDO, NULL };
  BDPluginSpec *plugins[] = { &vdo_plugin, NULL };

  if (! bd_is_plugin_available (BD_PLUGIN_VDO))
    {
      if (! bd_reinit (plugins, FALSE, NULL, error))
        return FALSE;
    }

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = initable_init;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
udisks_linux_module_vdo_new_manager (UDisksModule *module)
{
  UDisksLinuxManagerVDO *manager;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_VDO (module), NULL);

  manager = udisks_linux_manager_vdo_new (UDISKS_LINUX_MODULE_VDO (module));

  return G_DBUS_INTERFACE_SKELETON (manager);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_module_vdo_check_block:
 * @module: A #UDisksLinuxModuleVDO.
 * @device: A #UDisksLinuxDevice.
 *
 * Checks whether the block device contains VDO signature.
 *
 * Returns: %TRUE when VDO signature is present, %FALSE otherwise.
 */
gboolean
udisks_linux_module_vdo_check_block (UDisksLinuxModuleVDO *module,
                                     UDisksLinuxDevice    *device)
{
  gboolean ret;
  const gchar *dm_uuid;
  const gchar *dm_name;
  BDVDOInfo *bd_info;
  GError *local_error = NULL;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_VDO (module), FALSE);
  g_return_val_if_fail (UDISKS_IS_LINUX_DEVICE (device), FALSE);

  /* Check for associated DM udev attributes. Unfortunately there are no VDO-specific
   * attributes exposed at the moment. */
  dm_uuid = g_udev_device_get_property (device->udev_device, "DM_UUID");
  dm_name = g_udev_device_get_property (device->udev_device, "DM_NAME");

  ret = FALSE;
  /* XXX: this is not guaranteed in any way */
  if (dm_uuid != NULL && dm_name != NULL && g_str_has_prefix (dm_uuid, "VDO-"))
    {
      /* Test if we can get VDO info */
      bd_info = bd_vdo_info (dm_name, &local_error);
      /* libblockdev always needs non-NULL error */
      g_clear_error (&local_error);
      ret = bd_info != NULL;
      if (bd_info != NULL)
        bd_vdo_info_free (bd_info);
    }

  return ret;
}

static GType *
udisks_linux_module_vdo_get_block_object_interface_types (UDisksModule *module)
{
  static GType block_object_interface_types[2];

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_VDO (module), NULL);

  if (g_once_init_enter (&block_object_interface_types[0]))
    g_once_init_leave (&block_object_interface_types[0], UDISKS_TYPE_LINUX_BLOCK_VDO);

  return block_object_interface_types;
}

static GDBusInterfaceSkeleton *
udisks_linux_module_vdo_new_block_object_interface (UDisksModule           *module,
                                                    UDisksLinuxBlockObject *object,
                                                    GType                   interface_type)
{
  GDBusInterfaceSkeleton *interface = NULL;
  UDisksLinuxDevice *device;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_VDO (module), NULL);

  if (interface_type == UDISKS_TYPE_LINUX_BLOCK_VDO)
    {
      device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));
      if (udisks_linux_module_vdo_check_block (UDISKS_LINUX_MODULE_VDO (module), device))
        {
          interface = G_DBUS_INTERFACE_SKELETON (udisks_linux_block_vdo_new (UDISKS_LINUX_MODULE_VDO (module), object));
        }
      g_object_unref (device);
    }
  else
    {
      udisks_error ("Invalid interface type");
    }

  return interface;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_module_vdo_class_init (UDisksLinuxModuleVDOClass *klass)
{
  GObjectClass *gobject_class;
  UDisksModuleClass *module_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = udisks_linux_module_vdo_constructed;
  gobject_class->finalize = udisks_linux_module_vdo_finalize;

  module_class = UDISKS_MODULE_CLASS (klass);
  module_class->new_manager = udisks_linux_module_vdo_new_manager;
  module_class->get_block_object_interface_types = udisks_linux_module_vdo_get_block_object_interface_types;
  module_class->new_block_object_interface = udisks_linux_module_vdo_new_block_object_interface;
}
