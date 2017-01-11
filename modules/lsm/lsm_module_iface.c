/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Gris Ge <fge@redhat.com>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gprintf.h>

#include <modules/udisksmoduleiface.h>

#include <udisks/udisks-generated.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdriveobject.h>
#include <src/udiskslinuxdevice.h>

#include "lsm_types.h"
#include "lsm_data.h"

#define _UDEV_ACTION_ADD        "add"
#define _UDEV_ACTION_REMOVE     "remove"
#define _UDEV_ACTION_CHANGE     "change"
#define _UDEV_ACTION_ONLINE     "online"
#define _UDEV_ACTION_OFFLINE    "offline"

static gboolean
_lsm_local_check (UDisksObject *object);

static void
_lsm_local_connect (UDisksObject *object);

static gboolean
_lsm_local_update(UDisksObject *object,
                         const gchar *uevent_action,
                         GDBusInterface *_iface);

gchar *
udisks_module_id (void)
{
  return g_strdup (LSM_MODULE_NAME);
}

gpointer
udisks_module_init (UDisksDaemon *daemon)
{
  udisks_debug ("LSM: udisks_module_init ()");
  std_lsm_data_init (daemon);
  return NULL;
}

void
udisks_module_teardown (UDisksDaemon *daemon)
{
  udisks_debug ("LSM: udisks_module_teardown ()");
  std_lsm_data_teardown ();
}

static gboolean
_drive_check (UDisksObject *object)
{
  gboolean is_managed = FALSE;
  UDisksLinuxDriveObject *std_lx_drv_obj = NULL;
  UDisksLinuxDevice *st_lx_dev = NULL;
  gboolean rc = FALSE;
  const gchar *wwn = NULL;

  udisks_debug ("LSM: _drive_check");
  std_lx_drv_obj = UDISKS_LINUX_DRIVE_OBJECT (object);

  st_lx_dev = udisks_linux_drive_object_get_device (std_lx_drv_obj, TRUE);
  if (st_lx_dev == NULL)
    {
      goto out;
    }

  if (g_udev_device_get_property_as_boolean (st_lx_dev->udev_device,
                                             "ID_CDROM"))
    goto out;

  wwn = g_udev_device_get_property (st_lx_dev->udev_device,
                                    "ID_WWN_WITH_EXTENSION");
  if ((!wwn) || (strlen (wwn) < 2))
    goto out;

  // Udev ID_WWN is started with 0x.
  is_managed = std_lsm_vpd83_is_managed (wwn + 2);

  if (is_managed == FALSE)
    {
      // Refresh and try again.
      std_lsm_vpd83_list_refresh ();
      is_managed = std_lsm_vpd83_is_managed (wwn + 2);
    }

  if (is_managed == FALSE)
    {
      udisks_debug ("LSM: VPD %s is not managed by LibstorageMgmt",
                    wwn + 2 );
      goto out;
    }
  else
    rc = TRUE;

out:
  if (st_lx_dev != NULL)
    g_object_unref (st_lx_dev);

  return rc;
}

static void
_drive_connect (UDisksObject *object)
{
}

static gboolean
_drive_update (UDisksObject   *object,
               const gchar    *uevent_action,
               GDBusInterface *_iface)
{
  udisks_debug ("LSM: _drive_update: got udevent_action %s", uevent_action);

  if (strcmp (uevent_action, _UDEV_ACTION_ADD) == 0)
    {
      return udisks_linux_drive_lsm_update
        (UDISKS_LINUX_DRIVE_LSM (_iface),
         UDISKS_LINUX_DRIVE_OBJECT (object));
    }
  else if (strcmp (uevent_action, _UDEV_ACTION_CHANGE) == 0)
    {
      /* Some LibStorageMgmt actions(like HPSA) might cause change uevent
       * we should ignore them to avoid check loop.
       */
      return FALSE;
    }
  else if (strcmp (uevent_action, _UDEV_ACTION_ONLINE) == 0)
    {
      // disk became online via sysfs, ignore
      return FALSE;
    }
  else if (strcmp (uevent_action, _UDEV_ACTION_OFFLINE) == 0)
    {
      // disk became offline via sysfs, ignore
      return FALSE;
    }
  else if (strcmp (uevent_action, _UDEV_ACTION_REMOVE) == 0)
    {
      if (UDISKS_IS_LINUX_DRIVE_LSM (_iface))
        g_object_unref (UDISKS_LINUX_DRIVE_LSM (_iface));
      return TRUE;
    }
  else
    {
      udisks_warning ("LSM: BUG: Got unknown udev action: %s, ignoring",
                      (const char *) uevent_action);
      return FALSE;
    }
}

