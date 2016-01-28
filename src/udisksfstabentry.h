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

#ifndef __UDISKS_FSTAB_ENTRY_H__
#define __UDISKS_FSTAB_ENTRY_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_FSTAB_ENTRY         (udisks_fstab_entry_get_type ())
#define UDISKS_FSTAB_ENTRY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_FSTAB_ENTRY, UDisksFstabEntry))
#define UDISKS_IS_FSTAB_ENTRY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_FSTAB_ENTRY))

GType            udisks_fstab_entry_get_type     (void) G_GNUC_CONST;
const gchar     *udisks_fstab_entry_get_fsname   (UDisksFstabEntry *entry);
const gchar     *udisks_fstab_entry_get_dir      (UDisksFstabEntry *entry);
const gchar     *udisks_fstab_entry_get_fstype   (UDisksFstabEntry *entry);
const gchar     *udisks_fstab_entry_get_opts     (UDisksFstabEntry *entry);
gint             udisks_fstab_entry_get_freq     (UDisksFstabEntry *entry);
gint             udisks_fstab_entry_get_passno   (UDisksFstabEntry *entry);
gint             udisks_fstab_entry_compare      (UDisksFstabEntry *entry,
                                                  UDisksFstabEntry *other_entry);

G_END_DECLS

#endif /* __UDISKS_FSTAB_ENTRY_H__ */
