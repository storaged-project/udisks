/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#ifndef __TYPES_H__
#define __TYPES_H__

#include <gio/gio.h>
#include <polkit/polkit.h>
#include <udisks/udisks.h>
#include <gudev/gudev.h>
#include <gdbusobjectmanager.h>

#include <sys/types.h>

struct _UDisksDaemon;
typedef struct _UDisksDaemon UDisksDaemon;

struct _UDisksLinuxProvider;
typedef struct _UDisksLinuxProvider UDisksLinuxProvider;

struct _UDisksLinuxBlock;
typedef struct _UDisksLinuxBlock UDisksLinuxBlock;

struct _UDisksLinuxLun;
typedef struct _UDisksLinuxLun UDisksLinuxLun;

struct _UDisksFilesystemImpl;
typedef struct _UDisksFilesystemImpl UDisksFilesystemImpl;

struct _UDisksBaseJob;
typedef struct _UDisksBaseJob UDisksBaseJob;

struct _UDisksSpawnedJob;
typedef struct _UDisksSpawnedJob UDisksSpawnedJob;

struct _UDisksThreadedJob;
typedef struct _UDisksThreadedJob UDisksThreadedJob;

struct _UDisksSimpleJob;
typedef struct _UDisksSimpleJob UDisksSimpleJob;

struct _UDisksMountMonitor;
typedef struct _UDisksMountMonitor UDisksMountMonitor;

struct _UDisksMount;
typedef struct _UDisksMount UDisksMount;

struct _UDisksFstabProvider;
typedef struct _UDisksFstabProvider UDisksFstabProvider;

struct _UDisksProvider;
typedef struct _UDisksProvider UDisksProvider;

struct _UDisksLinuxFilesystem;
typedef struct _UDisksLinuxFilesystem UDisksLinuxFilesystem;

struct _UDisksIScsiProvider;
typedef struct _UDisksIScsiProvider UDisksIScsiProvider;

/**
 * UDisksThreadedJobFunc:
 * @job: A #UDisksThreadedJob.
 * @cancellable: A #GCancellable (never %NULL).
 * @user_data: User data passed when creating @job.
 * @error: Return location for error (never %NULL).
 *
 * Job function that runs in a separate thread.
 *
 * Long-running jobs should periodically check @cancellable to see if
 * they have been cancelled.
 *
 * Returns: %TRUE if the job succeeded, %FALSE if @error is set.
 */
typedef gboolean (*UDisksThreadedJobFunc) (UDisksThreadedJob   *job,
                                           GCancellable        *cancellable,
                                           gpointer             user_data,
                                           GError             **error);

struct _UDisksPersistentStore;
typedef struct _UDisksPersistentStore UDisksPersistentStore;

#endif
