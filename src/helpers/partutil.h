/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/***************************************************************************
 *
 * part.h : library for reading and writing partition tables - uses
 *          libparted for the heavy lifting
 *
 * Copyright (C) 2006 David Zeuthen, <david@fubar.dk>
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
 **************************************************************************/

#ifndef PARTUTIL_H
#define PARTUTIL_H

#include <stdio.h>
#include <glib.h>

/* Partition schemes understood by this library */
typedef enum
  {
    PART_TYPE_UNKNOWN = -1,
    PART_TYPE_MSDOS = 0,
    PART_TYPE_MSDOS_EXTENDED = 1,
    PART_TYPE_APPLE = 2,
    PART_TYPE_GPT = 3
  } PartitionScheme;

/**
 * part_get_scheme_name:
 * @scheme: the partitioning scheme
 *
 * Get a name for the partitioning scheme. The current mapping is used
 *
 *  PART_TYPE_MSDOS          -> mbr
 *  PART_TYPE_MSDOS_EXTENDED -> embr
 *  PART_TYPE_APPLE          -> apm
 *  PART_TYPE_GPT            -> gpt
 *
 * Returns: Name of scheme or NULL for unknown scheme. Caller shall not free this string.
 */
const char *
part_get_scheme_name (PartitionScheme scheme);

struct PartitionTable_s;
typedef struct PartitionTable_s PartitionTable;

/**
 * part_table_load_from_disk:
 * @fd: a file descriptor for the disk
 *
 * Scans a disk and collect all partition entries and nested partition tables.
 *
 * Returns: A partition table object. Use part_table_free() to free this object.
 */
PartitionTable *
part_table_load_from_disk (int fd);

/**
 * part_table_free:
 * @part_table: the partition table
 *
 * Frees the partition table returned from part_table_load_from_disk().
 */
void
part_table_free (PartitionTable *part_table);

/* partition table inspection */

/**
 * part_table_get_scheme:
 * @part_table: the partition table
 *
 * Get partitioning scheme.
 *
 * Returns: The partitioning scheme.
 */
PartitionScheme
part_table_get_scheme (PartitionTable *part_table);

/**
 * part_table_get_num_entries:
 * @part_table: the partition table
 *
 * Get number of entries in partition table.
 *
 * Returns: Number of entries.
 */
int
part_table_get_num_entries (PartitionTable *part_table);

/**
 * part_table_get_offset:
 * @part_table: the partition table
 *
 * Get offset from start of disk where partition table starts (as
 * referenced in the partition table entry if it's an embedded
 * partition table, otherwise zero for the full disk)
 *
 * Returns: offset, from start of disk, in bytes
 */
guint64
part_table_get_offset (PartitionTable *part_table);

/**
 * part_table_get_size:
 * @part_table: the partition table
 *
 * Get size of partition table (as referenced in the partition table
 * entry if it's an embedded partition table, otherwise the size of
 * the full disk)
 *
 * Returns: size of partition, in bytes
 */
guint64
part_table_get_size (PartitionTable *part_table);

/**
 * part_table_find:
 * @part_table: the partition table
 * @offset: the offset to test for
 * @out_part_table: where the (embedded) enclosing the entry will be stored
 * @out_entry: there the partition table entry number will be stored
 *
 * This function finds the entry that a certain byte of the disk belongs to.
 * As partition tables can be embedded (think MS-DOS extended partitions)
 * the returned partition table (out_part_table) might be different from
 * the one passed. If the offset belongs to a primary partition then the
 * return partition_table will be the same as the passed one.
 *
 * If there is no partition at the given offset (might be free space),
 * out_entry will be set to -1. Note that out_part_table will always
 * be set though and free space in the primary disk space and the
 * extended partition space differs.
 *
 * This is a convenience function.
 */
void
part_table_find (PartitionTable *part_table,
                 guint64 offset,
                 PartitionTable **out_part_table,
                 int *out_entry);

