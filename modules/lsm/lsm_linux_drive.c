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

#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <libstoragemgmt/libstoragemgmt.h>

#include <src/udiskslogging.h>
#include <src/udiskslinuxdriveobject.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udisksbasejob.h>
#include <src/udiskssimplejob.h>
#include <src/udisksthreadedjob.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>

#include "lsm_linux_drive.h"
#include "lsm_data.h"
#include "lsm_types.h"

typedef struct _UDisksLinuxDriveLSMClass UDisksLinuxDriveLSMClass;

struct _UDisksLinuxDriveLSM
{
  UDisksDriveLSMSkeleton parent_instance;

  UDisksLinuxModuleLSM   *module;
  UDisksLinuxDriveObject *drive_object;
  struct StdLsmVolData   *old_lsm_data;
  gchar                  *vpd83;
  guint                   timeout_id;
};

struct _UDisksLinuxDriveLSMClass
{
  UDisksDriveLSMSkeletonClass parent_class;
};

static void udisks_linux_drive_lsm_iface_init (UDisksDriveLSMIface *iface);
static void udisks_linux_drive_lsm_module_object_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxDriveLSM, udisks_linux_drive_lsm, UDISKS_TYPE_DRIVE_LSM_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_DRIVE_LSM, udisks_linux_drive_lsm_iface_init)
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT, udisks_linux_drive_lsm_module_object_iface_init));

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_DRIVE_OBJECT,
  N_PROPERTIES
};


