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

#ifndef __UDISKS_ENUMS_H__
#define __UDISKS_ENUMS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * UDisksError:
 * @UDISKS_ERROR_FAILED: The operation failed.
 * @UDISKS_ERROR_CANCELLED: The operation was cancelled.
 * @UDISKS_ERROR_ALREADY_CANCELLED: The operation has already been cancelled.
 * @UDISKS_ERROR_NOT_AUTHORIZED: Not authorized to perform the requested operation.
 * @UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN: Like %UDISKS_ERROR_NOT_AUTHORIZED but authorization can be obtained through e.g. authentication.
 * @UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED: Like %UDISKS_ERROR_NOT_AUTHORIZED but an authentication was shown and the user dimissed it.
 * @UDISKS_ERROR_ALREADY_MOUNTED: The device is already mounted.
 * @UDISKS_ERROR_NOT_MOUNTED: The device is not mounted.
 * @UDISKS_ERROR_OPTION_NOT_PERMITTED: Not permitted to use the requested option.
 * @UDISKS_ERROR_MOUNTED_BY_OTHER_USER: The device is mounted by another user.
 * @UDISKS_ERROR_ALREADY_UNMOUNTING: The device is already unmounting.
 * @UDISKS_ERROR_NOT_SUPPORTED: The operation is not supported due to missing driver/tool support.
 * @UDISKS_ERROR_TIMED_OUT: The operation timed out.
 * @UDISKS_ERROR_WOULD_WAKEUP: The operation would wake up a disk that is in a deep-sleep state.
 * @UDISKS_ERROR_DEVICE_BUSY: Attempting to unmount a device that is busy.
 *
 * Error codes for the #UDISKS_ERROR error domain and the
 * corresponding D-Bus error names.
 */
typedef enum
{
  UDISKS_ERROR_FAILED,                        /* org.freedesktop.UDisks2.Error.Failed */
  UDISKS_ERROR_CANCELLED,                     /* org.freedesktop.UDisks2.Error.Cancelled */
  UDISKS_ERROR_ALREADY_CANCELLED,             /* org.freedesktop.UDisks2.Error.AlreadyCancelled */
  UDISKS_ERROR_NOT_AUTHORIZED,                /* org.freedesktop.UDisks2.Error.NotAuthorized */
  UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN,     /* org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain */
  UDISKS_ERROR_NOT_AUTHORIZED_DISMISSED,      /* org.freedesktop.UDisks2.Error.NotAuthorizedDismissed */
  UDISKS_ERROR_ALREADY_MOUNTED,               /* org.freedesktop.UDisks2.Error.AlreadyMounted */
  UDISKS_ERROR_NOT_MOUNTED,                   /* org.freedesktop.UDisks2.Error.NotMounted */
  UDISKS_ERROR_OPTION_NOT_PERMITTED,          /* org.freedesktop.UDisks2.Error.OptionNotPermitted */
  UDISKS_ERROR_MOUNTED_BY_OTHER_USER,         /* org.freedesktop.UDisks2.Error.MountedByOtherUser */
  UDISKS_ERROR_ALREADY_UNMOUNTING,            /* org.freedesktop.UDisks2.Error.AlreadyUnmounting */
  UDISKS_ERROR_NOT_SUPPORTED,                 /* org.freedesktop.UDisks2.Error.NotSupported */
  UDISKS_ERROR_TIMED_OUT,                     /* org.freedesktop.UDisks2.Error.Timedout */
  UDISKS_ERROR_WOULD_WAKEUP,                  /* org.freedesktop.UDisks2.Error.WouldWakeup */
  UDISKS_ERROR_DEVICE_BUSY,                   /* org.freedesktop.UDisks2.Error.DeviceBusy */
  UDISKS_ERROR_ISCSI_DAEMON_TRANSPORT_FAILED, /* org.freedesktop.UDisks2.Error.ISCSI.CommunicationFailed */
  UDISKS_ERROR_ISCSI_HOST_NOT_FOUND,          /* org.freedesktop.UDisks2.Error.ISCSI.HostNotFound */
  UDISKS_ERROR_ISCSI_IDMB,                    /* org.freedesktop.UDisks2.Error.ISCSI.IDMB */
  UDISKS_ERROR_ISCSI_LOGIN_FAILED,            /* org.freedesktop.UDisks2.Error.ISCSI.LoginFailed */
  UDISKS_ERROR_ISCSI_LOGIN_AUTH_FAILED,       /* org.freedesktop.UDisks2.Error.ISCSI.LoginAuthFailed */
  UDISKS_ERROR_ISCSI_LOGIN_FATAL,             /* org.freedesktop.UDisks2.Error.ISCSI.LoginFatal */
  UDISKS_ERROR_ISCSI_LOGOUT_FAILED,           /* org.freedesktop.UDisks2.Error.ISCSI.LogoutFailed */
  UDISKS_ERROR_ISCSI_NO_FIRMWARE,             /* org.freedesktop.UDisks2.Error.ISCSI.NoFirmware */
  UDISKS_ERROR_ISCSI_NO_OBJECTS_FOUND,        /* org.freedesktop.UDisks2.Error.ISCSI.NoObjectsFound */
  UDISKS_ERROR_ISCSI_NOT_CONNECTED,           /* org.freedesktop.UDisks2.Error.ISCSI.NotConnected */
  UDISKS_ERROR_ISCSI_TRANSPORT_FAILED,        /* org.freedesktop.UDisks2.Error.ISCSI.TransportFailed */
  UDISKS_ERROR_ISCSI_UNKNOWN_DISCOVERY_TYPE   /* org.freedesktop.UDisks2.Error.ISCSI.UnknownDiscoveryType */
} UDisksError;

