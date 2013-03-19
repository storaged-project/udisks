/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#define _GNU_SOURCE /* for O_DIRECT */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <mntent.h>

#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>

#include "udiskslogging.h"
#include "udiskslinuxblock.h"
#include "udiskslinuxblockobject.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxfsinfo.h"
#include "udisksdaemon.h"
#include "udisksstate.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxprovider.h"
#include "udisksfstabmonitor.h"
#include "udisksfstabentry.h"
#include "udiskscrypttabmonitor.h"
#include "udiskscrypttabentry.h"
#include "udisksdaemonutil.h"
#include "udisksbasejob.h"
#include "udiskssimplejob.h"
#include "udiskslinuxdriveata.h"
#include "udiskslinuxmdraidobject.h"
#include "udiskslinuxdevice.h"
#include "udiskslinuxpartition.h"

/**
 * SECTION:udiskslinuxblock
 * @title: UDisksLinuxBlock
 * @short_description: Linux implementation of #UDisksBlock
 *
 * This type provides an implementation of the #UDisksBlock
 * interface on Linux.
 */

typedef struct _UDisksLinuxBlockClass   UDisksLinuxBlockClass;

/**
 * UDisksLinuxBlock:
 *
 * The #UDisksLinuxBlock structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxBlock
{
  UDisksBlockSkeleton parent_instance;
};

struct _UDisksLinuxBlockClass
{
  UDisksBlockSkeletonClass parent_class;
};

static void block_iface_init (UDisksBlockIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxBlock, udisks_linux_block, UDISKS_TYPE_BLOCK_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_BLOCK, block_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_block_init (UDisksLinuxBlock *block)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (block),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_block_class_init (UDisksLinuxBlockClass *klass)
{
}

/**
 * udisks_linux_block_new:
 *
 * Creates a new #UDisksLinuxBlock instance.
 *
 * Returns: A new #UDisksLinuxBlock. Free with g_object_unref().
 */