static void
udisks_linux_drive_lsm_get_property (GObject     *object,
                                      guint        property_id,
                                      GValue      *value,
                                      GParamSpec  *pspec)
{
  UDisksLinuxDriveLSM *drive_lsm = UDISKS_LINUX_DRIVE_LSM (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_value_set_object (value, UDISKS_MODULE (drive_lsm->module));
      break;

    case PROP_DRIVE_OBJECT:
      g_value_set_object (value, drive_lsm->drive_object);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_drive_lsm_set_property (GObject       *object,
                                      guint          property_id,
                                      const GValue  *value,
                                      GParamSpec    *pspec)
{
  UDisksLinuxDriveLSM *drive_lsm = UDISKS_LINUX_DRIVE_LSM (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_assert (drive_lsm->module == NULL);
      drive_lsm->module = UDISKS_LINUX_MODULE_LSM (g_value_dup_object (value));
      break;

    case PROP_DRIVE_OBJECT:
      g_assert (drive_lsm->drive_object == NULL);
      /* we don't take reference to drive_object */
      drive_lsm->drive_object = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_drive_lsm_finalize (GObject *object)
{
  UDisksLinuxDriveLSM *drive_lsm = UDISKS_LINUX_DRIVE_LSM (object);

  udisks_debug ("LSM: udisks_linux_drive_lsm_finalize ()");

  if (drive_lsm->timeout_id)
    g_source_remove (drive_lsm->timeout_id);

  /* we don't take reference to drive_object */
  g_object_unref (drive_lsm->module);
  g_free (drive_lsm->vpd83);

  std_lsm_vol_data_free (drive_lsm->old_lsm_data);

  if (G_OBJECT_CLASS (udisks_linux_drive_lsm_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_drive_lsm_parent_class)->finalize (object);
}


static void
udisks_linux_drive_lsm_init (UDisksLinuxDriveLSM *drive_lsm)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (drive_lsm),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_drive_lsm_class_init (UDisksLinuxDriveLSMClass *class)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->get_property = udisks_linux_drive_lsm_get_property;
  gobject_class->set_property = udisks_linux_drive_lsm_set_property;
  gobject_class->finalize = udisks_linux_drive_lsm_finalize;

  /**
   * UDisksLinuxDriveLSM:module:
   *
   * The #UDisksModule for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MODULE,
                                   g_param_spec_object ("module",
                                                        "Module",
                                                        "The module for the object",
                                                        UDISKS_TYPE_MODULE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
  /**
   * UDisksLinuxDriveLSM:driveobject:
   *
   * The #UDisksLinuxDriveObject for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DRIVE_OBJECT,
                                   g_param_spec_object ("driveobject",
                                                        "Drive object",
                                                        "The drive object for the interface",
                                                        UDISKS_TYPE_LINUX_DRIVE_OBJECT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
_fill_drive_lsm (UDisksLinuxDriveLSM  *drive_lsm,
                 struct StdLsmVolData *lsm_vol_data)
{
  UDisksDriveLSM *std_drv_lsm = UDISKS_DRIVE_LSM (drive_lsm);

  if (lsm_vol_data == NULL)
    return;

  udisks_drive_lsm_set_status_info (std_drv_lsm, lsm_vol_data->status_info);
  udisks_drive_lsm_set_raid_type (std_drv_lsm, lsm_vol_data->raid_type);
  udisks_drive_lsm_set_is_ok (std_drv_lsm, lsm_vol_data->is_ok);
  udisks_drive_lsm_set_is_raid_degraded (std_drv_lsm, lsm_vol_data->is_raid_degraded);
  udisks_drive_lsm_set_is_raid_error (std_drv_lsm, lsm_vol_data->is_raid_error);
  udisks_drive_lsm_set_is_raid_verifying (std_drv_lsm, lsm_vol_data->is_raid_verifying);
  udisks_drive_lsm_set_is_raid_reconstructing (std_drv_lsm, lsm_vol_data->is_raid_reconstructing);
  udisks_drive_lsm_set_min_io_size (std_drv_lsm, lsm_vol_data->min_io_size);
  udisks_drive_lsm_set_opt_io_size (std_drv_lsm, lsm_vol_data->opt_io_size);
  udisks_drive_lsm_set_raid_disk_count (std_drv_lsm, lsm_vol_data->raid_disk_count);

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (std_drv_lsm));
}


/*
 * Compare old data and new data. Return TRUE if changed.
 */
static gboolean
_is_std_lsm_vol_data_changed (struct StdLsmVolData *old_lsm_data,
                              struct StdLsmVolData *new_lsm_data)
{
  if (old_lsm_data == NULL || new_lsm_data == NULL)
    return TRUE;

  if (strcmp (old_lsm_data->status_info, new_lsm_data->status_info) != 0 ||
      strcmp (old_lsm_data->raid_type, new_lsm_data->raid_type) != 0 ||
      old_lsm_data->is_ok != new_lsm_data->is_ok ||
      old_lsm_data->is_raid_degraded != new_lsm_data->is_raid_degraded ||
      old_lsm_data->is_raid_error != new_lsm_data->is_raid_error ||
      old_lsm_data->is_raid_verifying != new_lsm_data->is_raid_verifying ||
      old_lsm_data->is_raid_reconstructing != new_lsm_data->is_raid_reconstructing ||
      old_lsm_data->min_io_size != new_lsm_data->min_io_size ||
      old_lsm_data->opt_io_size != new_lsm_data->opt_io_size ||
      old_lsm_data->raid_disk_count != new_lsm_data->raid_disk_count)
    return TRUE;

  return FALSE;
}

static gboolean
_on_refresh_data (UDisksLinuxDriveLSM *drive_lsm)
{
  struct StdLsmVolData *new_lsm_data;

  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_LSM (drive_lsm), G_SOURCE_REMOVE);

  udisks_debug ("LSM: Refreshing LSM RAID info for VPD83/WWN %s", drive_lsm->vpd83);

  new_lsm_data = std_lsm_vol_data_get (drive_lsm->vpd83);
  if (new_lsm_data == NULL)
    {
      udisks_debug ("LSM: Disk drive VPD83/WWN %s is not LSM managed any more", drive_lsm->vpd83);
      return G_SOURCE_REMOVE;
    }

  if (_is_std_lsm_vol_data_changed (drive_lsm->old_lsm_data, new_lsm_data))
    {
      _fill_drive_lsm (drive_lsm, new_lsm_data);
      std_lsm_vol_data_free (drive_lsm->old_lsm_data);
      drive_lsm->old_lsm_data = new_lsm_data;
    }
  else
    std_lsm_vol_data_free (new_lsm_data);

  return G_SOURCE_CONTINUE;
}

/**
 * udisks_linux_drive_lsm_new:
 * @module: A #UDisksLinuxModuleLSM.
 * @drive_object: A #UDisksLinuxDriveObject.
 *
 * Creates a new #UDisksLinuxDriveLSM instance.
 *
 * Returns: A new #UDisksLinuxDriveLSM. Free with g_object_unref().
 */
UDisksLinuxDriveLSM *
udisks_linux_drive_lsm_new (UDisksLinuxModuleLSM   *module,
                            UDisksLinuxDriveObject *drive_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_LSM (module), NULL);
  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_OBJECT (drive_object), NULL);

  udisks_debug ("LSM: udisks_linux_drive_lsm_new");

  return g_object_new (UDISKS_TYPE_LINUX_DRIVE_LSM,
                       "module", UDISKS_MODULE (module),
                       "driveobject", drive_object,
                       NULL);
}

gboolean
udisks_linux_drive_lsm_update (UDisksLinuxDriveLSM    *drive_lsm,
                               UDisksLinuxDriveObject *drive_object)
{
  UDisksLinuxDevice *device;
  const gchar *wwn = NULL;
  gboolean rc = FALSE;

  udisks_debug ("LSM: udisks_linux_drive_lsm_update");

  device = udisks_linux_drive_object_get_device (drive_object, TRUE);
  if (device == NULL)
    {
      udisks_debug ("LSM: udisks_linux_drive_lsm_update (): Got NULL udisks_linux_drive_object_get_device () return");
      goto out;
    }

  wwn = g_udev_device_get_property (device->udev_device, "ID_WWN_WITH_EXTENSION");
  if (! wwn || strlen (wwn) < 2)
    {
      udisks_debug ("LSM: udisks_linux_drive_lsm_update (): Got empty ID_WWN_WITH_EXTENSION dbus property");
      goto out;
    }

  g_free (drive_lsm->vpd83);
  drive_lsm->vpd83 = g_strdup (wwn + 2);

  _on_refresh_data (drive_lsm);

  /* Start polling */
  if (drive_lsm->timeout_id == 0)
    {
      drive_lsm->timeout_id = g_timeout_add_seconds (std_lsm_refresh_time_get (),
                                                     (GSourceFunc) _on_refresh_data,
                                                     drive_lsm);
      udisks_debug ("LSM: VPD83 %s added to refresh event loop", wwn + 2);
    }

  rc = TRUE;

out:
  g_clear_object (&device);

  return rc;
}

static void
udisks_linux_drive_lsm_iface_init (UDisksDriveLSMIface *iface)
{
  udisks_debug ("LSM: udisks_linux_drive_lsm_iface_init");
}

/* -------------------------------------------------------------------------- */

static gboolean
udisks_linux_drive_lsm_module_object_process_uevent (UDisksModuleObject *module_object,
                                                     const gchar        *action,
                                                     UDisksLinuxDevice  *device,
                                                     gboolean           *keep)
{
  UDisksLinuxDriveLSM *drive_lsm = UDISKS_LINUX_DRIVE_LSM (module_object);

  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_LSM (module_object), FALSE);

  *keep = udisks_linux_module_lsm_drive_check (drive_lsm->module, drive_lsm->drive_object);
  if (*keep)
    {
      udisks_linux_drive_lsm_update (drive_lsm, drive_lsm->drive_object);
    }
  else
    {
      /* Stop polling */
      if (drive_lsm->timeout_id)
        {
          g_source_remove (drive_lsm->timeout_id);
          drive_lsm->timeout_id = 0;
        }
    }

  return TRUE;
}

static void
udisks_linux_drive_lsm_module_object_iface_init (UDisksModuleObjectIface *iface)
{
  iface->process_uevent = udisks_linux_drive_lsm_module_object_process_uevent;
}
