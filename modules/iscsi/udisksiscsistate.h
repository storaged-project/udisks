/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Peter Hatina <phatina@redhat.com>
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
 */

#ifndef __UDISKS_ISCSI_STATE_H__
#define __UDISKS_ISCSI_STATE_H__

#include <glib.h>
#include <src/udisksdaemontypes.h>
#include "udisksiscsitypes.h"

G_BEGIN_DECLS

UDisksISCSIState        *udisks_iscsi_state_new  (UDisksDaemon *daemon);
void                     udisks_iscsi_state_free (UDisksISCSIState *state);

struct libiscsi_context *udisks_iscsi_state_get_libiscsi_context    (UDisksISCSIState *state);
void                     udisks_iscsi_state_lock_libiscsi_context   (UDisksISCSIState *state);
void                     udisks_iscsi_state_unlock_libiscsi_context (UDisksISCSIState *state);

G_END_DECLS

#endif /* __UDISKS_ISCSI_STATE_H__ */
