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

#ifndef __STORAGED_CRYPTTAB_ENTRY_H__
#define __STORAGED_CRYPTTAB_ENTRY_H__

#include "storageddaemontypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_CRYPTTAB_ENTRY         (storaged_crypttab_entry_get_type ())
#define STORAGED_CRYPTTAB_ENTRY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_CRYPTTAB_ENTRY, StoragedCrypttabEntry))
#define STORAGED_IS_CRYPTTAB_ENTRY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_CRYPTTAB_ENTRY))

GType            storaged_crypttab_entry_get_type            (void) G_GNUC_CONST;
const gchar     *storaged_crypttab_entry_get_name            (StoragedCrypttabEntry *entry);
const gchar     *storaged_crypttab_entry_get_device          (StoragedCrypttabEntry *entry);
const gchar     *storaged_crypttab_entry_get_passphrase_path (StoragedCrypttabEntry *entry);
const gchar     *storaged_crypttab_entry_get_options         (StoragedCrypttabEntry *entry);
gint             storaged_crypttab_entry_compare             (StoragedCrypttabEntry *entry,
                                                              StoragedCrypttabEntry *other_entry);

G_END_DECLS

#endif /* __STORAGED_CRYPTTAB_ENTRY_H__ */
