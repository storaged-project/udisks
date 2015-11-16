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

#ifndef __MP_TYPES_H__
#define __MP_TYPES_H__

#include <gio/gio.h>
#include <polkit/polkit.h>
#include <storaged/storaged.h>
#include <gudev/gudev.h>
#include <sys/types.h>
#include <src/storagedlogging.h>
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

/* org.storaged.Storaged.Drive.Multipath dbus interface */
struct _StoragedLinuxDriveMultipath;
typedef struct _StoragedLinuxDriveMultipath StoragedLinuxDriveMultipath;

#define STORAGED_TYPE_LINUX_DRIVE_MULTIPATH (std_lx_drv_mp_get_type ())

#define STORAGED_LINUX_DRIVE_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_DRIVE_MULTIPATH, \
                               StoragedLinuxDriveMultipath))

#define STORAGED_IS_LINUX_DRIVE_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_DRIVE_MULTIPATH))

GType std_lx_drv_mp_get_type (void) G_GNUC_CONST;

StoragedLinuxDriveMultipath *std_lx_drv_mp_new (void);

gboolean
std_lx_drv_mp_update (StoragedLinuxDriveMultipath *std_lx_drv_mp,
                      StoragedLinuxDriveObject *st_lx_drv_obj,
                      const gchar *uevent_action,
                      struct dmmp_mpath *mpath);

/* org.storaged.Storaged.Block.Multipath dbus interface */

struct _StoragedLinuxBlockMultipath;
typedef struct _StoragedLinuxBlockMultipath StoragedLinuxBlockMultipath;

#define STORAGED_TYPE_LINUX_BLOCK_MULTIPATH (std_lx_blk_mp_get_type ())

#define STORAGED_LINUX_BLOCK_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_BLOCK_MULTIPATH, \
                               StoragedLinuxBlockMultipath))

#define STORAGED_IS_LINUX_BLOCK_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_BLOCK_MULTIPATH))

GType std_lx_blk_mp_get_type (void) G_GNUC_CONST;

StoragedLinuxBlockMultipath *std_lx_blk_mp_new (void);

gboolean
std_lx_blk_mp_update (StoragedLinuxBlockMultipath *std_lx_drv_mp,
                      StoragedLinuxBlockObject *st_lx_drv_obj,
                      const gchar *uevent_action,
                      struct dmmp_mpath *mpath);

/* org.storaged.Storaged.Manager.Multipath dbus interface */

struct _StoragedLinuxManagerMultipath;
typedef struct _StoragedLinuxManagerMultipath StoragedLinuxManagerMultipath;

#define STORAGED_TYPE_LINUX_MANAGER_MULTIPATH (std_lx_mgr_mp_get_type ())

#define STORAGED_LINUX_MANAGER_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_MANAGER_MULTIPATH, \
                               StoragedLinuxManagerMultipath))
#define STORAGED_IS_LINUX_MANAGER_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_MANAGER_MULTIPATH))

GType std_lx_mgr_mp_get_type (void) G_GNUC_CONST;
StoragedLinuxManagerMultipath *std_lx_mgr_mp_new (void);

/* org.storaged.Storaged.Multipath dbus interface */
struct _StoragedLinuxMultipath;
typedef struct _StoragedLinuxMultipath StoragedLinuxMultipath;

#define STORAGED_TYPE_LINUX_MULTIPATH (std_lx_mp_get_type ())

#define STORAGED_LINUX_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_MULTIPATH, \
                               StoragedLinuxMultipath))
#define STORAGED_IS_LINUX_MULTIPATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_MULTIPATH))

GType std_lx_mp_get_type (void) G_GNUC_CONST;

StoragedLinuxMultipath *std_lx_mp_new (struct dmmp_mpath *mpath);

gboolean std_lx_mp_update (StoragedLinuxMultipath *std_lx_mp,
                           struct dmmp_mpath *mpath);

/* org.storaged.Storaged.Multipath dbus object*/
struct _StoragedLinuxMultipathObject;
typedef struct _StoragedLinuxMultipathObject StoragedLinuxMultipathObject;

#define STORAGED_TYPE_LINUX_MULTIPATH_OBJECT (std_lx_mp_obj_get_type ())

#define STORAGED_LINUX_MULTIPATH_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_MULTIPATH_OBJECT, \
                               StoragedLinuxMultipathObject))
#define STORAGED_IS_LINUX_MULTIPATH_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_MULTIPATH_OBJECT))

GType std_lx_mp_obj_get_type (void) G_GNUC_CONST;

StoragedLinuxMultipathObject *
std_lx_mp_obj_new (GDBusObjectManagerServer *dbus_mgr,
                   struct dmmp_mpath *mpath);

gboolean std_lx_mp_obj_update (StoragedLinuxMultipathObject *std_lx_mp_obj,
                               struct dmmp_mpath *mpath);

void std_lx_mp_obj_set_block (StoragedLinuxMultipathObject *std_lx_mp_obj,
                              const char *blk_obj_path);

/* org.storaged.Storaged.Multipath.PathGroup dbus object*/
struct _StoragedLinuxMultipathPathGroupObject;
typedef struct _StoragedLinuxMultipathPathGroupObject
  StoragedLinuxMultipathPathGroupObject;

#define STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_OBJECT \
  (std_lx_mp_pg_obj_get_type ())

#define STORAGED_LINUX_MULTIPATH_PATH_GROUP_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST \
    ((o), STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_OBJECT, \
     StoragedLinuxMultipathPathGroupObject))
