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

#ifndef __UDISKS_PERSISTENT_STORE_H__
#define __UDISKS_PERSISTENT_STORE_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

/**
 * UDisksPersistentFlags:
 * @UDISKS_PERSISTENT_FLAGS_NONE: No flags set.
 * @UDISKS_PERSISTENT_FLAGS_NORMAL_STORE: The value should be
 * set/get in the database that persist across reboots.
 * @UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE: The value should be
 * set/get in the database that does not persist across reboots.
 *
 * Flag enumeration used in the #UDisksPersistentStore type.
 */
typedef enum
{
  UDISKS_PERSISTENT_FLAGS_NONE = 0,
  UDISKS_PERSISTENT_FLAGS_NORMAL_STORE = (1<<0),
  UDISKS_PERSISTENT_FLAGS_TEMPORARY_STORE = (1<<1),
} UDisksPersistentFlags;

#define UDISKS_TYPE_PERSISTENT_STORE         (udisks_persistent_store_get_type ())
#define UDISKS_PERSISTENT_STORE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_PERSISTENT_STORE, UDisksPersistentStore))
#define UDISKS_IS_PERSISTENT_STORE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_PERSISTENT_STORE))

GType                   udisks_persistent_store_get_type       (void) G_GNUC_CONST;
UDisksPersistentStore  *udisks_persistent_store_new            (const gchar             *path,
                                                                const gchar             *temp_path);
const gchar            *udisks_persistent_store_get_path       (UDisksPersistentStore   *store);
const gchar            *udisks_persistent_store_get_temp_path  (UDisksPersistentStore   *store);

GVariant               *udisks_persistent_store_get            (UDisksPersistentStore   *store,
                                                                UDisksPersistentFlags    flags,
                                                                const gchar             *key,
                                                                const GVariantType      *type,
                                                                GError                 **error);
gboolean                udisks_persistent_store_set            (UDisksPersistentStore   *store,
                                                                UDisksPersistentFlags    flags,
                                                                const gchar             *key,
                                                                const GVariantType      *type,
                                                                GVariant                *value,
                                                                GError                 **error);


G_END_DECLS

#endif /* __UDISKS_PERSISTENT_STORE_H__ */
