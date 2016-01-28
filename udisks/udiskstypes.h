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

#ifndef __UDISKS_TYPES_H__
#define __UDISKS_TYPES_H__

#include <gio/gio.h>
#include <udisks/udisksenums.h>

G_BEGIN_DECLS

struct _UDisksClient;
typedef struct _UDisksClient UDisksClient;

struct _UDisksPartitionTypeInfo;
typedef struct _UDisksPartitionTypeInfo UDisksPartitionTypeInfo;

struct _UDisksObjectInfo;
typedef struct _UDisksObjectInfo UDisksObjectInfo;

G_END_DECLS

#endif /* __UDISKS_TYPES_H__ */
