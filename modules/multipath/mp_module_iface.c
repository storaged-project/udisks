/*
 * Copyright (C) 2015 - 2017 Gris Ge <fge@redhat.com>
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

#include <modules/udisksmoduleiface.h>

#include <udisks/udisks-generated.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udiskslinuxdriveobject.h>
#include <src/udiskslinuxdevice.h>
#include <src/udiskslinuxdrive.h>
#include <src/udisksdaemon.h>
#include <src/udisksmodulemanager.h>

#include "mp_types.h"

#define __RETRY_COUNT            30
#define __RETRY_INTERVAL         2
/* Multipathd might not ready for IPC communication when udev event is
 * trigged, we have to do retry after _RETRY_INTERVAL seconds sleep.
 */


struct _MultipathPluginState {
  struct dmmp_context *dmmp_ctx;
  struct dmmp_mpath **dmmp_mps;
  uint32_t dmmp_mp_count;
};

static struct _MultipathPluginState *
_state_new (void);

static int
_state_refresh (struct _MultipathPluginState *p);

static void
_state_free (struct _MultipathPluginState *p);

static struct _MultipathPluginState *
_state_get (UDisksDaemon *daemon);

static const gchar *
_mpath_name_of_ud_obj(UDisksObject *ud_obj);

static struct dmmp_mpath *
_mpath_update (UDisksDaemon *daemon, const gchar *mpath_name,
               const gchar *blk_name, const gchar *action);

static void
_ud_mp_obj_refresh (UDisksDaemon *daemon, struct dmmp_mpath *mpath,
                    const gchar *blk_name, const gchar *action);

static struct dmmp_mpath *
_dmmp_mpath_search (struct dmmp_mpath **mps, uint32_t mp_count,
                    const char *mpath_name, const char *blk_name);

