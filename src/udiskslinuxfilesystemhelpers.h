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

#ifndef __UDISKS_LINUX_FILESYSTEM_HELPERS_H__
#define __UDISKS_LINUX_FILESYSTEM_HEPLERS_H__

#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include <blockdev/fs.h>

G_BEGIN_DECLS

gboolean take_filesystem_ownership (const gchar *device,
                                    const gchar *fstype,
                                    uid_t caller_uid,
                                    gid_t caller_gid,
                                    gboolean recursive,
                                    GError **error);

G_END_DECLS


#endif /* __UDISKS_LINUX_FILESYSTEM_HEPLERS_H__ */
