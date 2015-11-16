/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 - 2016 Gris Ge <fge@redhat.com>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <inttypes.h>

#include <libdmmp/libdmmp.h>

#include <modules/storagedmoduleiface.h>

#include <storaged/storaged-generated.h>
#include <src/storagedlinuxblockobject.h>
#include <src/storagedlinuxdriveobject.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedlinuxdrive.h>
#include <src/storageddaemon.h>
#include <src/storagedmodulemanager.h>

#include "mp_types.h"

struct _MultipathPluginState {
  struct dmmp_context *dmmp_ctx;
  struct dmmp_mpath **dmmp_mps;
  uint32_t dmmp_mp_count;
};

static struct _MultipathPluginState *
_state_new (void)
{
  struct _MultipathPluginState *p = NULL;

  p = (struct _MultipathPluginState *) g_malloc
    (sizeof(struct _MultipathPluginState*));
  p->dmmp_ctx= dmmp_context_new();
  if (p->dmmp_ctx == NULL)
    {
      g_free(p);
      return NULL;
    }
  p->dmmp_mps = NULL;
  p->dmmp_mp_count = 0;
  return p;
}

static void
_state_free (struct _MultipathPluginState *p)
{
  if (p == NULL)
    return;
  dmmp_mpath_array_free(p->dmmp_mps, p->dmmp_mp_count);
  dmmp_context_free(p->dmmp_ctx);
  g_free(p);
}

static struct _MultipathPluginState *
_state_get (StoragedObject *std_obj)
{
  StoragedDaemon *daemon = NULL;
  StoragedModuleManager *manager = NULL;

  if (STORAGED_IS_LINUX_DRIVE_OBJECT (std_obj))
    daemon = storaged_linux_drive_object_get_daemon
      (STORAGED_LINUX_DRIVE_OBJECT (std_obj));
  else if (STORAGED_IS_LINUX_BLOCK_OBJECT (std_obj))
    daemon = storaged_linux_block_object_get_daemon
      (STORAGED_LINUX_BLOCK_OBJECT (std_obj));
  else
    return NULL;

  manager = storaged_daemon_get_module_manager (daemon);
  return (struct _MultipathPluginState *)
    storaged_module_manager_get_module_state_pointer (manager, MP_MODULE_NAME);
}

/*
 * Returned pointer should be freed by g_free().
 */
static const gchar *
_mpath_name_of_std_obj(StoragedObject *std_obj)
{
  const gchar *mpath_name = NULL;
  StoragedLinuxDevice *std_lx_dev = NULL;

  if (STORAGED_IS_LINUX_DRIVE_OBJECT (std_obj))
    std_lx_dev = storaged_linux_drive_object_get_device
      (STORAGED_LINUX_DRIVE_OBJECT (std_obj), FALSE /* dm dev is OK */);
  else if (STORAGED_IS_LINUX_BLOCK_OBJECT (std_obj))
    std_lx_dev = storaged_linux_block_object_get_device
      (STORAGED_LINUX_BLOCK_OBJECT (std_obj));
  else
    return NULL;

  mpath_name = storaged_linux_device_multipath_name (std_lx_dev);

  if (std_lx_dev != NULL)
    g_object_unref (std_lx_dev);

  return mpath_name;
}

/*
 * For udev add action, use cached data, for other udev action, refresh the
 * cache.
 * Don't free the returned pointer, it will be freed in
 * storaged_module_teardown ().
 */
static struct dmmp_mpath *
_mpath_get (struct _MultipathPluginState *state,
            const gchar *mpath_name, const gchar *action)
{
  struct dmmp_mpath *mpath = NULL;
  struct dmmp_mpath *cur_mpath = NULL;
  uint32_t i = 0;
  const char *cur_mpath_name = NULL;
  int mp_rc = 0;

  if ((mpath_name == NULL) || (state == NULL) || (action == NULL))
    return NULL;

  if (MP_MODULE_IS_UDEV_ADD(action))
      goto search;

  dmmp_mpath_array_free (state->dmmp_mps, state->dmmp_mp_count);
  storaged_debug ("_mpath_get(): requesting multipath list");
  mp_rc = dmmp_mpath_array_get (state->dmmp_ctx, &(state->dmmp_mps),
                                &(state->dmmp_mp_count));
  if (mp_rc != 0)
    {
      storaged_debug ("_mpath_get(): Failed to retrieve mpath list"
                      "%d", mp_rc);
      goto out;
    }

search:
  for (; i < state->dmmp_mp_count; ++i)
    {
      cur_mpath = state->dmmp_mps[i];
      cur_mpath_name = dmmp_mpath_name_get (cur_mpath);
      if (g_strcmp0(cur_mpath_name, mpath_name) == 0)
        {
          mpath = cur_mpath;
          goto out;
        }
    }

out:
  return mpath;
}

gchar *
storaged_module_id (void)
{
  return g_strdup (MP_MODULE_NAME);
}

gpointer
storaged_module_init (StoragedDaemon *daemon)
{
  struct _MultipathPluginState *state = NULL;
  int mp_rc = 0;

  storaged_debug ("Multipath: storaged_module_init ()");

  state = _state_new ();
  if (state == NULL)
    return NULL;

  storaged_debug ("storaged_module_init(): requesting multipath list");

  mp_rc = dmmp_mpath_array_get (state->dmmp_ctx, &(state->dmmp_mps),
                                &(state->dmmp_mp_count));

  if (mp_rc != DMMP_OK)
    storaged_debug ("storaged_module_init(): Failed to retrieve mpath list"
                    "%d(%s)", mp_rc, dmmp_strerror(mp_rc));

  return state;
}

