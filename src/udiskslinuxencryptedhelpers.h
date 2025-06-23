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

#ifndef __UDISKS_LINUX_ENCRYPTED_HELPERS_H__
#define __UDISKS_LINUX_ENCRYPTED_HELPERS_H__

#include <glib.h>
#ifdef HAVE_CRYPTO
#  include <blockdev/crypto.h>
#endif

#include "udisksthreadedjob.h"

G_BEGIN_DECLS

typedef struct {
  const gchar *device;
  gchar *map_name;
  GString *passphrase;
  GString *new_passphrase;
  const gchar **keyfiles;
  gsize keyfiles_count;
  guint32 pim;
  gboolean hidden;
  gboolean system;
  gboolean read_only;
  const gchar *type;
  const gchar *pbkdf;
  guint32 memory;
  guint32 iterations;
  guint32 time;
  guint32 threads;
  const gchar *label;
} CryptoJobData;

gboolean luks_format_job_func (UDisksThreadedJob  *job,
                               GCancellable       *cancellable,
                               gpointer            user_data,
                               GError            **error);

gboolean luks_open_job_func (UDisksThreadedJob  *job,
                             GCancellable       *cancellable,
                             gpointer            user_data,
                             GError            **error);

gboolean luks_close_job_func (UDisksThreadedJob  *job,
                              GCancellable       *cancellable,
                              gpointer            user_data,
                              GError            **error);

gboolean luks_change_key_job_func (UDisksThreadedJob  *job,
                                   GCancellable       *cancellable,
                                   gpointer            user_data,
                                   GError            **error);

gboolean tcrypt_open_job_func (UDisksThreadedJob  *job,
                               GCancellable       *cancellable,
                               gpointer            user_data,
                               GError            **error);

gboolean tcrypt_close_job_func (UDisksThreadedJob  *job,
                                GCancellable       *cancellable,
                                gpointer            user_data,
                                GError            **error);

gboolean bitlk_open_job_func (UDisksThreadedJob  *job,
                              GCancellable       *cancellable,
                              gpointer            user_data,
                              GError            **error);

gboolean bitlk_close_job_func (UDisksThreadedJob  *job,
                               GCancellable       *cancellable,
                               gpointer            user_data,
                               GError            **error);
G_END_DECLS

#endif /* __UDISKS_LINUX_ENCRYPTED_HELPERS_H__ */
