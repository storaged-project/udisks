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

#include <src/storagedlogging.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storageddaemon.h>
#include <src/storagedstate.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlinuxprovider.h>

#include "storagedlinuxphysicalvolume.h"
#include "storagedlinuxvolumegroup.h"
#include "storagedlinuxvolumegroupobject.h"

#include "storagedlvm2dbusutil.h"
#include "storaged-lvm2-generated.h"

/**
 * SECTION:storagedlinuxphysicalvolume
 * @title: StoragedLinuxPhysicalVolume
 * @short_description: Linux implementation of #StoragedPhysicalVolume
 *
 * This type provides an implementation of the #StoragedPhysicalVolume
 * interface on Linux.
 */

typedef struct _StoragedLinuxPhysicalVolumeClass   StoragedLinuxPhysicalVolumeClass;

/**
 * StoragedLinuxPhysicalVolume:
 *
 * The #StoragedLinuxPhysicalVolume structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxPhysicalVolume
{
  StoragedPhysicalVolumeSkeleton parent_instance;
};

struct _StoragedLinuxPhysicalVolumeClass
{
  StoragedPhysicalVolumeSkeletonClass parent_class;
};

static void physical_volume_iface_init (StoragedPhysicalVolumeIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxPhysicalVolume, storaged_linux_physical_volume,
                         STORAGED_TYPE_PHYSICAL_VOLUME_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_PHYSICAL_VOLUME, physical_volume_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_physical_volume_init (StoragedLinuxPhysicalVolume *physical_volume)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (physical_volume),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_physical_volume_class_init (StoragedLinuxPhysicalVolumeClass *klass)
{
}

/**
 * storaged_linux_physical_volume_new:
 *
 * Creates a new #StoragedLinuxPhysicalVolume instance.
 *
 * Returns: A new #StoragedLinuxPhysicalVolume. Free with g_object_unref().
 */
StoragedPhysicalVolume *
storaged_linux_physical_volume_new (void)
{
  return STORAGED_PHYSICAL_VOLUME (g_object_new (STORAGED_TYPE_LINUX_PHYSICAL_VOLUME,
                                                 NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_physical_volume_update:
 * @physical_volume: A #StoragedLinuxPhysicalVolume.
 * @object: The enclosing #StoragedLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_physical_volume_update   (StoragedLinuxPhysicalVolume    *physical_volume,
                                         StoragedLinuxBlockObject       *object,
                                         StoragedLinuxVolumeGroupObject *group_object,
                                         GVariant                       *info)
{
  StoragedPhysicalVolume *iface;
  guint64 num;

  iface = STORAGED_PHYSICAL_VOLUME (physical_volume);

  storaged_physical_volume_set_volume_group (iface, g_dbus_object_get_object_path (G_DBUS_OBJECT (group_object)));

  if (g_variant_lookup (info, "size", "t", &num))
    storaged_physical_volume_set_size (iface, num);

  if (g_variant_lookup (info, "free-size", "t", &num))
    storaged_physical_volume_set_free_size (iface, num);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
physical_volume_iface_init (StoragedPhysicalVolumeIface *iface)
{
}

/* ---------------------------------------------------------------------------------------------------- */

void
storaged_linux_block_object_update_lvm_pv (StoragedLinuxBlockObject       *object,
                                           StoragedLinuxVolumeGroupObject *group_object,
                                           GVariant                       *info)
{
  StoragedPhysicalVolume *iface_physical_volume;

  iface_physical_volume = storaged_object_peek_physical_volume (STORAGED_OBJECT (object));

  if (group_object)
    {
      if (iface_physical_volume == NULL)
        {
          iface_physical_volume = storaged_linux_physical_volume_new ();
          storaged_linux_physical_volume_update (STORAGED_LINUX_PHYSICAL_VOLUME (iface_physical_volume),
                                               object, group_object, info);
          g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                                G_DBUS_INTERFACE_SKELETON (iface_physical_volume));
          g_object_unref (iface_physical_volume);
        }
      else
        storaged_linux_physical_volume_update (STORAGED_LINUX_PHYSICAL_VOLUME (iface_physical_volume),
                                             object, group_object, info);
    }
  else
    {
      if (iface_physical_volume != NULL)
        g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (object),
                                                 G_DBUS_INTERFACE_SKELETON (iface_physical_volume));
    }
}
