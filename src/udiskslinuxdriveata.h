/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#ifndef __UDISKS_LINUX_DRIVE_ATA_H__
#define __UDISKS_LINUX_DRIVE_ATA_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_LINUX_DRIVE_ATA  (udisks_linux_drive_ata_get_type ())
#define UDISKS_LINUX_DRIVE_ATA(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_LINUX_DRIVE_ATA, UDisksLinuxDriveAta))
#define UDISKS_IS_LINUX_DRIVE_ATA(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_LINUX_DRIVE_ATA))

GType           udisks_linux_drive_ata_get_type           (void) G_GNUC_CONST;
UDisksDriveAta *udisks_linux_drive_ata_new                (void);
gboolean        udisks_linux_drive_ata_update             (UDisksLinuxDriveAta     *drive,
                                                           UDisksLinuxDriveObject  *object);
gboolean        udisks_linux_drive_ata_refresh_smart_sync (UDisksLinuxDriveAta     *drive,
                                                           gboolean                 nowakeup,
                                                           const gchar             *simulate_path,
                                                           GCancellable            *cancellable,
                                                           GError                 **error);
gboolean        udisks_linux_drive_ata_smart_selftest_sync (UDisksLinuxDriveAta     *drive,
                                                            const gchar             *type,
                                                            GCancellable            *cancellable,
                                                            GError                 **error);
gboolean        udisks_linux_drive_ata_secure_erase_sync   (UDisksLinuxDriveAta     *drive,
                                                            uid_t                    caller_uid,
                                                            gboolean                 enhanced,
                                                            GError                 **error);

void            udisks_linux_drive_ata_apply_configuration (UDisksLinuxDriveAta     *drive,
                                                            UDisksLinuxDevice       *device,
                                                            GVariant                *configuration);

G_END_DECLS

#endif /* __UDISKS_LINUX_DRIVE_ATA_H__ */
