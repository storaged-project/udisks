/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- *
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

#ifndef __STORAGED_DAEMON_TYPES_H__
#define __STORAGED_DAEMON_TYPES_H__

#include <gio/gio.h>
#include <polkit/polkit.h>
#include <storaged/storaged.h>
#include <gudev/gudev.h>

#include <sys/types.h>

struct _StoragedDaemon;
typedef struct _StoragedDaemon StoragedDaemon;

struct _StoragedLinuxProvider;
typedef struct _StoragedLinuxProvider StoragedLinuxProvider;

struct _StoragedLinuxBlockObject;
typedef struct _StoragedLinuxBlockObject StoragedLinuxBlockObject;

struct _StoragedLinuxBlock;
typedef struct _StoragedLinuxBlock StoragedLinuxBlock;

struct _StoragedLinuxDriveObject;
typedef struct _StoragedLinuxDriveObject StoragedLinuxDriveObject;

struct _StoragedLinuxDrive;
typedef struct _StoragedLinuxDrive StoragedLinuxDrive;

struct _StoragedLinuxDriveAta;
typedef struct _StoragedLinuxDriveAta StoragedLinuxDriveAta;

struct _StoragedLinuxMDRaidObject;
typedef struct _StoragedLinuxMDRaidObject StoragedLinuxMDRaidObject;

struct _StoragedLinuxMDRaid;
typedef struct _StoragedLinuxMDRaid StoragedLinuxMDRaid;

struct _StoragedBaseJob;
typedef struct _StoragedBaseJob StoragedBaseJob;

struct _StoragedSpawnedJob;
typedef struct _StoragedSpawnedJob StoragedSpawnedJob;

struct _StoragedThreadedJob;
typedef struct _StoragedThreadedJob StoragedThreadedJob;

struct _StoragedSimpleJob;
typedef struct _StoragedSimpleJob StoragedSimpleJob;

struct _StoragedMountMonitor;
typedef struct _StoragedMountMonitor StoragedMountMonitor;

struct _StoragedMount;
typedef struct _StoragedMount StoragedMount;

struct _StoragedProvider;
typedef struct _StoragedProvider StoragedProvider;

struct _StoragedLinuxFilesystem;
typedef struct _StoragedLinuxFilesystem StoragedLinuxFilesystem;

struct _StoragedLinuxEncrypted;
typedef struct _StoragedLinuxEncrypted StoragedLinuxEncrypted;

struct _StoragedLinuxLoop;
typedef struct _StoragedLinuxLoop StoragedLinuxLoop;

struct _StoragedLinuxManager;
typedef struct _StoragedLinuxManager StoragedLinuxManager;

struct _StoragedLinuxSwapspace;
typedef struct _StoragedLinuxSwapspace StoragedLinuxSwapspace;

struct _StoragedFstabMonitor;
typedef struct _StoragedFstabMonitor StoragedFstabMonitor;

struct _StoragedFstabEntry;
typedef struct _StoragedFstabEntry StoragedFstabEntry;

struct _StoragedCrypttabMonitor;
typedef struct _StoragedCrypttabMonitor StoragedCrypttabMonitor;

struct _StoragedCrypttabEntry;
typedef struct _StoragedCrypttabEntry StoragedCrypttabEntry;

struct _StoragedLinuxPartition;
typedef struct _StoragedLinuxPartition StoragedLinuxPartition;

struct _StoragedLinuxPartitionTable;
typedef struct _StoragedLinuxPartitionTable StoragedLinuxPartitionTable;

struct StoragedInhibitCookie;
typedef struct StoragedInhibitCookie StoragedInhibitCookie;

struct _StoragedModuleManager;
typedef struct _StoragedModuleManager StoragedModuleManager;

/**
 * StoragedThreadedJobFunc:
 * @job: A #StoragedThreadedJob.
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
typedef gboolean (*StoragedThreadedJobFunc) (StoragedThreadedJob   *job,
                                             GCancellable          *cancellable,
                                             gpointer               user_data,
                                             GError               **error);

struct _StoragedState;
typedef struct _StoragedState StoragedState;

/**
 * StoragedMountType:
 * @STORAGED_MOUNT_TYPE_FILESYSTEM: Object correspond to a mounted filesystem.
 * @STORAGED_MOUNT_TYPE_SWAP: Object correspond to an in-use swap device.
 *
 * Types of a mount.
 */
