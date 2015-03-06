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

#if !defined (__STORAGED_INSIDE_STORAGED_H__) && !defined (STORAGED_COMPILATION)
#error "Only <storaged/storaged.h> can be included directly."
#endif

#ifndef __STORAGED_OBJECT_INFO_H__
#define __STORAGED_OBJECT_INFO_H__

#include <storaged/storagedtypes.h>
#include <storaged/storaged-generated.h>

G_BEGIN_DECLS

#define STORAGED_TYPE_OBJECT_INFO  (storaged_object_info_get_type ())
#define STORAGED_OBJECT_INFO(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_OBJECT_INFO, StoragedObjectInfo))
#define STORAGED_IS_OBJECT_INFO(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_OBJECT_INFO))

GType           storaged_object_info_get_type                (void) G_GNUC_CONST;
StoragedObject *storaged_object_info_get_object              (StoragedObjectInfo  *info);
const gchar    *storaged_object_info_get_name                (StoragedObjectInfo  *info);
const gchar    *storaged_object_info_get_description         (StoragedObjectInfo  *info);
GIcon          *storaged_object_info_get_icon                (StoragedObjectInfo  *info);
GIcon          *storaged_object_info_get_icon_symbolic       (StoragedObjectInfo  *info);
const gchar    *storaged_object_info_get_media_description   (StoragedObjectInfo  *info);
GIcon          *storaged_object_info_get_media_icon          (StoragedObjectInfo  *info);
GIcon          *storaged_object_info_get_media_icon_symbolic (StoragedObjectInfo  *info);
const gchar    *storaged_object_info_get_one_liner           (StoragedObjectInfo  *info);
const gchar    *storaged_object_info_get_sort_key            (StoragedObjectInfo  *info);

G_END_DECLS

#endif /* __STORAGED_OBJECT_INFO_H__ */