/**
 * part_table_entry_get_nested:
 * @part_table: the partition table
 * @entry: zero-based index of entry in partition table
 *
 * If the partition table entry points to an embedded partition table this
 * function will return a PartitionTable object representing it.
 *
 * Returns: NULL if the entry does not point to a an embedded partition table.
 *          Do not free with part_table_free() - the object will be freed when
 *          freeing the root object.
 */
PartitionTable *
part_table_entry_get_nested (PartitionTable *part_table,
                             int entry);

/**
 * part_table_entry_is_in_use:
 * @part_table: the partition table
 * @entry: zero-based index of entry in partition table
 *
 * Some partition table formats, notably PART_TYPE_MSDOS, has the
 * notion of unused partition table entries. For example it's
 * perfectly fine to only have /dev/sda being partitioned into
 * /dev/sda1 and /dev/sda3.
 *
 * This function determines whether a partition table entry is in use
 * or not.
 *
 * Returns: Whether the partition table entry is in use
 */
gboolean
part_table_entry_is_in_use (PartitionTable *part_table,
                            int entry);

/**
 * part_table_entry_get_type:
 * @part_table: the partition table
 * @entry: zero-based index of entry in partition table
 *
 * Get the partition table type - the type itself is partitioning scheme
 * specific as described below.
 *
 * For PART_TYPE_MSDOS and PART_TYPE_MSDOS_EXTENDED, the type is an integer
 * encoded in a string, e.g. 0x83 is Linux. Use atoi() to convert back to
 * an integer. See http://www.win.tue.nl/~aeb/partitions/partition_types-1.html
 * for details.
 *
 * For PART_TYPE_GPT, this is the GUID encoded as a string, see
 * http://en.wikipedia.org/wiki/GUID_Partition_Table for details.
 *
 * For PART_TYPE_APPLE, this is a string as defined in
 * http://developer.apple.com/documentation/mac/Devices/Devices-126.html.
 * For FAT file systems, it appears that "DOS_FAT_32", "DOS_FAT_16" and
 * "DOS_FAT_12" are also recognized under Mac OS X (I've tested this too) cf.
 * http://lists.apple.com/archives/Darwin-drivers/2003/May/msg00021.html
 *
 * Returns: The partition table type. Caller shall free this with g_free().
 */
char *
part_table_entry_get_type (PartitionTable *part_table,
                           int entry);

/**
 * part_table_entry_get_label:
 * @part_table: the partition table
 * @entry: zero-based index of entry in partition table
 *
 * Label of the partition. This is only supported for PART_TYPE_APPLE and
 * PART_TYPE_GPT. Note that this is not the same as the file system label
 * in a file system in the partition.
 *
 * Returns: The label or NULL if the partitioning scheme does not support
 *          labels. Caller shall free this with g_free().
 */
char *
part_table_entry_get_label (PartitionTable *part_table,
                            int entry);

/**
 * part_table_entry_get_uuid:
 * @part_table: the partition table
 * @entry: zero-based index of entry in partition table
 *
 * Some UUID/GUID of the partition. This is only supported for PART_TYPE_GPT.
 *
 * Returns: The UUID or NULL if the partitioning scheme does not support
 *          UUID/GUID. Caller shall free this with g_free().
 */
char *
part_table_entry_get_uuid (PartitionTable *part_table,
                           int entry);

