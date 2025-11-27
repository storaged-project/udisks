/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2016 Gris Ge <fge@redhat.com>
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
#include <glib/gi18n-lib.h>
#include <libstoragemgmt/libstoragemgmt.h>

#include <src/udiskslogging.h>
#include <src/udiskslinuxdriveobject.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udisksbasejob.h>
#include <src/udiskssimplejob.h>
#include <src/udisksthreadedjob.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodule.h>
#include <src/udisksmoduleobject.h>

#include "lsm_types.h"
#include "lsm_linux_drive_local.h"


typedef struct _UDisksLinuxDriveLSMLocalClass UDisksLinuxDriveLSMLocalClass;

struct _UDisksLinuxDriveLSMLocal
{
  UDisksDriveLsmLocalSkeleton parent_instance;

  UDisksLinuxModuleLSM   *module;
  UDisksLinuxDriveObject *drive_object;
};

struct _UDisksLinuxDriveLSMLocalClass
{
  UDisksDriveLsmLocalSkeletonClass parent_class;
};

static void udisks_linux_drive_lsm_local_iface_init (UDisksDriveLsmLocalIface *iface);
static void udisks_linux_drive_lsm_local_module_object_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxDriveLSMLocal, udisks_linux_drive_lsm_local, UDISKS_TYPE_DRIVE_LSM_LOCAL_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_DRIVE_LSM_LOCAL, udisks_linux_drive_lsm_local_iface_init)
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT, udisks_linux_drive_lsm_local_module_object_iface_init));

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_DRIVE_OBJECT,
  N_PROPERTIES
};


