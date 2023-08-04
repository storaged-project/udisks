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

#include <libiscsi.h>

#include <src/udisksdaemon.h>
#include <src/udiskslogging.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodulemanager.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>

#include "udisksiscsitypes.h"
#include "udiskslinuxmoduleiscsi.h"
#include "udiskslinuxmanageriscsiinitiator.h"

#ifdef HAVE_LIBISCSI_GET_SESSION_INFOS
#  include "udiskslinuxiscsisessionobject.h"
#endif

/**
 * SECTION:udiskslinuxmoduleiscsi
 * @title: UDisksLinuxModuleISCSI
 * @short_description: iSCSI module.
 *
 * The iSCSI module.
 */

/**
 * UDisksLinuxModuleISCSI:
 *
 * The #UDisksLinuxModuleISCSI structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _UDisksLinuxModuleISCSI {
  UDisksModule parent_instance;

  GMutex libiscsi_mutex;
  struct libiscsi_context *iscsi_ctx;
};

typedef struct _UDisksLinuxModuleISCSIClass UDisksLinuxModuleISCSIClass;

struct _UDisksLinuxModuleISCSIClass {
  UDisksModuleClass parent_class;
};

static void initable_iface_init (GInitableIface *initable_iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxModuleISCSI, udisks_linux_module_iscsi, UDISKS_TYPE_MODULE,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init));


static void
udisks_linux_module_iscsi_init (UDisksLinuxModuleISCSI *module)
{
  g_return_if_fail (UDISKS_IS_LINUX_MODULE_ISCSI (module));

  g_mutex_init (&module->libiscsi_mutex);
}

static void
udisks_linux_module_iscsi_constructed (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_module_iscsi_parent_class)->constructed)
    G_OBJECT_CLASS (udisks_linux_module_iscsi_parent_class)->constructed (object);
}

static void
udisks_linux_module_iscsi_finalize (GObject *object)
{
  UDisksLinuxModuleISCSI *module = UDISKS_LINUX_MODULE_ISCSI (object);

  if (module->iscsi_ctx)
    libiscsi_cleanup (module->iscsi_ctx);

  if (G_OBJECT_CLASS (udisks_linux_module_iscsi_parent_class)->finalize)
    G_OBJECT_CLASS (udisks_linux_module_iscsi_parent_class)->finalize (object);
}

gchar *
udisks_module_id (void)
{
  return g_strdup (ISCSI_MODULE_NAME);
}

/**
 * udisks_module_iscsi_new:
 * @daemon: A #UDisksDaemon.
 * @cancellable: (nullable): A #GCancellable or %NULL
 * @error: Return location for error or %NULL.
 *
 * Creates new #UDisksLinuxModuleISCSI object.
 *
 * Returns: (transfer full) (type UDisksLinuxModuleISCSI): A
 *   #UDisksLinuxModuleISCSI object or %NULL if @error is set. Free
 *   with g_object_unref().
 */
UDisksModule *
udisks_module_iscsi_new (UDisksDaemon  *daemon,
                         GCancellable  *cancellable,
                         GError       **error)
{
  GInitable *initable;

  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  initable = g_initable_new (UDISKS_TYPE_LINUX_MODULE_ISCSI,
                             cancellable,
                             error,
                             "daemon", daemon,
                             "name", ISCSI_MODULE_NAME,
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
  UDisksLinuxModuleISCSI *module = UDISKS_LINUX_MODULE_ISCSI (initable);

  module->iscsi_ctx = libiscsi_init ();
  if (! module->iscsi_ctx)
    {
      g_set_error_literal (error, UDISKS_ERROR, UDISKS_ERROR_ISCSI_DAEMON_TRANSPORT_FAILED,
                           "Failed to initialize libiscsi.");
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

void
udisks_linux_module_iscsi_lock_libiscsi_context (UDisksLinuxModuleISCSI *module)
{
  g_return_if_fail (UDISKS_IS_LINUX_MODULE_ISCSI (module));
  g_mutex_lock (&module->libiscsi_mutex);
}

void
udisks_linux_module_iscsi_unlock_libiscsi_context (UDisksLinuxModuleISCSI *module)
{
  g_return_if_fail (UDISKS_IS_LINUX_MODULE_ISCSI (module));
  g_mutex_unlock (&module->libiscsi_mutex);
}

struct libiscsi_context *
udisks_linux_module_iscsi_get_libiscsi_context (UDisksLinuxModuleISCSI *module)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ISCSI (module), NULL);
  return module->iscsi_ctx;
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusInterfaceSkeleton *
udisks_linux_module_iscsi_new_manager (UDisksModule *module)
{
  UDisksLinuxManagerISCSIInitiator *manager;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ISCSI (module), NULL);

  manager = udisks_linux_manager_iscsi_initiator_new (UDISKS_LINUX_MODULE_ISCSI (module));

  return G_DBUS_INTERFACE_SKELETON (manager);
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusObjectSkeleton **
udisks_linux_module_iscsi_new_object (UDisksModule      *module,
                                      UDisksLinuxDevice *device)
{
#ifdef HAVE_LIBISCSI_GET_SESSION_INFOS
  GDBusObjectSkeleton **objects;
  UDisksLinuxISCSISessionObject *session_object = NULL;
  const gchar *sysfs_path;
  gchar *session_id;
  gboolean keep = FALSE;

  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ISCSI (module), NULL);

  /* Session ID */
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);
  session_id = udisks_linux_iscsi_session_object_get_session_id_from_sysfs_path (sysfs_path);

  if (session_id)
    {
       session_object = udisks_linux_iscsi_session_object_new (UDISKS_LINUX_MODULE_ISCSI (module), session_id);
       udisks_linux_iscsi_session_object_process_uevent (UDISKS_MODULE_OBJECT (session_object), "add", device, &keep);
       g_warn_if_fail (keep == TRUE);
       g_free (session_id);
    }

  if (session_object)
    {
      objects = g_new0 (GDBusObjectSkeleton *, 2);
      objects[0] = G_DBUS_OBJECT_SKELETON (session_object);
      return objects;
    }
#endif /* HAVE_LIBISCSI_GET_SESSION_INFOS */

  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_module_iscsi_class_init (UDisksLinuxModuleISCSIClass *klass)
{
  GObjectClass *gobject_class;
  UDisksModuleClass *module_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = udisks_linux_module_iscsi_constructed;
  gobject_class->finalize = udisks_linux_module_iscsi_finalize;

  module_class = UDISKS_MODULE_CLASS (klass);
  module_class->new_manager = udisks_linux_module_iscsi_new_manager;
  module_class->new_object = udisks_linux_module_iscsi_new_object;
}
