/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2010 David Zeuthen <zeuthen@gmail.com>
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

#ifndef __UDISKS_CLEANUP_H__
#define __UDISKS_CLEANUP_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_CLEANUP         (udisks_cleanup_get_type ())
#define UDISKS_CLEANUP(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_CLEANUP, UDisksCleanup))
#define UDISKS_IS_CLEANUP(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_CLEANUP))

GType            udisks_cleanup_get_type             (void) G_GNUC_CONST;
UDisksCleanup   *udisks_cleanup_new                  (UDisksDaemon  *daemon);
UDisksDaemon    *udisks_cleanup_get_daemon           (UDisksCleanup *cleanup);

void             udisks_cleanup_start                (UDisksCleanup *cleanup);
void             udisks_cleanup_stop                 (UDisksCleanup *cleanup);
void             udisks_cleanup_check                (UDisksCleanup *cleanup);

/* mounted-fs */

void             udisks_cleanup_add_mounted_fs       (UDisksCleanup   *cleanup,
                                                      const gchar     *mount_point,
                                                      dev_t            block_device,
                                                      uid_t            uid,
                                                      gboolean         fstab_mount);

gchar           *udisks_cleanup_find_mounted_fs      (UDisksCleanup   *cleanup,
                                                      dev_t            block_device,
                                                      uid_t           *out_uid,
                                                      gboolean        *out_fstab_mount);

/* unlocked-luks */

void             udisks_cleanup_add_unlocked_luks      (UDisksCleanup   *cleanup,
                                                        dev_t            cleartext_device,
                                                        dev_t            crypto_device,
                                                        const gchar     *dm_uuid,
                                                        uid_t            uid);

dev_t            udisks_cleanup_find_unlocked_luks     (UDisksCleanup   *cleanup,
                                                        dev_t            crypto_device,
                                                        uid_t           *out_uid);

/* loop */

void             udisks_cleanup_add_loop      (UDisksCleanup   *cleanup,
                                               const gchar     *device_file,
                                               const gchar     *backing_file,
                                               dev_t            backing_file_device,
                                               uid_t            uid);

gboolean         udisks_cleanup_has_loop      (UDisksCleanup   *cleanup,
                                               const gchar     *device_file,
                                               uid_t           *out_uid);

G_END_DECLS

#endif /* __UDISKS_CLEANUP_H__ */