typedef enum
{
  STORAGED_MOUNT_TYPE_FILESYSTEM,
  STORAGED_MOUNT_TYPE_SWAP
} StoragedMountType;


/**
 * StoragedLogLevel:
 * @STORAGED_LOG_LEVEL_DEBUG: Debug messages.
 * @STORAGED_LOG_LEVEL_INFO: Informational messages.
 * @STORAGED_LOG_LEVEL_NOTICE: Messages that the administrator should take notice of.
 * @STORAGED_LOG_LEVEL_WARNING: Warning messages.
 * @STORAGED_LOG_LEVEL_ERROR: Error messages.
 *
 * Logging levels. The level @STORAGED_LOG_LEVEL_NOTICE and above goes to syslog.
 *
 * Unlike g_warning() and g_error(), none of these logging levels causes the program to ever terminate.
 */
typedef enum
{
  STORAGED_LOG_LEVEL_DEBUG,
  STORAGED_LOG_LEVEL_INFO,
  STORAGED_LOG_LEVEL_NOTICE,
  STORAGED_LOG_LEVEL_WARNING,
  STORAGED_LOG_LEVEL_ERROR
} StoragedLogLevel;

struct _StoragedAtaCommandOutput;
typedef struct _StoragedAtaCommandOutput StoragedAtaCommandOutput;

struct _StoragedAtaCommandInput;
typedef struct _StoragedAtaCommandInput StoragedAtaCommandInput;

/**
 * StoragedAtaCommandProtocol:
 * @STORAGED_ATA_COMMAND_PROTOCOL_NONE: Non-data
 * @STORAGED_ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST: PIO Data-In
 * @STORAGED_ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE: PIO Data-Out
 *
 * Enumeration used to specify the protocol of an ATA command
 */
typedef enum
{
  STORAGED_ATA_COMMAND_PROTOCOL_NONE,
  STORAGED_ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST,
  STORAGED_ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE
} StoragedAtaCommandProtocol;

struct _StoragedLinuxDevice;
typedef struct _StoragedLinuxDevice StoragedLinuxDevice;

/**
 * StoragedObjectHasInterfaceFunc:
 * @object: A #StoragedObject to consider.
 *
 * Function prototype that is used to determine whether the @object is applicable
 * for carrying a particular D-Bus interface (determined by the callback function itself).
 *
 * Used typically over #StoragedLinuxBlockObject and #StoragedLinuxDriveObject
 * objects for checking specific feature that leads to exporting extra D-Bus
 * interface on the object.
 *
 * Returns: %TRUE if the @object is a valid candidate for the particular D-Bus interface, %FALSE otherwise.
 */
typedef gboolean (*StoragedObjectHasInterfaceFunc)     (StoragedObject   *object);

/**
 * StoragedObjectConnectInterfaceFunc:
 * @object: A #StoragedObject to perform connection operation onto.
 *
 * Function prototype that is used once a new D-Bus interface is created (meaning
 * the #StoragedObjectHasInterfaceFunc call was successful) to perform optional
 * additional tasks before the interface is exported on the @object.
 *
 * Used typically over #StoragedLinuxBlockObject and #StoragedLinuxDriveObject objects.
 */
typedef void     (*StoragedObjectConnectInterfaceFunc) (StoragedObject   *object);

/**
 * StoragedObjectUpdateInterfaceFunc:
 * @object: A #StoragedObject.
 * @uevent_action: An uevent action string.
 * @interface: Existing #GDBusInterface exported on the @object.
 *
 * Function prototype that is used on existing @interface on the @object to process
 * incoming uevents.
 *
 * Used typically over #StoragedLinuxBlockObject and #StoragedLinuxDriveObject objects.
 *
 * Returns: %TRUE if configuration (properties) on the interface have changed, %FALSE otherwise.
 */
typedef gboolean (*StoragedObjectUpdateInterfaceFunc)  (StoragedObject   *object,
                                                        const gchar      *uevent_action,
                                                        GDBusInterface   *interface);

#endif /* __STORAGED_DAEMON_TYPES_H__ */