#define UDISKS_ERROR_NUM_ENTRIES  (UDISKS_ERROR_ISCSI_UNKNOWN_DISCOVERY_TYPE + 1)

/**
 * UDisksPartitionTypeInfoFlags:
 * @UDISKS_PARTITION_TYPE_INFO_FLAGS_NONE: No flags set.
 * @UDISKS_PARTITION_TYPE_INFO_FLAGS_SWAP: Partition type is used for swap.
 * @UDISKS_PARTITION_TYPE_INFO_FLAGS_RAID: Partition type is used for RAID/LVM or similar.
 * @UDISKS_PARTITION_TYPE_INFO_FLAGS_HIDDEN: Partition type indicates the partition is hidden (e.g. 'dos' type 0x1b "Hidden W95 FAT32"). Note that this is not the same as user-toggleable attributs/flags for a partition.
 * @UDISKS_PARTITION_TYPE_INFO_FLAGS_CREATE_ONLY: Partition type can only be used when creating a partition and e.g. should not be selectable in a "change partition type" user interface (e.g. 'dos' type 0x05, 0x0f and 0x85 for extended partitions).
 * @UDISKS_PARTITION_TYPE_INFO_FLAGS_SYSTEM: Partition type indicates the partition is part of the system / bootloader (e.g. 'dos' types 0xee, 0xff, 'gpt' types for 'EFI System partition' and 'BIOS Boot partition').
 *
 * Flags describing a partition type.
 */
typedef enum
{
  UDISKS_PARTITION_TYPE_INFO_FLAGS_NONE        = 0,
  UDISKS_PARTITION_TYPE_INFO_FLAGS_SWAP        = (1<<0),
  UDISKS_PARTITION_TYPE_INFO_FLAGS_RAID        = (1<<1),
  UDISKS_PARTITION_TYPE_INFO_FLAGS_HIDDEN      = (1<<2),
  UDISKS_PARTITION_TYPE_INFO_FLAGS_CREATE_ONLY = (1<<3),
  UDISKS_PARTITION_TYPE_INFO_FLAGS_SYSTEM      = (1<<4)
} UDisksPartitionTypeInfoFlags;

G_END_DECLS

#endif /* __UDISKS_ENUMS_H__ */
