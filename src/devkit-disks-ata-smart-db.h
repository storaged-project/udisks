/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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

#ifndef __DEVKIT_DISKS_ATA_SMART_DB_H__
#define __DEVKIT_DISKS_ATA_SMART_DB_H__

#include "devkit-disks-types.h"

G_BEGIN_DECLS

#define DEVKIT_DISKS_TYPE_ATA_SMART_DB         (devkit_disks_ata_smart_db_get_type ())
#define DEVKIT_DISKS_ATA_SMART_DB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), DEVKIT_DISKS_TYPE_ATA_SMART_DB, DevkitDisksAtaSmartDb))
#define DEVKIT_DISKS_ATA_SMART_DB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), DEVKIT_DISKS_TYPE_ATA_SMART_DB, DevkitDisksAtaSmartDbClass))
#define DEVKIT_DISKS_IS_ATA_SMART_DB(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), DEVKIT_DISKS_TYPE_ATA_SMART_DB))
#define DEVKIT_DISKS_IS_ATA_SMART_DB_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), DEVKIT_DISKS_TYPE_ATA_SMART_DB))
#define DEVKIT_DISKS_ATA_SMART_DB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), DEVKIT_DISKS_TYPE_ATA_SMART_DB, DevkitDisksAtaSmartDbClass))

typedef struct DevkitDisksAtaSmartDbClass   DevkitDisksAtaSmartDbClass;
typedef struct DevkitDisksAtaSmartDbPrivate DevkitDisksAtaSmartDbPrivate;

struct DevkitDisksAtaSmartDb
{
        GObject                       parent;
        DevkitDisksAtaSmartDbPrivate *priv;
};

struct DevkitDisksAtaSmartDbClass
{
        GObjectClass parent_class;
};

typedef gboolean (*DevkitDisksAtaSmartDbGetEntriesFunc) (time_t      time_collected,
                                                         gboolean    is_failing,
                                                         gboolean    is_failing_valid,
                                                         gboolean    has_bad_sectors,
                                                         gboolean    has_bad_attributes,
                                                         gdouble     temperature_kelvin,
                                                         guint64     power_on_seconds,
                                                         const void *blob,
                                                         gsize       blob_size,
                                                         gpointer    user_data);

GType                  devkit_disks_ata_smart_db_get_type       (void) G_GNUC_CONST;
DevkitDisksAtaSmartDb *devkit_disks_ata_smart_db_new            (void);
void                   devkit_disks_ata_smart_db_add_entry      (DevkitDisksAtaSmartDb              *db,
                                                                 DevkitDisksDevice                  *device,
                                                                 time_t                              time_collected,
                                                                 gboolean                            is_failing,
                                                                 gboolean                            is_failing_valid,
                                                                 gboolean                            has_bad_sectors,
                                                                 gboolean                            has_bad_attributes,
                                                                 gdouble                             temperature_kelvin,
                                                                 guint64                             power_on_seconds,
                                                                 const void                         *blob,
                                                                 gsize                               blob_size);
void                   devkit_disks_ata_smart_db_delete_entries (DevkitDisksAtaSmartDb              *db,
                                                                 time_t                              cut_off_point);
gboolean               devkit_disks_ata_smart_db_get_entries    (DevkitDisksAtaSmartDb              *db,
                                                                 DevkitDisksDevice                  *device,
                                                                 time_t                              since,
                                                                 time_t                              until,
                                                                 guint64                             spacing,
                                                                 DevkitDisksAtaSmartDbGetEntriesFunc callback,
                                                                 gpointer                            user_data);

G_END_DECLS

#endif /* __DEVKIT_DISKS_ATA_SMART_DB_H__ */
