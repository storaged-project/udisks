/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
 * Copyright (C) 2013 Marius Vollmer <marius.vollmer@redhat.com>
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
#include <glib/gi18n-lib.h>

#include <blockdev/lvm.h>

#include <src/udiskslogging.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udisksdaemon.h>
#include <src/udisksstate.h>
#include <src/udisksdaemonutil.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslinuxprovider.h>

#include "udiskslinuxphysicalvolume.h"
#include "udiskslinuxvolumegroup.h"
#include "udiskslinuxvolumegroupobject.h"

#include "udiskslvm2dbusutil.h"
#include "udisks-lvm2-generated.h"

/**
 * SECTION:udiskslinuxphysicalvolume
 * @title: UDisksLinuxPhysicalVolume
 * @short_description: Linux implementation of #UDisksPhysicalVolume
 *
 * This type provides an implementation of the #UDisksPhysicalVolume
 * interface on Linux.
 */

typedef struct _UDisksLinuxPhysicalVolumeClass   UDisksLinuxPhysicalVolumeClass;

/**
 * UDisksLinuxPhysicalVolume:
 *
 * The #UDisksLinuxPhysicalVolume structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxPhysicalVolume
{
  UDisksPhysicalVolumeSkeleton parent_instance;
};

struct _UDisksLinuxPhysicalVolumeClass
{
  UDisksPhysicalVolumeSkeletonClass parent_class;
};

static void physical_volume_iface_init (UDisksPhysicalVolumeIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxPhysicalVolume, udisks_linux_physical_volume,
                         UDISKS_TYPE_PHYSICAL_VOLUME_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_PHYSICAL_VOLUME, physical_volume_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_physical_volume_init (UDisksLinuxPhysicalVolume *physical_volume)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (physical_volume),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_physical_volume_class_init (UDisksLinuxPhysicalVolumeClass *klass)
{
}

/**
 * udisks_linux_physical_volume_new:
 *
 * Creates a new #UDisksLinuxPhysicalVolume instance.
 *
 * Returns: A new #UDisksLinuxPhysicalVolume. Free with g_object_unref().
 */
UDisksPhysicalVolume *
udisks_linux_physical_volume_new (void)
{
  return UDISKS_PHYSICAL_VOLUME (g_object_new (UDISKS_TYPE_LINUX_PHYSICAL_VOLUME,
                                               NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_physical_volume_update:
 * @physical_volume: A #UDisksLinuxPhysicalVolume.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_physical_volume_update   (UDisksLinuxPhysicalVolume    *physical_volume,
                                       UDisksLinuxBlockObject       *object,
                                       UDisksLinuxVolumeGroupObject *group_object,
                                       BDLVMPVdata                  *pv_info)
{
  UDisksPhysicalVolume *iface;

  iface = UDISKS_PHYSICAL_VOLUME (physical_volume);

  udisks_physical_volume_set_volume_group (iface, g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object)));

  /* udiskslinuxvolumegroupobject.c:487 looks like we may not be given actual pv_info */
  if (pv_info)
    {
      udisks_physical_volume_set_size (iface, pv_info->pv_size);
      udisks_physical_volume_set_free_size (iface, pv_info->pv_free);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
physical_volume_iface_init (UDisksPhysicalVolumeIface *iface)
{
}

/* ---------------------------------------------------------------------------------------------------- */

void
udisks_linux_block_object_update_lvm_pv (UDisksLinuxBlockObject       *object,
                                         UDisksLinuxVolumeGroupObject *group_object,
                                         BDLVMPVdata                  *pv_info)
{
  UDisksPhysicalVolume *iface_physical_volume;

  iface_physical_volume = udisks_object_peek_physical_volume (UDISKS_OBJECT (object));

  if (group_object)
    {
      if (iface_physical_volume == NULL)
        {
          iface_physical_volume = udisks_linux_physical_volume_new ();
          udisks_linux_physical_volume_update (UDISKS_LINUX_PHYSICAL_VOLUME (iface_physical_volume),
                                               object, group_object, pv_info);
          g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                                G_DBUS_INTERFACE_SKELETON (iface_physical_volume));
          g_object_unref (iface_physical_volume);
        }
      else
        udisks_linux_physical_volume_update (UDISKS_LINUX_PHYSICAL_VOLUME (iface_physical_volume),
                                             object, group_object, pv_info);
    }
  else
    {
      if (iface_physical_volume != NULL)
        g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (object),
                                                 G_DBUS_INTERFACE_SKELETON (iface_physical_volume));
    }
}
