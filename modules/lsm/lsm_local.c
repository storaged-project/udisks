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

/* Note: This is inspired by modules/dummy/dummylinuxdrive.c  */

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
#include <modules/udisksmoduleobject.h>

#include "lsm_types.h"

#define _AUTH_MSG       "Authentication is required to change $(drive) LED"
#define _AUTH_ACTION_ID "org.freedesktop.udisks2.manage-led"

typedef struct _UDisksLinuxDriveLsmLocalClass
  UDisksLinuxDriveLsmLocalClass;

struct _UDisksLinuxDriveLsmLocal
{
  UDisksDriveLsmLocalSkeleton parent_instance;
  UDisksLinuxDriveObject *ud_lx_drv_obj;
};

struct _UDisksLinuxDriveLsmLocalClass
{
  UDisksDriveLsmLocalSkeletonClass parent_class;
};

static void
udisks_linux_drive_lsm_local_iface_init
  (UDisksDriveLsmLocalIface *iface);

G_DEFINE_TYPE_WITH_CODE
  (UDisksLinuxDriveLsmLocal, udisks_linux_drive_lsm_local,
   UDISKS_TYPE_DRIVE_LSM_LOCAL_SKELETON,
   G_IMPLEMENT_INTERFACE (UDISKS_TYPE_DRIVE_LSM_LOCAL,
                          udisks_linux_drive_lsm_local_iface_init));

static const gchar *
get_blk_path (UDisksDriveLsmLocal *ud_drv_lsm_local,
               GDBusMethodInvocation *invocation);

static gboolean
handle_turn_ident_ledon (UDisksDriveLsmLocal *ud_drv_lsm_local,
                         GDBusMethodInvocation *invocation,
                         GVariant *options);

static gboolean
handle_turn_ident_ledoff (UDisksDriveLsmLocal *ud_drv_lsm_local,
                          GDBusMethodInvocation *invocation,
                          GVariant *options);

static gboolean
handle_turn_fault_ledon (UDisksDriveLsmLocal *ud_drv_lsm_local,
                         GDBusMethodInvocation *invocation,
                         GVariant *options);

static gboolean
handle_turn_fault_ledoff (UDisksDriveLsmLocal *ud_drv_lsm_local,
                          GDBusMethodInvocation *invocation,
                          GVariant *options);

static gboolean
is_authed (GDBusMethodInvocation *invocation,
           UDisksDriveLsmLocal *ud_drv_lsm_local,
           const gchar *auth_msg, const gchar *action_id, GVariant *options);

static gboolean
led_control (UDisksDriveLsmLocal *ud_drv_lsm_local,
             GDBusMethodInvocation *invocation, GVariant *options,
             int (*lsm_func) (const char *blk_path, lsm_error **lsm_err),
             const gchar *lsm_fun_name);

static const gchar *
get_blk_path (UDisksDriveLsmLocal *ud_drv_lsm_local,
              GDBusMethodInvocation *invocation)
{
  const gchar *blk_path = NULL;
  UDisksLinuxDriveLsmLocal *ud_lx_drv_lsm_local = NULL;
  UDisksLinuxDriveObject *ud_lx_drv_obj = NULL;
  UDisksLinuxBlockObject *ud_lx_blk_obj = NULL;
  UDisksBlock *ud_blk = NULL;

  ud_lx_drv_lsm_local = UDISKS_LINUX_DRIVE_LSM_LOCAL (ud_drv_lsm_local);

  ud_lx_drv_obj = ud_lx_drv_lsm_local->ud_lx_drv_obj;

  ud_lx_blk_obj = udisks_linux_drive_object_get_block (ud_lx_drv_obj, FALSE);
  if (ud_lx_blk_obj == NULL)
    {
      g_dbus_method_invocation_return_error
        (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
         "Unable to find block device for drive");
      goto out;
    }

  ud_blk = udisks_object_get_block (UDISKS_OBJECT (ud_lx_blk_obj));
  blk_path = udisks_block_dup_device (ud_blk);

out:
  g_clear_object (&ud_blk);
  g_clear_object (&ud_lx_blk_obj);
  return blk_path;
}

