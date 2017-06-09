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

#ifndef __MP_TYPES_H__
#define __MP_TYPES_H__

#include <gio/gio.h>
#include <polkit/polkit.h>
#include <udisks/udisks.h>
#include <gudev/gudev.h>
#include <sys/types.h>
#include <src/udiskslogging.h>
#include <libdmmp/libdmmp.h>

#include "mp_generated.h"

G_BEGIN_DECLS

#define MP_MODULE_NAME "multipath"

#define MP_MODULE_UDEV_ACTION_ADD        "add"
#define MP_MODULE_UDEV_ACTION_REMOVE     "remove"
#define MP_MODULE_UDEV_ACTION_CHANGE     "change"
#define MP_MODULE_UDEV_ACTION_ONLINE     "online"
#define MP_MODULE_UDEV_ACTION_OFFLINE    "offline"

#define MP_MODULE_IS_UDEV_ADD(x) \
  strcmp (x, MP_MODULE_UDEV_ACTION_ADD) == 0
#define MP_MODULE_IS_UDEV_REMOVE(x) \
  strcmp (x, MP_MODULE_UDEV_ACTION_REMOVE) == 0
#define MP_MODULE_IS_UDEV_CHANGE(x) \
  strcmp (x, MP_MODULE_UDEV_ACTION_CHANGE) == 0
#define MP_MODULE_IS_UDEV_ONLINE(x) \
  strcmp (x, MP_MODULE_UDEV_ACTION_ONLINE) == 0
#define MP_MODULE_IS_UDEV_OFFLINE(x) \
  strcmp (x, MP_MODULE_UDEV_ACTION_OFFLINE) == 0

/* org.udisks.UDisks.Drive.Multipath dbus interface */
struct _UDisksLinuxDriveMultipath;
typedef struct _UDisksLinuxDriveMultipath UDisksLinuxDriveMultipath;

#define UDISKS_TYPE_LINUX_DRIVE_MULTIPATH (ud_lx_drv_mp_get_type ())

#define UDISKS_LINUX_DRIVE_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_DRIVE_MULTIPATH, \
                               UDisksLinuxDriveMultipath))

#define UDISKS_IS_LINUX_DRIVE_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_DRIVE_MULTIPATH))

GType ud_lx_drv_mp_get_type (void) G_GNUC_CONST;

UDisksLinuxDriveMultipath *ud_lx_drv_mp_new (void);

gboolean
ud_lx_drv_mp_update (UDisksLinuxDriveMultipath *ud_lx_drv_mp,
                     UDisksLinuxDriveObject *ud_lx_drv_obj,
                     const gchar *uevent_action,
                     struct dmmp_mpath *mpath);

/* org.udisks.UDisks.Block.Multipath dbus interface */

struct _UDisksLinuxBlockMultipath;
typedef struct _UDisksLinuxBlockMultipath UDisksLinuxBlockMultipath;

#define UDISKS_TYPE_LINUX_BLOCK_MULTIPATH (ud_lx_blk_mp_get_type ())

#define UDISKS_LINUX_BLOCK_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_BLOCK_MULTIPATH, \
                               UDisksLinuxBlockMultipath))

#define UDISKS_IS_LINUX_BLOCK_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_BLOCK_MULTIPATH))

GType ud_lx_blk_mp_get_type (void) G_GNUC_CONST;

UDisksLinuxBlockMultipath *ud_lx_blk_mp_new (void);

gboolean
ud_lx_blk_mp_update (UDisksLinuxBlockMultipath *ud_lx_blk_mp,
                     UDisksLinuxBlockObject *ud_lx_blk_obj,
                     const gchar *uevent_action, struct dmmp_mpath *mpath,
                     const gchar *blk_name);


/* org.udisks.UDisks.Manager.Multipath dbus interface */

struct _UDisksLinuxManagerMultipath;
typedef struct _UDisksLinuxManagerMultipath UDisksLinuxManagerMultipath;

#define UDISKS_TYPE_LINUX_MANAGER_MULTIPATH (ud_lx_mgr_mp_get_type ())

#define UDISKS_LINUX_MANAGER_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_MANAGER_MULTIPATH, \
                               UDisksLinuxManagerMultipath))
#define UDISKS_IS_LINUX_MANAGER_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_MANAGER_MULTIPATH))

GType ud_lx_mgr_mp_get_type (void) G_GNUC_CONST;
UDisksLinuxManagerMultipath *ud_lx_mgr_mp_new (void);

/* org.udisks.UDisks.Multipath dbus interface */
struct _UDisksLinuxMultipath;
typedef struct _UDisksLinuxMultipath UDisksLinuxMultipath;

#define UDISKS_TYPE_LINUX_MULTIPATH (ud_lx_mp_get_type ())

#define UDISKS_LINUX_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_MULTIPATH, \
                               UDisksLinuxMultipath))
#define UDISKS_IS_LINUX_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_MULTIPATH))

GType ud_lx_mp_get_type (void) G_GNUC_CONST;

UDisksLinuxMultipath *ud_lx_mp_new (struct dmmp_mpath *mpath,
                                    const gchar *mp_obj_path);

