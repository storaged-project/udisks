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

#ifndef __STORAGED_CLIENT_H__
#define __STORAGED_CLIENT_H__

#include <storaged/storagedtypes.h>
#include <storaged/storaged-generated.h>

G_BEGIN_DECLS

#define STORAGED_TYPE_CLIENT  (storaged_client_get_type ())
#define STORAGED_CLIENT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_CLIENT, StoragedClient))
#define STORAGED_IS_CLIENT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_CLIENT))

GType                   storaged_client_get_type           (void) G_GNUC_CONST;
void                    storaged_client_new                (GCancellable        *cancellable,
                                                            GAsyncReadyCallback  callback,
                                                            gpointer             user_data);
StoragedClient         *storaged_client_new_finish         (GAsyncResult        *res,
                                                            GError             **error);
StoragedClient         *storaged_client_new_sync           (GCancellable        *cancellable,
                                                            GError             **error);
GDBusObjectManager     *storaged_client_get_object_manager (StoragedClient        *client);
StoragedManager        *storaged_client_get_manager        (StoragedClient        *client);
void                    storaged_client_settle             (StoragedClient        *client);
void                    storaged_client_queue_changed      (StoragedClient        *client);

StoragedObject         *storaged_client_get_object          (StoragedClient        *client,
                                                             const gchar           *object_path);
StoragedObject         *storaged_client_peek_object         (StoragedClient        *client,
                                                             const gchar           *object_path);

StoragedBlock          *storaged_client_get_block_for_dev   (StoragedClient        *client,
                                                             dev_t                  block_device_number);
GList                  *storaged_client_get_block_for_label (StoragedClient        *client,
                                                             const gchar           *label);
GList                  *storaged_client_get_block_for_uuid  (StoragedClient        *client,
                                                             const gchar           *uuid);

StoragedBlock          *storaged_client_get_block_for_drive (StoragedClient        *client,
                                                             StoragedDrive         *drive,
                                                             gboolean               get_physical);
StoragedDrive          *storaged_client_get_drive_for_block (StoragedClient        *client,
                                                             StoragedBlock         *block);
StoragedMDRaid         *storaged_client_get_mdraid_for_block (StoragedClient        *client,
                                                              StoragedBlock         *block);

StoragedBlock          *storaged_client_get_cleartext_block (StoragedClient        *client,
                                                             StoragedBlock         *block);

StoragedBlock          *storaged_client_get_block_for_mdraid (StoragedClient       *client,
                                                              StoragedMDRaid       *raid);
GList                  *storaged_client_get_all_blocks_for_mdraid (StoragedClient  *client,
                                                                   StoragedMDRaid  *raid);

GList                  *storaged_client_get_members_for_mdraid (StoragedClient     *client,
                                                                StoragedMDRaid     *raid);

StoragedPartitionTable *storaged_client_get_partition_table (StoragedClient      *client,
                                                             StoragedPartition   *partition);

StoragedLoop           *storaged_client_get_loop_for_block  (StoragedClient  *client,
                                                             StoragedBlock   *block);

GList              *storaged_client_get_partitions      (StoragedClient        *client,
                                                         StoragedPartitionTable *table);

GList              *storaged_client_get_drive_siblings  (StoragedClient       *client,
                                                         StoragedDrive        *drive);

GList              *storaged_client_get_jobs_for_object (StoragedClient        *client,
                                                         StoragedObject        *object);

G_DEPRECATED_FOR(storaged_client_get_object_info)
void                storaged_client_get_drive_info      (StoragedClient        *client,
                                                         StoragedDrive         *drive,
                                                         gchar              **out_name,
                                                         gchar              **out_description,
                                                         GIcon              **out_drive_icon,
                                                         gchar              **out_media_description,
                                                         GIcon              **out_media_icon);

StoragedObjectInfo   *storaged_client_get_object_info     (StoragedClient        *client,
                                                         StoragedObject        *object);

gchar              *storaged_client_get_partition_info  (StoragedClient        *client,
                                                         StoragedPartition     *partition);


gchar              *storaged_client_get_size_for_display (StoragedClient *client,
                                                          guint64       size,
                                                          gboolean      use_pow2,
                                                          gboolean      long_string);

gchar              *storaged_client_get_media_compat_for_display (StoragedClient       *client,
                                                                  const gchar* const *media_compat);


gchar              *storaged_client_get_id_for_display (StoragedClient *client,
                                                        const gchar  *usage,
                                                         const gchar  *type,
                                                        const gchar  *version,
                                                        gboolean      long_string);


const gchar       **storaged_client_get_partition_table_subtypes         (StoragedClient  *client,
                                                                          const gchar   *partition_table_type);

GList              *storaged_client_get_partition_type_infos             (StoragedClient  *client,
                                                                          const gchar   *partition_table_type,
                                                                          const gchar   *partition_table_subtype);

const gchar        *storaged_client_get_partition_type_for_display       (StoragedClient  *client,
                                                                          const gchar   *partition_table_type,
                                                                          const gchar   *partition_type);

const gchar        *storaged_client_get_partition_type_and_subtype_for_display (StoragedClient  *client,
                                                                                const gchar   *partition_table_type,
                                                                                const gchar   *partition_table_subtype,
                                                                                const gchar   *partition_type);


const gchar        *storaged_client_get_partition_table_type_for_display (StoragedClient  *client,
                                                                          const gchar   *partition_table_type);

const gchar        *storaged_client_get_partition_table_subtype_for_display (StoragedClient  *client,
                                                                             const gchar   *partition_table_type,
                                                                             const gchar   *partition_table_subtype);

gchar *storaged_client_get_job_description (StoragedClient   *client,
                                          StoragedJob      *job);

/**
 * StoragedPartitionTypeInfo:
 * @table_type: A partition table type e.g. 'dos' or 'gpt'
 * @table_subtype: A partition table sub-type.
 * @type: A partition type.
 * @flags: Flags from the #StoragedPartitionTypeInfoFlags enumeration.
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
struct _StoragedPartitionTypeInfo
{
  /*< public >*/
  const gchar                  *table_type;
  const gchar                  *table_subtype;
  const gchar                  *type;
  StoragedPartitionTypeInfoFlags  flags;
};

GType                storaged_partition_type_info_get_type   (void) G_GNUC_CONST;
void                 storaged_partition_type_info_free       (StoragedPartitionTypeInfo  *info);

G_END_DECLS

#endif /* __STORAGED_CLIENT_H__ */