UDisksBlock *
udisks_linux_block_new (void)
{
  return UDISKS_BLOCK (g_object_new (UDISKS_TYPE_LINUX_BLOCK,
                                     NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_sysfs_attr (GUdevDevice *device,
                const gchar *attr)
{
  gchar *filename;
  gchar *value;
  filename = g_strconcat (g_udev_device_get_sysfs_path (device),
                          "/",
                          attr,
                          NULL);
  value = NULL;
  /* don't care about errors */
  g_file_get_contents (filename,
                       &value,
                       NULL,
                       NULL);
  g_free (filename);
  return value;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
find_block_device_by_sysfs_path (GDBusObjectManagerServer *object_manager,
                                 const gchar              *sysfs_path)
{
  gchar *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectSkeleton *object = G_DBUS_OBJECT_SKELETON (l->data);
      UDisksLinuxDevice *device;

      if (!UDISKS_IS_LINUX_BLOCK_OBJECT (object))
        continue;

      device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));
      if (g_strcmp0 (sysfs_path, g_udev_device_get_sysfs_path (device->udev_device)) == 0)
        {
          ret = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
          g_object_unref (device);
          goto out;
        }
      g_object_unref (device);
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
find_drive (GDBusObjectManagerServer  *object_manager,
            GUdevDevice               *block_device,
            UDisksDrive              **out_drive)
{
  GUdevDevice *whole_disk_block_device;
  const gchar *whole_disk_block_device_sysfs_path;
  gchar *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  if (g_strcmp0 (g_udev_device_get_devtype (block_device), "disk") == 0)
    whole_disk_block_device = g_object_ref (block_device);
  else
    whole_disk_block_device = g_udev_device_get_parent_with_subsystem (block_device, "block", "disk");
  whole_disk_block_device_sysfs_path = g_udev_device_get_sysfs_path (whole_disk_block_device);

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectSkeleton *object = G_DBUS_OBJECT_SKELETON (l->data);
      GList *drive_devices;
      GList *j;

      if (!UDISKS_IS_LINUX_DRIVE_OBJECT (object))
        continue;

      drive_devices = udisks_linux_drive_object_get_devices (UDISKS_LINUX_DRIVE_OBJECT (object));
      for (j = drive_devices; j != NULL; j = j->next)
        {
          UDisksLinuxDevice *drive_device = UDISKS_LINUX_DEVICE (j->data);
          const gchar *drive_sysfs_path;

          drive_sysfs_path = g_udev_device_get_sysfs_path (drive_device->udev_device);
          if (g_strcmp0 (whole_disk_block_device_sysfs_path, drive_sysfs_path) == 0)
            {
              if (out_drive != NULL)
                *out_drive = udisks_object_get_drive (UDISKS_OBJECT (object));
              ret = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
              g_list_foreach (drive_devices, (GFunc) g_object_unref, NULL);
              g_list_free (drive_devices);
              goto out;
            }
        }
      g_list_foreach (drive_devices, (GFunc) g_object_unref, NULL);
      g_list_free (drive_devices);
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  g_object_unref (whole_disk_block_device);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksLinuxMDRaidObject *
find_mdraid (GDBusObjectManagerServer  *object_manager,
             const gchar               *md_uuid)
{
  UDisksLinuxMDRaidObject *ret = NULL;
  GList *objects = NULL, *l;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectSkeleton *object = G_DBUS_OBJECT_SKELETON (l->data);
      if (UDISKS_IS_LINUX_MDRAID_OBJECT (object))
        {
          UDisksMDRaid *mdraid = udisks_object_get_mdraid (UDISKS_OBJECT (object));
          if (mdraid != NULL)
            {
              if (g_strcmp0 (udisks_mdraid_get_uuid (mdraid), md_uuid) == 0)
                {
                  ret = g_object_ref (object);
                  g_object_unref (mdraid);
                  goto out;
                }
              g_object_unref (mdraid);
            }
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_mdraid (UDisksLinuxBlock         *block,
               UDisksLinuxDevice        *device,
               UDisksDrive              *drive,
               GDBusObjectManagerServer *object_manager)
{
  UDisksBlock *iface = UDISKS_BLOCK (block);
  const gchar *uuid;
  const gchar *objpath_mdraid = "/";
  const gchar *objpath_mdraid_member = "/";
  UDisksLinuxMDRaidObject *object = NULL;

  uuid = g_udev_device_get_property (device->udev_device, "UDISKS_MD_UUID");
  if (uuid != NULL && strlen (uuid) > 0)
    {
      object = find_mdraid (object_manager, uuid);
      if (object != NULL)
        {
          objpath_mdraid = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
          g_clear_object (&object);
        }
    }

  uuid = g_udev_device_get_property (device->udev_device, "UDISKS_MD_MEMBER_UUID");
  if (uuid != NULL && strlen (uuid) > 0)
    {
      object = find_mdraid (object_manager, uuid);
      if (object != NULL)
        {
          objpath_mdraid_member = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
          g_clear_object (&object);
        }
    }

  udisks_block_set_mdraid (iface, objpath_mdraid);
  udisks_block_set_mdraid_member (iface, objpath_mdraid_member);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
update_hints (UDisksLinuxBlock  *block,
              UDisksLinuxDevice *device,
              UDisksDrive       *drive)
{
  UDisksBlock *iface = UDISKS_BLOCK (block);
  gboolean hint_partitionable;
  gboolean hint_system;
  gboolean hint_ignore;
  gboolean hint_auto;
  const gchar *hint_name;
  const gchar *hint_icon_name;
  const gchar *hint_symbolic_icon_name;
  const gchar *device_file;

  /* very conservative defaults */
  hint_partitionable = TRUE;
  hint_system = TRUE;
  hint_ignore = FALSE;
  hint_auto = FALSE;
  hint_name = NULL;
  hint_icon_name = NULL;
  hint_symbolic_icon_name = NULL;

  device_file = g_udev_device_get_device_file (device->udev_device);

  /* Provide easy access to _only_ the following devices
   *
   *  - anything connected via known local buses (e.g. USB or Firewire, MMC or MemoryStick)
   *  - any device with removable media
   *
   * Be careful when extending this list as we don't want to automount
   * the world when (inadvertently) connecting to a SAN.
   */
  if (drive != NULL)
    {
      const gchar *connection_bus;
      gboolean removable;
      connection_bus = udisks_drive_get_connection_bus (drive);
      removable = udisks_drive_get_media_removable (drive);
      if (removable ||
          (g_strcmp0 (connection_bus, "usb") == 0 || g_strcmp0 (connection_bus, "ieee1394") == 0) ||
          (g_str_has_prefix (device_file, "/dev/mmcblk") || g_str_has_prefix (device_file, "/dev/mspblk")))
        {
          hint_system = FALSE;
          hint_auto = TRUE;
        }
    }

  /* Floppy drives are not partitionable and should never be auto-mounted */
  if (g_str_has_prefix (device_file, "/dev/fd"))
    {
      hint_system = FALSE;
      hint_partitionable = FALSE;
      hint_auto = FALSE;
    }

  /* CD-ROM media / drives are not partitionable, at least not here on Linux */
  if (g_udev_device_get_property_as_boolean (device->udev_device, "ID_CDROM"))
    hint_partitionable = FALSE;

  /* device-mapper devices are not partitionable (TODO: for multipath, they are via kpartx(8) hacks) */
  if (g_str_has_prefix (g_udev_device_get_name (device->udev_device), "dm-"))
    hint_partitionable = FALSE;

  /* TODO: set ignore to TRUE for physical paths belonging to a drive with multiple paths */

  /* override from udev properties */
  if (g_udev_device_has_property (device->udev_device, "UDISKS_SYSTEM"))
    hint_system = g_udev_device_get_property_as_boolean (device->udev_device, "UDISKS_SYSTEM");
  if (g_udev_device_has_property (device->udev_device, "UDISKS_IGNORE"))
    hint_ignore = g_udev_device_get_property_as_boolean (device->udev_device, "UDISKS_IGNORE");
  if (g_udev_device_has_property (device->udev_device, "UDISKS_AUTO"))
    hint_auto = g_udev_device_get_property_as_boolean (device->udev_device, "UDISKS_AUTO");
  if (g_udev_device_has_property (device->udev_device, "UDISKS_NAME"))
    hint_name = g_udev_device_get_property (device->udev_device, "UDISKS_NAME");
  if (g_udev_device_has_property (device->udev_device, "UDISKS_ICON_NAME"))
    hint_icon_name = g_udev_device_get_property (device->udev_device, "UDISKS_ICON_NAME");
  if (g_udev_device_has_property (device->udev_device, "UDISKS_SYMBOLIC_ICON_NAME"))
    hint_symbolic_icon_name = g_udev_device_get_property (device->udev_device, "UDISKS_SYMBOLIC_ICON_NAME");

  /* ... and scene! */
  udisks_block_set_hint_partitionable (iface, hint_partitionable);
  udisks_block_set_hint_system (iface, hint_system);
  udisks_block_set_hint_ignore (iface, hint_ignore);
  udisks_block_set_hint_auto (iface, hint_auto);
  udisks_block_set_hint_name (iface, hint_name);
  udisks_block_set_hint_icon_name (iface, hint_icon_name);
  udisks_block_set_hint_symbolic_icon_name (iface, hint_symbolic_icon_name);
}

/* ---------------------------------------------------------------------------------------------------- */

static GList *
find_fstab_entries_for_device (UDisksLinuxBlock *block,
                               UDisksDaemon     *daemon)
{
  GList *entries;
  GList *l;
  GList *ret;

  ret = NULL;

  /* if this is too slow, we could add lookup methods to UDisksFstabMonitor... */
  entries = udisks_fstab_monitor_get_entries (udisks_daemon_get_fstab_monitor (daemon));
  for (l = entries; l != NULL; l = l->next)
    {
      UDisksFstabEntry *entry = UDISKS_FSTAB_ENTRY (l->data);
      const gchar *const *symlinks;
      const gchar *fsname;
      const gchar *device = NULL;
      const gchar *label = NULL;
      const gchar *uuid = NULL;
      guint n;

      fsname = udisks_fstab_entry_get_fsname (entry);
      device = NULL;
      if (g_str_has_prefix (fsname, "UUID="))
        {
          uuid = fsname + 5;
        }
      else if (g_str_has_prefix (fsname, "LABEL="))
        {
          label = fsname + 6;
        }
      else if (g_str_has_prefix (fsname, "/dev"))
        {
          device = fsname;
        }
      else
        {
          /* ignore non-device entries */
          goto continue_loop;
        }

      if (device != NULL)
        {
          if (g_strcmp0 (device, udisks_block_get_device (UDISKS_BLOCK (block))) == 0)
            {
              ret = g_list_prepend (ret, g_object_ref (entry));
            }
          else
            {
              symlinks = udisks_block_get_symlinks (UDISKS_BLOCK (block));
              if (symlinks != NULL)
                {
                  for (n = 0; symlinks[n] != NULL; n++)
                    {
                      if (g_strcmp0 (device, symlinks[n]) == 0)
                        {
                          ret = g_list_prepend (ret, g_object_ref (entry));
                        }
                    }
                }
            }
        }
      else if (label != NULL && g_strcmp0 (label, udisks_block_get_id_label (UDISKS_BLOCK (block))) == 0)
        {
          ret = g_list_prepend (ret, g_object_ref (entry));
        }
      else if (uuid != NULL && g_strcmp0 (uuid, udisks_block_get_id_uuid (UDISKS_BLOCK (block))) == 0)
        {
          ret = g_list_prepend (ret, g_object_ref (entry));
        }

    continue_loop:
      ;
    }

  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
  g_list_free (entries);
  return ret;
}

static GList *
find_crypttab_entries_for_device (UDisksLinuxBlock *block,
                                  UDisksDaemon     *daemon)
{
  GList *entries;
  GList *l;
  GList *ret;

  ret = NULL;

  /* if this is too slow, we could add lookup methods to UDisksCrypttabMonitor... */
  entries = udisks_crypttab_monitor_get_entries (udisks_daemon_get_crypttab_monitor (daemon));
  for (l = entries; l != NULL; l = l->next)
    {
      UDisksCrypttabEntry *entry = UDISKS_CRYPTTAB_ENTRY (l->data);
      const gchar *const *symlinks;
      const gchar *device_in_entry;
      const gchar *device = NULL;
      const gchar *label = NULL;
      const gchar *uuid = NULL;
      guint n;

      device_in_entry = udisks_crypttab_entry_get_device (entry);
      if (g_str_has_prefix (device_in_entry, "UUID="))
        {
          uuid = device_in_entry + 5;
        }
      else if (g_str_has_prefix (device_in_entry, "LABEL="))
        {
          label = device_in_entry + 6;
        }
      else if (g_str_has_prefix (device_in_entry, "/dev"))
        {
          device = device_in_entry;
        }
      else
        {
          /* ignore non-device entries */
          goto continue_loop;
        }

      if (device != NULL)
        {
          if (g_strcmp0 (device, udisks_block_get_device (UDISKS_BLOCK (block))) == 0)
            {
              ret = g_list_prepend (ret, g_object_ref (entry));
            }
          else
            {
              symlinks = udisks_block_get_symlinks (UDISKS_BLOCK (block));
              if (symlinks != NULL)
                {
                  for (n = 0; symlinks[n] != NULL; n++)
                    {
                      if (g_strcmp0 (device, symlinks[n]) == 0)
                        {
                          ret = g_list_prepend (ret, g_object_ref (entry));
                        }
                    }
                }
            }
        }
      else if (label != NULL && g_strcmp0 (label, udisks_block_get_id_label (UDISKS_BLOCK (block))) == 0)
        {
          ret = g_list_prepend (ret, g_object_ref (entry));
        }
      else if (uuid != NULL && g_strcmp0 (uuid, udisks_block_get_id_uuid (UDISKS_BLOCK (block))) == 0)
        {
          ret = g_list_prepend (ret, g_object_ref (entry));
        }

    continue_loop:
      ;
    }

  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
  g_list_free (entries);
  return ret;
}

/* returns a floating GVariant */
static GVariant *
calculate_configuration (UDisksLinuxBlock  *block,
                         UDisksDaemon      *daemon,
                         gboolean           include_secrets,
                         GError           **error)
{
  GList *entries;
  GList *l;
  GVariantBuilder builder;
  GVariant *ret;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sa{sv})"));
  /* First the /etc/fstab entries */
  entries = find_fstab_entries_for_device (block, daemon);
  for (l = entries; l != NULL; l = l->next)
    {
      UDisksFstabEntry *entry = UDISKS_FSTAB_ENTRY (l->data);
      GVariantBuilder dict_builder;
      g_variant_builder_init (&dict_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&dict_builder, "{sv}", "fsname",
                             g_variant_new_bytestring (udisks_fstab_entry_get_fsname (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "dir",
                             g_variant_new_bytestring (udisks_fstab_entry_get_dir (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "type",
                             g_variant_new_bytestring (udisks_fstab_entry_get_fstype (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "opts",
                             g_variant_new_bytestring (udisks_fstab_entry_get_opts (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "freq",
                             g_variant_new_int32 (udisks_fstab_entry_get_freq (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "passno",
                             g_variant_new_int32 (udisks_fstab_entry_get_passno (entry)));
      g_variant_builder_add (&builder,
                             "(sa{sv})",
                             "fstab", &dict_builder);
    }
  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
  g_list_free (entries);

  /* Then the /etc/crypttab entries */
  entries = find_crypttab_entries_for_device (block, daemon);
  for (l = entries; l != NULL; l = l->next)
    {
      UDisksCrypttabEntry *entry = UDISKS_CRYPTTAB_ENTRY (l->data);
      GVariantBuilder dict_builder;
      const gchar *passphrase_path;
      const gchar *options;
      gchar *passphrase_contents;
      gsize passphrase_contents_length;

      passphrase_path = udisks_crypttab_entry_get_passphrase_path (entry);
      if (passphrase_path == NULL || g_strcmp0 (passphrase_path, "none") == 0)
        passphrase_path = "";
      passphrase_contents = NULL;
      if (!(g_strcmp0 (passphrase_path, "") == 0 || g_str_has_prefix (passphrase_path, "/dev")))
        {
          if (include_secrets)
            {
              if (!g_file_get_contents (passphrase_path,
                                        &passphrase_contents,
                                        &passphrase_contents_length,
                                        error))
                {
                  g_prefix_error (error,
                                  "Error loading secrets from file `%s' referenced in /etc/crypttab entry: ",
                                  passphrase_path);
                  g_variant_builder_clear (&builder);
                  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
                  g_list_free (entries);
                  goto out;
                }
            }
        }

      options = udisks_crypttab_entry_get_options (entry);
      if (options == NULL)
        options = "";

      g_variant_builder_init (&dict_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&dict_builder, "{sv}", "name",
                             g_variant_new_bytestring (udisks_crypttab_entry_get_name (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "device",
                             g_variant_new_bytestring (udisks_crypttab_entry_get_device (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "passphrase-path",
                             g_variant_new_bytestring (passphrase_path));
      if (passphrase_contents != NULL)
        {
          g_variant_builder_add (&dict_builder, "{sv}", "passphrase-contents",
                                 g_variant_new_bytestring (passphrase_contents));
        }
      g_variant_builder_add (&dict_builder, "{sv}", "options",
                             g_variant_new_bytestring (options));
      g_variant_builder_add (&builder,
                             "(sa{sv})",
                             "crypttab", &dict_builder);
      if (passphrase_contents != NULL)
        {
          memset (passphrase_contents, '\0', passphrase_contents_length);
          g_free (passphrase_contents);
        }
    }
  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
  g_list_free (entries);

  ret = g_variant_builder_end (&builder);

 out:
  return ret;
}

static void
update_configuration (UDisksLinuxBlock  *block,
                      UDisksDaemon      *daemon)
{
  GVariant *configuration;
  GError *error;

  error = NULL;
  configuration = calculate_configuration (block, daemon, FALSE, &error);
  if (configuration == NULL)
    {
      udisks_warning ("Error loading configuration: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      configuration = g_variant_new ("a(sa{sv})", NULL);
    }
  udisks_block_set_configuration (UDISKS_BLOCK (block), configuration);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_block_update:
 * @block: A #UDisksLinuxBlock.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_block_update (UDisksLinuxBlock        *block,
                           UDisksLinuxBlockObject *object)
{
  UDisksBlock *iface = UDISKS_BLOCK (block);
  UDisksDaemon *daemon;
  GDBusObjectManagerServer *object_manager;
  UDisksLinuxDevice *device;
  GUdevDeviceNumber dev;
  gchar *drive_object_path;
  UDisksDrive *drive;
  gchar *s;
  const gchar *device_file;
  const gchar *const *symlinks;
  const gchar *preferred_device_file;
  const gchar *id_device_file;
  gboolean media_removable = FALSE;
  guint64 size;
  gboolean media_available;
  gboolean media_change_detected;
  gboolean read_only;
  guint n;

  drive = NULL;

  device = udisks_linux_block_object_get_device (object);
  if (device == NULL)
    goto out;

  daemon = udisks_linux_block_object_get_daemon (object);
  object_manager = udisks_daemon_get_object_manager (daemon);

  dev = g_udev_device_get_device_number (device->udev_device);
  device_file = g_udev_device_get_device_file (device->udev_device);
  symlinks = g_udev_device_get_device_file_symlinks (device->udev_device);

  udisks_block_set_device (iface, device_file);
  udisks_block_set_symlinks (iface, symlinks);
  udisks_block_set_device_number (iface, dev);

  size = udisks_daemon_util_block_get_size (device->udev_device,
                                            &media_available,
                                            &media_change_detected);
  udisks_block_set_size (iface, size);

  read_only = g_udev_device_get_sysfs_attr_as_boolean (device->udev_device, "ro");
  if (!read_only && g_str_has_prefix (g_udev_device_get_name (device->udev_device), "sr"))
    read_only = TRUE;
  udisks_block_set_read_only (iface, read_only);

  /* dm-crypt
   *
   * TODO: this might not be the best way to determine if the device-mapper device
   *       is a dm-crypt device.. but unfortunately device-mapper keeps all this stuff
   *       in user-space and wants you to use libdevmapper to obtain it...
   */
  udisks_block_set_crypto_backing_device (iface, "/");
  if (g_str_has_prefix (g_udev_device_get_name (device->udev_device), "dm-"))
    {
      gchar *dm_uuid;
      dm_uuid = get_sysfs_attr (device->udev_device, "dm/uuid");
      if (dm_uuid != NULL && g_str_has_prefix (dm_uuid, "CRYPT-LUKS1"))
        {
          gchar **slaves;
          slaves = udisks_daemon_util_resolve_links (g_udev_device_get_sysfs_path (device->udev_device),
                                                     "slaves");
          if (g_strv_length (slaves) == 1)
            {
              gchar *slave_object_path;
              slave_object_path = find_block_device_by_sysfs_path (object_manager, slaves[0]);
              if (slave_object_path != NULL)
                {
                  udisks_block_set_crypto_backing_device (iface, slave_object_path);
                }
              g_free (slave_object_path);
            }
          g_strfreev (slaves);
        }
      g_free (dm_uuid);
    }

  /* Sort out preferred device... this is what UI shells should
   * display. We default to the block device name.
   *
   * This is mostly for things like device-mapper where device file is
   * a name of the form dm-%d and a symlink name conveys more
   * information.
   */
  preferred_device_file = NULL;
  if (g_str_has_prefix (device_file, "/dev/dm-"))
    {
      const gchar *dm_name;
      gchar *dm_name_dev_file = NULL;
      const gchar *dm_name_dev_file_as_symlink = NULL;

      dm_name = g_udev_device_get_property (device->udev_device, "DM_NAME");
      if (dm_name != NULL)
        dm_name_dev_file = g_strdup_printf ("/dev/mapper/%s", dm_name);
      for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
        {
          if (g_str_has_prefix (symlinks[n], "/dev/vg_"))
            {
              /* LVM2 */
              preferred_device_file = symlinks[n];
              break;
            }
          else if (g_strcmp0 (symlinks[n], dm_name_dev_file) == 0)
            {
              dm_name_dev_file_as_symlink = symlinks[n];
            }
        }
      /* fall back to /dev/mapper/$DM_NAME, if available as a symlink */
      if (preferred_device_file == NULL && dm_name_dev_file_as_symlink != NULL)
        preferred_device_file = dm_name_dev_file_as_symlink;
      g_free (dm_name_dev_file);
    }
  else if (g_str_has_prefix (device_file, "/dev/md"))
    {
      const gchar *md_name;

      md_name = g_udev_device_get_property (device->udev_device, "UDISKS_MD_NAME");
      if (md_name != NULL)
        {
          gchar *md_name_dev_file = NULL;
          const gchar *sep;
          const gchar *md_name_dev_file_as_symlink = NULL;
          /* skip homehost */
          sep = strstr (md_name, ":");
          if (sep != NULL)
            md_name_dev_file = g_strdup_printf ("/dev/md/%s", sep + 1);
          else
            md_name_dev_file = g_strdup_printf ("/dev/md/%s", md_name);
          for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
            {
              if (g_str_has_prefix (symlinks[n], md_name_dev_file))
                {
                  md_name_dev_file_as_symlink = symlinks[n];
                }
            }
          /* Use /dev/md/$MD_NAME, if it's available as a symlink */
          if (preferred_device_file == NULL && md_name_dev_file_as_symlink != NULL)
            preferred_device_file = md_name_dev_file_as_symlink;
          g_free (md_name_dev_file);
        }
    }
  /* fallback to the device name */
  if (preferred_device_file == NULL)
    preferred_device_file = g_udev_device_get_device_file (device->udev_device);
  udisks_block_set_preferred_device (iface, preferred_device_file);

  /* Determine the drive this block device belongs to
   *
   * TODO: if this is slow we could have a cache or ensure that we
   * only do this once or something else
   */
  drive_object_path = find_drive (object_manager, device->udev_device, &drive);
  if (drive_object_path != NULL)
    {
      udisks_block_set_drive (iface, drive_object_path);
      g_free (drive_object_path);
    }
  else
    {
      udisks_block_set_drive (iface, "/");
    }

  if (drive != NULL)
    media_removable = udisks_drive_get_media_removable (drive);

  id_device_file = NULL;
  if (media_removable)
    {
      /* Drive with removable media: determine id by finding a
       * suitable /dev/disk/by-uuid symlink (fall back to
       * /dev/disk/by-label)
       *
       * TODO: add features to ata_id / cdrom_id in systemd to extract
       *       medium identiers (at optical discs have these) and add
       *       udev rules to create symlinks in something like
       *       /dev/disk/by-medium. Then use said symlinks to for the
       *       id_device_file
       */
      for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
        {
          if (g_str_has_prefix (symlinks[n], "/dev/disk/by-uuid/"))
            {
              id_device_file = symlinks[n];
              break;
            }
          else if (g_str_has_prefix (symlinks[n], "/dev/disk/by-label/"))
            {
              id_device_file = symlinks[n];
            }
        }
    }
  else
    {
      /* Drive without removable media: determine id by finding a
       * suitable /dev/disk/by-id symlink
       */
      for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
        {
          if (g_str_has_prefix (symlinks[n], "/dev/disk/by-id/"))
            {
              id_device_file = symlinks[n];
              break;
            }
        }
    }
  if (id_device_file != NULL)
    {
      gchar *id = g_strdup (id_device_file + strlen ("/dev/disk/"));
      for (n = 0; id[n] != '\0'; n++)
        {
          if (id[n] == '/' || id[n] == ' ')
            id[n] = '-';
        }
      udisks_block_set_id (iface, id);
      g_free (id);
    }
  else
    {
      udisks_block_set_id (iface, NULL);
    }

  udisks_block_set_id_usage (iface, g_udev_device_get_property (device->udev_device, "ID_FS_USAGE"));
  udisks_block_set_id_type (iface, g_udev_device_get_property (device->udev_device, "ID_FS_TYPE"));
  s = udisks_decode_udev_string (g_udev_device_get_property (device->udev_device, "ID_FS_VERSION"));
  udisks_block_set_id_version (iface, s);
  g_free (s);
  s = udisks_decode_udev_string (g_udev_device_get_property (device->udev_device, "ID_FS_LABEL_ENC"));
  udisks_block_set_id_label (iface, s);
  g_free (s);
  s = udisks_decode_udev_string (g_udev_device_get_property (device->udev_device, "ID_FS_UUID_ENC"));
  udisks_block_set_id_uuid (iface, s);
  g_free (s);

  update_hints (block, device, drive);
  update_configuration (block, daemon);
  update_mdraid (block, device, drive, object_manager);

 out:
  if (device != NULL)
    g_object_unref (device);
  if (drive != NULL)
    g_object_unref (drive);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_get_secret_configuration (UDisksBlock           *_block,
                                 GDBusMethodInvocation *invocation,
                                 GVariant              *options)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (_block);
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon;
  GVariant *configuration;
  GError *error;

  error = NULL;
  object = udisks_daemon_util_dup_object (block, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (object);

  error = NULL;
  configuration = calculate_configuration (block, daemon, TRUE, &error);
  if (configuration == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    NULL,
                                                    "org.freedesktop.udisks2.read-system-configuration-secrets",
                                                    options,
                                                    /* Translators: This is shown in an authentcation dialog when
                                                     * the user is editing settings that involve system-level
                                                     * passwords and secrets
                                                     */
                                                    N_("Authentication is required to read system-level secrets"),
                                                    invocation))
    {
      g_variant_unref (configuration);
      goto out;
    }

  udisks_block_complete_get_secret_configuration (UDISKS_BLOCK (block),
                                                  invocation,
                                                  configuration); /* consumes floating ref */

 out:
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
escape_fstab (const gchar *source)
{
  GString *s;
  guint n;
  s = g_string_new (NULL);
  for (n = 0; source[n] != '\0'; n++)
    {
      switch (source[n])
        {
        case ' ':
        case '\t':
        case '\n':
        case '\\':
          g_string_append_printf (s, "\\%03o", source[n]);
          break;

        default:
          g_string_append_c (s, source[n]);
          break;
        }
    }
  return g_string_free (s, FALSE);
}

/* based on g_strcompress() */
static gchar *
unescape_fstab (const gchar *source)
{
  const gchar *p = source, *octal;
  gchar *dest = g_malloc (strlen (source) + 1);
  gchar *q = dest;

  while (*p)
    {
      if (*p == '\\')
        {
          p++;
          switch (*p)
            {
            case '\0':
              udisks_warning ("unescape_fstab: trailing \\");
              goto out;
            case '0':  case '1':  case '2':  case '3':  case '4':
            case '5':  case '6':  case '7':
              *q = 0;
              octal = p;
              while ((p < octal + 3) && (*p >= '0') && (*p <= '7'))
                {
                  *q = (*q * 8) + (*p - '0');
                  p++;
                }
              q++;
              p--;
              break;
            default:            /* Also handles \" and \\ */
              *q++ = *p;
              break;
            }
        }
      else
        *q++ = *p;
      p++;
    }
out:
  *q = 0;

  return dest;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
add_remove_fstab_entry (GVariant  *remove,
                        GVariant  *add,
                        GError   **error)
{
  struct mntent mntent_remove;
  struct mntent mntent_add;
  gboolean ret;
  gchar *contents;
  gchar **lines;
  GString *str;
  gboolean removed;
  guint n;

  contents = NULL;
  lines = NULL;
  str = NULL;
  ret = FALSE;

  if (remove != NULL)
    {
      if (!g_variant_lookup (remove, "fsname", "^&ay", &mntent_remove.mnt_fsname) ||
          !g_variant_lookup (remove, "dir", "^&ay", &mntent_remove.mnt_dir) ||
          !g_variant_lookup (remove, "type", "^&ay", &mntent_remove.mnt_type) ||
          !g_variant_lookup (remove, "opts", "^&ay", &mntent_remove.mnt_opts) ||
          !g_variant_lookup (remove, "freq", "i", &mntent_remove.mnt_freq) ||
          !g_variant_lookup (remove, "passno", "i", &mntent_remove.mnt_passno))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Missing fsname, dir, type, opts, freq or passno parameter in entry to remove");
          goto out;
        }
    }

  if (add != NULL)
    {
      if (!g_variant_lookup (add, "fsname", "^&ay", &mntent_add.mnt_fsname) ||
          !g_variant_lookup (add, "dir", "^&ay", &mntent_add.mnt_dir) ||
          !g_variant_lookup (add, "type", "^&ay", &mntent_add.mnt_type) ||
          !g_variant_lookup (add, "opts", "^&ay", &mntent_add.mnt_opts) ||
          !g_variant_lookup (add, "freq", "i", &mntent_add.mnt_freq) ||
          !g_variant_lookup (add, "passno", "i", &mntent_add.mnt_passno))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Missing fsname, dir, type, opts, freq or passno parameter in entry to add");
          goto out;
        }

      if (strlen (mntent_add.mnt_opts) == 0)
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "opts must not be blank");
          goto out;
        }
    }

  if (!g_file_get_contents ("/etc/fstab",
                            &contents,
                            NULL,
                            error))
    goto out;

  lines = g_strsplit (contents, "\n", 0);

  str = g_string_new (NULL);
  removed = FALSE;
  for (n = 0; lines != NULL && lines[n] != NULL; n++)
    {
      const gchar *line = lines[n];
      if (strlen (line) == 0 && lines[n+1] == NULL)
        break;
      if (remove != NULL && !removed)
        {
          gchar parsed_fsname[512];
          gchar parsed_dir[512];
          gchar parsed_type[512];
          gchar parsed_opts[512];
          gint parsed_freq;
          gint parsed_passno;
          if (sscanf (line, "%511s %511s %511s %511s %d %d",
                      parsed_fsname,
                      parsed_dir,
                      parsed_type,
                      parsed_opts,
                      &parsed_freq,
                      &parsed_passno) == 6)
            {
              gchar *unescaped_fsname = unescape_fstab (parsed_fsname);
              gchar *unescaped_dir = unescape_fstab (parsed_dir);
              gchar *unescaped_type = unescape_fstab (parsed_type);
              gchar *unescaped_opts = unescape_fstab (parsed_opts);
              gboolean matches = FALSE;
              if (g_strcmp0 (unescaped_fsname,   mntent_remove.mnt_fsname) == 0 &&
                  g_strcmp0 (unescaped_dir,      mntent_remove.mnt_dir) == 0 &&
                  g_strcmp0 (unescaped_type,     mntent_remove.mnt_type) == 0 &&
                  g_strcmp0 (unescaped_opts,     mntent_remove.mnt_opts) == 0 &&
                  parsed_freq ==      mntent_remove.mnt_freq &&
                  parsed_passno ==    mntent_remove.mnt_passno)
                {
                  matches = TRUE;
                }
              g_free (unescaped_fsname);
              g_free (unescaped_dir);
              g_free (unescaped_type);
              g_free (unescaped_opts);
              if (matches)
                {
                  removed = TRUE;
                  continue;
                }
            }
        }
      g_string_append (str, line);
      g_string_append_c (str, '\n');
    }

  if (remove != NULL && !removed)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Didn't find entry to remove");
      goto out;
    }

  if (add != NULL)
    {
      gchar *escaped_fsname = escape_fstab (mntent_add.mnt_fsname);
      gchar *escaped_dir = escape_fstab (mntent_add.mnt_dir);
      gchar *escaped_type = escape_fstab (mntent_add.mnt_type);
      gchar *escaped_opts = escape_fstab (mntent_add.mnt_opts);
      g_string_append_printf (str, "%s %s %s %s %d %d\n",
                              escaped_fsname,
                              escaped_dir,
                              escaped_type,
                              escaped_opts,
                              mntent_add.mnt_freq,
                              mntent_add.mnt_passno);
      g_free (escaped_fsname);
      g_free (escaped_dir);
      g_free (escaped_type);
      g_free (escaped_opts);
    }

  if (!udisks_daemon_util_file_set_contents ("/etc/fstab",
                                             str->str,
                                             -1,
                                             0644, /* mode to use if non-existant */
                                             error) != 0)
    goto out;

  ret = TRUE;

 out:
  g_strfreev (lines);
  g_free (contents);
  if (str != NULL)
    g_string_free (str, TRUE);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
has_whitespace (const gchar *s)
{
  guint n;
  g_return_val_if_fail (s != NULL, TRUE);
  for (n = 0; s[n] != '\0'; n++)
    if (g_ascii_isspace (s[n]))
      return TRUE;
  return FALSE;
}

static gboolean
add_remove_crypttab_entry (GVariant  *remove,
                           GVariant  *add,
                           GError   **error)
{
  const gchar *remove_name = NULL;
  const gchar *remove_device = NULL;
  const gchar *remove_passphrase_path = NULL;
  const gchar *remove_options = NULL;
  const gchar *add_name = NULL;
  const gchar *add_device = NULL;
  const gchar *add_passphrase_path = NULL;
  const gchar *add_options = NULL;
  const gchar *add_passphrase_contents = NULL;
  gboolean ret;
  gchar *contents;
  gchar **lines;
  GString *str;
  gboolean removed;
  guint n;

  contents = NULL;
  lines = NULL;
  str = NULL;
  ret = FALSE;

  if (remove != NULL)
    {
      if (!g_variant_lookup (remove, "name", "^&ay", &remove_name) ||
          !g_variant_lookup (remove, "device", "^&ay", &remove_device) ||
          !g_variant_lookup (remove, "passphrase-path", "^&ay", &remove_passphrase_path) ||
          !g_variant_lookup (remove, "options", "^&ay", &remove_options))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Missing name, device, passphrase-path, options or parameter in entry to remove");
          goto out;
        }
    }

  if (add != NULL)
    {
      if (!g_variant_lookup (add, "name", "^&ay", &add_name) ||
          !g_variant_lookup (add, "device", "^&ay", &add_device) ||
          !g_variant_lookup (add, "passphrase-path", "^&ay", &add_passphrase_path) ||
          !g_variant_lookup (add, "options", "^&ay", &add_options) ||
          !g_variant_lookup (add, "passphrase-contents", "^&ay", &add_passphrase_contents))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Missing name, device, passphrase-path, options or passphrase-contents parameter in entry to add");
          goto out;
        }

      /* reject strings with whitespace in them */
      if (has_whitespace (add_name) ||
          has_whitespace (add_device) ||
          has_whitespace (add_passphrase_path) ||
          has_whitespace (add_options))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "One of name, device, passphrase-path or options parameter are invalid (whitespace)");
          goto out;
        }
    }

  if (!g_file_get_contents ("/etc/crypttab",
                            &contents,
                            NULL,
                            error))
    goto out;

  lines = g_strsplit (contents, "\n", 0);

  str = g_string_new (NULL);
  removed = FALSE;
  for (n = 0; lines != NULL && lines[n] != NULL; n++)
    {
      const gchar *line = lines[n];
      if (strlen (line) == 0 && lines[n+1] == NULL)
        break;
      if (remove != NULL && !removed)
        {
          gchar parsed_name[512];
          gchar parsed_device[512];
          gchar parsed_passphrase_path[512];
          gchar parsed_options[512];
          guint num_parsed;

          num_parsed = sscanf (line, "%511s %511s %511s %511s",
                               parsed_name, parsed_device, parsed_passphrase_path, parsed_options);
          if (num_parsed >= 2)
            {
              if (num_parsed < 3 || g_strcmp0 (parsed_passphrase_path, "none") == 0)
                strcpy (parsed_passphrase_path, "");
              if (num_parsed < 4)
                strcpy (parsed_options, "");
              if (g_strcmp0 (parsed_name,            remove_name) == 0 &&
                  g_strcmp0 (parsed_device,          remove_device) == 0 &&
                  g_strcmp0 (parsed_passphrase_path, remove_passphrase_path) == 0 &&
                  g_strcmp0 (parsed_options,         remove_options) == 0)
                {
                  /* Nuke passphrase file */
                  if (strlen (remove_passphrase_path) > 0 && !g_str_has_prefix (remove_passphrase_path, "/dev"))
                    {
                      /* Is this exploitable? No, 1. the user would have to control
                       * the /etc/crypttab file for us to delete it; and 2. editing the
                       * /etc/crypttab file requires a polkit authorization that can't
                       * be retained (e.g. the user is always asked for the password)..
                       */
                      if (unlink (remove_passphrase_path) != 0)
                        {
                          g_set_error (error,
                                       UDISKS_ERROR,
                                       UDISKS_ERROR_FAILED,
                                       "Error deleting file `%s' with passphrase",
                                       remove_passphrase_path);
                          goto out;
                        }
                    }
                  removed = TRUE;
                  continue;
                }
            }
        }
      g_string_append (str, line);
      g_string_append_c (str, '\n');
    }

  if (remove != NULL && !removed)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Didn't find entry to remove");
      goto out;
    }

  if (add != NULL)
    {
      /* First write add_passphrase_content to add_passphrase_path,
       * if applicable..
       *
       * Is this exploitable? No, because editing the /etc/crypttab
       * file requires a polkit authorization that can't be retained
       * (e.g. the user is always asked for the password)...
       *
       * Just to be on the safe side we only allow writing into the
       * directory /etc/luks-keys if create a _new_ entry.
       */
      if (strlen (add_passphrase_path) > 0)
        {
          gchar *filename;
          if (g_strcmp0 (add_passphrase_path, remove_passphrase_path) == 0)
            {
              filename = g_strdup (add_passphrase_path);
            }
          else
            {
              if (!g_str_has_prefix (add_passphrase_path, "/etc/luks-keys/"))
                {
                  g_set_error (error,
                               UDISKS_ERROR,
                               UDISKS_ERROR_FAILED,
                               "Crypttab passphrase file can only be created in the /etc/luks-keys directory");
                  goto out;
                }
              /* ensure the directory exists */
              if (g_mkdir_with_parents ("/etc/luks-keys", 0700) != 0)
                {
                  g_set_error (error,
                               UDISKS_ERROR,
                               UDISKS_ERROR_FAILED,
                               "Error creating /etc/luks-keys directory: %m");
                  goto out;
                }
              /* avoid symlink attacks */
              filename = g_strdup_printf ("/etc/luks-keys/%s", strrchr (add_passphrase_path, '/') + 1);
            }

          /* Bail if the requested file already exists */
          if (g_file_test (filename, G_FILE_TEST_EXISTS))
            {
                  g_set_error (error,
                               UDISKS_ERROR,
                               UDISKS_ERROR_FAILED,
                               "Refusing to overwrite existing file %s",
                               filename);
                  g_free (filename);
                  goto out;
            }

          if (!udisks_daemon_util_file_set_contents (filename,
                                                     add_passphrase_contents,
                                                     -1,
                                                     0600, /* mode to use if non-existant */
                                                     error))
            {
              g_free (filename);
              goto out;
            }
          g_free (filename);
        }
      g_string_append_printf (str, "%s %s %s %s\n",
                              add_name,
                              add_device,
                              strlen (add_passphrase_path) > 0 ? add_passphrase_path : "none",
                              add_options);
    }

  if (!udisks_daemon_util_file_set_contents ("/etc/crypttab",
                                             str->str,
                                             -1,
                                             0600, /* mode to use if non-existant */
                                             error) != 0)
    goto out;

  ret = TRUE;

 out:
  g_strfreev (lines);
  g_free (contents);
  if (str != NULL)
    g_string_free (str, TRUE);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_add_configuration_item (UDisksBlock           *_block,
                               GDBusMethodInvocation *invocation,
                               GVariant              *item,
                               GVariant              *options)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (_block);
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon;
  const gchar *type;
  GVariant *details;
  GError *error;

  error = NULL;
  object = udisks_daemon_util_dup_object (block, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (object);

  g_variant_get (item, "(&s@a{sv})", &type, &details);
  if (g_strcmp0 (type, "fstab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        /* Translators: shown in authentication dialog - do not translate /etc/fstab */
                                                        N_("Authentication is required to add an entry to the /etc/fstab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_fstab_entry (NULL, details, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_complete_add_configuration_item (UDISKS_BLOCK (block), invocation);
    }
  else if (g_strcmp0 (type, "crypttab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        /* Translators: shown in authentication dialog - do not tranlsate /etc/crypttab */
                                                        N_("Authentication is required to add an entry to the /etc/crypttab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_crypttab_entry (NULL, details, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_complete_add_configuration_item (UDISKS_BLOCK (block), invocation);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Only /etc/fstab or /etc/crypttab items can be added");
      goto out;
    }

 out:
  g_variant_unref (details);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_remove_configuration_item (UDisksBlock           *_block,
                                  GDBusMethodInvocation *invocation,
                                  GVariant              *item,
                                  GVariant              *options)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (_block);
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon;
  const gchar *type;
  GVariant *details;
  GError *error;

  error = NULL;
  object = udisks_daemon_util_dup_object (block, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (object);

  g_variant_get (item, "(&s@a{sv})", &type, &details);
  if (g_strcmp0 (type, "fstab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        /* Translators: shown in authentication dialog - do not translate /etc/fstab */
                                                        N_("Authentication is required to remove an entry from /etc/fstab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_fstab_entry (details, NULL, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_complete_remove_configuration_item (UDISKS_BLOCK (block), invocation);
    }
  else if (g_strcmp0 (type, "crypttab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        /* Translators: shown in authentication dialog - do not translate /etc/crypttab */
                                                        N_("Authentication is required to remove an entry from the /etc/crypttab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_crypttab_entry (details, NULL, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_complete_remove_configuration_item (UDISKS_BLOCK (block), invocation);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Only fstab or crypttab items can be removed");
      goto out;
    }

 out:
  g_variant_unref (details);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_update_configuration_item (UDisksBlock           *_block,
                                  GDBusMethodInvocation *invocation,
                                  GVariant              *old_item,
                                  GVariant              *new_item,
                                  GVariant              *options)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (_block);
  UDisksLinuxBlockObject *object;
  UDisksDaemon *daemon;
  const gchar *old_type;
  const gchar *new_type;
  GVariant *old_details;
  GVariant *new_details;
  GError *error;

  error = NULL;
  object = udisks_daemon_util_dup_object (block, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (object);

  g_variant_get (old_item, "(&s@a{sv})", &old_type, &old_details);
  g_variant_get (new_item, "(&s@a{sv})", &new_type, &new_details);
  if (g_strcmp0 (old_type, new_type) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "old and new item are not of the same type");
      goto out;
    }

  if (g_strcmp0 (old_type, "fstab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        /* Translators: shown in authentication dialog - do not translate /etc/fstab */
                                                        N_("Authentication is required to modify the /etc/fstab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_fstab_entry (old_details, new_details, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_complete_update_configuration_item (UDISKS_BLOCK (block), invocation);
    }
  else if (g_strcmp0 (old_type, "crypttab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        /* Translators: shown in authentication dialog - do not translate /etc/crypttab */
                                                        N_("Authentication is required to modify the /etc/crypttab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_crypttab_entry (old_details, new_details, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_complete_update_configuration_item (UDISKS_BLOCK (block), invocation);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Only fstab or crypttab items can be updated");
      goto out;
    }

 out:
  g_variant_unref (new_details);
  g_variant_unref (old_details);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
subst_str (const gchar *str,
           const gchar *from,
           const gchar *to)
{
    gchar **parts;
    gchar *result;

    parts = g_strsplit (str, from, 0);
    result = g_strjoinv (to, parts);
    g_strfreev (parts);
    return result;
}


static gchar *
subst_str_and_escape (const gchar *str,
                      const gchar *from,
                      const gchar *to)
{
  gchar *quoted_and_escaped;
  gchar *ret;
  quoted_and_escaped = udisks_daemon_util_escape_and_quote (to);
  ret = subst_str (str, from, quoted_and_escaped);
  g_free (quoted_and_escaped);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  UDisksObject *object;
  const gchar  *type;
} FormatWaitData;

/* ---------------------------------------------------------------------------------------------------- */

static UDisksObject *
wait_for_filesystem (UDisksDaemon *daemon,
                     gpointer      user_data)
{
  FormatWaitData *data = user_data;
  UDisksObject *ret = NULL;
  UDisksBlock *block = NULL;
  UDisksPartitionTable *partition_table = NULL;
  gchar *id_type = NULL;
  gchar *partition_table_type = NULL;

  block = udisks_object_get_block (data->object);
  if (block == NULL)
    goto out;

  partition_table = udisks_object_get_partition_table (data->object);

  id_type = udisks_block_dup_id_type (block);

  if (g_strcmp0 (data->type, "empty") == 0)
    {
      if ((id_type == NULL || g_strcmp0 (id_type, "") == 0) && partition_table == NULL)
        {
          ret = g_object_ref (data->object);
          goto out;
        }
    }

  if (g_strcmp0 (id_type, data->type) == 0)
    {
      ret = g_object_ref (data->object);
      goto out;
    }

  if (partition_table != NULL)
    {
      partition_table_type = udisks_partition_table_dup_type_ (partition_table);
      if (g_strcmp0 (partition_table_type, data->type) == 0)
        {
          ret = g_object_ref (data->object);
          goto out;
        }
    }

 out:
  g_free (partition_table_type);
  g_free (id_type);
  g_clear_object (&partition_table);
  g_clear_object (&block);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksObject *
wait_for_luks_uuid (UDisksDaemon *daemon,
                    gpointer      user_data)
{
  FormatWaitData *data = user_data;
  UDisksObject *ret = NULL;
  UDisksBlock *block = NULL;

  block = udisks_object_get_block (data->object);
  if (block == NULL)
    goto out;

  if (g_strcmp0 (udisks_block_get_id_type (block), "crypto_LUKS") != 0)
    goto out;

  ret = g_object_ref (data->object);

 out:
  g_clear_object (&block);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksObject *
wait_for_luks_cleartext (UDisksDaemon *daemon,
                         gpointer      user_data)
{
  FormatWaitData *data = user_data;
  UDisksObject *ret = NULL;
  GList *objects, *l;

  objects = udisks_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block = NULL;

      block = udisks_object_get_block (object);
      if (block != NULL)
        {
          if (g_strcmp0 (udisks_block_get_crypto_backing_device (block),
                         g_dbus_object_get_object_path (G_DBUS_OBJECT (data->object))) == 0)
            {
              g_object_unref (block);
              ret = g_object_ref (object);
              goto out;
            }
          g_object_unref (block);
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
erase_ata_device (UDisksBlock  *block,
                  UDisksObject *object,
                  UDisksDaemon *daemon,
                  uid_t         caller_uid,
                  gboolean      enhanced,
                  GError      **error)
{
  gboolean ret = FALSE;
  UDisksObject *drive_object = NULL;
  UDisksDriveAta *ata = NULL;

  drive_object = udisks_daemon_find_object (daemon, udisks_block_get_drive (block));
  if (drive_object == NULL)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED, "No drive object");
      goto out;
    }
  ata = udisks_object_get_drive_ata (drive_object);
  if (ata == NULL)
    {
      g_set_error (error, UDISKS_ERROR, UDISKS_ERROR_FAILED, "Drive is not an ATA drive");
      goto out;
    }

  /* sleep a tiny bit here to avoid the secure erase code racing with
   * programs spawned by udev
   */
  g_usleep (500 * 1000);

  ret = udisks_linux_drive_ata_secure_erase_sync (UDISKS_LINUX_DRIVE_ATA (ata),
                                                  caller_uid,
                                                  enhanced,
                                                  error);

 out:
  g_clear_object (&ata);
  g_clear_object (&drive_object);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

#define ERASE_SIZE (1 * 1024*1024)

static gboolean
erase_device (UDisksBlock  *block,
              UDisksObject *object,
              UDisksDaemon *daemon,
              uid_t         caller_uid,
              const gchar  *erase_type,
              GError      **error)
{
  gboolean ret = FALSE;
  const gchar *device_file = NULL;
  UDisksBaseJob *job = NULL;
  gint fd = -1;
  guint64 size;
  guint64 pos;
  guchar *buf = NULL;
  gint64 time_of_last_signal;
  GError *local_error = NULL;

  if (g_strcmp0 (erase_type, "ata-secure-erase") == 0)
    {
      ret = erase_ata_device (block, object, daemon, caller_uid, FALSE, error);
      goto out;
    }
  else if (g_strcmp0 (erase_type, "ata-secure-erase-enhanced") == 0)
    {
      ret = erase_ata_device (block, object, daemon, caller_uid, TRUE, error);
      goto out;
    }
  else if (g_strcmp0 (erase_type, "zero") != 0)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Unknown or unsupported erase type `%s'",
                   erase_type);
      goto out;
    }

  device_file = udisks_block_get_device (block);
  fd = open (device_file, O_WRONLY | O_SYNC | O_EXCL);
  if (fd == -1)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error opening device %s: %m", device_file);
      goto out;
    }

  job = udisks_daemon_launch_simple_job (daemon, object, "format-erase", caller_uid, NULL);
  udisks_base_job_set_auto_estimate (UDISKS_BASE_JOB (job), TRUE);
  udisks_job_set_progress_valid (UDISKS_JOB (job), TRUE);

  if (ioctl (fd, BLKGETSIZE64, &size) != 0)
    {
      g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                   "Error doing BLKGETSIZE64 iotctl on %s: %m", device_file);
      goto out;
    }

  udisks_job_set_bytes (UDISKS_JOB (job), size);

  buf = g_new0 (guchar, ERASE_SIZE);
  pos = 0;
  time_of_last_signal = g_get_monotonic_time ();
  while (pos < size)
    {
      size_t to_write;
      ssize_t num_written;
      gint64 now;

      to_write = MIN (size - pos, ERASE_SIZE);
    again:
      num_written = write (fd, buf, to_write);
      if (num_written == -1 || num_written == 0)
        {
          if (errno == EINTR)
            goto again;
          g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                       "Error writing %d bytes to %s: %m",
                       (gint) to_write, device_file);
          goto out;
        }
      pos += num_written;

      if (g_cancellable_is_cancelled (udisks_base_job_get_cancellable (job)))
        {
          g_set_error (&local_error, UDISKS_ERROR, UDISKS_ERROR_CANCELLED,
                       "Job was canceled");
          goto out;
        }

      /* only emit D-Bus signal at most once a second */
      now = g_get_monotonic_time ();
      if (now - time_of_last_signal > G_USEC_PER_SEC)
        {
          /* TODO: estimation etc. */
          udisks_job_set_progress (UDISKS_JOB (job), ((gdouble) pos) / size);
          time_of_last_signal = now;
        }
    }

  ret = TRUE;

 out:
  if (job != NULL)
    {
      if (local_error != NULL)
        udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, local_error->message);
      else
        udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, "");
    }
  if (local_error != NULL)
    g_propagate_error (error, local_error);
  g_free (buf);
  if (fd != -1)
    close (fd);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static const struct
{
  const gchar *table_type;
  const gchar *id_type;
  const gchar *partition_type;
} partition_types_by_id[] = {
  {"dos", "vfat",         "0x0c"},
  {"dos", "ntfs",         "0x07"},
  {"dos", "exfat",        "0x0c"},
  {"dos", "swap",         "0x82"},
  {"dos", "ext2",         "0x83"},
  {"dos", "ext3",         "0x83"},
  {"dos", "ext4",         "0x83"},
  {"dos", "xfs",          "0x83"},
  {"dos", "btrfs",        "0x83"},
  {"dos", "crypto_LUKS",  "0x83"}, /* TODO: perhaps default to LUKS-specific type */

  {"gpt", "vfat",         "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"}, /* Microsoft Basic Data */
  {"gpt", "ntfs",         "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"},
  {"gpt", "exfat",        "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"},
  {"gpt", "swap",         "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f"}, /* Linux Swap */
  {"gpt", "ext2",         "0fc63daf-8483-4772-8e79-3d69d8477de4"}, /* Linux Filesystem */
  {"gpt", "ext3",         "0fc63daf-8483-4772-8e79-3d69d8477de4"},
  {"gpt", "ext4",         "0fc63daf-8483-4772-8e79-3d69d8477de4"},
  {"gpt", "xfs",          "0fc63daf-8483-4772-8e79-3d69d8477de4"},
  {"gpt", "btrfs",        "0fc63daf-8483-4772-8e79-3d69d8477de4"},
  {"gpt", "crypto_LUKS",  "0fc63daf-8483-4772-8e79-3d69d8477de4"}, /* TODO: perhaps default to LUKS-specific type */
};


/* may return NULL if nothing suitable was found */
static const gchar *
determine_partition_type_for_id (const gchar *table_type,
                                 const gchar *id_type)
{
  const gchar *ret = NULL;
  guint n;

  for (n = 0; n < G_N_ELEMENTS (partition_types_by_id); n++)
    {
      if (g_strcmp0 (partition_types_by_id[n].table_type, table_type) == 0 &&
          g_strcmp0 (partition_types_by_id[n].id_type,    id_type) == 0)
        {
          ret = partition_types_by_id[n].partition_type;
          goto out;
        }
    }
 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_format (UDisksBlock           *block,
               GDBusMethodInvocation *invocation,
               const gchar           *type,
               GVariant              *options)
{
  FormatWaitData *wait_data = NULL;
  UDisksObject *object;
  UDisksPartition *partition = NULL;
  UDisksPartitionTable *partition_table = NULL;
  UDisksObject *cleartext_object = NULL;
  UDisksBlock *cleartext_block = NULL;
  UDisksLinuxDevice *udev_cleartext_device = NULL;
  UDisksBlock *block_to_mkfs = NULL;
  UDisksObject *object_to_mkfs = NULL;
  UDisksDaemon *daemon;
  UDisksState *state;
  const gchar *action_id;
  const gchar *message;
  const FSInfo *fs_info;
  gchar *command = NULL;
  gchar *tmp;
  gchar *error_message;
  GError *error;
  int status;
  uid_t caller_uid;
  gid_t caller_gid;
  pid_t caller_pid;
  gboolean take_ownership = FALSE;
  gchar *encrypt_passphrase = NULL;
  gchar *erase_type = NULL;
  gchar *mapped_name = NULL;
  const gchar *label = NULL;
  gchar *escaped_device = NULL;
  gboolean was_partitioned = FALSE;
  UDisksInhibitCookie *inhibit_cookie = NULL;
  gboolean no_block = FALSE;
  gboolean update_partition_type = FALSE;
  const gchar *partition_type = NULL;

  error = NULL;
  object = udisks_daemon_util_dup_object (block, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);
  command = NULL;
  error_message = NULL;

  g_variant_lookup (options, "take-ownership", "b", &take_ownership);
  g_variant_lookup (options, "encrypt.passphrase", "s", &encrypt_passphrase);
  g_variant_lookup (options, "erase", "s", &erase_type);
  g_variant_lookup (options, "no-block", "b", &no_block);
  g_variant_lookup (options, "update-partition-type", "b", &update_partition_type);

  partition = udisks_object_get_partition (object);
  if (partition != NULL)
    {
      UDisksObject *partition_table_object;
      partition_table_object = udisks_daemon_find_object (daemon, udisks_partition_get_table (partition));
      if (partition_table_object == NULL)
        {
          g_clear_object (&partition);
        }
      else
        {
          partition_table = udisks_object_get_partition_table (partition_table_object);
          g_clear_object (&partition_table_object);
        }
    }
  /* figure out partition type to set, if requested */
  if (update_partition_type && partition != NULL && partition_table != NULL)
    {
      partition_type = determine_partition_type_for_id (udisks_partition_table_get_type_ (partition_table),
                                                        encrypt_passphrase != NULL ? "crypto_LUKS" : type);
    }

  error = NULL;
  if (!udisks_daemon_util_get_caller_pid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_pid,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon, invocation, NULL /* GCancellable */, &caller_uid, &caller_gid, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  if (g_strcmp0 (erase_type, "ata-secure-erase") == 0 ||
      g_strcmp0 (erase_type, "ata-secure-erase-enhanced") == 0)
    {
      /* Translators: Shown in authentication dialog when the user
       * requests erasing a hard disk using the SECURE ERASE UNIT
       * command.
       *
       * Do not translate $(drive), it's a placeholder and
       * will be replaced by the name of the drive/device in question
       */
      message = N_("Authentication is required to perform a secure erase of $(drive)");
      action_id = "org.freedesktop.udisks2.ata-secure-erase";
    }
  else
    {
      /* Translators: Shown in authentication dialog when formatting a
       * device. This includes both creating a filesystem or partition
       * table.
       *
       * Do not translate $(drive), it's a placeholder and will
       * be replaced by the name of the drive/device in question
       */
      message = N_("Authentication is required to format $(drive)");
      action_id = "org.freedesktop.udisks2.modify-device";
      if (!udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
        {
          if (udisks_block_get_hint_system (block))
            {
              action_id = "org.freedesktop.udisks2.modify-device-system";
            }
          else if (!udisks_daemon_util_on_same_seat (daemon, object, caller_pid))
            {
              action_id = "org.freedesktop.udisks2.modify-device-other-seat";
            }
        }
    }

  /* TODO: Consider just accepting any @type and just running "mkfs -t <type>".
   *       There are some obvious security implications by doing this, though
   */
  fs_info = get_fs_info (type);
  if (fs_info == NULL || fs_info->command_create_fs == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                   UDISKS_ERROR,
                   UDISKS_ERROR_NOT_SUPPORTED,
                   "Creation of file system type %s is not supported",
                   type);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  inhibit_cookie = udisks_daemon_util_inhibit_system_sync (N_("Formatting Device"));

  escaped_device = udisks_daemon_util_escape_and_quote (udisks_block_get_device (block));

  was_partitioned = (udisks_object_peek_partition_table (object) != NULL);

  /* return early, if requested */
  if (no_block)
    {
      udisks_block_complete_format (block, invocation);
      invocation = NULL;
    }

  /* First wipe the device... */
  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              object,
                                              "format-erase", caller_uid,
                                              NULL, /* cancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              &status,
                                              &error_message,
                                              NULL, /* input_string */
                                              "wipefs -a %s",
                                              escaped_device))
    {
      if (invocation != NULL)
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Error wiping device: %s",
                                               error_message);
      g_free (error_message);
      goto out;
    }
  /* ...then wait until this change has taken effect */
  wait_data = g_new0 (FormatWaitData, 1);
  wait_data->object = object;
  wait_data->type = "empty";
  udisks_linux_block_object_trigger_uevent (UDISKS_LINUX_BLOCK_OBJECT (object));
  if (was_partitioned)
    udisks_linux_block_object_reread_partition_table (UDISKS_LINUX_BLOCK_OBJECT (object));
  if (udisks_daemon_wait_for_object_sync (daemon,
                                          wait_for_filesystem,
                                          wait_data,
                                          NULL,
                                          15,
                                          &error) == NULL)
    {
      g_prefix_error (&error, "Error synchronizing after initial wipe: ");
      if (invocation != NULL)
        g_dbus_method_invocation_take_error (invocation, error);
      else
        g_clear_error (&error);
      goto out;
    }

  /* Erase the device, if requested
   *
   * (but not if using encryption, we want to erase the cleartext device, see below)
   */
  if (erase_type != NULL && encrypt_passphrase == NULL)
    {
      if (!erase_device (block, object, daemon, caller_uid, erase_type, &error))
        {
          g_prefix_error (&error, "Error erasing device: ");
          if (invocation != NULL)
            g_dbus_method_invocation_take_error (invocation, error);
          else
            g_clear_error (&error);
          goto out;
        }
    }

  /* And now create the desired filesystem */
  wait_data->type = type;

  if (encrypt_passphrase != NULL)
    {
      /* Create it */
      if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                                  object,
                                                  "format-mkfs", caller_uid,
                                                  NULL, /* cancellable */
                                                  0,    /* uid_t run_as_uid */
                                                  0,    /* uid_t run_as_euid */
                                                  &status,
                                                  &error_message,
                                                  encrypt_passphrase, /* input_string */
                                                  "cryptsetup luksFormat \"%s\"",
                                                  escaped_device))
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation,
                                                   UDISKS_ERROR,
                                                   UDISKS_ERROR_FAILED,
                                                   "Error creating LUKS device: %s",
                                                   error_message);
          g_free (error_message);
          goto out;
        }

      /* Wait for the UUID to be set */
      if (udisks_daemon_wait_for_object_sync (daemon,
                                              wait_for_luks_uuid,
                                              wait_data,
                                              NULL,
                                              30,
                                              &error) == NULL)
        {
          g_prefix_error (&error, "Error waiting for LUKS UUID: ");
          if (invocation != NULL)
            g_dbus_method_invocation_take_error (invocation, error);
          else
            g_clear_error (&error);
          goto out;
        }

      /* Open it */
      mapped_name = g_strdup_printf ("luks-%s", udisks_block_get_id_uuid (block));
      if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                                  object,
                                                  "format-mkfs", caller_uid,
                                                  NULL, /* cancellable */
                                                  0,    /* uid_t run_as_uid */
                                                  0,    /* uid_t run_as_euid */
                                                  &status,
                                                  &error_message,
                                                  encrypt_passphrase, /* input_string */
                                                  "cryptsetup luksOpen \"%s\" %s",
                                                  escaped_device,
                                                  mapped_name))
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation,
                                                   UDISKS_ERROR,
                                                   UDISKS_ERROR_FAILED,
                                                   "Error opening LUKS device: %s",
                                                   error_message);
          g_free (error_message);
          goto out;
        }

      /* Wait for it */
      cleartext_object = udisks_daemon_wait_for_object_sync (daemon,
                                                             wait_for_luks_cleartext,
                                                             wait_data,
                                                             NULL,
                                                             30,
                                                             &error);
      if (cleartext_object == NULL)
        {
          g_prefix_error (&error, "Error waiting for LUKS cleartext device: ");
          if (invocation != NULL)
            g_dbus_method_invocation_take_error (invocation, error);
          else
            g_clear_error (&error);
          goto out;
        }
      cleartext_block = udisks_object_get_block (cleartext_object);
      if (cleartext_block == NULL)
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                   "LUKS cleartext device does not have block interface");
          goto out;
        }

      /* update the unlocked-luks file */
      udev_cleartext_device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (cleartext_object));
      udisks_state_add_unlocked_luks (state,
                                      udisks_block_get_device_number (cleartext_block),
                                      udisks_block_get_device_number (block),
                                      g_udev_device_get_sysfs_attr (udev_cleartext_device->udev_device, "dm/uuid"),
                                      caller_uid);

      object_to_mkfs = cleartext_object;
      block_to_mkfs = cleartext_block;
    }
  else
    {
      object_to_mkfs = object;
      block_to_mkfs = block;
    }

  /* If using encryption, now erase the cleartext device (if requested) */
  if (encrypt_passphrase != NULL && erase_type != NULL)
    {
      if (!erase_device (block_to_mkfs, object_to_mkfs, daemon, caller_uid, erase_type, &error))
        {
          g_prefix_error (&error, "Error erasing cleartext device: ");
          if (invocation != NULL)
            g_dbus_method_invocation_take_error (invocation, error);
          else
            g_clear_error (&error);
          goto out;
        }
    }

  /* Set label, if needed */
  if (g_variant_lookup (options, "label", "&s", &label))
    {
      /* TODO: return an error if label is too long */
      if (strstr (fs_info->command_create_fs, "$LABEL") == NULL)
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation,
                                                   UDISKS_ERROR,
                                                   UDISKS_ERROR_NOT_SUPPORTED,
                                                   "File system type %s does not support labels",
                                                   type);
          goto out;
        }
    }

  /* Build and run mkfs shell command */
  tmp = subst_str_and_escape (fs_info->command_create_fs, "$DEVICE", udisks_block_get_device (block_to_mkfs));
  command = subst_str_and_escape (tmp, "$LABEL", label != NULL ? label : "");
  g_free (tmp);
  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              object_to_mkfs,
                                              "format-mkfs", caller_uid,
                                              NULL, /* cancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              &status,
                                              &error_message,
                                              NULL, /* input_string */
                                              "%s", command))
    {
      if (invocation != NULL)
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Error creating file system: %s",
                                               error_message);
      g_free (error_message);
      goto out;
    }
  /* The mkfs program may not generate all the uevents we need - so explicitly
   * trigger an event here
   */
  udisks_linux_block_object_trigger_uevent (UDISKS_LINUX_BLOCK_OBJECT (object_to_mkfs));
  wait_data->object = object_to_mkfs;
  if (udisks_daemon_wait_for_object_sync (daemon,
                                          wait_for_filesystem,
                                          wait_data,
                                          NULL,
                                          30,
                                          &error) == NULL)
    {
      g_prefix_error (&error,
                      "Error synchronizing after formatting with type `%s': ",
                      type);
      if (invocation != NULL)
        g_dbus_method_invocation_take_error (invocation, error);
      else
        g_clear_error (&error);
      goto out;
    }

  /* Change overship, if requested and supported */
  if (take_ownership && fs_info->supports_owners)
    {
      gchar tos_dir[256] = PACKAGE_LOCALSTATE_DIR "/run/udisks2/block-format-tos-XXXXXX";

      if (mkdtemp (tos_dir) == NULL)
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                   "Cannot create directory %s: %m", tos_dir);
          goto out;
        }
      if (mount (udisks_block_get_device (block_to_mkfs), tos_dir, type, 0, NULL) != 0)
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                   "Cannot mount %s at %s: %m",
                                                   udisks_block_get_device (block_to_mkfs),
                                                   tos_dir);
          if (rmdir (tos_dir) != 0)
            {
              udisks_warning ("Error removing directory %s: %m", tos_dir);
            }
          goto out;
        }
      if (chown (tos_dir, caller_uid, caller_gid) != 0)
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                   "Cannot chown %s to uid=%d and gid=%d: %m", tos_dir, caller_uid, caller_gid);
          if (umount (tos_dir) != 0)
            {
              udisks_warning ("Error unmounting directory %s: %m", tos_dir);
              goto out;
            }
          if (rmdir (tos_dir) != 0)
            {
              udisks_warning ("Error removing directory %s: %m", tos_dir);
            }
          goto out;
        }
      if (chmod (tos_dir, 0700) != 0)
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                   "Cannot chmod %s to mode 0700: %m", tos_dir);
          if (umount (tos_dir) != 0)
            {
              udisks_warning ("Error unmounting directory %s: %m", tos_dir);
              goto out;
            }
          if (rmdir (tos_dir) != 0)
            {
              udisks_warning ("Error removing directory %s: %m", tos_dir);
            }
          goto out;
        }

      if (umount (tos_dir) != 0)
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                   "Cannot unmount %s: %m", tos_dir);
          if (rmdir (tos_dir) != 0)
            {
              udisks_warning ("Error removing directory %s: %m", tos_dir);
            }
          goto out;
        }

      if (rmdir (tos_dir) != 0)
        {
          if (invocation != NULL)
            g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                                   "Cannot remove directory %s: %m", tos_dir);
          goto out;
        }
    }

  /* Set the partition type, if requested */
  if (partition_type != NULL && partition != NULL)
    {
      if (g_strcmp0 (udisks_partition_get_type_ (partition), partition_type) != 0)
        {
          if (!udisks_linux_partition_set_type_sync (UDISKS_LINUX_PARTITION (partition),
                                                     partition_type,
                                                     caller_uid,
                                                     NULL, /* cancellable */
                                                     &error))
            {
              g_prefix_error (&error, "Error setting partition type after formatting: ");
              g_dbus_method_invocation_take_error (invocation, error);
              goto out;
            }
        }
    }


  if (invocation != NULL)
    udisks_block_complete_format (block, invocation);

 out:
  udisks_daemon_util_uninhibit_system_sync (inhibit_cookie);
  g_free (escaped_device);
  g_free (mapped_name);
  g_free (command);
  g_free (erase_type);
  g_free (encrypt_passphrase);
  g_clear_object (&cleartext_object);
  g_clear_object (&cleartext_block);
  g_clear_object (&udev_cleartext_device);
  g_free (wait_data);
  g_clear_object (&partition_table);
  g_clear_object (&partition);
  g_clear_object (&object);
  return TRUE; /* returning true means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_open_for_backup (UDisksBlock           *block,
                        GDBusMethodInvocation *invocation,
                        GUnixFDList           *fd_list,
                        GVariant              *options)
{
  UDisksObject *object;
  UDisksDaemon *daemon;
  const gchar *action_id;
  const gchar *device;
  GUnixFDList *out_fd_list = NULL;
  GError *error;
  gint fd;

  error = NULL;
  object = udisks_daemon_util_dup_object (block, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));

  action_id = "org.freedesktop.udisks2.open-device";
  if (udisks_block_get_hint_system (block))
    action_id = "org.freedesktop.udisks2.open-device-system";

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    /* Translators: Shown in authentication dialog when creating a
                                                     * disk image file.
                                                     *
                                                     * Do not translate $(drive), it's a placeholder and will
                                                     * be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to open $(drive) for reading"),
                                                    invocation))
    goto out;


  device = udisks_block_get_device (UDISKS_BLOCK (block));

  fd = open (device, O_RDONLY | O_CLOEXEC | O_EXCL);
  if (fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Error opening %s: %m", device);
      goto out;
    }

  out_fd_list = g_unix_fd_list_new_from_array (&fd, 1);
  udisks_block_complete_open_for_backup (block, invocation, out_fd_list, g_variant_new_handle (0));

 out:
  g_clear_object (&out_fd_list);
  g_clear_object (&object);
  return TRUE; /* returning true means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_open_for_restore (UDisksBlock           *block,
                         GDBusMethodInvocation *invocation,
                         GUnixFDList           *fd_list,
                         GVariant              *options)
{
  UDisksObject *object;
  UDisksDaemon *daemon;
  const gchar *action_id;
  const gchar *device;
  GUnixFDList *out_fd_list = NULL;
  GError *error;
  gint fd;

  error = NULL;
  object = udisks_daemon_util_dup_object (block, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));

  action_id = "org.freedesktop.udisks2.open-device";
  if (udisks_block_get_hint_system (block))
    action_id = "org.freedesktop.udisks2.open-device-system";

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    /* Translators: Shown in authentication dialog when restoring
                                                     * from a disk image file.
                                                     *
                                                     * Do not translate $(drive), it's a placeholder and will
                                                     * be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to open $(drive) for writing"),
                                                    invocation))
    goto out;


  device = udisks_block_get_device (UDISKS_BLOCK (block));

  fd = open (device, O_WRONLY | O_SYNC | O_CLOEXEC | O_EXCL);
  if (fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Error opening %s: %m", device);
      goto out;
    }

  out_fd_list = g_unix_fd_list_new_from_array (&fd, 1);
  udisks_block_complete_open_for_restore (block, invocation, out_fd_list, g_variant_new_handle (0));

 out:
  g_clear_object (&out_fd_list);
  g_clear_object (&object);
  return TRUE; /* returning true means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_open_for_benchmark (UDisksBlock           *block,
                           GDBusMethodInvocation *invocation,
                           GUnixFDList           *fd_list,
                           GVariant              *options)
{
  UDisksObject *object;
  UDisksDaemon *daemon;
  const gchar *action_id;
  const gchar *device;
  GUnixFDList *out_fd_list = NULL;
  gboolean opt_writable = FALSE;
  GError *error;
  gint fd;
  gint open_flags;

  error = NULL;
  object = udisks_daemon_util_dup_object (block, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));

  action_id = "org.freedesktop.udisks2.open-device";
  if (udisks_block_get_hint_system (block))
    action_id = "org.freedesktop.udisks2.open-device-system";

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    /* Translators: Shown in authentication dialog when an application
                                                     * wants to benchmark a device.
                                                     *
                                                     * Do not translate $(drive), it's a placeholder and will
                                                     * be replaced by the name of the drive/device in question
                                                     */
                                                    N_("Authentication is required to open $(drive) for benchmarking"),
                                                    invocation))
    goto out;

  g_variant_lookup (options, "writable", "b", &opt_writable);

  if (opt_writable)
    open_flags = O_RDWR  | O_EXCL;
  else
    open_flags = O_RDONLY;

  open_flags |= O_DIRECT | O_SYNC | O_CLOEXEC;

  device = udisks_block_get_device (UDISKS_BLOCK (block));

  fd = open (device, open_flags);
  if (fd == -1)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Error opening %s: %m", device);
      goto out;
    }

  out_fd_list = g_unix_fd_list_new_from_array (&fd, 1);
  udisks_block_complete_open_for_benchmark (block, invocation, out_fd_list, g_variant_new_handle (0));

 out:
  g_clear_object (&out_fd_list);
  g_clear_object (&object);
  return TRUE; /* returning true means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_rescan (UDisksBlock           *block,
               GDBusMethodInvocation *invocation,
               GVariant              *options)
{
  UDisksObject *object = NULL;
  UDisksLinuxDevice *device = NULL;
  UDisksDaemon *daemon;
  const gchar *action_id;
  const gchar *message;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (block, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));

  /* Translators: Shown in authentication dialog when an application
   * wants to rescan a device.
   *
   * Do not translate $(drive), it's a placeholder and will
   * be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to rescan $(drive)");
  action_id = "org.freedesktop.udisks2.rescan";

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));

  udisks_linux_block_object_trigger_uevent (UDISKS_LINUX_BLOCK_OBJECT (object));
  if (g_strcmp0 (g_udev_device_get_devtype (device->udev_device), "disk") == 0)
    udisks_linux_block_object_reread_partition_table (UDISKS_LINUX_BLOCK_OBJECT (object));

  udisks_block_complete_rescan (block, invocation);

 out:
  g_clear_object (&device);
  g_clear_object (&object);
  return TRUE; /* returning true means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
block_iface_init (UDisksBlockIface *iface)
{
  iface->handle_get_secret_configuration  = handle_get_secret_configuration;
  iface->handle_add_configuration_item    = handle_add_configuration_item;
  iface->handle_remove_configuration_item = handle_remove_configuration_item;
  iface->handle_update_configuration_item = handle_update_configuration_item;
  iface->handle_format                    = handle_format;
  iface->handle_open_for_backup           = handle_open_for_backup;
  iface->handle_open_for_restore          = handle_open_for_restore;
  iface->handle_open_for_benchmark        = handle_open_for_benchmark;
  iface->handle_rescan                    = handle_rescan;
}
