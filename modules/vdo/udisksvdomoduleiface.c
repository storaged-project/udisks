/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2018 Tomas Bzatek <tbzatek@redhat.com>
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
 */

#include "config.h"

#include <blockdev/blockdev.h>
#include <blockdev/vdo.h>

#include <modules/udisksmoduleiface.h>

#include <udisks/udisks-generated.h>
#include <src/udisksdaemon.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslogging.h>
#include <src/udisksmodulemanager.h>

#include "udisksvdotypes.h"

#include "udiskslinuxblockvdo.h"
#include "udiskslinuxmanagervdo.h"

/* ---------------------------------------------------------------------------------------------------- */

gchar *
udisks_module_id (void)
{
  return g_strdup (VDO_MODULE_NAME);
}

gpointer
udisks_module_init (UDisksDaemon *daemon)
{
  gboolean ret;
  GError *error = NULL;

  /* NULL means no specific so_name (implementation) */
  BDPluginSpec vdo_plugin = {BD_PLUGIN_VDO, NULL};
  BDPluginSpec *plugins[] = {&vdo_plugin, NULL};

  if (!bd_is_plugin_available (BD_PLUGIN_VDO))
    {
      ret = bd_reinit (plugins, FALSE, NULL, &error);
      if (!ret)
        {
          udisks_error ("Error initializing the vdo libblockdev plugin: %s (%s, %d)",
                        error->message, g_quark_to_string (error->domain), error->code);
          g_clear_error (&error);
          /* XXX: can do nothing more here even though we know the module will be unusable! */
        }
    }

  /* No need for extra module state struct */
  return NULL;
}

void
udisks_module_teardown (UDisksDaemon *daemon)
{
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
check_want_vdo_block (UDisksObject *object)
{
  UDisksLinuxDevice *device;
  gboolean ret;
  const gchar *dm_uuid;
  const gchar *dm_name;
  BDVDOInfo *bd_info;

  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK_OBJECT (object), FALSE);

  /* Check for associated DM udev attributes. Unfortunately there are no VDO-specific
   * attributes exposed at the moment. */
  device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));
  dm_uuid = g_udev_device_get_property (device->udev_device, "DM_UUID");
  dm_name = g_udev_device_get_property (device->udev_device, "DM_NAME");

  ret = FALSE;
  /* XXX: this is not guaranteed in any way */
  if (dm_uuid != NULL && dm_name != NULL && g_str_has_prefix (dm_uuid, "VDO-"))
    {
      /* Test if we can get VDO info */
      bd_info = bd_vdo_info (dm_name, NULL);
      ret = bd_info != NULL;
      if (bd_info != NULL)
        bd_vdo_info_free (bd_info);
    }

  g_object_unref (device);

  return ret;
}

static void
vdo_block_connect (UDisksObject *object)
{
}

static gboolean
vdo_block_update (UDisksObject   *object,
                  const gchar    *uevent_action,
                  GDBusInterface *_iface)
{
  return udisks_linux_block_vdo_update (UDISKS_LINUX_BLOCK_VDO (_iface),
                                        UDISKS_LINUX_BLOCK_OBJECT (object));
}

UDisksModuleInterfaceInfo **
udisks_module_get_block_object_iface_setup_entries (void)
{
  UDisksModuleInterfaceInfo **iface;

  iface = g_new0 (UDisksModuleInterfaceInfo *, 2);
  iface[0] = g_new0 (UDisksModuleInterfaceInfo, 1);
  iface[0]->has_func = &check_want_vdo_block;
  iface[0]->connect_func = &vdo_block_connect;
  iface[0]->update_func = &vdo_block_update;
  iface[0]->skeleton_type = UDISKS_TYPE_LINUX_BLOCK_VDO;

  return iface;
}

/* ---------------------------------------------------------------------------------------------------- */

UDisksModuleInterfaceInfo **
udisks_module_get_drive_object_iface_setup_entries (void)
{
  return NULL;
}

UDisksModuleObjectNewFunc *
udisks_module_get_object_new_funcs (void)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
new_manager_vdo_manager_iface (UDisksDaemon *daemon)
{
  UDisksLinuxManagerVDO *manager;

  manager = udisks_linux_manager_vdo_new (daemon);

  return G_DBUS_INTERFACE_SKELETON (manager);
}

UDisksModuleNewManagerIfaceFunc *
udisks_module_get_new_manager_iface_funcs (void)
{
  UDisksModuleNewManagerIfaceFunc *funcs;

  funcs = g_new0 (UDisksModuleNewManagerIfaceFunc, 2);
  funcs[0] = &new_manager_vdo_manager_iface;

  return funcs;
}