gboolean ud_lx_mp_update (UDisksLinuxMultipath *ud_lx_mp,
                          struct dmmp_mpath *mpath,
                          const gchar *mp_obj_path);

/* org.udisks.UDisks.Multipath dbus object*/
struct _UDisksLinuxMultipathObject;
typedef struct _UDisksLinuxMultipathObject UDisksLinuxMultipathObject;

#define UDISKS_TYPE_LINUX_MULTIPATH_OBJECT (ud_lx_mp_obj_get_type ())

#define UDISKS_LINUX_MULTIPATH_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_MULTIPATH_OBJECT, \
                               UDisksLinuxMultipathObject))
#define UDISKS_IS_LINUX_MULTIPATH_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_MULTIPATH_OBJECT))

GType ud_lx_mp_obj_get_type (void) G_GNUC_CONST;

/*
 * Don't unref the returned object. The object is taken by object manager.
 */
UDisksLinuxMultipathObject *
ud_lx_mp_obj_new (GDBusObjectManagerServer *dbus_mgr,
                  struct dmmp_mpath *mpath);

gboolean ud_lx_mp_obj_update (UDisksLinuxMultipathObject *ud_lx_mp_obj,
                              struct dmmp_mpath *mpath);

void ud_lx_mp_obj_set_block (UDisksLinuxMultipathObject *ud_lx_mp_obj,
                             const gchar *blk_obj_path);

void ud_lx_mp_obj_set_drive (UDisksLinuxMultipathObject *ud_lx_mp_obj,
                             const gchar *drv_obj_path);

/* org.udisks.UDisks.Multipath.Path dbus object */

struct _UDisksLinuxMultipathPathObject;

typedef struct _UDisksLinuxMultipathPathObject
  UDisksLinuxMultipathPathObject;

#define UDISKS_TYPE_LINUX_MULTIPATH_PATH_OBJECT \
  (ud_lx_mp_path_obj_get_type ())

#define UDISKS_LINUX_MULTIPATH_PATH_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST \
   ((o), UDISKS_TYPE_LINUX_MULTIPATH_PATH_OBJECT, \
    UDisksLinuxMultipathPathObject))
#define UDISKS_IS_LINUX_MULTIPATH_PATH_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE \
   ((o), UDISKS_TYPE_LINUX_MULTIPATH_PATH_OBJECT))

GType ud_lx_mp_path_obj_get_type (void) G_GNUC_CONST;

UDisksLinuxMultipathPathObject *
ud_lx_mp_path_obj_new (GDBusObjectManagerServer *dbus_mgr,
                       struct dmmp_path *mp_path,
                       const gchar *mp_obj_path);

gboolean
ud_lx_mp_path_obj_update
  (UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj,
   struct dmmp_path *mp_path, const gchar *mp_obj_path);

void
ud_lx_mp_path_obj_set_block
  (UDisksLinuxMultipathPathObject *ud_lx_mp_path_obj,
   const gchar *blk_obj_path);

/* org.udisks.UDisks.Multipath.Path dbus interface*/
struct _UDisksLinuxMultipathPath;

typedef struct _UDisksLinuxMultipathPath
  UDisksLinuxMultipathPath;

#define UDISKS_TYPE_LINUX_MULTIPATH_PATH \
  (ud_lx_mp_path_get_type ())

#define UDISKS_LINUX_MULTIPATH_PATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_MULTIPATH_PATH, \
                               UDisksLinuxMultipathPath))

#define UDISKS_IS_LINUX_MULTIPATH_PATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_MULTIPATH_PATH))

GType ud_lx_mp_path_get_type (void) G_GNUC_CONST;

UDisksLinuxMultipathPath *
ud_lx_mp_path_new (void);

gboolean
ud_lx_mp_path_update (UDisksLinuxMultipathPath *ud_lx_mp_path,
                      struct dmmp_path *mp_path, const gchar *mp_obj_path);

/*
 * Need g_free () on returned string.
 */
const gchar *
ud_lx_mp_obj_path_gen (const char *mp_name, const char *wwid);

/*
 * Need g_free () on returned string.
 */
const gchar *
ud_lx_mp_path_obj_path_gen (const gchar *mp_obj_path, const char *blk_name);

/*
 * Need g_object_unref () on returned object.
 */
UDisksLinuxMultipathObject *
ud_lx_mp_obj_get (GDBusObjectManagerServer *dbus_mgr,
                  const gchar *mp_obj_path);

/*
 * Need g_object_unref () on returned object.
 */
UDisksLinuxMultipathPathObject *
ud_lx_mp_path_obj_get (GDBusObjectManagerServer *dbus_mgr,
                       const gchar *mp_path_obj_path);

void
ud_lx_mp_obj_unexport (GDBusObjectManagerServer *dbus_mgr,
                       const gchar *mp_obj_path);

void
ud_lx_mp_path_obj_unexport (GDBusObjectManagerServer *dbus_mgr,
                            const gchar *mp_path_obj_path);

G_END_DECLS

#endif /* __LSM_TYPES_H__ */