static gboolean
_lsm_local_check (UDisksObject *object)
{
  /* The LsmLocalDisk interface is designated available on all disk drives as
   * there is no reliable way to determine whether LED control is properly
   * supported. Client code can only invoke the appropriate procedures for
   * controlling the lights and check for errors that may indicate failure.
   */

  return TRUE;
}

static void
_lsm_local_connect (UDisksObject *object)
{
}

static gboolean
_lsm_local_update(UDisksObject *object,
                  const gchar *uevent_action,
                  GDBusInterface *_iface)
{

  if (strcmp (uevent_action, _UDEV_ACTION_ADD) == 0)
    {
      return udisks_linux_drive_lsm_local_update
        (UDISKS_LINUX_DRIVE_LSM_LOCAL (_iface),
         UDISKS_LINUX_DRIVE_OBJECT (object));
    }
  else if (strcmp (uevent_action, _UDEV_ACTION_REMOVE) == 0)
    {
      if (UDISKS_IS_LINUX_DRIVE_LSM_LOCAL (_iface))
        g_object_unref
          (UDISKS_LINUX_DRIVE_LSM_LOCAL (_iface));
      return TRUE;
    }
  return FALSE;
}


UDisksModuleInterfaceInfo **
udisks_module_get_block_object_iface_setup_entries (void)
{
  return NULL;
}

UDisksModuleInterfaceInfo **
udisks_module_get_drive_object_iface_setup_entries (void)
{
  UDisksModuleInterfaceInfo **iface;

  iface = g_new0 (UDisksModuleInterfaceInfo *,
                  3 /* With trailing NULL as terminator */);
  iface[0] = g_new0 (UDisksModuleInterfaceInfo, 1);
  iface[0]->has_func = &_drive_check;
  iface[0]->connect_func = &_drive_connect;
  iface[0]->update_func = &_drive_update;
  iface[0]->skeleton_type = UDISKS_TYPE_LINUX_DRIVE_LSM;
  iface[1] = g_new0 (UDisksModuleInterfaceInfo, 1);
  iface[1]->has_func = &_lsm_local_check;
  iface[1]->connect_func = &_lsm_local_connect;
  iface[1]->update_func = &_lsm_local_update;
  iface[1]->skeleton_type = UDISKS_TYPE_LINUX_DRIVE_LSM_LOCAL;

  return iface;
}

UDisksModuleObjectNewFunc *
udisks_module_get_object_new_funcs (void)
{
  return NULL;
}

static GDBusInterfaceSkeleton *
_manager_iface_new (UDisksDaemon *daemon)
{
  UDisksLinuxManagerLSM *manager;

  manager = udisks_linux_manager_lsm_new ();

  return G_DBUS_INTERFACE_SKELETON (manager);
}

UDisksModuleNewManagerIfaceFunc *
udisks_module_get_new_manager_iface_funcs (void)
{
  UDisksModuleNewManagerIfaceFunc *funcs = NULL;
  funcs = g_new0 (UDisksModuleNewManagerIfaceFunc, 2);
  funcs[0] = &_manager_iface_new;

  return funcs;
}
