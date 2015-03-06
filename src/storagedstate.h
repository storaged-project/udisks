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

#ifndef __STORAGED_STATE_H__
#define __STORAGED_STATE_H__

#include "storageddaemontypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_STATE         (storaged_state_get_type ())
#define STORAGED_STATE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_STATE, StoragedState))
#define STORAGED_IS_STATE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_STATE))

GType            storaged_state_get_type             (void) G_GNUC_CONST;
StoragedState   *storaged_state_new                  (StoragedDaemon  *daemon);
StoragedDaemon  *storaged_state_get_daemon           (StoragedState   *state);
void             storaged_state_start_cleanup        (StoragedState   *state);
void             storaged_state_stop_cleanup         (StoragedState   *state);
void             storaged_state_check                (StoragedState   *state);
/* mounted-fs */
void             storaged_state_add_mounted_fs       (StoragedState   *state,
                                                      const gchar     *mount_point,
                                                      dev_t            block_device,
                                                      uid_t            uid,
                                                      gboolean         fstab_mount);
gchar           *storaged_state_find_mounted_fs      (StoragedState   *state,
                                                      dev_t            block_device,
                                                      uid_t           *out_uid,
                                                      gboolean        *out_fstab_mount);
/* unlocked-luks */
void             storaged_state_add_unlocked_luks    (StoragedState   *state,
                                                      dev_t            cleartext_device,
                                                      dev_t            crypto_device,
                                                      const gchar     *dm_uuid,
                                                      uid_t            uid);
dev_t            storaged_state_find_unlocked_luks   (StoragedState   *state,
                                                      dev_t            crypto_device,
                                                      uid_t           *out_uid);
/* loop */
void             storaged_state_add_loop             (StoragedState   *state,
                                                      const gchar     *device_file,
                                                      const gchar     *backing_file,
                                                      dev_t            backing_file_device,
                                                      uid_t            uid);
gboolean         storaged_state_has_loop             (StoragedState   *state,
                                                      const gchar     *device_file,
                                                      uid_t           *out_uid);
/* mdraid */
void             storaged_state_add_mdraid           (StoragedState   *state,
                                                      dev_t            raid_device,
                                                      uid_t            uid);
gboolean         storaged_state_has_mdraid           (StoragedState   *state,
                                                      dev_t            raid_device,
                                                      uid_t           *out_uid);

G_END_DECLS

#endif /* __STORAGED_STATE_H__ */
