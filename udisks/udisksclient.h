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

UDisksObject       *udisks_client_get_object          (UDisksClient        *client,
                                                       const gchar         *object_path);
UDisksObject       *udisks_client_peek_object         (UDisksClient        *client,
                                                       const gchar         *object_path);

UDisksBlock        *udisks_client_get_block_for_dev   (UDisksClient        *client,
                                                       dev_t                block_device_number);
GList              *udisks_client_get_block_for_label (UDisksClient        *client,
                                                       const gchar         *label);
GList              *udisks_client_get_block_for_uuid  (UDisksClient        *client,
                                                       const gchar         *uuid);

UDisksBlock        *udisks_client_get_block_for_drive (UDisksClient        *client,
                                                       UDisksDrive         *drive,
                                                       gboolean             get_physical);
UDisksDrive        *udisks_client_get_drive_for_block (UDisksClient        *client,
                                                       UDisksBlock         *block);
UDisksMDRaid       *udisks_client_get_mdraid_for_block (UDisksClient        *client,
                                                        UDisksBlock         *block);

UDisksBlock        *udisks_client_get_cleartext_block (UDisksClient        *client,
                                                       UDisksBlock         *block);

UDisksBlock        *udisks_client_get_block_for_mdraid (UDisksClient       *client,
                                                        UDisksMDRaid       *raid);

GList              *udisks_client_get_members_for_mdraid (UDisksClient       *client,
                                                          UDisksMDRaid       *raid);

UDisksPartitionTable *udisks_client_get_partition_table (UDisksClient        *client,
                                                         UDisksPartition     *partition);

UDisksLoop         *udisks_client_get_loop_for_block  (UDisksClient  *client,
                                                       UDisksBlock   *block);

GList              *udisks_client_get_partitions      (UDisksClient        *client,
                                                       UDisksPartitionTable *table);

GList              *udisks_client_get_jobs_for_object (UDisksClient        *client,
                                                       UDisksObject        *object);

G_DEPRECATED_FOR(udisks_client_get_object_info)
void                udisks_client_get_drive_info      (UDisksClient        *client,
                                                       UDisksDrive         *drive,
                                                       gchar              **out_name,
                                                       gchar              **out_description,
                                                       GIcon              **out_drive_icon,
                                                       gchar              **out_media_description,
                                                       GIcon              **out_media_icon);

UDisksObjectInfo   *udisks_client_get_object_info     (UDisksClient        *client,
                                                       UDisksObject        *object);

gchar              *udisks_client_get_partition_info  (UDisksClient        *client,
                                                       UDisksPartition     *partition);


gchar              *udisks_client_get_size_for_display (UDisksClient *client,
                                                        guint64       size,
                                                        gboolean      use_pow2,
                                                        gboolean      long_string);

gchar              *udisks_client_get_media_compat_for_display (UDisksClient       *client,
                                                                const gchar* const *media_compat);


gchar              *udisks_client_get_id_for_display (UDisksClient *client,
                                                      const gchar  *usage,
                                                      const gchar  *type,
                                                      const gchar  *version,
                                                      gboolean      long_string);


const gchar       **udisks_client_get_partition_table_subtypes         (UDisksClient  *client,
                                                                        const gchar   *partition_table_type);

GList              *udisks_client_get_partition_type_infos             (UDisksClient  *client,
                                                                        const gchar   *partition_table_type,
                                                                        const gchar   *partition_table_subtype);

const gchar        *udisks_client_get_partition_type_for_display       (UDisksClient  *client,
                                                                        const gchar   *partition_table_type,
                                                                        const gchar   *partition_type);

const gchar        *udisks_client_get_partition_table_type_for_display (UDisksClient  *client,
                                                                        const gchar   *partition_table_type);

const gchar        *udisks_client_get_partition_table_subtype_for_display (UDisksClient  *client,
                                                                           const gchar   *partition_table_type,
                                                                           const gchar   *partition_table_subtype);

gchar *udisks_client_get_job_description (UDisksClient   *client,
                                          UDisksJob      *job);

/**
 * UDisksPartitionTypeInfo:
 * @table_type: A partition table type e.g. 'dos' or 'gpt'
 * @table_subtype: A partition table sub-type.
 * @type: A partition type.
 * @flags: Flags from the #UDisksPartitionTypeInfoFlags enumeration.
 *
 * Detailed information about a partition type.
 *
 * @table_subtype is used to break the set of partition types for
 * @table_type into a logical subsets. It is typically only used in
 * user interfaces where the partition type is selected.
 *
 * This struct may grow in the future without it being considered an
 * ABI break.
 */
struct _UDisksPartitionTypeInfo
{
  /*< public >*/
  const gchar                  *table_type;
  const gchar                  *table_subtype;
  const gchar                  *type;
  UDisksPartitionTypeInfoFlags  flags;
};

GType                udisks_partition_type_info_get_type   (void) G_GNUC_CONST;
void                 udisks_partition_type_info_free       (UDisksPartitionTypeInfo  *info);

/**
 * UDisksObjectInfo:
 * @object: The #UDisksObject that the information is for.
 * @name: (allow-none): An name for the object or %NULL.
 * @description: (allow-none): A description for the object or %NULL.
 * @icon: (allow-none): An icon for the object or %NULL.
 * @icon_symbolic: (allow-none): A symbolic icon for the object or %NULL.
 * @media_description: (allow-none): An icon for the media of the object or %NULL.
 * @media_icon: (allow-none): An icon for the media for the object or %NULL.
 * @media_icon_symbolic: (allow-none): A symbolic icon for the media for the object or %NULL.
 *
 * Detailed information about the D-Bus interfaces (such as
 * #UDisksBlock and #UDisksDrive) on a #UDisksObject that is suitable
 * to display in an user interface. Use
 * udisks_client_get_object_info() to get an instance and
 * udisks_object_info_unref() to free it.
 *
 * The
 * <link linkend="gdbus-property-org-freedesktop-UDisks2-Block.HintName">HintName</link>
 * and/or
 * <link linkend="gdbus-property-org-freedesktop-UDisks2-Block.HintName">HintIconName</link>
 * propreties on associated #UDisksBlock interfaces (if any) may influence
 * the @icon and @media_icon fields.
 *
 * The @media_description, @media_icon and @media_icon_symbolic fields
 * are only set for #UDisksDrive interfaces where the drive has
 * removable media.
 *
 * This struct may grow in the future without it being considered an
 * ABI break.
 *
 * Since: 2.1
 */
struct _UDisksObjectInfo
{
  /*< private >*/
  volatile gint ref_count;
  /*< public >*/
  UDisksObject *object;
  gchar *name;
  gchar *description;
  GIcon *icon;
  GIcon *icon_symbolic;
  gchar *media_description;
  GIcon *media_icon;
  GIcon *media_icon_symbolic;
};

GType              udisks_object_info_get_type   (void) G_GNUC_CONST;
UDisksObjectInfo  *udisks_object_info_ref        (UDisksObjectInfo  *info);
void               udisks_object_info_unref      (UDisksObjectInfo  *info);

G_END_DECLS

#endif /* __UDISKS_CLIENT_H__ */
