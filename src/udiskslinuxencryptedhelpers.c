/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2017 Red Hat, Inc.
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
 * Author: Vratislav Podzimek <vpodzime@redhat.com>
 *
 */

#include <glib.h>
#include <blockdev/crypto.h>

#include "udisksthreadedjob.h"
#include "udiskslinuxencryptedhelpers.h"

gboolean luks_format_job_func (UDisksThreadedJob  *job,
                      GCancellable       *cancellable,
                      gpointer            user_data,
                      GError            **error)
{
  LuksJobData *data = (LuksJobData*) user_data;

  /* device, cipher, key_size, passphrase, key_file, min_entropy, error */
  return bd_crypto_luks_format_blob (data->device, NULL, 0,
                                     (const guint8*) data->passphrase->str, data->passphrase->len, 0,
                                     error);
}

gboolean luks_open_job_func (UDisksThreadedJob  *job,
                    GCancellable       *cancellable,
                    gpointer            user_data,
                    GError            **error)
{
  LuksJobData *data = (LuksJobData*) user_data;

  /* device, name, passphrase, key_file, read_only, error */
  return bd_crypto_luks_open_blob (data->device, data->map_name,
                                   (const guint8*) data->passphrase->str, data->passphrase->len, data->read_only,
                                   error);
}

gboolean luks_close_job_func (UDisksThreadedJob  *job,
                    GCancellable       *cancellable,
                    gpointer            user_data,
                    GError            **error)
{
  LuksJobData *data = (LuksJobData*) user_data;
  return bd_crypto_luks_close (data->map_name, error);
}

gboolean luks_change_key_job_func (UDisksThreadedJob  *job,
                          GCancellable       *cancellable,
                          gpointer            user_data,
                          GError            **error)
{
  LuksJobData *data = (LuksJobData*) user_data;
  return bd_crypto_luks_change_key_blob (data->device,
                                         (const guint8*) data->passphrase->str, data->passphrase->len,
                                         (const guint8*) data->new_passphrase->str, data->new_passphrase->len,
                                         error);
}
