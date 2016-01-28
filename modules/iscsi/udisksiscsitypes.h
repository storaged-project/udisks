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

#ifndef __UDISKS_ISCSI_TYPES_H__
#define __UDISKS_ISCSI_TYPES_H__

#define ISCSI_MODULE_NAME "iscsi"

typedef struct _UDisksISCSIState UDisksISCSIState;

typedef struct _UDisksLinuxManagerISCSIInitiator        UDisksLinuxManagerISCSIInitiator;
typedef struct _UDisksLinuxManagerISCSIInitiatorClass   UDisksLinuxManagerISCSIInitiatorClass;

typedef struct _UDisksLinuxISCSISession        UDisksLinuxISCSISession;
typedef struct _UDisksLinuxISCSISessionClass   UDisksLinuxISCSISessionClass;

typedef struct _UDisksLinuxISCSISessionObject        UDisksLinuxISCSISessionObject;
typedef struct _UDisksLinuxISCSISessionObjectClass   UDisksLinuxISCSISessionObjectClass;

#endif /* __UDISKS_ISCSI_TYPES_H__ */