static struct _MultipathPluginState *
_state_new (void)
{
  struct _MultipathPluginState *p = NULL;

  p = (struct _MultipathPluginState *) g_malloc
    (sizeof(struct _MultipathPluginState));
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

static int
_state_refresh (struct _MultipathPluginState *p)
{
  int mp_rc = DMMP_OK;
  uint32_t i = 0;
  uint32_t pg_count = 0;
  struct dmmp_path_group **pgs = NULL;
  struct dmmp_mpath *mp = NULL;

  dmmp_mpath_array_free (p->dmmp_mps, p->dmmp_mp_count);
  udisks_debug ("Multipath: _state_refresh(): requesting multipath list");
  mp_rc = dmmp_mpath_array_get (p->dmmp_ctx, &(p->dmmp_mps),
                                &(p->dmmp_mp_count));
  if (mp_rc == DMMP_OK)
    {
      udisks_debug ("_state_refresh(): Got %" PRIu32 " mpaths",
                    p->dmmp_mp_count);
      for (; i < p->dmmp_mp_count; ++i)
        {
          mp = p->dmmp_mps[i];
          dmmp_path_group_array_get(mp, &pgs, &pg_count);
          udisks_debug ("_state_refresh(): Got %s with %" PRIu32
                        " path groups", dmmp_mpath_name_get(mp), pg_count);
        }
      return mp_rc;
    }

  udisks_debug ("Mutlipath: _state_refresh(): Failed to retrieve mpath list "
                "%d: %s", mp_rc, dmmp_strerror(mp_rc));
  return mp_rc;
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
_state_get (UDisksDaemon *daemon)
{
  UDisksModuleManager *manager = NULL;

  manager = udisks_daemon_get_module_manager (daemon);
  return (struct _MultipathPluginState *)
    udisks_module_manager_get_module_state_pointer (manager, MP_MODULE_NAME);
}

static void
_ud_mp_obj_refresh (UDisksDaemon *daemon, struct dmmp_mpath *mpath,
                    const gchar *blk_name, const gchar *action)
{
  UDisksLinuxMultipathObject *ud_lx_mp_obj = NULL;
  UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj = NULL;
  const char *mpath_name = NULL;
  const char *wwid = NULL;
  const gchar *mp_obj_path = NULL;
  const gchar *mp_path_obj_path = NULL;
  GDBusObjectManagerServer *dbus_mgr = NULL;

  if (mpath == NULL)
    return;

  mpath_name = dmmp_mpath_name_get (mpath);
  wwid = dmmp_mpath_wwid_get (mpath);

  udisks_debug ("Multipath: _ud_mp_obj_refresh(): %s %s %s",
                mpath_name, wwid, blk_name);

  mp_obj_path = ud_lx_mp_obj_path_gen (mpath_name, wwid);

  dbus_mgr = udisks_daemon_get_object_manager (daemon);

  if (dbus_mgr == NULL)
    goto out;

  ud_lx_mp_obj = ud_lx_mp_obj_get (dbus_mgr, mp_obj_path);
  if (ud_lx_mp_obj == NULL)
    /* Create new */
    ud_lx_mp_obj = ud_lx_mp_obj_new (dbus_mgr, mpath);
  else
    {
      if (MP_MODULE_IS_UDEV_ADD(action))
        {
          if (blk_name == NULL)
            goto out;
          if (strncmp (blk_name, "dm-", strlen("dm-")) == 0)
            goto out;
          mp_path_obj_path = ud_lx_mp_path_obj_path_gen (mp_obj_path,
                                                         blk_name);
          ud_lx_mp_path_obj = ud_lx_mp_path_obj_get (dbus_mgr,
                                                     mp_path_obj_path);
          if (ud_lx_mp_path_obj != NULL)
            goto out;
        }
      /* Update */
      ud_lx_mp_obj_update (ud_lx_mp_obj, mpath);
    }

out:
  g_free ((gpointer) mp_obj_path);
  g_free ((gpointer) mp_path_obj_path);
  if (ud_lx_mp_obj != NULL)
    g_object_unref (ud_lx_mp_obj);
  if (ud_lx_mp_path_obj != NULL)
    g_object_unref (ud_lx_mp_path_obj);
}

/*
 * Returned pointer should be freed by g_free().
 */
static const gchar *
_mpath_name_of_ud_obj(UDisksObject *ud_obj)
{
  const gchar *mpath_name = NULL;
  UDisksLinuxDevice *ud_lx_dev = NULL;

  if (UDISKS_IS_LINUX_DRIVE_OBJECT (ud_obj))
    ud_lx_dev = udisks_linux_drive_object_get_device
      (UDISKS_LINUX_DRIVE_OBJECT (ud_obj), FALSE /* dm dev is OK */);
  else if (UDISKS_IS_LINUX_BLOCK_OBJECT (ud_obj))
    ud_lx_dev = udisks_linux_block_object_get_device
      (UDISKS_LINUX_BLOCK_OBJECT (ud_obj));
  else
    return NULL;

  mpath_name = udisks_linux_device_multipath_name (ud_lx_dev);

  if (ud_lx_dev != NULL)
    g_object_unref (ud_lx_dev);

  return mpath_name;
}

/*
 * For udev add action, use cached data, for other udev action, refresh the
 * cache.
 * If blk_name == NULL, skip the path check.
 */
static struct dmmp_mpath *
_mpath_update (UDisksDaemon *daemon, const gchar *mpath_name,
               const gchar *blk_name, const gchar *action)
{
  struct dmmp_mpath *mpath = NULL;
  int i = __RETRY_COUNT;
  int mp_rc = DMMP_OK;
  struct _MultipathPluginState *state = NULL;

  if ((mpath_name == NULL) || (daemon == NULL) || (action == NULL))
    return NULL;

  state = _state_get (daemon);
  if (state == NULL)
    return NULL;

  if ((MP_MODULE_IS_UDEV_ADD(action)) && (state->dmmp_mp_count != 0))
    goto search;

refresh:
  udisks_debug ("Multipath: Refreshing data from multipathd for %s %s",
                mpath_name, blk_name);
  mp_rc = _state_refresh (state);

  if (mp_rc == DMMP_ERR_NO_DAEMON)
    {
      udisks_warning ("Multipath: multipathd daemon is not running");
      goto out;
    }

search:
  mpath = _dmmp_mpath_search (state->dmmp_mps, state->dmmp_mp_count,
                              mpath_name, blk_name);

  /* Multipathd might busy with dealing udev events which cause libdmmp
   * return NULL or partial data.
   * Since the caller of this function is getting the mpath_name from udev,
   * we should keep refresh(with timeout) tile multipathd is providing required
   * data.
   */
  if (mpath == NULL)
    {
      if (i >= 0)
        {
          --i;
          sleep(__RETRY_INTERVAL);
          goto refresh;
        }
      else
        udisks_warning ("Multipath: _mpath_update(): Failed to find "
                        "mpath of '%s', %s", mpath_name, blk_name);
    }

out:
  if (mpath != NULL)
    udisks_debug ("Multipath: _mpath_update(): Found mpath %s, %s", mpath_name,
                  dmmp_mpath_wwid_get(mpath));
  _ud_mp_obj_refresh (daemon, mpath, blk_name, action);
  return mpath;
}

static struct dmmp_mpath *
_dmmp_mpath_search (struct dmmp_mpath **mps, uint32_t mp_count,
                    const char *mpath_name, const char *blk_name)
{
  struct dmmp_mpath *cur_mpath = NULL;
  struct dmmp_path_group **pgs = NULL;
  struct dmmp_path **ps = NULL;
  const char *cur_mpath_name = NULL;
  uint32_t pg_count = 0;
  uint32_t p_count = 0;
  uint32_t i = 0;
  uint32_t j = 0;
  uint32_t m = 0;

  udisks_debug ("Multipath: _dmmp_mpath_search(): Searching '%s' '%s'",
                mpath_name, blk_name);

  for (i = 0; i < mp_count; ++i)
    {
      cur_mpath = mps[i];
      cur_mpath_name = dmmp_mpath_name_get (cur_mpath);
      if (g_strcmp0 (cur_mpath_name, mpath_name) != 0)
        continue;

      if (blk_name == NULL)
        return cur_mpath;

      if ((strncmp (blk_name, "dm-", strlen("dm-")) == 0) &&
          (strcmp (blk_name, dmmp_mpath_kdev_name_get (cur_mpath)) == 0))
        /* Is searching the dm-XX block of mpath */
        return cur_mpath;

      /* Search for path */
      dmmp_path_group_array_get (cur_mpath, &pgs, &pg_count);

      for (j = 0; j < pg_count; ++j)
        {
          dmmp_path_array_get (pgs[j], &ps, &p_count);
          for (m = 0; m < p_count; ++m)
            if (g_strcmp0 (blk_name, dmmp_path_blk_name_get (ps[m])) == 0)
              return cur_mpath;
        }
    }
  udisks_debug ("Multipath: _dmmp_mpath_search(): Not found for '%s' '%s'",
                mpath_name, blk_name);
  return NULL;
}

gchar *
udisks_module_id (void)
{
  return g_strdup (MP_MODULE_NAME);
}

gpointer
udisks_module_init (UDisksDaemon *daemon)
{
  struct _MultipathPluginState *state = NULL;

  udisks_debug ("Multipath: udisks_module_init ()");

  state = _state_new ();
  if (state == NULL)    /* no memory */
    return NULL;

  udisks_debug ("udisks_module_init(): requesting multipath list");

  return state;
}

void
udisks_module_teardown (UDisksDaemon *daemon)
{
  struct _MultipathPluginState *state = NULL;
  UDisksModuleManager *manager = NULL;

  udisks_debug ("Multipath: udisks_module_teardown ()");

  manager = udisks_daemon_get_module_manager (daemon);
  state = (struct _MultipathPluginState *)
    udisks_module_manager_get_module_state_pointer (manager, MP_MODULE_NAME);

  _state_free(state);
}

static gboolean
_drive_check (UDisksObject *object)
{
  gboolean rc = FALSE;
  const gchar *mpath_name = NULL;

  udisks_debug ("Multipath: _drive_check()");

  mpath_name = _mpath_name_of_ud_obj (object);

  if (mpath_name != NULL)
    rc = TRUE;

  g_free ((gchar *) mpath_name);

  if (rc == TRUE)
    udisks_debug ("Multipath: _drive_check() is mpath");
  else
    udisks_debug ("Multipath: _drive_check() is not mpath");
  return rc;
}

static void
_drive_connect (UDisksObject *object)
{
}

static gboolean
_drive_update (UDisksObject *object,
               const gchar *uevent_action, GDBusInterface *_iface)
{
  const gchar *mp_name = NULL;
  gboolean rc = FALSE;
  UDisksDaemon *daemon = NULL;
  struct dmmp_mpath *mpath = NULL;

  udisks_debug ("Multipath: _drive_update: got udevent_action %s",
                uevent_action);

  if ((! MP_MODULE_IS_UDEV_ADD(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_CHANGE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_ONLINE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_OFFLINE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_REMOVE(uevent_action)))
    {
      udisks_warning ("BUG: Multipath: Got unknown udev action: %s,"
                      "ignoring", (const char *) uevent_action);
      goto out;
    }

  mp_name = _mpath_name_of_ud_obj (object);

  if (mp_name == NULL)
    goto out;

  udisks_debug ("Multipath: _drive_update(): mpath_name '%s'", mp_name);

  daemon = udisks_linux_drive_object_get_daemon
    (UDISKS_LINUX_DRIVE_OBJECT (object));
  mpath = _mpath_update (daemon, mp_name, NULL, uevent_action);

  rc = ud_lx_drv_mp_update (UDISKS_LINUX_DRIVE_MULTIPATH (_iface),
                            UDISKS_LINUX_DRIVE_OBJECT (object),
                            uevent_action, mpath);
out:
  g_free ((gchar *) mp_name);

  return rc;
}

/*
 * Return TRUE if /dev/dm-X is multipath, or /dev/sdb is multipath slave.
 */
static gboolean
_block_check (UDisksObject *object)
{
  gboolean rc = FALSE;
  const gchar *mp_name = NULL;

  udisks_debug ("Multipath: _block_check ()");

  mp_name = _mpath_name_of_ud_obj (object);

  if (mp_name != NULL)
      rc = TRUE;

  if (rc == TRUE)
    udisks_debug ("Multipath: _block_check() is mpath");
  else
    udisks_debug ("Multipath: _block_check() is not mpath");

  g_free ((gchar *) mp_name);

  return rc;
}

static void
_block_connect (UDisksObject *object)
{
}

static gboolean
_block_update (UDisksObject *object,
               const gchar *uevent_action, GDBusInterface *_iface)
{
  const gchar *mp_name = NULL;
  gboolean rc = FALSE;
  const gchar *blk_name = NULL;
  UDisksLinuxDevice *ud_lx_dev = NULL;
  UDisksDaemon *daemon = NULL;
  struct dmmp_mpath *mpath = NULL;

  udisks_debug ("Multipath: _block_update: got uevent_action %s",
                uevent_action);

  if ((! MP_MODULE_IS_UDEV_ADD(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_CHANGE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_ONLINE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_OFFLINE(uevent_action)) &&
      (! MP_MODULE_IS_UDEV_REMOVE(uevent_action)))
    {
      udisks_warning ("Multipath: BUG: Got unknown udev action: %s,"
                      "ignoring", (const char *) uevent_action);
      goto out;
    }

  mp_name = _mpath_name_of_ud_obj (object);

  if (mp_name == NULL)
    goto out;

  ud_lx_dev = udisks_linux_block_object_get_device
    ( UDISKS_LINUX_BLOCK_OBJECT (object));

  blk_name = g_udev_device_get_name (ud_lx_dev->udev_device);

  daemon = udisks_linux_block_object_get_daemon
    (UDISKS_LINUX_BLOCK_OBJECT (object));

  mpath = _mpath_update (daemon, mp_name, blk_name, uevent_action);

  rc = ud_lx_blk_mp_update (UDISKS_LINUX_BLOCK_MULTIPATH (_iface),
                            UDISKS_LINUX_BLOCK_OBJECT (object),
                            uevent_action, mpath, blk_name);
out:
  if (ud_lx_dev != NULL)
    g_object_unref (ud_lx_dev);

  g_free ((gchar *) mp_name);

  return rc;
}

UDisksModuleInterfaceInfo **
udisks_module_get_block_object_iface_setup_entries (void)
{
  UDisksModuleInterfaceInfo **iface;

  iface = g_new0 (UDisksModuleInterfaceInfo *, 2);
  iface[0] = g_new0 (UDisksModuleInterfaceInfo, 1);
  iface[0]->has_func = &_block_check;
  iface[0]->connect_func = &_block_connect;
  iface[0]->update_func = &_block_update;
  iface[0]->skeleton_type = UDISKS_TYPE_LINUX_BLOCK_MULTIPATH;

  return iface;
}

UDisksModuleInterfaceInfo **
udisks_module_get_drive_object_iface_setup_entries (void)
{
  UDisksModuleInterfaceInfo **iface;

  iface = g_new0 (UDisksModuleInterfaceInfo *, 2);
  iface[0] = g_new0 (UDisksModuleInterfaceInfo, 1);
  iface[0]->has_func = &_drive_check;
  iface[0]->connect_func = &_drive_connect;
  iface[0]->update_func = &_drive_update;
  iface[0]->skeleton_type = UDISKS_TYPE_LINUX_DRIVE_MULTIPATH;

  return iface;
}

UDisksModuleObjectNewFunc *
udisks_module_get_object_new_funcs (void)
{
  return NULL;
}

static GDBusInterfaceSkeleton *
_manager_iface_new (UDisksDaemon *daemon)
{
  UDisksLinuxManagerMultipath *manager;

  manager = ud_lx_mgr_mp_new ();

  return G_DBUS_INTERFACE_SKELETON (manager);
}

UDisksModuleNewManagerIfaceFunc *
udisks_module_get_new_manager_iface_funcs (void)
{
  UDisksModuleNewManagerIfaceFunc *funcs = NULL;
  funcs = g_new0 (UDisksModuleNewManagerIfaceFunc, 2);
  funcs[0] = &_manager_iface_new;

  return funcs;
}
