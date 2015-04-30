/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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

#include "config.h"
#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>

#include <glib/gstdio.h>

#include "storagedlogging.h"
#include "storagedlinuxpartitiontable.h"
#include "storagedlinuxblockobject.h"
#include "storageddaemon.h"
#include "storageddaemonutil.h"
#include "storagedlinuxdevice.h"
#include "storagedlinuxblock.h"

/**
 * SECTION:storagedlinuxpartitiontable
 * @title: StoragedLinuxPartitionTable
 * @short_description: Linux implementation of #StoragedPartitionTable
 *
 * This type provides an implementation of the #StoragedPartitionTable
 * interface on Linux.
 */

typedef struct _StoragedLinuxPartitionTableClass   StoragedLinuxPartitionTableClass;

/**
 * StoragedLinuxPartitionTable:
 *
 * The #StoragedLinuxPartitionTable structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxPartitionTable
{
  StoragedPartitionTableSkeleton parent_instance;
};

struct _StoragedLinuxPartitionTableClass
{
  StoragedPartitionTableSkeletonClass parent_class;
};

static void partition_table_iface_init (StoragedPartitionTableIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxPartitionTable, storaged_linux_partition_table, STORAGED_TYPE_PARTITION_TABLE_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_PARTITION_TABLE, partition_table_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_partition_table_init (StoragedLinuxPartitionTable *partition_table)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (partition_table),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_partition_table_class_init (StoragedLinuxPartitionTableClass *klass)
{
}

/**
 * storaged_linux_partition_table_new:
 *
 * Creates a new #StoragedLinuxPartitionTable instance.
 *
 * Returns: A new #StoragedLinuxPartitionTable. Free with g_object_unref().
 */