static void
udisks_linux_drive_lsm_local_get_property (GObject     *object,
                                           guint        property_id,
                                           GValue      *value,
                                           GParamSpec  *pspec)
{
  UDisksLinuxDriveLSMLocal *drive_lsm_local = UDISKS_LINUX_DRIVE_LSM_LOCAL (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_value_set_object (value, UDISKS_MODULE (drive_lsm_local->module));
      break;

    case PROP_DRIVE_OBJECT:
      g_value_set_object (value, drive_lsm_local->drive_object);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_drive_lsm_local_set_property (GObject       *object,
                                           guint          property_id,
                                           const GValue  *value,
                                           GParamSpec    *pspec)
{
  UDisksLinuxDriveLSMLocal *drive_lsm_local = UDISKS_LINUX_DRIVE_LSM_LOCAL (object);

  switch (property_id)
    {
    case PROP_MODULE:
      g_assert (drive_lsm_local->module == NULL);
      drive_lsm_local->module = UDISKS_LINUX_MODULE_LSM (g_value_dup_object (value));
      break;

    case PROP_DRIVE_OBJECT:
      g_assert (drive_lsm_local->drive_object == NULL);
      /* we don't take reference to drive_object */
      drive_lsm_local->drive_object = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
udisks_linux_drive_lsm_local_finalize (GObject *object)
{
  UDisksLinuxDriveLSMLocal *drive_lsm_local = UDISKS_LINUX_DRIVE_LSM_LOCAL (object);

  udisks_debug ("LSM: udisks_linux_drive_lsm_local_finalize ()");

  /* we don't take reference to drive_object */
  g_object_unref (drive_lsm_local->module);

  if (G_OBJECT_CLASS (udisks_linux_drive_lsm_local_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_drive_lsm_local_parent_class)->finalize (object);
}

static void
udisks_linux_drive_lsm_local_init (UDisksLinuxDriveLSMLocal *drive_lsm_local)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (drive_lsm_local),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_drive_lsm_local_class_init (UDisksLinuxDriveLSMLocalClass *class)
{
  GObjectClass *gobject_class;

  udisks_debug ("LSM: udisks_linux_drive_lsm_local_class_init");

  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->get_property = udisks_linux_drive_lsm_local_get_property;
  gobject_class->set_property = udisks_linux_drive_lsm_local_set_property;
  gobject_class->finalize = udisks_linux_drive_lsm_local_finalize;

  /**
   * UDisksLinuxDriveLSMLocal:module:
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
   * UDisksLinuxDriveLSMLocal:driveobject:
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

/**
 * udisks_linux_drive_lsm_local_new:
 * @module: A #UDisksLinuxModuleLSM.
 * @drive_object: A #UDisksLinuxDriveObject.
 *
 * Creates a new #UDisksLinuxDriveLSMLocal instance.
 *
 * Returns: A new #UDisksLinuxDriveLSMLocal. Free with g_object_unref().
 */
UDisksLinuxDriveLSMLocal *
udisks_linux_drive_lsm_local_new (UDisksLinuxModuleLSM   *module,
                                  UDisksLinuxDriveObject *drive_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_LSM (module), NULL);
  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_OBJECT (drive_object), NULL);

  udisks_debug ("LSM: udisks_linux_drive_lsm_local_new");

  return g_object_new (UDISKS_TYPE_LINUX_DRIVE_LSM_LOCAL,
                       "module", UDISKS_MODULE (module),
                       "driveobject", drive_object,
                       NULL);
}

static gchar *
get_blk_path (UDisksLinuxDriveLSMLocal *drive_lsm_local,
              GDBusMethodInvocation    *invocation)
{
  UDisksLinuxDriveObject *drive_object;
  UDisksLinuxBlockObject *block_object;
  UDisksBlock *block = NULL;
  gchar *blk_path = NULL;

  drive_object = drive_lsm_local->drive_object;
  block_object = udisks_linux_drive_object_get_block (drive_object, FALSE);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Unable to find block device for drive");
      goto out;
    }

  block = udisks_object_get_block (UDISKS_OBJECT (block_object));
  blk_path = udisks_block_dup_device (block);
  if (blk_path == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to retrieve block path of specified disk drive");
      goto out;
    }

out:
  g_clear_object (&block);
  g_clear_object (&block_object);
  return blk_path;
}

static gboolean
is_authed (GDBusMethodInvocation    *invocation,
           UDisksLinuxDriveLSMLocal *drive_lsm_local,
           GVariant                 *options)
{
  UDisksLinuxDriveObject *drive_object;
  UDisksLinuxBlockObject *block_object;
  UDisksDaemon *daemon = NULL;
  gboolean rc = FALSE;

  drive_object = drive_lsm_local->drive_object;
  daemon = udisks_module_get_daemon (UDISKS_MODULE (drive_lsm_local->module));
  block_object = udisks_linux_drive_object_get_block (drive_object, FALSE);
  if (block_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Unable to find block device for drive");
      goto out;
    }

  /* Check that the user is actually authorized */
  if (! udisks_daemon_util_check_authorization_sync (daemon,
                                                     UDISKS_OBJECT (block_object),
                                                     LSM_POLICY_ACTION_ID,
                                                     options,
                                                     N_("Authentication is required to change $(device.name) LED"),
                                                     invocation))
    goto out;

  rc = TRUE;

out:
  g_clear_object (&block_object);
  return rc;
}

static gboolean
led_control (UDisksLinuxDriveLSMLocal *drive_lsm_local,
             GDBusMethodInvocation    *invocation,
             GVariant                 *options,
             int                     (*lsm_func) (const char *blk_path, lsm_error **lsm_err),
             const gchar              *lsm_fun_name)
{
  lsm_error *lsm_err = NULL;
  int lsm_rc = 0;
  gchar *blk_path = NULL;

  if (! is_authed (invocation, drive_lsm_local, options))
    goto out;

  blk_path = get_blk_path (drive_lsm_local, invocation);
  if (blk_path == NULL)
    goto out;

  lsm_rc = lsm_func (blk_path, &lsm_err);
  if (lsm_rc != LSM_ERR_OK)
    {
      if (lsm_rc == LSM_ERR_NO_SUPPORT)
        g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_NOT_SUPPORTED,
                                               "Specified disk does not support this action");
      else
        g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                               "%s failed %d: %s",
                                               lsm_fun_name, lsm_error_number_get (lsm_err),
                                               lsm_error_message_get (lsm_err));
      goto out;
    }

  /* success, complete the method call in a generic way */
  g_dbus_method_invocation_return_value (invocation, g_variant_new ("()"));

 out:
  g_free (blk_path);
  if (lsm_err != NULL)
    lsm_error_free (lsm_err);

  return TRUE;
}

static gboolean
handle_turn_ident_ledon (UDisksDriveLsmLocal   *drive_lsm_local,
                         GDBusMethodInvocation *invocation,
                         GVariant              *options)
{
  return led_control (UDISKS_LINUX_DRIVE_LSM_LOCAL (drive_lsm_local),
                      invocation, options,
                      lsm_local_disk_ident_led_on, "lsm_local_ident_led_on");
}

static gboolean
handle_turn_ident_ledoff (UDisksDriveLsmLocal   *drive_lsm_local,
                          GDBusMethodInvocation *invocation,
                          GVariant              *options)
{
  return led_control (UDISKS_LINUX_DRIVE_LSM_LOCAL (drive_lsm_local),
                      invocation, options,
                      lsm_local_disk_ident_led_off, "lsm_local_ident_led_off");
}

static gboolean
handle_turn_fault_ledon (UDisksDriveLsmLocal   *drive_lsm_local,
                         GDBusMethodInvocation *invocation,
                         GVariant              *options)
{
  return led_control (UDISKS_LINUX_DRIVE_LSM_LOCAL (drive_lsm_local),
                      invocation, options,
                      lsm_local_disk_fault_led_on, "lsm_local_fault_led_on");
}

static gboolean
handle_turn_fault_ledoff (UDisksDriveLsmLocal   *drive_lsm_local,
                          GDBusMethodInvocation *invocation,
                          GVariant              *options)
{
  return led_control (UDISKS_LINUX_DRIVE_LSM_LOCAL (drive_lsm_local),
                      invocation, options,
                      lsm_local_disk_fault_led_off, "lsm_local_fault_led_off");
}

gboolean
udisks_linux_drive_lsm_local_update (UDisksLinuxDriveLSMLocal *drive_lsm_local,
                                     UDisksLinuxDriveObject   *drive_object)
{
  return FALSE;
}

static void
udisks_linux_drive_lsm_local_iface_init (UDisksDriveLsmLocalIface *iface)
{
  iface->handle_turn_ident_ledon = handle_turn_ident_ledon;
  iface->handle_turn_ident_ledoff = handle_turn_ident_ledoff;
  iface->handle_turn_fault_ledon = handle_turn_fault_ledon;
  iface->handle_turn_fault_ledoff = handle_turn_fault_ledoff;
}

/* -------------------------------------------------------------------------- */

static gboolean
udisks_linux_drive_lsm_local_module_object_process_uevent (UDisksModuleObject *module_object,
                                                           const gchar        *action,
                                                           UDisksLinuxDevice  *device,
                                                           gboolean           *keep)
{
  UDisksLinuxDriveLSMLocal *drive_lsm_local = UDISKS_LINUX_DRIVE_LSM_LOCAL (module_object);

  g_return_val_if_fail (UDISKS_IS_LINUX_DRIVE_LSM_LOCAL (module_object), FALSE);

  *keep = udisks_linux_module_lsm_drive_local_check (drive_lsm_local->module, drive_lsm_local->drive_object);
  if (*keep)
    {
      udisks_linux_drive_lsm_local_update (drive_lsm_local, drive_lsm_local->drive_object);
    }

  return TRUE;
}

static void
udisks_linux_drive_lsm_local_module_object_iface_init (UDisksModuleObjectIface *iface)
{
  iface->process_uevent = udisks_linux_drive_lsm_local_module_object_process_uevent;
}