static gboolean
led_control (UDisksDriveLsmLocal *ud_drv_lsm_local,
             GDBusMethodInvocation *invocation, GVariant *options,
             int (*lsm_func) (const char *blk_path, lsm_error **lsm_err),
             const gchar *lsm_fun_name)
{
  lsm_error *lsm_err = NULL;
  int lsm_rc = 0;
  const gchar *blk_path = NULL;

  if (is_authed(invocation, ud_drv_lsm_local, N_(_AUTH_MSG), _AUTH_ACTION_ID,
                options) == FALSE)
    goto out;

  blk_path = get_blk_path(ud_drv_lsm_local, invocation);
  if (blk_path == NULL)
    {
      g_dbus_method_invocation_return_error
        (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
         "Failed to retrieve block path of specified disk drive");
      goto out;
    }

  lsm_rc = lsm_func ((const char *) blk_path, &lsm_err);
  if (lsm_rc != LSM_ERR_OK)
    {
      if (lsm_rc == LSM_ERR_NO_SUPPORT)
        g_dbus_method_invocation_return_error
          (invocation, UDISKS_ERROR, UDISKS_ERROR_NOT_SUPPORTED,
           "Specified disk does not support this action");
      else
        g_dbus_method_invocation_return_error
          (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
           "%s failed %d: %s", lsm_fun_name, lsm_error_number_get (lsm_err),
           lsm_error_message_get (lsm_err));
      goto out;
    }

 out:
  g_free ((gchar *) blk_path);
  if (lsm_err != NULL)
    lsm_error_free (lsm_err);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static gboolean
handle_turn_ident_ledon (UDisksDriveLsmLocal *ud_drv_lsm_local,
                         GDBusMethodInvocation *invocation, GVariant *options)
{
  return led_control (ud_drv_lsm_local, invocation, options,
                      lsm_local_disk_ident_led_on, "lsm_local_ident_led_on");
}

static gboolean
handle_turn_ident_ledoff (UDisksDriveLsmLocal *ud_drv_lsm_local,
                          GDBusMethodInvocation *invocation, GVariant *options)
{
  return led_control (ud_drv_lsm_local, invocation, options,
                      lsm_local_disk_ident_led_off, "lsm_local_ident_led_off");
}

static gboolean
handle_turn_fault_ledon (UDisksDriveLsmLocal *ud_drv_lsm_local,
                         GDBusMethodInvocation *invocation, GVariant *options)
{
  return led_control (ud_drv_lsm_local, invocation, options,
                      lsm_local_disk_fault_led_on, "lsm_local_fault_led_on");
}

static gboolean
handle_turn_fault_ledoff (UDisksDriveLsmLocal *ud_drv_lsm_local,
                          GDBusMethodInvocation *invocation, GVariant *options)
{
  return led_control (ud_drv_lsm_local, invocation, options,
                      lsm_local_disk_fault_led_off, "lsm_local_fault_led_off");
}

static void
udisks_linux_drive_lsm_local_finalize (GObject *object)
{
  udisks_debug ("LSM: udisks_linux_drive_lsm_local_finalize ()");

  if (G_OBJECT_CLASS
      (udisks_linux_drive_lsm_local_parent_class)->finalize != NULL)
    G_OBJECT_CLASS
      (udisks_linux_drive_lsm_local_parent_class)->finalize (object);
}


static void
udisks_linux_drive_lsm_local_init
  (UDisksLinuxDriveLsmLocal *ud_lx_drv_lsm_local)
{
  udisks_debug ("LSM: udisks_linux_drive_lsm_local_init");
  ud_lx_drv_lsm_local->ud_lx_drv_obj = NULL;
  return;
}

static void
udisks_linux_drive_lsm_local_class_init
  (UDisksLinuxDriveLsmLocalClass *class)
{
  GObjectClass *gobject_class;

  udisks_debug ("LSM: udisks_linux_drive_lsm_local_class_init");
  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = udisks_linux_drive_lsm_local_finalize;
}

static void
udisks_linux_drive_lsm_local_iface_init
  (UDisksDriveLsmLocalIface *iface)
{
  udisks_debug ("LSM: udisks_linux_drive_lsm_local_iface_init");
  iface->handle_turn_ident_ledon = handle_turn_ident_ledon;
  iface->handle_turn_ident_ledoff = handle_turn_ident_ledoff;
  iface->handle_turn_fault_ledon = handle_turn_fault_ledon;
  iface->handle_turn_fault_ledoff = handle_turn_fault_ledoff;
}

UDisksLinuxDriveLsmLocal *
udisks_linux_drive_lsm_local_new (void)
{
  udisks_debug ("LSM: udisks_linux_drive_lsm_local_new");
  return UDISKS_LINUX_DRIVE_LSM_LOCAL
    (g_object_new (UDISKS_TYPE_DRIVE_LSM_LOCAL, NULL));
}

gboolean udisks_linux_drive_lsm_local_update
  (UDisksLinuxDriveLsmLocal *ud_lx_drv_lsm_local,
   UDisksLinuxDriveObject *ud_lx_drv_obj)
{
  ud_lx_drv_lsm_local->ud_lx_drv_obj = ud_lx_drv_obj;
  return FALSE;
  /* Nothing changed, just save UDisksLinuxDriveObject for future use  */
}

static gboolean
is_authed (GDBusMethodInvocation *invocation,
           UDisksDriveLsmLocal *ud_drv_lsm_local,
           const gchar *auth_msg, const gchar *action_id, GVariant *options)
{
  gboolean rc = FALSE;
  UDisksLinuxDriveObject *ud_lx_drv_obj = NULL;
  UDisksLinuxBlockObject *ud_lx_blk_obj = NULL;
  UDisksDaemon *daemon = NULL;
  UDisksLinuxDriveLsmLocal *ud_lx_drv_lsm_local = NULL;

  ud_lx_drv_lsm_local = UDISKS_LINUX_DRIVE_LSM_LOCAL (ud_drv_lsm_local);
  ud_lx_drv_obj = ud_lx_drv_lsm_local->ud_lx_drv_obj;
  daemon = udisks_linux_drive_object_get_daemon (ud_lx_drv_obj);
  ud_lx_blk_obj = udisks_linux_drive_object_get_block (ud_lx_drv_obj, FALSE);
  if (ud_lx_blk_obj == NULL)
    {
      g_dbus_method_invocation_return_error
        (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
         "Unable to find block device for drive");
      goto out;
    }

  /* Check that the user is actually authorized */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT
                                                    (ud_lx_blk_obj),
                                                    action_id,
                                                    options,
                                                    auth_msg,
                                                    invocation))
    goto out;

  rc = TRUE;

out:
  g_clear_object (&ud_lx_blk_obj);
  return rc;
}
