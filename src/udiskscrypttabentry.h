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

#ifndef __UDISKS_CRYPTTAB_ENTRY_H__
#define __UDISKS_CRYPTTAB_ENTRY_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_CRYPTTAB_ENTRY         (udisks_crypttab_entry_get_type ())
#define UDISKS_CRYPTTAB_ENTRY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_CRYPTTAB_ENTRY, UDisksCrypttabEntry))
#define UDISKS_IS_CRYPTTAB_ENTRY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_CRYPTTAB_ENTRY))

GType            udisks_crypttab_entry_get_type            (void) G_GNUC_CONST;
const gchar     *udisks_crypttab_entry_get_name            (UDisksCrypttabEntry *entry);
const gchar     *udisks_crypttab_entry_get_device          (UDisksCrypttabEntry *entry);
const gchar     *udisks_crypttab_entry_get_passphrase_path (UDisksCrypttabEntry *entry);
const gchar     *udisks_crypttab_entry_get_options         (UDisksCrypttabEntry *entry);
gint             udisks_crypttab_entry_compare             (UDisksCrypttabEntry *entry,
                                                            UDisksCrypttabEntry *other_entry);

G_END_DECLS

#endif /* __UDISKS_CRYPTTAB_ENTRY_H__ */