/**
 * part_table_entry_get_flags:
 * @part_table: the partition table
 * @entry: zero-based index of entry in partition table
 *
 * Get flags of partition table entry. This is dependent on the partitioning
 * scheme.
 *
 * For PART_TYPE_MSDOS and PART_TYPE_MSDOS_EXTENDED the following flags are
 * recognized:
 * - "boot"; meaning that the bootable flag is set. This is used by some
 *   BIOS'es and boot loaders to populate a boot menu.
 *
 * For PART_TYPE_GPT the following flags are recognized:
 * - "required" which corresponds to bit 0 of the attibutes
 *   (offset 48), meaning "Required for the platform to function. The
 *   system cannot function normally if this partition is removed. This
 *   partition should be considered as part of the hardware of the
 *   system, and if it is removed the system may not boot. It may
 *   contain diagnostics, recovery tools, or other code or data that is
 *   critical to the functioning of a system independent of any OS."
 *
 *
 * For PART_TYPE_APPLE the following flags are recognized:
 * - "allocated"; if the partition is already allocated
 * - "in_use"; if the partition is in use; may be cleared after a system reset
 * - "boot"; if partition contains valid boot information
 * - "allow_read"; if partition allows reading
 * - "allow_write"; if partition allows writing
 * - "boot_code_is_pic"; if boot code is position independent
 *
 * Returns: An array of strings, one per flag, terminated by NULL. Caller
 *          shall free this with g_strfreev().
 */
char **
part_table_entry_get_flags (PartitionTable *part_table,
                            int entry);

/**
 * part_table_entry_get_offset:
 * @part_table: the partition table
 * @entry: zero-based index of entry in partition table
 *
 * Get offset from start of disk where partition starts (as referenced in the
 * partition table entry)
 *
 * Returns: offset, from start of disk, in bytes
 */
guint64
part_table_entry_get_offset (PartitionTable *part_table,
                             int entry);

/**
 * part_table_entry_get_size:
 * @part_table: the partition table
 * @entry: zero-based index of entry in partition table
 *
 * Get size of partition (as referenced in the partition table entry)
 *
 * Returns: size of partition, in bytes
 */
guint64
part_table_entry_get_size (PartitionTable *part_table,
                           int entry);

/**
 * part_create_partition_table:
 * @device: name of device file for entire disk, e.g. /dev/sda
 * @scheme: the partitioning scheme
 *
 * Create a new fresh partition on a disk.
 *
 * Returns: TRUE if the operation was succesful, otherwise FALSE
 */
gboolean
part_create_partition_table (char *device,
                             PartitionScheme scheme);

/**
 * part_add_partition:
 * @device: name of device file for entire disk, e.g. /dev/sda
 * @start: start offset of partition, in bytes
 * @size: size of partition, in bytes
 * @out_start: where partition will start, after satisfying disk geometry constraints
 * @out_size: size of partition, after satisfying disk geometry constraints
 * @type: the partition type as defined in part_table_entry_get_type()
 * @flags: the partition flags as defined in part_table_entry_get_flags()
 * @label: the partition label as defined in part_table_entry_get_label()
 * @geometry_hps: heads-per-sector used for LBA<->CHS conversions
 * @geometry_spt: sectors-per-track used for LBA<->CHS conversions
 * @poke_kernel: whether to update the kernels in-memory representation of
 * the partition table
 *
 * Adds a new partition to a disk.
 *
 * If geometry_hps and geomtry_spt are both positive, they will be
 * used as the geometry of the disk for CHS<->LBA conversions. Notably
 * this is only applicable for MSDOS / MSDOS_EXTENDED partition
 * tables. Also, in this case, geometry is enforced to ensure that
 * partitions start and end at cylinder boundaries.
 *
 * If either geometry_hps or geomtry_spt are zero, geometry is
 * simply ignored and partitions will only be aligned to blocks, e.g.
 * normally 512 byte boundaries.
 *
 * If both geometry_hps or geomtry_spt are -1, then geometry information
 * probed from existing partition table entries / file systems on the
 * disk. This is not always reliable.
 *
 * As such, the caller cannot always expect that the partition created
 * will be at the requested offset and size due to e.g. geometry and
 * block boundary alignment. Therefore, the start and size where the
 * partition ends up is passed in the out_start and out_size
 * arguments.
 *
 * As embedded partition tables are supported, the caller should use
 * part_table_find() in advance to make sure that the passed type,
 * label and flags match the (embedded) partition table that this
 * partition will be part of.
 *
 * To create an MSDOS extended partition table in a MSDOS partition
 * table, simply pass 0x05, 0x0f or 0x85 as the partition type.
 *
 * Unless @poke_kernel is set to #TRUE, in order for changes to take
 * effect, the caller needs to poke the OS kernel himself to make it
 * reload the partition table.
 *
 * NOTE: After calling this function you need to discard any partition table
 * obtained with part_table_load_from_disk() since the in-memory data structure
 * is not updated.
 *
 * Returns: TRUE if the operation was succesful, otherwise FALSE
 */