void
storaged_module_teardown (StoragedDaemon *daemon)
{
  struct _MultipathPluginState *state = NULL;
  StoragedModuleManager *manager = NULL;

  storaged_debug ("Multipath: storaged_module_teardown ()");

  manager = storaged_daemon_get_module_manager (daemon);
  state = (struct _MultipathPluginState *)
    storaged_module_manager_get_module_state_pointer (manager, MP_MODULE_NAME);

  _state_free(state);
}

static gboolean
_drive_check (StoragedObject *object)
{
  gboolean rc = FALSE;
  const gchar *mpath_name = NULL;

  storaged_debug ("Multipath: _drive_check ()");

  mpath_name = _mpath_name_of_std_obj (object);

  if (mpath_name != NULL)
    rc = TRUE;

  g_free ((gchar *) mpath_name);

  return rc;
}

static void
_drive_connect (StoragedObject *object)
{
}

static gboolean
_drive_update (StoragedObject *object,
               const gchar *uevent_action, GDBusInterface *_iface)
{
  const gchar *mp_name = NULL;
  gboolean rc = FALSE;
  struct dmmp_mpath *mpath = NULL;

  storaged_debug ("Multipath: _drive_update: got udevent_action %s",
                  uevent_action);

  if ((! MP_MODULE_IS_UDEV_ADD(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_CHANGE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_ONLINE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_OFFLINE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_REMOVE(uevent_action)))
    {
      storaged_warning ("Multipath: BUG: Got unknown udev action: %s,"
                        "ignoring", (const char *) uevent_action);
      goto out;
    }

  mp_name = _mpath_name_of_std_obj (object);

  if (mp_name == NULL)
    goto out;

  mpath = _mpath_get(_state_get (object), mp_name, uevent_action);

  rc = std_lx_drv_mp_update (STORAGED_LINUX_DRIVE_MULTIPATH (_iface),
                             STORAGED_LINUX_DRIVE_OBJECT (object),
                             uevent_action, mpath);
out:
  g_free ((gchar *) mp_name);

  return rc;
}

/*
 * Return TRUE if /dev/dm-X is multipath, or /dev/sdb is multipath slave.
 */
static gboolean
_block_check (StoragedObject *object)
{
  gboolean rc = FALSE;
  const gchar *mp_name = NULL;

  storaged_debug ("Multipath: _block_check ()");

  mp_name = _mpath_name_of_std_obj (object);

  if (mp_name != NULL)
      rc = TRUE;

  g_free ((gchar *) mp_name);

  return rc;
}

static void
_block_connect (StoragedObject *object)
{
}

static gboolean
_block_update (StoragedObject *object,
               const gchar *uevent_action, GDBusInterface *_iface)
{
  const gchar *mp_name = NULL;
  gboolean rc = FALSE;
  struct dmmp_mpath *mpath = NULL;

  storaged_debug ("Multipath: _block_update: got udevent_action %s",
                  uevent_action);

  if ((! MP_MODULE_IS_UDEV_ADD(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_CHANGE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_ONLINE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_OFFLINE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_REMOVE(uevent_action)))
    {
      storaged_warning ("Multipath: BUG: Got unknown udev action: %s,"
                        "ignoring", (const char *) uevent_action);
      goto out;
    }

  mp_name = _mpath_name_of_std_obj (object);

  if (mp_name == NULL)
    goto out;

  mpath = _mpath_get(_state_get (object), mp_name, uevent_action);

  rc = std_lx_blk_mp_update (STORAGED_LINUX_BLOCK_MULTIPATH (_iface),
                             STORAGED_LINUX_BLOCK_OBJECT (object),
                             uevent_action, mpath);
out:
  g_free ((gchar *) mp_name);

  return rc;
}

StoragedModuleInterfaceInfo **
storaged_module_get_block_object_iface_setup_entries (void)
{
  StoragedModuleInterfaceInfo **iface;

  iface = g_new0 (StoragedModuleInterfaceInfo *, 2);
  iface[0] = g_new0 (StoragedModuleInterfaceInfo, 1);
  iface[0]->has_func = &_block_check;
  iface[0]->connect_func = &_block_connect;
  iface[0]->update_func = &_block_update;
  iface[0]->skeleton_type = STORAGED_TYPE_LINUX_BLOCK_MULTIPATH;

  return iface;
}

StoragedModuleInterfaceInfo **
storaged_module_get_drive_object_iface_setup_entries (void)
{
  StoragedModuleInterfaceInfo **iface;

  iface = g_new0 (StoragedModuleInterfaceInfo *, 2);
  iface[0] = g_new0 (StoragedModuleInterfaceInfo, 1);
  iface[0]->has_func = &_drive_check;
  iface[0]->connect_func = &_drive_connect;
  iface[0]->update_func = &_drive_update;
  iface[0]->skeleton_type = STORAGED_TYPE_LINUX_DRIVE_MULTIPATH;

  return iface;
}

StoragedModuleObjectNewFunc *
storaged_module_get_object_new_funcs (void)
{
  return NULL;
}

static GDBusInterfaceSkeleton *
_manager_iface_new (StoragedDaemon *daemon)
{
  StoragedLinuxManagerMultipath *manager;

  manager = std_lx_mgr_mp_new ();

  return G_DBUS_INTERFACE_SKELETON (manager);
}

StoragedModuleNewManagerIfaceFunc *
storaged_module_get_new_manager_iface_funcs (void)
{
  StoragedModuleNewManagerIfaceFunc *funcs = NULL;
  funcs = g_new0 (StoragedModuleNewManagerIfaceFunc, 2);
  funcs[0] = &_manager_iface_new;

  return funcs;
}

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
