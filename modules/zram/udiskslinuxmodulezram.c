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

#include <src/udisksdaemon.h>
#include <src/udiskslogging.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodulemanager.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>
#include <src/udiskslinuxblockobject.h>

#include "udiskslinuxmodulezram.h"
#include "udiskslinuxmanagerzram.h"
#include "udiskslinuxblockzram.h"

/**
 * SECTION:udiskslinuxmodulezram
 * @title: UDisksLinuxModuleZRAM
 * @short_description: ZRAM module.
 *
 * The ZRAM module.
 */

/**
 * UDisksLinuxModuleZRAM:
 *
 * The #UDisksLinuxModuleZRAM structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _UDisksLinuxModuleZRAM {
  UDisksModule parent_instance;

};

typedef struct _UDisksLinuxModuleZRAMClass UDisksLinuxModuleZRAMClass;

struct _UDisksLinuxModuleZRAMClass {
  UDisksModuleClass parent_class;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxModuleZRAM, udisks_linux_module_zram, UDISKS_TYPE_MODULE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));


static void
udisks_linux_module_zram_init (UDisksLinuxModuleZRAM *module)
{
}

static void
udisks_linux_module_zram_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_module_zram_parent_class)->constructed)
    G_OBJECT_CLASS (udisks_linux_module_zram_parent_class)->constructed (object);
}

static void
udisks_linux_module_zram_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_module_zram_parent_class)->finalize)
    G_OBJECT_CLASS (udisks_linux_module_zram_parent_class)->finalize (object);
}

gchar *
udisks_module_id (void)
{
  return g_strdup (ZRAM_MODULE_NAME);
}

/**
 * udisks_module_zram_new:
 * @daemon: A #UDisksDaemon.
 * @cancellable: (nullable): A #GCancellable or %NULL
 * @error: Return location for error or %NULL.
 *
 * Creates new #UDisksLinuxModuleZRAM object.
 *
 * Returns: (transfer full) (type UDisksLinuxModuleZRAM): A
 *   #UDisksLinuxModuleZRAM object or %NULL if @error is set. Free
 *   with g_object_unref().
 */
UDisksModule *
udisks_module_zram_new (UDisksDaemon  *daemon,
                        GCancellable  *cancellable,
                        GError       **error)
{
  GInitable *initable;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  initable = g_initable_new (UDISKS_TYPE_LINUX_MODULE_ZRAM,
                             cancellable,
                             error,
                             "daemon", daemon,
                             "name", ZRAM_MODULE_NAME,
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
  BDPluginSpec kbd_plugin = { BD_PLUGIN_KBD, NULL };
  BDPluginSpec swap_plugin = { BD_PLUGIN_SWAP, NULL };
  BDPluginSpec *plugins[] = { &kbd_plugin, &swap_plugin, NULL };

  if (! bd_is_plugin_available (BD_PLUGIN_KBD) || ! bd_is_plugin_available (BD_PLUGIN_SWAP))
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
udisks_linux_module_zram_new_manager (UDisksModule *module)
{
  UDisksLinuxManagerZRAM *manager;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZRAM (module), NULL);

  manager = udisks_linux_manager_zram_new (UDISKS_LINUX_MODULE_ZRAM (module));

  return G_DBUS_INTERFACE_SKELETON (manager);
}

/* ---------------------------------------------------------------------------------------------------- */

static GType *
udisks_linux_module_zram_get_block_object_interface_types (UDisksModule *module)
{
  static GType block_object_interface_types[2];

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZRAM (module), NULL);

  if (g_once_init_enter (&block_object_interface_types[0]))
    g_once_init_leave (&block_object_interface_types[0], UDISKS_TYPE_LINUX_BLOCK_ZRAM);

  return block_object_interface_types;
}

static GDBusInterfaceSkeleton *
udisks_linux_module_zram_new_block_object_interface (UDisksModule           *module,
                                                     UDisksLinuxBlockObject *object,
                                                     GType                   interface_type)
{
  GDBusInterfaceSkeleton *interface = NULL;
  UDisksLinuxDevice *device;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ZRAM (module), NULL);

  if (interface_type == UDISKS_TYPE_LINUX_BLOCK_ZRAM)
    {
      /* Check device name */
      device = udisks_linux_block_object_get_device (UDISKS_LINUX_BLOCK_OBJECT (object));
      if (g_str_has_prefix (g_udev_device_get_device_file (device->udev_device), "/dev/zram"))
        {
          interface = G_DBUS_INTERFACE_SKELETON (udisks_linux_block_zram_new (UDISKS_LINUX_MODULE_ZRAM (module), object));
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
udisks_linux_module_zram_class_init (UDisksLinuxModuleZRAMClass *klass)
{
  GObjectClass *gobject_class;
  UDisksModuleClass *module_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = udisks_linux_module_zram_constructed;
  gobject_class->finalize = udisks_linux_module_zram_finalize;

  module_class = UDISKS_MODULE_CLASS (klass);
  module_class->new_manager = udisks_linux_module_zram_new_manager;
  module_class->get_block_object_interface_types = udisks_linux_module_zram_get_block_object_interface_types;
  module_class->new_block_object_interface = udisks_linux_module_zram_new_block_object_interface;
}