gboolean
part_add_partition (char *device,
                    guint64 start,
                    guint64 size,
                    guint64 *out_start,
                    guint64 *out_size,
                    guint *out_num,
                    char *type,
                    char *label,
                    char **flags,
                    int geometry_hps,
                    int geometry_spt,
                    gboolean poke_kernel);

/**
 * part_change_partition:
 * @device_file: name of device file for entire disk, e.g. /dev/sda
 * @start: start offset of existing partition, in bytes
 * @new_start: new start offset of partition, in bytes
 * @new_size: new size of partition, in bytes
 * @out_start: where partition will start, after satisfying disk geometry constraints
 * @out_size: size of partition, after satisfying disk geometry constraints
 * @type: the partition type as defined in part_table_entry_get_type() or NULL to not change
 * @flags: the partition flags as defined in part_table_entry_get_flags() or NULL to not change
 * @label: the partition label as defined in part_table_entry_get_label() or NULL to not change
 * @geometry_hps: heads-per-sector used for LBA<->CHS conversions
 * @geometry_spt: sectors-per-track used for LBA<->CHS conversions
 *
 * Changes an existing partition table entry on disk. The contents of
 * the partition will not be touched.
 *
 * XXX TODO FIXME: probably be careful with overlapping partitions as
 * e.g. extended MS-DOS partitions have the partition information just
 * before the partition data itself. Need to look into this.
 *
 * If new_start and new_size matches the existing start and size, only
 * flags, label and type are changed. Any of flags, label and type can
 * be set to NULL to signal there should be no change. Thus, this
 * function serves two purposes. It can be used to both change offset and/or
 * size and it can be used to change type and/or flags and/or label.
 *
 * See part_add_partition() for information about geometry_hps and
 * geometry_spt and how it affects the resulting partition offset and
 * size. This function gives one guarantee though: the resulting size
 * will never be smaller than the requested size. This is useful for
 * two-step operations by which a file system is first shrinked and
 * then the partition table is updated.
 *
 * In order for changes to take effect, the caller needs to poke the
 * OS kernel himself to make it reload the partition table. It is not
 * automatically done by this function.
 *
 * NOTE: After calling this function you need to discard any partition
 * table obtained with part_table_load_from_disk() since the in-memory
 * data structure is not updated.
 *
 * Returns: TRUE if the operation was succesful, otherwise FALSE
 */
gboolean
part_change_partition (char *device_file,
                       guint64 start,
                       guint64 new_start,
                       guint64 new_size,
                       guint64 *out_start,
                       guint64 *out_size,
                       char *type,
                       char *label,
                       char **flags,
                       int geometry_hps,
                       int geometry_spt);

/**
 * part_del_partition:
 * @device: name of device file for entire disk, e.g. /dev/sda
 * @offset: offset of somewhere within the partition to delete, in bytes
 * @poke_kernel: whether to update the kernels in-memory representation of
 * the partition table
 *
 * Deletes a partition. Just pass the offset of the partition. If you
 * delete an extended partition all logical partitions will be deleted
 * too.
 *
 * NOTE: After calling this function you need to discard any partition table
 * obtained with part_table_load_from_disk() since the in-memory data structure
 * is not updated.
 *
 * Returns: TRUE if the operation was succesful, otherwise FALSE
 */
gboolean
part_del_partition (char *device,
                    guint64 offset,
                    gboolean poke_kernel);

#endif /* PARTUTIL_H */
