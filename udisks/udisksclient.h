/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 David Zeuthen <zeuthen@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#if !defined (__UDISKS_INSIDE_UDISKS_H__) && !defined (UDISKS_COMPILATION)
#error "Only <udisks/udisks.h> can be included directly."
#endif

#ifndef __UDISKS_CLIENT_H__
#define __UDISKS_CLIENT_H__

#include <udisks/udiskstypes.h>
#include <udisks/udisks-generated.h>

G_BEGIN_DECLS

#define UDISKS_TYPE_CLIENT  (udisks_client_get_type ())
#define UDISKS_CLIENT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_CLIENT, UDisksClient))
#define UDISKS_IS_CLIENT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_CLIENT))

GType               udisks_client_get_type           (void) G_GNUC_CONST;
void                udisks_client_new                (GCancellable        *cancellable,
                                                      GAsyncReadyCallback  callback,
                                                      gpointer             user_data);
UDisksClient       *udisks_client_new_finish         (GAsyncResult        *res,
                                                      GError             **error);
UDisksClient       *udisks_client_new_sync           (GCancellable        *cancellable,
                                                      GError             **error);
GDBusObjectManager *udisks_client_get_object_manager (UDisksClient        *client);
UDisksManager      *udisks_client_get_manager        (UDisksClient        *client);
void                udisks_client_settle             (UDisksClient        *client);

UDisksBlock        *udisks_client_get_block_for_drive (UDisksClient        *client,
                                                       UDisksDrive         *drive,
                                                       gboolean             get_physical);

G_END_DECLS

#endif /* __UDISKS_CLIENT_H__ */
