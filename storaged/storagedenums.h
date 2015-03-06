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

#if !defined (__STORAGED_INSIDE_STORAGED_H__) && !defined (STORAGED_COMPILATION)
#error "Only <storaged/storaged.h> can be included directly."
#endif

#ifndef __STORAGED_ENUMS_H__
#define __STORAGED_ENUMS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * StoragedError:
 * @STORAGED_ERROR_FAILED: The operation failed.
 * @STORAGED_ERROR_CANCELLED: The operation was cancelled.
 * @STORAGED_ERROR_ALREADY_CANCELLED: The operation has already been cancelled.
 * @STORAGED_ERROR_NOT_AUTHORIZED: Not authorized to perform the requested operation.
 * @STORAGED_ERROR_NOT_AUTHORIZED_CAN_OBTAIN: Like %STORAGED_ERROR_NOT_AUTHORIZED but authorization can be obtained through e.g. authentication.
 * @STORAGED_ERROR_NOT_AUTHORIZED_DISMISSED: Like %STORAGED_ERROR_NOT_AUTHORIZED but an authentication was shown and the user dimissed it.
 * @STORAGED_ERROR_ALREADY_MOUNTED: The device is already mounted.
 * @STORAGED_ERROR_NOT_MOUNTED: The device is not mounted.
 * @STORAGED_ERROR_OPTION_NOT_PERMITTED: Not permitted to use the requested option.
 * @STORAGED_ERROR_MOUNTED_BY_OTHER_USER: The device is mounted by another user.
 * @STORAGED_ERROR_ALREADY_UNMOUNTING: The device is already unmounting.
 * @STORAGED_ERROR_NOT_SUPPORTED: The operation is not supported due to missing driver/tool support.
 * @STORAGED_ERROR_TIMED_OUT: The operation timed out.
 * @STORAGED_ERROR_WOULD_WAKEUP: The operation would wake up a disk that is in a deep-sleep state.
 * @STORAGED_ERROR_DEVICE_BUSY: Attempting to unmount a device that is busy.
 *
 * Error codes for the #STORAGED_ERROR error domain and the
 * corresponding D-Bus error names.
 */
typedef enum
{
  STORAGED_ERROR_FAILED,                     /* org.storaged.Storaged.Error.Failed */
  STORAGED_ERROR_CANCELLED,                  /* org.storaged.Storaged.Error.Cancelled */
  STORAGED_ERROR_ALREADY_CANCELLED,          /* org.storaged.Storaged.Error.AlreadyCancelled */
  STORAGED_ERROR_NOT_AUTHORIZED,             /* org.storaged.Storaged.Error.NotAuthorized */
  STORAGED_ERROR_NOT_AUTHORIZED_CAN_OBTAIN,  /* org.storaged.Storaged.Error.NotAuthorizedCanObtain */
  STORAGED_ERROR_NOT_AUTHORIZED_DISMISSED,   /* org.storaged.Storaged.Error.NotAuthorizedDismissed */
  STORAGED_ERROR_ALREADY_MOUNTED,            /* org.storaged.Storaged.Error.AlreadyMounted */
  STORAGED_ERROR_NOT_MOUNTED,                /* org.storaged.Storaged.Error.NotMounted */
  STORAGED_ERROR_OPTION_NOT_PERMITTED,       /* org.storaged.Storaged.Error.OptionNotPermitted */
  STORAGED_ERROR_MOUNTED_BY_OTHER_USER,      /* org.storaged.Storaged.Error.MountedByOtherUser */
  STORAGED_ERROR_ALREADY_UNMOUNTING,         /* org.storaged.Storaged.Error.AlreadyUnmounting */
  STORAGED_ERROR_NOT_SUPPORTED,              /* org.storaged.Storaged.Error.NotSupported */
  STORAGED_ERROR_TIMED_OUT,                  /* org.storaged.Storaged.Error.Timedout */
  STORAGED_ERROR_WOULD_WAKEUP,               /* org.storaged.Storaged.Error.WouldWakeup */
  STORAGED_ERROR_DEVICE_BUSY                 /* org.storaged.Storaged.Error.DeviceBusy */
} StoragedError;

#define STORAGED_ERROR_NUM_ENTRIES  (STORAGED_ERROR_DEVICE_BUSY + 1)

/**
 * StoragedPartitionTypeInfoFlags:
 * @STORAGED_PARTITION_TYPE_INFO_FLAGS_NONE: No flags set.
 * @STORAGED_PARTITION_TYPE_INFO_FLAGS_SWAP: Partition type is used for swap.
 * @STORAGED_PARTITION_TYPE_INFO_FLAGS_RAID: Partition type is used for RAID/LVM or similar.
 * @STORAGED_PARTITION_TYPE_INFO_FLAGS_HIDDEN: Partition type indicates the partition is hidden (e.g. 'dos' type 0x1b "Hidden W95 FAT32"). Note that this is not the same as user-toggleable attributs/flags for a partition.
 * @STORAGED_PARTITION_TYPE_INFO_FLAGS_CREATE_ONLY: Partition type can only be used when creating a partition and e.g. should not be selectable in a "change partition type" user interface (e.g. 'dos' type 0x05, 0x0f and 0x85 for extended partitions).
 * @STORAGED_PARTITION_TYPE_INFO_FLAGS_SYSTEM: Partition type indicates the partition is part of the system / bootloader (e.g. 'dos' types 0xee, 0xff, 'gpt' types for 'EFI System partition' and 'BIOS Boot partition').
 *
 * Flags describing a partition type.
 */
typedef enum
{
  STORAGED_PARTITION_TYPE_INFO_FLAGS_NONE        = 0,
  STORAGED_PARTITION_TYPE_INFO_FLAGS_SWAP        = (1<<0),
  STORAGED_PARTITION_TYPE_INFO_FLAGS_RAID        = (1<<1),
  STORAGED_PARTITION_TYPE_INFO_FLAGS_HIDDEN      = (1<<2),
  STORAGED_PARTITION_TYPE_INFO_FLAGS_CREATE_ONLY = (1<<3),
  STORAGED_PARTITION_TYPE_INFO_FLAGS_SYSTEM      = (1<<4)
} StoragedPartitionTypeInfoFlags;

G_END_DECLS

#endif /* __STORAGED_ENUMS_H__ */