StoragedPartitionTable *
storaged_linux_partition_table_new (void)
{
  return STORAGED_PARTITION_TABLE (g_object_new (STORAGED_TYPE_LINUX_PARTITION_TABLE,
                                                 NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_partition_table_update:
 * @table: A #StoragedLinuxPartitionTable.
 * @object: The enclosing #StoragedLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_partition_table_update (StoragedLinuxPartitionTable  *table,
                                       StoragedLinuxBlockObject     *object)
{
  const gchar *type = NULL;
  StoragedLinuxDevice *device = NULL;;

  device = storaged_linux_block_object_get_device (object);
  if (device != NULL)
    type = g_udev_device_get_property (device->udev_device, "ID_PART_TABLE_TYPE");

  storaged_partition_table_set_type_ (STORAGED_PARTITION_TABLE (table), type);

  g_clear_object (&device);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ranges_overlap (guint64 a_offset, guint64 a_size,
                guint64 b_offset, guint64 b_size)
{
  guint64 a1 = a_offset, a2 = a_offset + a_size;
  guint64 b1 = b_offset, b2 = b_offset + b_size;
  gboolean ret = FALSE;

  /* There are only two cases when these intervals can overlap
   *
   * 1.  [a1-------a2]
   *               [b1------b2]
   *
   * 2.            [a1-------a2]
   *     [b1------b2]
   */

  if (a1 <= b1)
    {
      /* case 1 */
      if (a2 > b1)
        {
          ret = TRUE;
          goto out;
        }
    }
  else
    {
      /* case 2 */
      if (b2 > a1)
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  return ret;
}

static gboolean
have_partition_in_range (StoragedPartitionTable     *table,
                         StoragedObject             *object,
                         guint64                     start,
                         guint64                     end,
                         gboolean                    ignore_container)
{
  gboolean ret = FALSE;
  StoragedDaemon *daemon = NULL;
  GDBusObjectManager *object_manager = NULL;
  const gchar *table_object_path;
  GList *objects = NULL, *l;

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  object_manager = G_DBUS_OBJECT_MANAGER (storaged_daemon_get_object_manager (daemon));

  table_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));

  objects = g_dbus_object_manager_get_objects (object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      StoragedObject *i_object = STORAGED_OBJECT (l->data);
      StoragedPartition *i_partition = NULL;

      i_partition = storaged_object_get_partition (i_object);

      if (i_partition == NULL)
        goto cont;

      if (g_strcmp0 (storaged_partition_get_table (i_partition), table_object_path) != 0)
        goto cont;

      if (ignore_container && storaged_partition_get_is_container (i_partition))
        goto cont;

      if (!ranges_overlap (start, end - start,
                           storaged_partition_get_offset (i_partition), storaged_partition_get_size (i_partition)))
        goto cont;

      ret = TRUE;
      g_clear_object (&i_partition);
      goto out;

    cont:
      g_clear_object (&i_partition);
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static StoragedPartition *
find_container_partition (StoragedPartitionTable     *table,
                          StoragedObject             *object,
                          guint64                     start,
                          guint64                     end)
{
  StoragedPartition *ret = NULL;
  StoragedDaemon *daemon = NULL;
  GDBusObjectManager *object_manager = NULL;
  const gchar *table_object_path;
  GList *objects = NULL, *l;

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  object_manager = G_DBUS_OBJECT_MANAGER (storaged_daemon_get_object_manager (daemon));

  table_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));

  objects = g_dbus_object_manager_get_objects (object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      StoragedObject *i_object = STORAGED_OBJECT (l->data);
      StoragedPartition *i_partition = NULL;

      i_partition = storaged_object_get_partition (i_object);

      if (i_partition == NULL)
        goto cont;

      if (g_strcmp0 (storaged_partition_get_table (i_partition), table_object_path) != 0)
        goto cont;

      if (storaged_partition_get_is_container (i_partition)
          && ranges_overlap (start, end - start,
                             storaged_partition_get_offset (i_partition),
                             storaged_partition_get_size (i_partition)))
        {
          ret = i_partition;
          goto out;
        }

    cont:
      g_clear_object (&i_partition);
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  StoragedObject *partition_table_object;
  guint64         pos_to_wait_for;
  gboolean        ignore_container;
} WaitForPartitionData;

static StoragedObject *
wait_for_partition (StoragedDaemon *daemon,
                    gpointer        user_data)
{
  WaitForPartitionData *data = user_data;
  StoragedObject *ret = NULL;
  GList *objects, *l;

  objects = storaged_daemon_get_objects (daemon);
  for (l = objects; l != NULL; l = l->next)
    {
      StoragedObject *object = STORAGED_OBJECT (l->data);
      StoragedPartition *partition = storaged_object_get_partition (object);
      if (partition != NULL)
        {
          if (g_strcmp0 (storaged_partition_get_table (partition),
                         g_dbus_object_get_object_path (G_DBUS_OBJECT (data->partition_table_object))) == 0)
            {
              guint64 offset = storaged_partition_get_offset (partition);
              guint64 size = storaged_partition_get_size (partition);

              if (data->pos_to_wait_for >= offset && data->pos_to_wait_for < offset + size)
                {
                  if (!(storaged_partition_get_is_container (partition) && data->ignore_container))
                    {
                      g_object_unref (partition);
                      ret = g_object_ref (object);
                      goto out;
                    }
                }
            }
          g_object_unref (partition);
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

#define MIB_SIZE (1048576L)

static StoragedObject *
storaged_linux_partition_table_handle_create_partition (StoragedPartitionTable   *table,
                                                        GDBusMethodInvocation    *invocation,
                                                        guint64                   offset,
                                                        guint64                   size,
                                                        const gchar              *type,
                                                        const gchar              *name,
                                                        GVariant                 *options)
{
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  StoragedBlock *block = NULL;
  StoragedObject *object = NULL;
  StoragedDaemon *daemon = NULL;
  gchar *error_message = NULL;
  gchar *escaped_device = NULL;
  gchar *command_line = NULL;
  WaitForPartitionData *wait_data = NULL;
  StoragedObject *partition_object = NULL;
  StoragedBlock *partition_block = NULL;
  gchar *escaped_partition_device = NULL;
  const gchar *table_type;
  pid_t caller_pid;
  uid_t caller_uid;
  gid_t caller_gid;
  gboolean do_wipe = TRUE;
  GError *error;

  error = NULL;
  object = storaged_daemon_util_dup_object (table, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  block = storaged_object_get_block (object);
  if (block == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Partition table object is not a block device");
      goto out;
    }

  error = NULL;
  if (!storaged_daemon_util_get_caller_pid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_pid,
                                                 &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &caller_gid,
                                                 NULL,
                                                 &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  action_id = "org.storaged.Storaged.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests creating a new partition.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to create a partition on $(drive)");
  if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (storaged_block_get_hint_system (block))
        {
          action_id = "org.storaged.Storaged.modify-device-system";
        }
      else if (!storaged_daemon_util_on_same_seat (daemon, object, caller_pid))
        {
          action_id = "org.storaged.Storaged.modify-device-other-seat";
        }
    }

  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (block));

  table_type = storaged_partition_table_get_type_ (table);
  wait_data = g_new0 (WaitForPartitionData, 1);
  if (g_strcmp0 (table_type, "dos") == 0)
    {
      guint64 start_mib;
      guint64 end_bytes;
      guint64 max_end_bytes;
      const gchar *part_type;
      char *endp;
      gint type_as_int;
      gboolean is_logical = FALSE;

      max_end_bytes = storaged_block_get_size (block);

      if (strlen (name) > 0)
        {
          g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                                 "MBR partition table does not support names");
          goto out;
        }

      /* Determine whether we are creating a primary, extended or logical partition */
      type_as_int = strtol (type, &endp, 0);
      if (type[0] != '\0' && *endp == '\0' &&
          (type_as_int == 0x05 || type_as_int == 0x0f || type_as_int == 0x85))
        {
          part_type = "extended";
          do_wipe = FALSE;  // wiping an extended partition destroys it
          if (have_partition_in_range (table, object, offset, offset + size, FALSE))
            {
              g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                                     "Requested range is already occupied by a partition");
              goto out;
            }
        }
      else
        {
          if (have_partition_in_range (table, object, offset, offset + size, FALSE))
            {
              if (have_partition_in_range (table, object, offset, offset + size, TRUE))
                {
                  g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                                         "Requested range is already occupied by a partition");
                  goto out;
                }
              else
                {
                  StoragedPartition *container = find_container_partition (table, object,
                                                                           offset, offset + size);
                  g_assert (container != NULL);
                  is_logical = TRUE;
                  part_type = "logical ext2";
                  max_end_bytes = (storaged_partition_get_offset(container)
                                   + storaged_partition_get_size(container));
                }
            }
          else
            {
              part_type = "primary ext2";
            }
        }

      /* Ensure we _start_ at MiB granularity since that ensures optimal IO...
       * Also round up size to nearest multiple of 512
       */
      start_mib = offset / MIB_SIZE + 1L;
      end_bytes = start_mib * MIB_SIZE + ((size + 511L) & (~511L));

      /* Now reduce size until we are not
       *
       *  - overlapping neighboring partitions; or
       *  - exceeding the end of the disk
       */
      while (end_bytes > start_mib * MIB_SIZE && (have_partition_in_range (table,
                                                                           object,
                                                                           start_mib * MIB_SIZE,
                                                                           end_bytes, is_logical) ||
                                                  end_bytes > max_end_bytes))
        {
          /* TODO: if end_bytes is sufficiently big this could be *a lot* of loop iterations
           *       and thus a potential DoS attack...
           */
          end_bytes -= 512L;
        }
      wait_data->pos_to_wait_for = (start_mib*MIB_SIZE + end_bytes) / 2L;
      wait_data->ignore_container = is_logical;

      command_line = g_strdup_printf ("parted --align optimal --script %s "
                                      "\"mkpart %s %" G_GUINT64_FORMAT "MiB %" G_GUINT64_FORMAT "b\"",
                                      escaped_device,
                                      part_type,
                                      start_mib,
                                      end_bytes - 1); /* end_bytes is *INCLUSIVE* (!) */
    }
  else if (g_strcmp0 (table_type, "gpt") == 0)
    {
      guint64 start_mib;
      guint64 end_bytes;
      gchar *escaped_name;
      gchar *escaped_escaped_name;

      /* GPT is easy, no extended/logical crap */
      if (have_partition_in_range (table, object, offset, offset + size, FALSE))
        {
          g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                                 "Requested range is already occupied by a partition");
          goto out;
        }

      /* bah, parted(8) is broken with empty names (it sets the name to 'ext2' in that case)
       * TODO: file bug
       */
      if (strlen (name) == 0)
        {
          name = " ";
        }

      escaped_name = storaged_daemon_util_escape (name);
      escaped_escaped_name = storaged_daemon_util_escape (escaped_name);

      /* Ensure we _start_ at MiB granularity since that ensures optimal IO...
       * Also round up size to nearest multiple of 512
       */
      start_mib = offset / MIB_SIZE + 1L;
      end_bytes = start_mib * MIB_SIZE + ((size + 511L) & (~511L));

      /* Now reduce size until we are not
       *
       *  - overlapping neighboring partitions; or
       *  - exceeding the end of the disk (note: the 33 LBAs is the Secondary GPT)
       */
      while (end_bytes > start_mib * MIB_SIZE && (have_partition_in_range (table,
                                                                           object,
                                                                           start_mib * MIB_SIZE,
                                                                           end_bytes, FALSE) ||
                                                  (end_bytes > storaged_block_get_size (block) - 33*512)))
        {
          /* TODO: if end_bytes is sufficiently big this could be *a lot* of loop iterations
           *       and thus a potential DoS attack...
           */
          end_bytes -= 512L;
        }
      wait_data->pos_to_wait_for = (start_mib*MIB_SIZE + end_bytes) / 2L;
      command_line = g_strdup_printf ("parted --align optimal --script %s "
                                      "\"mkpart \\\"%s\\\" ext2 %" G_GUINT64_FORMAT "MiB %" G_GUINT64_FORMAT "b\"",
                                      escaped_device,
                                      escaped_escaped_name,
                                      start_mib,
                                      end_bytes - 1); /* end_bytes is *INCLUSIVE* (!) */
      g_free (escaped_escaped_name);
      g_free (escaped_name);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Don't know how to create partitions this partition table of type `%s'",
                                             table_type);
      goto out;
    }

  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                              object,
                                              "partition-create", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "%s",
                                              command_line))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error creating partition on %s: %s",
                                             storaged_block_get_device (block),
                                             error_message);
      goto out;
    }
  /* this is sometimes needed because parted(8) does not generate the uevent itself */
  storaged_linux_block_object_trigger_uevent (STORAGED_LINUX_BLOCK_OBJECT (object));

  /* sit and wait for the partition to show up */
  g_warn_if_fail (wait_data->pos_to_wait_for > 0);
  wait_data->partition_table_object = object;
  error = NULL;
  partition_object = storaged_daemon_wait_for_object_sync (daemon,
                                                           wait_for_partition,
                                                           wait_data,
                                                           NULL,
                                                           30,
                                                           &error);
  if (partition_object == NULL)
    {
      g_prefix_error (&error, "Error waiting for partition to appear: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }
  partition_block = storaged_object_get_block (partition_object);
  if (partition_block == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, STORAGED_ERROR, STORAGED_ERROR_FAILED,
                                             "Partition object is not a block device");
      g_clear_object (&partition_object);
      goto out;
    }
  escaped_partition_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (partition_block));

  /* TODO: set partition type */

  /* wipe the newly created partition if wanted */
  if (do_wipe)
    {
      if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                    partition_object,
                                                    "partition-create", caller_uid,
                                                    NULL, /* GCancellable */
                                                    0,    /* uid_t run_as_uid */
                                                    0,    /* uid_t run_as_euid */
                                                    NULL, /* gint *out_status */
                                                    &error_message,
                                                    NULL,  /* input_string */
                                                    "wipefs -a %s",
                                                    escaped_partition_device))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Error wiping newly created partition %s: %s",
                                                 storaged_block_get_device (partition_block),
                                                 error_message);
          g_clear_object (&partition_object);
          goto out;
        }
    }

  /* this is sometimes needed because parted(8) does not generate the uevent itself */
  storaged_linux_block_object_trigger_uevent (STORAGED_LINUX_BLOCK_OBJECT (partition_object));

 out:
  g_free (escaped_partition_device);
  g_free (wait_data);
  g_clear_object (&partition_block);
  g_free (command_line);
  g_free (escaped_device);
  g_free (error_message);
  g_clear_object (&object);
  g_clear_object (&block);
  return partition_object;
}

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_create_partition (StoragedPartitionTable   *table,
                         GDBusMethodInvocation    *invocation,
                         guint64                   offset,
                         guint64                   size,
                         const gchar              *type,
                         const gchar              *name,
                         GVariant                 *options)
{
  StoragedObject *partition_object =
    storaged_linux_partition_table_handle_create_partition (table,
                                                            invocation,
                                                            offset,
                                                            size,
                                                            type,
                                                            name,
                                                            options);

  if (partition_object)
    {
      storaged_partition_table_complete_create_partition
        (table, invocation, g_dbus_object_get_object_path (G_DBUS_OBJECT (partition_object)));
      g_object_unref (partition_object);
    }

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* runs in thread dedicated to handling @invocation */
struct FormatCompleteData {
  StoragedPartitionTable *table;
  GDBusMethodInvocation *invocation;
  StoragedObject *partition_object;
};

static void
handle_format_complete (gpointer user_data)
{
  struct FormatCompleteData *data = user_data;
  storaged_partition_table_complete_create_partition
    (data->table, data->invocation, g_dbus_object_get_object_path (G_DBUS_OBJECT (data->partition_object)));
}

static gboolean
handle_create_partition_and_format (StoragedPartitionTable   *table,
                                    GDBusMethodInvocation    *invocation,
                                    guint64                   offset,
                                    guint64                   size,
                                    const gchar              *type,
                                    const gchar              *name,
                                    GVariant                 *options,
                                    const gchar              *format_type,
                                    GVariant                 *format_options)
{
  StoragedObject *partition_object =
    storaged_linux_partition_table_handle_create_partition (table,
                                                            invocation,
                                                            offset,
                                                            size,
                                                            type,
                                                            name,
                                                            options);

  if (partition_object)
    {
      struct FormatCompleteData data;
      data.table = table;
      data.invocation = invocation;
      data.partition_object = partition_object;
      storaged_linux_block_handle_format (storaged_object_peek_block (partition_object),
                                          invocation,
                                          format_type,
                                          format_options,
                                          handle_format_complete, &data);
      g_object_unref (partition_object);
    }

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
partition_table_iface_init (StoragedPartitionTableIface *iface)
{
  iface->handle_create_partition = handle_create_partition;
  iface->handle_create_partition_and_format = handle_create_partition_and_format;
}
