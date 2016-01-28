/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014 Tomas Bzatek <tbzatek@redhat.com>
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

#ifndef __UDISKS_LVM2_STATE_H__
#define __UDISKS_LVM2_STATE_H__

#include <glib.h>
#include <glib-object.h>
#include <src/udisksdaemontypes.h>
#include "udiskslvm2types.h"

G_BEGIN_DECLS

UDisksLVM2State *udisks_lvm2_state_new  (UDisksDaemon *daemon);
void             udisks_lvm2_state_free (UDisksLVM2State *state);

GHashTable      *udisks_lvm2_state_get_name_to_volume_group  (UDisksLVM2State *state);
gint             udisks_lvm2_state_get_lvm_delayed_update_id (UDisksLVM2State *state);
gboolean         udisks_lvm2_state_get_coldplug_done         (UDisksLVM2State *state);

void             udisks_lvm2_state_set_lvm_delayed_update_id (UDisksLVM2State *state,
                                                              gint             id);
void             udisks_lvm2_state_set_coldplug_done         (UDisksLVM2State *state,
                                                              gboolean         coldplug_done);

G_END_DECLS

#endif /* __UDISKS_LVM2_STATE_H__ */
