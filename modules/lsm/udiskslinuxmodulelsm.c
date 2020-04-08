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

#include <src/udisksdaemon.h>
#include <src/udiskslogging.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodulemanager.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>
#include <src/udiskslinuxdriveobject.h>

#include "udiskslinuxmodulelsm.h"
#include "lsm_types.h"
#include "lsm_linux_drive.h"
#include "lsm_linux_drive_local.h"
#include "lsm_data.h"

/**
 * SECTION:udiskslinuxmodulelsm
 * @title: UDisksLinuxModuleLSM
 * @short_description: libstoragemgmt module.
 *
 * The libstoragemgmt module.
 */

/**
 * UDisksLinuxModuleLSM:
 *
 * The #UDisksLinuxModuleLSM structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _UDisksLinuxModuleLSM {
  UDisksModule parent_instance;
};

typedef struct _UDisksLinuxModuleLSMClass UDisksLinuxModuleLSMClass;

struct _UDisksLinuxModuleLSMClass {
  UDisksModuleClass parent_class;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxModuleLSM, udisks_linux_module_lsm, UDISKS_TYPE_MODULE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));


static void
udisks_linux_module_lsm_init (UDisksLinuxModuleLSM *module)
{
}

static void
udisks_linux_module_lsm_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_module_lsm_parent_class)->constructed)
    G_OBJECT_CLASS (udisks_linux_module_lsm_parent_class)->constructed (object);
}

static void
udisks_linux_module_lsm_finalize (GObject *object)
{
  std_lsm_data_teardown ();

  if (G_OBJECT_CLASS (udisks_linux_module_lsm_parent_class)->finalize)
    G_OBJECT_CLASS (udisks_linux_module_lsm_parent_class)->finalize (object);
}

gchar *
udisks_module_id (void)
{
  return g_strdup (LSM_MODULE_NAME);
}

/**
 * udisks_module_lsm_new:
 * @daemon: A #UDisksDaemon.
 * @cancellable: (nullable): A #GCancellable or %NULL
 * @error: Return location for error or %NULL.
 *
 * Creates new #UDisksLinuxModuleLSM object.
 *
 * Returns: (transfer full) (type UDisksLinuxModuleLSM): A
 *   #UDisksLinuxModuleLSM object or %NULL if @error is set. Free
 *   with g_object_unref().
 */
UDisksModule *
udisks_module_lsm_new (UDisksDaemon  *daemon,
                       GCancellable  *cancellable,
                       GError       **error)
{
  GInitable *initable;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  initable = g_initable_new (UDISKS_TYPE_LINUX_MODULE_LSM,
                             cancellable,
                             error,
                             "daemon", daemon,
                             "name", LSM_MODULE_NAME,
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
  UDisksLinuxModuleLSM *module = UDISKS_LINUX_MODULE_LSM (initable);
  UDisksDaemon *daemon;

  daemon = udisks_module_get_daemon (UDISKS_MODULE (module));
  if (! std_lsm_data_init (daemon, error))
    return FALSE;

  return TRUE;
}

static void
initable_iface_init (GInitableIface *initable_iface)
{
  initable_iface->init = initable_init;
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
udisks_linux_module_lsm_drive_check (UDisksLinuxModuleLSM   *module,
                                     UDisksLinuxDriveObject *drive_object)
{
  gboolean is_managed;
  UDisksLinuxDevice *device;
  gboolean rc = FALSE;
  const gchar *wwn;

  udisks_debug ("LSM: _drive_check");

  device = udisks_linux_drive_object_get_device (drive_object, TRUE);
  if (device == NULL)
    goto out;

  if (g_udev_device_get_property_as_boolean (device->udev_device, "ID_CDROM"))
    goto out;

  wwn = g_udev_device_get_property (device->udev_device, "ID_WWN_WITH_EXTENSION");
  if (! wwn || strlen (wwn) < 2)
    goto out;

  /* udev ID_WWN is started with 0x. */
  is_managed = std_lsm_vpd83_is_managed (wwn + 2);
  if (is_managed == FALSE)
    {
      /* Refresh and try again. */
      std_lsm_vpd83_list_refresh ();
      is_managed = std_lsm_vpd83_is_managed (wwn + 2);
    }

  if (is_managed == FALSE)
    {
      udisks_debug ("LSM: VPD %s is not managed by LibstorageMgmt", wwn + 2);
      goto out;
    }
  else
    rc = TRUE;

out:
  g_clear_object (&device);

  return rc;
}

gboolean
udisks_linux_module_lsm_drive_local_check (UDisksLinuxModuleLSM   *module,
                                           UDisksLinuxDriveObject *drive_object)
{
  /* TODO: attach only to drives libstoragemgmt can handle */

  /* The LsmLocalDisk interface is designated available on all disk drives as
   * there is no reliable way to determine whether LED control is properly
   * supported. Client code can only invoke the appropriate procedures for
   * controlling the lights and check for errors that may indicate failure.
   */

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static GType *
udisks_linux_module_lsm_get_drive_object_interface_types (UDisksModule *module)
{
  static GType drive_object_interface_types[3];

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_LSM (module), NULL);

  if (g_once_init_enter (&drive_object_interface_types[0]))
    {
      /* FIXME: not entirely atomic way, but should work. */
      drive_object_interface_types[1] = UDISKS_TYPE_LINUX_DRIVE_LSM_LOCAL;
      g_once_init_leave (&drive_object_interface_types[0], UDISKS_TYPE_LINUX_DRIVE_LSM);
    }

  return drive_object_interface_types;
}

static GDBusInterfaceSkeleton *
udisks_linux_module_lsm_new_drive_object_interface (UDisksModule           *module,
                                                    UDisksLinuxDriveObject *object,
                                                    GType                   interface_type)
{
  GDBusInterfaceSkeleton *interface = NULL;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_LSM (module), NULL);

  if (interface_type == UDISKS_TYPE_LINUX_DRIVE_LSM)
    {
      if (udisks_linux_module_lsm_drive_check (UDISKS_LINUX_MODULE_LSM (module), object))
        {
          interface = G_DBUS_INTERFACE_SKELETON (udisks_linux_drive_lsm_new (UDISKS_LINUX_MODULE_LSM (module), object));
        }
    }
  else
  if (interface_type == UDISKS_TYPE_LINUX_DRIVE_LSM_LOCAL)
    {
      if (udisks_linux_module_lsm_drive_local_check (UDISKS_LINUX_MODULE_LSM (module), object))
        {
          interface = G_DBUS_INTERFACE_SKELETON (udisks_linux_drive_lsm_local_new (UDISKS_LINUX_MODULE_LSM (module), object));
        }
    }
  else
    {
      udisks_error ("Invalid interface type");
    }

  return interface;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_module_lsm_class_init (UDisksLinuxModuleLSMClass *klass)
{
  GObjectClass *gobject_class;
  UDisksModuleClass *module_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = udisks_linux_module_lsm_constructed;
  gobject_class->finalize = udisks_linux_module_lsm_finalize;

  module_class = UDISKS_MODULE_CLASS (klass);
  module_class->get_drive_object_interface_types = udisks_linux_module_lsm_get_drive_object_interface_types;
  module_class->new_drive_object_interface = udisks_linux_module_lsm_new_drive_object_interface;
}