#define STORAGED_IS_LINUX_MULTIPATH_PATH_GROUP_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE \
    ((o), STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_OBJECT))

GType std_lx_mp_pg_obj_get_type (void) G_GNUC_CONST;

StoragedLinuxMultipathPathGroupObject *
std_lx_mp_pg_obj_new (GDBusObjectManagerServer *dbus_mgr,
                      struct dmmp_path_group *mp_pg,
                      const char *mp_obj_path);

gboolean
std_lx_mp_pg_obj_update
  (StoragedLinuxMultipathPathGroupObject *std_lx_mp_pg_obj,
   struct dmmp_path_group *mp_pg);

/* org.storaged.Storaged.Multipath.PathGroup dbus interface*/
struct _StoragedLinuxMultipathPathGroup;
typedef struct _StoragedLinuxMultipathPathGroup
  StoragedLinuxMultipathPathGroup;

#define STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP (std_lx_mp_pg_get_type ())

#define STORAGED_LINUX_MULTIPATH_PATH_GROUP(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP, \
                               StoragedLinuxMultipathPathGroup))

#define STORAGED_IS_LINUX_MULTIPATH_PATH_GROUP(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP))

GType std_lx_mp_pg_get_type (void) G_GNUC_CONST;

StoragedLinuxMultipathPathGroup *
std_lx_mp_pg_new (struct dmmp_path_group *mp_pg);

gboolean
std_lx_mp_pg_update (StoragedLinuxMultipathPathGroup *std_lx_mp_pg,
                     struct dmmp_path_group *mp_pg);

/* org.storaged.Storaged.Multipath.PathGroup.Path dbus object */

struct _StoragedLinuxMultipathPathGroupPathObject;
typedef struct _StoragedLinuxMultipathPathGroupPathObject
  StoragedLinuxMultipathPathGroupPathObject;

#define STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT \
  (std_lx_mp_path_obj_get_type ())

#define STORAGED_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST \
    ((o), STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT, \
     StoragedLinuxMultipathPathGroupPathObject))
#define STORAGED_IS_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE \
    ((o), STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_PATH_OBJECT))

GType std_lx_mp_path_obj_get_type (void) G_GNUC_CONST;

StoragedLinuxMultipathPathGroupPathObject *
std_lx_mp_path_obj_new (GDBusObjectManagerServer *dbus_mgr,
                        struct dmmp_path *mp_path,
                        const char *mp_pg_obj_path);

gboolean
std_lx_mp_path_obj_update
  (StoragedLinuxMultipathPathGroupPathObject *std_lx_mp_path_obj,
   struct dmmp_path *mp_path);

void
std_lx_mp_path_obj_set_block
  (StoragedLinuxMultipathPathGroupPathObject *std_lx_mp_path_obj,
   const char *blk_obj_path);

/* org.storaged.Storaged.Multipath.PathGroup dbus interface*/
struct _StoragedLinuxMultipathPathGroupPath;
typedef struct _StoragedLinuxMultipathPathGroupPath
  StoragedLinuxMultipathPathGroupPath;

#define STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_PATH \
  (std_lx_mp_path_get_type ())

#define STORAGED_LINUX_MULTIPATH_PATH_GROUP_PATH(o) \
  (G_TYPE_CHECK_INSTANCE_CAST \
    ((o), STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_PATH, \
     StoragedLinuxMultipathPathGroupPath))

#define STORAGED_IS_LINUX_MULTIPATH_PATH_GROUP_PATH(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
    STORAGED_TYPE_LINUX_MULTIPATH_PATH_GROUP_PATH))

GType std_lx_mp_path_get_type (void) G_GNUC_CONST;

StoragedLinuxMultipathPathGroupPath *
std_lx_mp_path_new (struct dmmp_path *mp_path);

gboolean
std_lx_mp_path_update (StoragedLinuxMultipathPathGroupPath *std_lx_mp_path,
                       struct dmmp_path *mp_path);

/*
 * Need g_free () on returned string.
 */
const char *std_lx_mp_obj_path_gen (const char *mp_name, const char *wwid);

/*
 * Need g_free () on returned string.
 */
const char *std_lx_mp_pg_obj_path_gen (const char *mp_obj_path, uint32_t pg_id);

/*
 * Need g_free () on returned string.
 */
const char *std_lx_mp_path_obj_path_gen (const char *mp_pg_obj_path,
                                         const char *blk_name);

/*
 * Need g_object_unref () on returned object.
 */
StoragedLinuxMultipathObject *
std_lx_mp_obj_get (GDBusObjectManager *dbus_mgr, const char *mp_obj_path);

/*
 * Need g_object_unref () on returned object.
 */
StoragedLinuxMultipathPathGroupPathObject *
std_lx_mp_path_obj_get (GDBusObjectManager *dbus_mgr,
                        const char *mp_path_obj_path);

/*
 * No need to unref returned object.
 */
StoragedLinuxMultipathPathGroupObject *
std_lx_mp_pg_obj_search (StoragedLinuxMultipathObject *std_lx_mp_obj,
                         uint32_t pg_id);

/*
 * No need to unref returned object.
 */
StoragedLinuxMultipathPathGroupPathObject *
std_lx_mp_path_obj_search
  (StoragedLinuxMultipathPathGroupObject *std_lx_mp_pg_obj,
   const char *blk_name);

G_END_DECLS

#endif /* __LSM_TYPES_H__ */

/* vim: set ts=2 sts=2 sw=2 tw=79 wrap et cindent fo=tcql : */
/* vim: set cino=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1 : */
/* vim: set cc=79 : */
