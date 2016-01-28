/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 David Zeuthen <zeuthen@gmail.com>
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

#ifndef __UDISKS_OBJECT_INFO_H__
#define __UDISKS_OBJECT_INFO_H__

#include <udisks/udiskstypes.h>
#include <udisks/udisks-generated.h>

G_BEGIN_DECLS

#define UDISKS_TYPE_OBJECT_INFO  (udisks_object_info_get_type ())
#define UDISKS_OBJECT_INFO(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_OBJECT_INFO, UDisksObjectInfo))
#define UDISKS_IS_OBJECT_INFO(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_OBJECT_INFO))

GType         udisks_object_info_get_type                (void) G_GNUC_CONST;
UDisksObject *udisks_object_info_get_object              (UDisksObjectInfo  *info);
const gchar  *udisks_object_info_get_name                (UDisksObjectInfo  *info);
const gchar  *udisks_object_info_get_description         (UDisksObjectInfo  *info);
GIcon        *udisks_object_info_get_icon                (UDisksObjectInfo  *info);
GIcon        *udisks_object_info_get_icon_symbolic       (UDisksObjectInfo  *info);
const gchar  *udisks_object_info_get_media_description   (UDisksObjectInfo  *info);
GIcon        *udisks_object_info_get_media_icon          (UDisksObjectInfo  *info);
GIcon        *udisks_object_info_get_media_icon_symbolic (UDisksObjectInfo  *info);
const gchar  *udisks_object_info_get_one_liner           (UDisksObjectInfo  *info);
const gchar  *udisks_object_info_get_sort_key            (UDisksObjectInfo  *info);

G_END_DECLS

#endif /* __UDISKS_OBJECT_INFO_H__ */
