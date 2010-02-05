/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
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

#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <dbus/dbus-glib.h>
#include <gudev/gudev.h>
#include <sys/types.h>

#include "types.h"

G_BEGIN_DECLS

#define TYPE_DEVICE         (device_get_type ())
#define DEVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TYPE_DEVICE, Device))
#define DEVICE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), TYPE_DEVICE, DeviceClass))
#define IS_DEVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TYPE_DEVICE))
#define IS_DEVICE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), TYPE_DEVICE))
#define DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TYPE_DEVICE, DeviceClass))

typedef struct DeviceClass DeviceClass;
typedef struct DevicePrivate DevicePrivate;

struct Device
{
  GObject parent;
  DevicePrivate *priv;
};

struct DeviceClass
{
  GObjectClass parent_class;
};

GType
device_get_type (void)
G_GNUC_CONST;

Device *device_new (Daemon *daemon, GUdevDevice *d);

gboolean device_changed (Device *device, GUdevDevice *d, gboolean synthesized);

void device_removed (Device *device);

/* local methods */

const char *device_local_get_object_path (Device *device);
const char * device_local_get_native_path (Device *device);

dev_t device_local_get_dev (Device *device);
const char *device_local_get_device_file (Device *device);

/* exported methods */

gboolean device_job_cancel (Device *device,
                            DBusGMethodInvocation *context);

gboolean device_filesystem_mount (Device *device,
                                  const char *filesystem_type,
                                  char **options,
                                  DBusGMethodInvocation *context);

gboolean device_filesystem_unmount (Device *device,
                                    char **options,
                                    DBusGMethodInvocation *context);

gboolean device_filesystem_list_open_files (Device *device,
                                            DBusGMethodInvocation *context);

gboolean device_drive_eject (Device *device,
                             char **options,
                             DBusGMethodInvocation *context);

gboolean device_filesystem_check (Device *device,
                                  char **options,
                                  DBusGMethodInvocation *context);

gboolean device_filesystem_create (Device *device,
                                   const char *fstype,
                                   char **options,
                                   DBusGMethodInvocation *context);

gboolean device_partition_delete (Device *device,
                                  char **options,
                                  DBusGMethodInvocation *context);

gboolean device_partition_create (Device *device,
                                  guint64 offset,
                                  guint64 size,
                                  const char *type,
                                  const char *label,
                                  char **flags,
                                  char **options,
                                  const char *fstype,
                                  char **fsoptions,
                                  DBusGMethodInvocation *context);

gboolean device_partition_modify (Device *device,
                                  const char *type,
                                  const char *label,
                                  char **flags,
                                  DBusGMethodInvocation *context);

gboolean device_partition_table_create (Device *device,
                                        const char *scheme,
                                        char **options,
                                        DBusGMethodInvocation *context);

gboolean device_luks_unlock (Device *device,
                             const char *secret,
                             char **options,
                             DBusGMethodInvocation *context);

gboolean device_luks_lock (Device *device,
                           char **options,
                           DBusGMethodInvocation *context);

gboolean device_luks_change_passphrase (Device *device,
                                        const char *old_secret,
                                        const char *new_secret,
                                        DBusGMethodInvocation *context);

gboolean device_filesystem_set_label (Device *device,
                                      const char *new_label,
                                      DBusGMethodInvocation *context);

gboolean device_drive_ata_smart_refresh_data (Device *device,
                                              char **options,
                                              DBusGMethodInvocation *context);

gboolean device_drive_ata_smart_get_historical_data (Device *device,
                                                     guint64 since,
                                                     guint64 until,
                                                     guint64 spacing,
                                                     DBusGMethodInvocation *context);

gboolean device_drive_ata_smart_initiate_selftest (Device *device,
                                                   const char *test,
                                                   char **options,
                                                   DBusGMethodInvocation *context);

gboolean device_drive_benchmark (Device *device,
                                 gboolean do_write_benchmark,
                                 char **options,
                                 DBusGMethodInvocation *context);

gboolean device_linux_md_stop (Device *device,
                               char **options,
                               DBusGMethodInvocation *context);

gboolean device_linux_md_check (Device *device,
                                char **options,
                                DBusGMethodInvocation *context);

gboolean device_linux_md_add_spare (Device *device,
                                    char *component,
                                    char **options,
                                    DBusGMethodInvocation *context);

gboolean device_linux_md_expand (Device *device,
                                 GPtrArray *components,
                                 char **options,
                                 DBusGMethodInvocation *context);

gboolean device_linux_md_remove_component (Device *device,
                                           char *component,
                                           char **options,
                                           DBusGMethodInvocation *context);

gboolean device_drive_inhibit_polling (Device *device,
                                       char **options,
                                       DBusGMethodInvocation *context);

gboolean device_drive_uninhibit_polling (Device *device,
                                         char *cookie,
                                         DBusGMethodInvocation *context);

gboolean device_drive_poll_media (Device *device,
                                  DBusGMethodInvocation *context);

gboolean device_drive_detach (Device *device,
                              char **options,
                              DBusGMethodInvocation *context);

gboolean device_drive_set_spindown_timeout (Device *device,
                                            int timeout_seconds,
                                            char **options,
                                            DBusGMethodInvocation *context);

gboolean device_drive_unset_spindown_timeout (Device *device,
                                              char *cookie,
                                              DBusGMethodInvocation *context);

gboolean device_linux_lvm2_lv_stop (Device *device,
                                    char **options,
                                    DBusGMethodInvocation *context);


G_END_DECLS

#endif /* __DEVICE_H__ */
