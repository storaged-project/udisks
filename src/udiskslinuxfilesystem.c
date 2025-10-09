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
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <mntent.h>
#include <sys/sysmacros.h>
#ifdef HAVE_ACL
#include <sys/acl.h>
#endif
#include <errno.h>
#include <blockdev/fs.h>
#include <blockdev/utils.h>

#include <libmount/libmount.h>

#include <glib/gstdio.h>

#include "udiskslogging.h"
#include "udiskslinuxfilesystem.h"
#include "udiskslinuxfilesystemhelpers.h"
#include "udiskslinuxblockobject.h"
#include "udiskslinuxblock.h"
#include "udisksdaemon.h"
#include "udisksstate.h"
#include "udisksdaemonutil.h"
#include "udisksmountmonitor.h"
#include "udisksmount.h"
#include "udiskslinuxdevice.h"
#include "udiskssimplejob.h"
#include "udiskslinuxdriveata.h"
#include "udiskslinuxmountoptions.h"
#include "udisksata.h"

/**
 * SECTION:udiskslinuxfilesystem
 * @title: UDisksLinuxFilesystem
 * @short_description: Linux implementation of #UDisksFilesystem
 *
 * This type provides an implementation of the #UDisksFilesystem
 * interface on Linux.
 */

typedef struct _UDisksLinuxFilesystemClass   UDisksLinuxFilesystemClass;

/**
 * UDisksLinuxFilesystem:
 *
 * The #UDisksLinuxFilesystem structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxFilesystem
{
  UDisksFilesystemSkeleton parent_instance;
  GMutex lock;
  guint64 cached_fs_size;
  gchar *cached_device_file;
  gchar *cached_fs_type;
  gboolean cached_drive_is_ata;
};

struct _UDisksLinuxFilesystemClass
{
  UDisksFilesystemSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_SIZE,
};

static void filesystem_iface_init (UDisksFilesystemIface *iface);
static guint64 get_filesystem_size (UDisksLinuxFilesystem *filesystem);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxFilesystem, udisks_linux_filesystem, UDISKS_TYPE_FILESYSTEM_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_FILESYSTEM, filesystem_iface_init));

#ifdef HAVE_FHS_MEDIA
#define MOUNT_BASE "/media"
#define MOUNT_BASE_PERSISTENT TRUE
#else
#define MOUNT_BASE "/run/media"
#define MOUNT_BASE_PERSISTENT FALSE
#endif

/* required for kernel module autoloading */
static const gchar *well_known_filesystems[] =
{
  "bcache",
  "bcachefs",
  "btrfs",
  "erofs",
  "exfat",
  "ext2",
  "ext3",
  "ext4",
  "f2fs",
  "hfs",
  "hfsplus",
  "iso9660",
  "jfs",
  "msdos",
  "nilfs",
  "nilfs2",
  "ntfs",
  "ntfs3",
  "udf",
  "reiserfs",
  "reiser4",
  "reiser5",
  "squashfs",
  "umsdos",
  "vfat",
  "xfs",
};

/* filesystems known to report their outer boundaries */
static const gchar *fs_lastblock_list[] =
{
  "ext2",
  "ext3",
  "ext4",
  "xfs",
};

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_filesystem_finalize (GObject *object)
{
  UDisksLinuxFilesystem *filesystem = UDISKS_LINUX_FILESYSTEM (object);

  g_mutex_clear (&(filesystem->lock));
  g_free (filesystem->cached_device_file);
  g_free (filesystem->cached_fs_type);

  if (G_OBJECT_CLASS (udisks_linux_filesystem_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_filesystem_parent_class)->finalize (object);
}

static void
udisks_linux_filesystem_init (UDisksLinuxFilesystem *filesystem)
{
  g_mutex_init (&filesystem->lock);
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (filesystem),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_filesystem_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  UDisksLinuxFilesystem *filesystem = UDISKS_LINUX_FILESYSTEM (object);

  switch (prop_id)
    {
    case PROP_SIZE:
      g_value_set_uint64 (value, get_filesystem_size (filesystem));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_filesystem_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_SIZE:
      g_warning ("udisks_linux_filesystem_set_property() should never be called, value = %" G_GUINT64_FORMAT, g_value_get_uint64 (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_filesystem_class_init (UDisksLinuxFilesystemClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_filesystem_finalize;
  gobject_class->get_property = udisks_linux_filesystem_get_property;
  gobject_class->set_property = udisks_linux_filesystem_set_property;

  g_object_class_override_property (gobject_class, PROP_SIZE, "size");
}

/**
 * udisks_linux_filesystem_new:
 *
 * Creates a new #UDisksLinuxFilesystem instance.
 *
 * Returns: A new #UDisksLinuxFilesystem. Free with g_object_unref().
 */
UDisksFilesystem *
udisks_linux_filesystem_new (void)
{
  return UDISKS_FILESYSTEM (g_object_new (UDISKS_TYPE_LINUX_FILESYSTEM,
                                          NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
in_fs_lastblock_list (const gchar *fstype)
{
  guint n;

  for (n = 0; n < G_N_ELEMENTS (fs_lastblock_list); n++)
    if (g_strcmp0 (fs_lastblock_list[n], fstype) == 0)
      return TRUE;
  return FALSE;
}

/* WARNING: called with GDBusObjectManager lock held, avoid any object lookup */
static guint64
get_filesystem_size (UDisksLinuxFilesystem *filesystem)
{
  guint64 size = 0;

  if (filesystem->cached_fs_size > 0)
    return filesystem->cached_fs_size;

  if (!filesystem->cached_device_file || !filesystem->cached_fs_type)
    return 0;

  /* manually getting size is supported only for Ext and XFS */
  if (!in_fs_lastblock_list (filesystem->cached_fs_type))
    return 0;

  /* if the drive is ATA and is sleeping, skip filesystem size check to prevent
   * drive waking up - nothing has changed anyway since it's been sleeping...
   */
  if (filesystem->cached_drive_is_ata)
    {
      guchar pm_state = 0;

      if (udisks_ata_get_pm_state (filesystem->cached_device_file, NULL, &pm_state))
        if (!UDISKS_ATA_PM_STATE_AWAKE (pm_state))
          return 0;
    }

  size = bd_fs_get_size (filesystem->cached_device_file, filesystem->cached_fs_type, NULL);
  filesystem->cached_fs_size = size;
  return size;
}

static UDisksDriveAta *
get_drive_ata (UDisksLinuxBlockObject *object)
{
  UDisksObject *drive_object = NULL;
  UDisksDriveAta *ata = NULL;
  UDisksBlock *block;

  block = udisks_object_peek_block (UDISKS_OBJECT (object));
  if (block == NULL)
    return NULL;

  drive_object = udisks_daemon_find_object (udisks_linux_block_object_get_daemon (object), udisks_block_get_drive (block));
  if (drive_object == NULL)
    return NULL;

  ata = udisks_object_get_drive_ata (drive_object);

  g_object_unref (drive_object);

  return ata;
}

static void
invalidate_property (UDisksLinuxFilesystem *filesystem,
                     const gchar           *prop_name)
{
  GVariantBuilder builder;
  GVariantBuilder invalidated_builder;
  GList *connections, *ll;
  GVariant *signal_variant;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
  g_variant_builder_add (&invalidated_builder, "s", prop_name);

  signal_variant = g_variant_ref_sink (g_variant_new ("(sa{sv}as)", "org.freedesktop.UDisks2.Filesystem",
                                       &builder, &invalidated_builder));
  connections = g_dbus_interface_skeleton_get_connections (G_DBUS_INTERFACE_SKELETON (filesystem));
  for (ll = connections; ll != NULL; ll = ll->next)
    {
      GDBusConnection *connection = ll->data;

      g_dbus_connection_emit_signal (connection,
                                     NULL, g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (filesystem)),
                                     "org.freedesktop.DBus.Properties",
                                     "PropertiesChanged",
                                     signal_variant,
                                     NULL);
    }
  g_variant_unref (signal_variant);
  g_list_free_full (connections, g_object_unref);
}

/**
 * udisks_linux_filesystem_update:
 * @filesystem: A #UDisksLinuxFilesystem.
 * @object: The enclosing #UDisksLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
udisks_linux_filesystem_update (UDisksLinuxFilesystem  *filesystem,
                                UDisksLinuxBlockObject *object)
{
  UDisksDriveAta *ata = NULL;
  UDisksMountMonitor *mount_monitor;
  UDisksLinuxDevice *device;
  GPtrArray *p;
  GList *mounts;
  GList *l;
  gboolean mounted;

  mount_monitor = udisks_daemon_get_mount_monitor (udisks_linux_block_object_get_daemon (object));
  device = udisks_linux_block_object_get_device (object);

  p = g_ptr_array_new ();
  mounts = udisks_mount_monitor_get_mounts_for_dev (mount_monitor, g_udev_device_get_device_number (device->udev_device));
  /* we are guaranteed that the list is sorted so if there are
   * multiple mounts we'll always get the same order
   */
  for (l = mounts; l != NULL; l = l->next)
    {
      UDisksMount *mount = UDISKS_MOUNT (l->data);
      if (udisks_mount_get_mount_type (mount) == UDISKS_MOUNT_TYPE_FILESYSTEM)
        g_ptr_array_add (p, (gpointer) udisks_mount_get_mount_path (mount));
    }
  g_ptr_array_add (p, NULL);
  udisks_filesystem_set_mount_points (UDISKS_FILESYSTEM (filesystem),
                                      (const gchar *const *) p->pdata);
  mounted = p->len > 0;
  g_ptr_array_free (p, TRUE);
  g_list_free_full (mounts, g_object_unref);

  /* cached device properties for on-demand filesystem size retrieval */
  g_free (filesystem->cached_device_file);
  g_free (filesystem->cached_fs_type);
  filesystem->cached_fs_type = g_strdup (g_udev_device_get_property (device->udev_device, "ID_FS_TYPE"));
  filesystem->cached_device_file = udisks_linux_block_object_get_device_file (object);

  /* TODO: this only looks for a drive object associated with the current
   * block object. In case of a complex layered structure this needs to walk
   * the tree and return a list of physical drives to check the powermanagement on.
   */
  ata = get_drive_ata (object);
  filesystem->cached_drive_is_ata = ata != NULL && udisks_drive_ata_get_pm_supported (ata);
  g_clear_object (&ata);

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (filesystem));

  if (mounted && g_strcmp0 (filesystem->cached_fs_type, "xfs") == 0)
    /* Force native filesystem tools for mounted XFS as superblock might
     * not have been written right after the grow.
     */
    filesystem->cached_fs_size = 0;
  else
    /* The ID_FS_SIZE property only contains data part of the total filesystem
     * size and comes with no guarantees while 'ID_FS_LASTBLOCK * ID_FS_BLOCKSIZE'
     * typically marks the boundary of the filesystem.
     */
    filesystem->cached_fs_size = g_udev_device_get_property_as_uint64 (device->udev_device, "ID_FS_LASTBLOCK") *
                                 g_udev_device_get_property_as_uint64 (device->udev_device, "ID_FS_BLOCKSIZE");

  /* The Size property is hacked to be retrieved on-demand, only need to
   * notify subscribers that it has changed.
   */
  invalidate_property (filesystem, "Size");

  g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_in_filesystem_file (const gchar *filesystems_file,
                       const gchar *fstype)
{
  gchar *filesystems = NULL;
  GError *error = NULL;
  gboolean ret = FALSE;
  gchar **lines = NULL;
  guint n;

  if (!g_file_get_contents (filesystems_file,
                            &filesystems,
                            NULL, /* gsize *out_length */
                            &error))
    {
      udisks_warning ("Error reading %s: %s (%s %d)",
                      filesystems_file,
                      error->message,
                      g_quark_to_string (error->domain),
                      error->code);
      g_clear_error (&error);
      goto out;
    }

  lines = g_strsplit (filesystems, "\n", -1);
  for (n = 0; lines != NULL && lines[n] != NULL && !ret; n++)
    {
      gchar **tokens;
      gint num_tokens;
      g_strdelimit (lines[n], " \t", ' ');
      g_strstrip (lines[n]);
      tokens = g_strsplit (lines[n], " ", -1);
      num_tokens = g_strv_length (tokens);
      if (num_tokens == 1 && g_strcmp0 (tokens[0], fstype) == 0)
        {
          ret = TRUE;
        }
      g_strfreev (tokens);
    }

 out:
  g_strfreev (lines);
  g_free (filesystems);
  return ret;
}

static gboolean
is_well_known_filesystem (const gchar *fstype)
{
  guint n;

  for (n = 0; n < G_N_ELEMENTS (well_known_filesystems); n++)
    if (g_strcmp0 (well_known_filesystems[n], fstype) == 0)
      return TRUE;
  return FALSE;
}

/* this is not a very efficient implementation but it's very rarely
 * called so no real point in optimizing it...
 */
static gboolean
is_allowed_filesystem (const gchar *fstype)
{
  return is_well_known_filesystem (fstype) ||
    is_in_filesystem_file ("/proc/filesystems", fstype) ||
    is_in_filesystem_file ("/etc/filesystems", fstype);
}

/* ---------------------------------------------------------------------------------------------------- */

/*
 * calculate_fs_type: <internal>
 * @block: A #UDisksBlock.
 * @given_options: The a{sv} #GVariant.
 * @fs_signature: A place to store the probed filesystem signature.
 * @fs_type: A place to store the specific filesystem type to use for mounting. %NULL indicates no preference in relation to @fs_signature.
 * @error: Return location for error or %NULL.
 *
 * Retrieves the actual filesystem type (superblock signature) and an optional
 * specified filesystem type. Under normal circumstances only the signature
 * is returned if known and @fs_type set to %NULL. In case of an explicit override
 * the @fs_type is set to a specific value. In case a filesystem signature
 * is unknown and no override requested, the @fs_type is set to "auto".
 *
 * The resulting values are valid UTF-8 strings, free them with g_free().
 *
 * Returns: %TRUE in case of success with @fs_signature and @fs_type set, %FALSE in case of a failure and @error being set.
 */
static gboolean
calculate_fs_type (UDisksBlock  *block,
                   GVariant     *given_options,
                   gchar       **fs_signature,
                   gchar       **fs_type,
                   GError      **error)
{
  const gchar *probed_fs_type = NULL;
  const gchar *requested_fs_type;

  g_warn_if_fail (fs_signature != NULL);
  g_warn_if_fail (fs_type != NULL);

  *fs_type = NULL;
  *fs_signature = NULL;

  if (block != NULL)
    probed_fs_type = udisks_block_get_id_type (block);

  if (g_variant_lookup (given_options,
                        "fstype",
                        "&s", &requested_fs_type) &&
      strlen (requested_fs_type) > 0)
    {
      /* If the user requests the filesystem type, error out unless the
       * filesystem type is
       *
       * - well-known [1]; or
       * - in the /proc/filesystems file; or
       * - in the /etc/filesystems file
       *
       * in that order. We do this because mount(8) on Linux allows
       * loading any arbitrary kernel module (when invoked as root) by
       * passing something appropriate to the -t option. So we have to
       * validate whatever we pass...
       *
       * See https://bugs.freedesktop.org/show_bug.cgi?id=32232 for more
       * details.
       *
       * [1] : since /etc/filesystems may be horribly out of date and
       *       not contain e.g. ext4
       */
      if (g_strcmp0 (requested_fs_type, "auto") != 0)
        {
          if (!is_allowed_filesystem (requested_fs_type))
            {
              g_set_error (error,
                           UDISKS_ERROR,
                           UDISKS_ERROR_OPTION_NOT_PERMITTED,
                           "Requested filesystem type `%s' is neither well-known nor "
                           "in /proc/filesystems nor in /etc/filesystems",
                           requested_fs_type);
              return FALSE;
            }
          *fs_type = g_ascii_strdown (requested_fs_type, -1);
        }
    }
  else
    {
      if (probed_fs_type == NULL || strlen (probed_fs_type) == 0)
        *fs_type = g_strdup ("auto");
     }

  if (probed_fs_type != NULL && strlen (probed_fs_type) > 0)
    *fs_signature = g_ascii_strdown (probed_fs_type, -1);

  if (*fs_type)
    g_warn_if_fail (g_utf8_validate (*fs_type, -1, NULL));
  if (*fs_signature)
    g_warn_if_fail (g_utf8_validate (*fs_signature, -1, NULL));

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
ensure_utf8 (const gchar *s)
{
  const gchar *end;
  gchar *ret;

  if (!g_utf8_validate (s, -1, &end))
    {
      gchar *tmp;
      gint pos;
      /* TODO: could possibly return a nicer UTF-8 string  */
      pos = (gint) (end - s);
      tmp = g_strndup (s, end - s);
      ret = g_strdup_printf ("%s (Invalid UTF-8 at byte %d)", tmp, pos);
      g_free (tmp);
    }
  else
    {
      ret = g_strdup (s);
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

#ifdef HAVE_ACL
static gboolean
add_acl (const gchar  *path,
         uid_t         uid,
         gid_t         gid,
         GError      **error)
{
  gboolean ret = FALSE;
  acl_t acl = NULL;
  acl_entry_t entry;
  acl_permset_t permset;

  acl = acl_get_file(path, ACL_TYPE_ACCESS);
  if (acl == NULL ||
      acl_create_entry (&acl, &entry) == -1 ||
      acl_set_tag_type (entry, ACL_USER) == -1 ||
      acl_set_qualifier (entry, &uid) == -1 ||
      acl_get_permset (entry, &permset) == -1 ||
      acl_add_perm (permset, ACL_READ|ACL_EXECUTE) == -1 ||
      acl_calc_mask (&acl) == -1 ||
      acl_set_file (path, ACL_TYPE_ACCESS, acl) == -1)
    {
      udisks_warning(
                   "Adding read ACL for uid %d to `%s' failed: %m",
                   (gint) uid, path);
      chown (path, -1, gid);
    }

  ret = TRUE;

  if (acl != NULL)
    acl_free (acl);
  return ret;
}
#endif

/*
 * Return mount_dir/label after sanitizing the label. Free with g_free().
 */
static gchar *
sanitize_mount_point (const gchar *mount_dir,
                      const gchar *label)
{
  gchar *s;
  const gchar *p;
  GString *str = g_string_new (NULL);
  g_string_append_printf (str, "%s/", mount_dir);
  s = ensure_utf8 (label);
  for (p = s; *p != '\0'; p = g_utf8_next_char (p))
    {
      gchar c = *p;
      if ((guchar) c < 128)
        {
          if (!g_ascii_isprint (c) || c == '/' || c == '"' || c == '\'' || c == '\\')
            g_string_append_c (str, '_');
          else
            g_string_append_c (str, c);
        }
      else
        {
          gunichar uc = g_utf8_get_char (p);
          if (!g_unichar_isprint (uc))
            g_string_append_c (str, '_');
          else
            g_string_append_unichar (str, uc);
        }
    }
  g_free (s);
  return g_string_free (str, FALSE);
}

/*
 * calculate_mount_point: <internal>
 * @daemon: A #UDisksDaemon.
 * @block: A #UDisksBlock.
 * @uid: user id of the calling user
 * @gid: group id of the calling user
 * @user_name: user name of the calling user
 * @fs_type: The file system type to mount with
 * @persistent: if the mount point is persistent (survives reboot) or not
 * @error: Return location for error or %NULL.
 *
 * Calculates the mount point to use.
 *
 * Returns: A UTF-8 string with the mount point to use or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_mount_point (UDisksDaemon  *daemon,
                       UDisksBlock   *block,
                       uid_t          uid,
                       gid_t          gid,
                       const gchar   *user_name,
                       const gchar   *fs_type,
                       gboolean      *persistent,
                       GError       **error)
{
  UDisksLinuxBlockObject *object = NULL;
  gboolean fs_shared = FALSE;
  const gchar *label = NULL;
  const gchar *uuid = NULL;
  gchar *escaped_user_name = NULL;
  gchar *mount_dir = NULL;
  gchar *mount_point = NULL;
  gchar *orig_mount_point;
  guint n;

  label = NULL;
  uuid = NULL;
  if (block != NULL)
    {
      label = udisks_block_get_id_label (block);
      uuid = udisks_block_get_id_uuid (block);
    }

  object = udisks_daemon_util_dup_object (block, NULL);
  if (object != NULL)
    {
      UDisksLinuxDevice *device = udisks_linux_block_object_get_device (object);
      if (device != NULL)
        {
          if (device->udev_device != NULL)
            {
              /* TODO: maybe introduce Block:HintFilesystemShared instead of pulling it directly from the udev device */
              fs_shared = g_udev_device_get_property_as_boolean (device->udev_device, "UDISKS_FILESYSTEM_SHARED");
            }
          g_object_unref (device);
        }
    }

  /* If we know the user-name and it doesn't have any '/' character in
   * it, mount in MOUNT_BASE/$USER
   */
  if (!fs_shared && (user_name != NULL && strstr (user_name, "/") == NULL))
    {
      mount_dir = g_strdup_printf (MOUNT_BASE "/%s", user_name);
      *persistent = MOUNT_BASE_PERSISTENT;
      if (!g_file_test (mount_dir, G_FILE_TEST_EXISTS))
        {
          /* First ensure that MOUNT_BASE exists */
          if (g_mkdir (MOUNT_BASE, 0755) != 0 && errno != EEXIST)
            {
              g_set_error (error,
                           UDISKS_ERROR,
                           UDISKS_ERROR_FAILED,
                           "Error creating directory " MOUNT_BASE ": %m");
              goto out;
            }
          /* Then create the per-user MOUNT_BASE/$USER */
          if (g_mkdir (mount_dir, 0750) != 0 && errno != EEXIST)
            {
              g_set_error (error,
                           UDISKS_ERROR,
                           UDISKS_ERROR_FAILED,
                           "Error creating directory `%s': %m",
                           mount_dir);
              goto out;
            }
          /* Finally, add the read+execute ACL for $USER */
#ifdef HAVE_ACL
          if (!add_acl (mount_dir, uid, gid, error))
            {
#else
          if (chown (mount_dir, -1, gid) == -1)
            {
               g_set_error (error, G_IO_ERROR,
                            g_io_error_from_errno (errno),
                            "Failed to change gid to %d for %s: %m",
                            (gint) gid, mount_dir);
#endif
              if (rmdir (mount_dir) != 0)
                udisks_warning ("Error calling rmdir() on %s: %m", mount_dir);
              goto out;
            }
        }
    }
  /* otherwise fall back to mounting in /media */
  if (mount_dir == NULL)
    {
      mount_dir = g_strdup ("/media");
      *persistent = TRUE;
    }

  if (label != NULL && strlen (label) > 0)
    {
      mount_point = sanitize_mount_point (mount_dir, label);
    }
  else if (uuid != NULL && strlen (uuid) > 0)
    {
      mount_point = sanitize_mount_point (mount_dir, uuid);
    }
  else
    {
      mount_point = g_strdup_printf ("%s/disk", mount_dir);
    }

  /* ... then uniqify the mount point */
  orig_mount_point = g_strdup (mount_point);
  n = 1;
  while (TRUE)
    {
      if (!g_file_test (mount_point, G_FILE_TEST_EXISTS))
        {
          break;
        }
      else
        {
          g_free (mount_point);
          mount_point = g_strdup_printf ("%s%u", orig_mount_point, n++);
        }
    }
  g_free (orig_mount_point);

 out:
  g_free (mount_dir);
  g_clear_object (&object);
  g_free (escaped_user_name);
  return mount_point;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
has_option (const gchar *options,
            const gchar *option)
{
  gboolean ret = FALSE;
  gchar **tokens;
  guint n;

  tokens = g_strsplit (options, ",", -1);
  for (n = 0; tokens != NULL && tokens[n] != NULL; n++)
    {
      if (g_strcmp0 (tokens[n], option) == 0)
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  g_strfreev (tokens);
  return ret;
}

/* returns TRUE if, and only if, device is referenced in e.g. /etc/fstab
 */
static gboolean
is_system_managed (UDisksDaemon *daemon,
                   UDisksBlock  *block,
                   gchar       **out_mount_point,
                   gchar       **out_mount_options)
{
  UDisksMountMonitor *mount_monitor = udisks_daemon_get_mount_monitor (daemon);
  gboolean ret = FALSE;
  struct libmnt_table *table;
  struct libmnt_iter* iter;
  struct libmnt_fs *fs = NULL;

  table = mnt_new_table ();
  if (mnt_table_parse_fstab (table, NULL) < 0)
    {
      mnt_free_table (table);
      return FALSE;
    }

  iter = mnt_new_iter (MNT_ITER_FORWARD);
  while (mnt_table_next_fs (table, iter, &fs) == 0)
    {
      if (udisks_linux_block_matches_id (UDISKS_LINUX_BLOCK (block), mnt_fs_get_source (fs)))
        {
          /* If this block device is found in fstab, but something else is already
           * mounted on that mount point, ignore the fstab entry.
           */
          UDisksMount *mount = udisks_mount_monitor_get_mount_for_path (mount_monitor, mnt_fs_get_target (fs));
          if (mount == NULL || udisks_block_get_device_number (block) == udisks_mount_get_dev (mount))
            {
              ret = TRUE;
              if (out_mount_point != NULL)
                *out_mount_point = g_strdup (mnt_fs_get_target (fs));
              if (out_mount_options != NULL)
                *out_mount_options = mnt_fs_strdup_options (fs);
            }

          g_clear_object (&mount);
          if (ret)
            break;
        }
    }
  mnt_free_iter (iter);
  mnt_free_table (table);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_mount_fstab (UDisksDaemon          *daemon,
                    UDisksObject          *object,
                    uid_t                  caller_uid,
                    gid_t                  caller_gid,
                    gboolean               mount_other_user,
                    const gchar           *mount_point_to_use,
                    const gchar           *fstab_mount_options,
                    GDBusMethodInvocation *invocation,
                    GVariant              *options)
{
  UDisksBlock *block;
  const gchar *device = NULL;
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  gboolean success = FALSE;
  gboolean mount_fstab_as_root = FALSE;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;

  block = udisks_object_peek_block (object);
  device = udisks_block_get_device (block);

  if (!has_option (fstab_mount_options, "x-udisks-auth") &&
      !has_option (fstab_mount_options, "user") &&
      !has_option (fstab_mount_options, "users"))
    {
      mount_fstab_as_root = TRUE;
      action_id = "org.freedesktop.udisks2.filesystem-mount";
      /* Translators: Shown in authentication dialog when the user
       * requests mounting a filesystem.
       *
       * Do not translate $(device.name), it's a placeholder and
       * will be replaced by the name of the drive/device in question
       */
      message = N_("Authentication is required to mount $(device.name)");
      if (mount_other_user)
        {
          action_id = "org.freedesktop.udisks2.filesystem-mount-other-user";
        }
      else if (!udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
        {
          if (udisks_block_get_hint_system (block))
            {
              action_id = "org.freedesktop.udisks2.filesystem-mount-system";
            }
          else if (!udisks_daemon_util_on_user_seat (daemon, object, caller_uid))
            {
              action_id = "org.freedesktop.udisks2.filesystem-mount-other-seat";
            }
        }

      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        object,
                                                        action_id,
                                                        options,
                                                        message,
                                                        invocation))
        return FALSE;
    }


  if (!g_file_test (mount_point_to_use, G_FILE_TEST_IS_DIR))
    {
      if (g_mkdir_with_parents (mount_point_to_use, 0755) != 0)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error creating directory `%s' to be used for mounting %s: %m",
                                                 mount_point_to_use,
                                                 device);
          return FALSE;
        }
    }

  while (TRUE)
    {
      job = udisks_daemon_launch_simple_job (daemon,
                                             UDISKS_OBJECT (object),
                                             "filesystem-mount",
                                             mount_fstab_as_root ? 0 : caller_uid,
                                             FALSE,
                                             NULL /* cancellable */);

      /* XXX: using run_as_uid for root doesn't work even if the caller is already root */
      if (!mount_fstab_as_root && caller_uid != 0)
        {
          BDExtraArg uid_arg = { g_strdup ("run_as_uid"), g_strdup_printf ("%d", caller_uid) };
          BDExtraArg gid_arg = { g_strdup ("run_as_gid"), g_strdup_printf ("%d", caller_gid) };
          const BDExtraArg *extra_args[3] = { &uid_arg, &gid_arg, NULL };

          success = bd_fs_mount (NULL, mount_point_to_use, NULL, NULL, extra_args, &error);

          g_free (uid_arg.opt);
          g_free (uid_arg.val);
          g_free (gid_arg.opt);
          g_free (gid_arg.val);
        }
      else
        {
          success = bd_fs_mount (NULL, mount_point_to_use, NULL, NULL, NULL, &error);
        }

      if (!success)
          udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      else
          udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

      if (!success)
        {
          if (!mount_fstab_as_root && g_error_matches (error, BD_FS_ERROR, BD_FS_ERROR_AUTH))
            {
              g_clear_error (&error);
              action_id = "org.freedesktop.udisks2.filesystem-fstab";
              /* Translators: Shown in authentication dialog when the
               * user requests mounting a filesystem that is in
               * /etc/fstab file with the x-udisks-auth option.
               *
               * Do not translate $(device.name), it's a
               * placeholder and will be replaced by the name of
               * the drive/device in question
               *
               * Do not translate /etc/fstab
               */
              message = N_("Authentication is required to mount $(device.name) referenced in the /etc/fstab file");
              if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                                object,
                                                                action_id,
                                                                options,
                                                                message,
                                                                invocation))
                return FALSE;
              mount_fstab_as_root = TRUE;
              continue;  /* retry */
            }

          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error mounting system-managed device %s: %s",
                                                 device,
                                                 error->message);
          g_clear_error (&error);
          return FALSE;
        }
      return TRUE;
    }
}

static inline void
free_mount_options (UDisksMountOptionsEntry **mount_options)
{
  UDisksMountOptionsEntry **mount_options_i;

  if (mount_options == NULL)
    return;
  for (mount_options_i = mount_options; *mount_options_i; mount_options_i++)
    udisks_mount_options_entry_free (*mount_options_i);
  g_free (mount_options);
}

static gboolean
handle_mount_dynamic (UDisksDaemon          *daemon,
                      UDisksObject          *object,
                      uid_t                  caller_uid,
                      gid_t                  caller_gid,
                      const gchar           *caller_user_name,
                      gboolean               mount_other_user,
                      gchar                **mount_point_to_use,
                      gboolean              *mpoint_persistent,
                      GDBusMethodInvocation *invocation,
                      GVariant              *options)
{
  UDisksBlock *block;
  const gchar *device = NULL;
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  const gchar *probed_fs_usage = NULL;
  gchar *fs_type_to_use = NULL;
  gchar *fs_signature = NULL;
  UDisksMountOptionsEntry **mount_options;
  UDisksMountOptionsEntry **mount_options_i;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;
  gboolean success;

  g_warn_if_fail (mount_point_to_use != NULL);
  g_warn_if_fail (mpoint_persistent != NULL);

  block = udisks_object_peek_block (object);
  device = udisks_block_get_device (block);

  /* Fail if the device is not mountable - we actually allow mounting
   * devices that are not probed since since it could be that we just
   * don't have the data in the udev database but the device has a
   * filesystem *anyway*...
   *
   * For example, this applies to PC floppy devices - automatically
   * probing for media them creates annoying noise. So they won't
   * appear in the udev database.
   */
  if (block != NULL)
    probed_fs_usage = udisks_block_get_id_usage (block);
  if (probed_fs_usage != NULL && strlen (probed_fs_usage) > 0 &&
      g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Cannot mount block device %s with probed usage `%s' - expected `filesystem'",
                                             device,
                                             probed_fs_usage);
      return FALSE;
    }

  /* Check that the user is actually authorized to mount the
   * device. Need to do this before calculating a mount point since we
   * may be racing with other threads...
   */
  action_id = "org.freedesktop.udisks2.filesystem-mount";
  /* Translators: Shown in authentication dialog when the user
   * requests mounting a filesystem.
   *
   * Do not translate $(device.name), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to mount $(device.name)");
  if (mount_other_user)
    {
      action_id = "org.freedesktop.udisks2.filesystem-mount-other-user";
    }
  else if (!udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (udisks_block_get_hint_system (block))
        {
          action_id = "org.freedesktop.udisks2.filesystem-mount-system";
        }
      else if (!udisks_daemon_util_on_user_seat (daemon, object, caller_uid))
        {
          action_id = "org.freedesktop.udisks2.filesystem-mount-other-seat";
        }
    }

  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    return FALSE;

  /* Calculate filesystem type (guaranteed to be valid UTF-8) */
  if (!calculate_fs_type (block, options, &fs_signature, &fs_type_to_use, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return FALSE;
    }

  /* Calculate mount point (guaranteed to be valid UTF-8) */
  *mount_point_to_use = calculate_mount_point (daemon,
                                               block,
                                               caller_uid,
                                               caller_gid,
                                               caller_user_name,
                                               fs_type_to_use,
                                               mpoint_persistent,
                                               &error);
  if (*mount_point_to_use == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      g_free (fs_signature);
      g_free (fs_type_to_use);
      return FALSE;
    }

  /* Calculate mount options (guaranteed to be valid UTF-8) */
  mount_options = udisks_linux_calculate_mount_options (daemon,
                                                        block,
                                                        caller_uid,
                                                        fs_signature,
                                                        fs_type_to_use,
                                                        options,
                                                        &error);
  g_free (fs_signature);
  g_free (fs_type_to_use);
  if (mount_options == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return FALSE;
    }

  /* Create the mount point */
  if (g_mkdir (*mount_point_to_use, 0700) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error creating mount point `%s': %m",
                                             *mount_point_to_use);
      free_mount_options (mount_options);
      return FALSE;
    }

  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         "filesystem-mount",
                                         0,
                                         FALSE,
                                         NULL /* cancellable */);

  success = FALSE;
  for (mount_options_i = mount_options; *mount_options_i; mount_options_i++)
    {
      if (!bd_fs_mount (device,
                        *mount_point_to_use,
                        (*mount_options_i)->fs_type,
                        (*mount_options_i)->options,
                        NULL, &error))
        {
          if (g_error_matches (error, BD_FS_ERROR, BD_FS_ERROR_UNKNOWN_FS) && *(mount_options_i + 1))
            {
              /* Unknown filesystem, continue to the next one unless this is the last entry */
              g_clear_error (&error);
              continue;
            }
          /* Clean up the created mount point */
          if (g_rmdir (*mount_point_to_use) != 0)
            udisks_warning ("Error removing directory %s: %m", *mount_point_to_use);
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error mounting %s at %s: %s",
                                                 device,
                                                 *mount_point_to_use,
                                                 error->message);
          udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
          g_clear_error (&error);
          break;
        }
      success = TRUE;
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);
      break;
    }

  free_mount_options (mount_options);
  return success;
}


/* runs in thread dedicated to handling @invocation */
static gboolean
handle_mount (UDisksFilesystem      *filesystem,
              GDBusMethodInvocation *invocation,
              GVariant              *options)
{
  UDisksObject *object = NULL;
  UDisksBlock *block;
  UDisksDaemon *daemon;
  UDisksState *state = NULL;
  gchar *opt_as_user = NULL;
  uid_t caller_uid;
  gid_t caller_gid;
  const gchar * const *existing_mount_points;
  gchar *mount_point_to_use = NULL;
  gboolean mpoint_persistent = TRUE;
  gchar *fstab_mount_options = NULL;
  gchar *caller_user_name = NULL;
  GError *error = NULL;
  gboolean system_managed = FALSE;
  gchar *device = NULL;

  /* Only allow a single call at a time */
  g_mutex_lock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);

  object = udisks_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (options != NULL)
    {
      g_variant_lookup (options,
                        "as-user",
                        "&s",
                        &opt_as_user);
    }

  block = udisks_object_peek_block (object);
  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);
  device = udisks_block_dup_device (block);

  /* Perform state cleanup to avoid duplicate entries for this block device */
  udisks_linux_block_object_lock_for_cleanup (UDISKS_LINUX_BLOCK_OBJECT (object));
  udisks_state_check_block (state, udisks_linux_block_object_get_device_number (UDISKS_LINUX_BLOCK_OBJECT (object)));

  /* Check if mount point is managed by e.g. /etc/fstab or similar */
  system_managed = is_system_managed (daemon, block, &mount_point_to_use, &fstab_mount_options);

  /* Fail if the device is already mounted */
  existing_mount_points = udisks_filesystem_get_mount_points (filesystem);
  if (existing_mount_points != NULL && g_strv_length ((gchar **) existing_mount_points) > 0)
    {
      GString *str;
      guint n;
      str = g_string_new (NULL);
      for (n = 0; existing_mount_points[n] != NULL; n++)
        {
          if (n > 0)
            g_string_append (str, ", ");
          g_string_append_printf (str, "`%s'", existing_mount_points[n]);
        }
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_ALREADY_MOUNTED,
                                             "Device %s is already mounted at %s.\n",
                                             device,
                                             str->str);
      g_string_free (str, TRUE);
      goto out;
    }

  if (opt_as_user)
    {
      if (!udisks_daemon_util_get_user_info_by_name (opt_as_user, &caller_uid, &caller_gid, &error))
        {
          g_dbus_method_invocation_return_gerror (invocation, error);
          g_clear_error (&error);
          goto out;
        }
      caller_user_name = g_strdup (opt_as_user);
    }
  else
    {
      if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                                   invocation,
                                                   NULL /* GCancellable */,
                                                   &caller_uid,
                                                   &error))
        {
          g_dbus_method_invocation_return_gerror (invocation, error);
          g_clear_error (&error);
          goto out;
        }

      if (!udisks_daemon_util_get_user_info (caller_uid, &caller_gid, &caller_user_name, &error))
        {
          g_dbus_method_invocation_return_gerror (invocation, error);
          g_clear_error (&error);
          goto out;
        }
    }

  /* Mount it */
  if (system_managed)
    {
      if (!handle_mount_fstab (daemon,
                               object,
                               caller_uid,
                               caller_gid,
                               opt_as_user != NULL,
                               mount_point_to_use,
                               fstab_mount_options,
                               invocation,
                               options))
          goto out;
    }
  else
    {
      if (!handle_mount_dynamic (daemon,
                                 object,
                                 caller_uid,
                                 caller_gid,
                                 caller_user_name,
                                 opt_as_user != NULL,
                                 &mount_point_to_use,
                                 &mpoint_persistent,
                                 invocation,
                                 options))
          goto out;
    }

  /* Update the mounted-fs file */
  udisks_state_add_mounted_fs (state,
                               mount_point_to_use,
                               udisks_block_get_device_number (block),
                               caller_uid,
                               system_managed,
                               system_managed ? FALSE : mpoint_persistent);

  udisks_info ("Mounted %s%s at %s on behalf of uid %u",
               device,
               system_managed ? " (system)" : "",
               mount_point_to_use,
               caller_uid);

  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  udisks_filesystem_complete_mount (filesystem, invocation, mount_point_to_use);

 out:
  if (object != NULL)
    udisks_linux_block_object_release_cleanup_lock (UDISKS_LINUX_BLOCK_OBJECT (object));
  g_mutex_unlock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);
  if (state != NULL)
    udisks_state_check (state);
  g_free (mount_point_to_use);
  g_free (fstab_mount_options);
  g_free (caller_user_name);
  g_free (device);
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static guint
get_error_code_for_umount (const gchar *error_message)
{
  if (strstr (error_message, "busy") != NULL)
    return UDISKS_ERROR_DEVICE_BUSY;
  else
    return UDISKS_ERROR_FAILED;
}

typedef struct
{
  const gchar *object_path;
  guint64      old_size;
  gchar       *mount_point;
} WaitForFilesystemMountPointsData;

static UDisksObject *
wait_for_filesystem_mount_points (UDisksDaemon *daemon,
                                  gpointer      user_data)
{
  WaitForFilesystemMountPointsData *data = user_data;
  UDisksObject *object = NULL;
  UDisksFilesystem *filesystem = NULL;
  const gchar * const *mount_points = NULL;

  object = udisks_daemon_find_object (daemon, data->object_path);

  if (object != NULL)
    {
      filesystem = udisks_object_peek_filesystem (object);
      if (filesystem != NULL)
        {
          mount_points = udisks_filesystem_get_mount_points (filesystem);
        }
      /* If we know which mount point should have gone, directly test for it, otherwise test if any has gone */
      if (mount_points != NULL && ((data->mount_point != NULL && g_strv_contains (mount_points, data->mount_point)) ||
          g_strv_length ((gchar **) mount_points) == data->old_size))
        {
          g_clear_object (&object);
        }
    }

  return object;
}

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_unmount (UDisksFilesystem      *filesystem,
                GDBusMethodInvocation *invocation,
                GVariant              *options)
{
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDaemon *daemon;
  UDisksState *state = NULL;
  gchar *mount_point = NULL;
  gchar *fstab_mount_options = NULL;
  GError *error = NULL;
  uid_t mounted_by_uid;
  uid_t caller_uid;
  gid_t caller_gid;
  const gchar *const *mount_points;
  gboolean opt_force = FALSE;
  gboolean system_managed = FALSE;
  gboolean fstab_mounted;
  gboolean success;
  UDisksBaseJob *job = NULL;
  UDisksObject *filesystem_object = NULL;
  WaitForFilesystemMountPointsData wait_data = {NULL, 0, NULL};

  /* only allow a single call at a time */
  g_mutex_lock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);

  object = udisks_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = udisks_object_peek_block (object);
  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);

  udisks_linux_block_object_lock_for_cleanup (UDISKS_LINUX_BLOCK_OBJECT (object));
  /* trigger state cleanup so that we match actual mountpoint */
  udisks_state_check_block (state, udisks_linux_block_object_get_device_number (UDISKS_LINUX_BLOCK_OBJECT (object)));

  if (options != NULL)
    {
      g_variant_lookup (options,
                        "force",
                        "b",
                        &opt_force);
    }

  mount_points = udisks_filesystem_get_mount_points (filesystem);
  if (mount_points == NULL || g_strv_length ((gchar **) mount_points) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_MOUNTED,
                                             "Device `%s' is not mounted",
                                             udisks_block_get_device (block));
      goto out;
    }

  wait_data.object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
  wait_data.old_size = g_strv_length ((gchar **) mount_points);

  if (!udisks_daemon_util_get_caller_uid_sync (daemon, invocation, NULL, &caller_uid, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }
  if (!udisks_daemon_util_get_user_info (caller_uid, &caller_gid, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* check if mount point is managed by e.g. /etc/fstab or similar */
  if (is_system_managed (daemon, block, &mount_point, &fstab_mount_options))
    {
      system_managed = TRUE;
    }

  /* if system-managed (e.g. referenced in /etc/fstab or similar) and
   * with the option x-udisks-auth or user(s), just run umount(8) as the
   * calling user
   */
  if (system_managed && (has_option (fstab_mount_options, "x-udisks-auth") ||
                         has_option (fstab_mount_options, "users") ||
                         has_option (fstab_mount_options, "user")))
    {
      gboolean unmount_fstab_as_root;

      unmount_fstab_as_root = FALSE;
    unmount_fstab_again:

      job = udisks_daemon_launch_simple_job (daemon,
                                             UDISKS_OBJECT (object),
                                             "filesystem-unmount",
                                             unmount_fstab_as_root ? 0 : caller_uid,
                                             FALSE,
                                             NULL);

      if (!unmount_fstab_as_root && caller_uid != 0)
        {
          BDExtraArg uid_arg = {g_strdup ("run_as_uid"), g_strdup_printf("%d", caller_uid)};
          BDExtraArg gid_arg = {g_strdup ("run_as_gid"), g_strdup_printf("%d", caller_gid)};
          const BDExtraArg *extra_args[3] = {&uid_arg, &gid_arg, NULL};

          success = bd_fs_unmount (mount_point, opt_force, FALSE, extra_args, &error);

          g_free (uid_arg.opt);
          g_free (uid_arg.val);
          g_free (gid_arg.opt);
          g_free (gid_arg.val);
        }
      else
          success = bd_fs_unmount (mount_point, opt_force, FALSE, NULL, &error);

      if (!success)
          udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      else
          udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

      if (!success)
        {
          if (!unmount_fstab_as_root && error->code == BD_FS_ERROR_AUTH)
            {
              g_clear_error (&error);
              if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                                object,
                                                                "org.freedesktop.udisks2.filesystem-fstab",
                                                                options,
                                                                /* Translators: Shown in authentication dialog when the
                                                                 * user requests unmounting a filesystem that is in
                                                                 * /etc/fstab file with the x-udisks-auth option.
                                                                 *
                                                                 * Do not translate $(device.name), it's a
                                                                 * placeholder and will be replaced by the name of
                                                                 * the drive/device in question
                                                                 *
                                                                 * Do not translate /etc/fstab
                                                                 */
                                                                N_("Authentication is required to unmount $(device.name) referenced in the /etc/fstab file"),
                                                                invocation))
                goto out;
              unmount_fstab_as_root = TRUE;
              goto unmount_fstab_again;
            }

          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 get_error_code_for_umount (error->message),
                                                 "Error unmounting system-managed device %s: %s",
                                                 udisks_block_get_device (block),
                                                 error->message);
          g_clear_error (&error);

          goto out;
        }
      udisks_info ("Unmounted %s (system) from %s on behalf of uid %u",
                   udisks_block_get_device (block),
                   mount_point,
                   caller_uid);
      goto waiting;
    }

  g_clear_pointer (&mount_point, g_free);
  mount_point = udisks_state_find_mounted_fs (state,
                                              udisks_block_get_device_number (block),
                                              &mounted_by_uid,
                                              &fstab_mounted);
  if (mount_point == NULL)
    {
      /* allow unmounting stuff not mentioned in mounted-fs, but treat it like root mounted it */
      mounted_by_uid = 0;
    }

  if (caller_uid != 0 && (caller_uid != mounted_by_uid))
    {
      const gchar *action_id;
      const gchar *message;

      action_id = "org.freedesktop.udisks2.filesystem-unmount-others";
      /* Translators: Shown in authentication dialog when the user
       * requests unmounting a filesystem previously mounted by
       * another user.
       *
       * Do not translate $(device.name), it's a placeholder and
       * will be replaced by the name of the drive/device in question
       */
      message = N_("Authentication is required to unmount $(device.name) mounted by another user");

      if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                        object,
                                                        action_id,
                                                        options,
                                                        message,
                                                        invocation))
        goto out;
    }

  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         "filesystem-unmount",
                                         0,
                                         FALSE,
                                         NULL);

  if (!bd_fs_unmount (mount_point ? mount_point : udisks_block_get_device (block),
                      opt_force, FALSE, NULL, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             get_error_code_for_umount (error->message),
                                             "Error unmounting %s: %s",
                                             udisks_block_get_device (block),
                                             error->message);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_clear_error (&error);
      goto out;
    }
  else
    udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

  /* filesystem unmounted, run the state/cleanup routines now to remove the mountpoint (if applicable) */
  udisks_state_check_block (state, udisks_linux_block_object_get_device_number (UDISKS_LINUX_BLOCK_OBJECT (object)));

  udisks_info ("Unmounted %s on behalf of uid %u",
               udisks_block_get_device (block),
               caller_uid);

  waiting:
  /* wait for mount-points update before returning from method */
  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  wait_data.mount_point = g_strdup (mount_point);
  filesystem_object = udisks_daemon_wait_for_object_sync (daemon,
                                                          wait_for_filesystem_mount_points,
                                                          &wait_data,
                                                          NULL,
                                                          UDISKS_DEFAULT_WAIT_TIMEOUT,
                                                          NULL);

  udisks_filesystem_complete_unmount (filesystem, invocation);

 out:
  if (object != NULL)
    udisks_linux_block_object_release_cleanup_lock (UDISKS_LINUX_BLOCK_OBJECT (object));
  if (state != NULL)
    udisks_state_check (state);
  g_free (wait_data.mount_point);
  g_free (mount_point);
  g_free (fstab_mount_options);
  g_clear_object (&object);
  g_clear_object (&filesystem_object);

  g_mutex_unlock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling method call */
static gboolean
handle_set_label (UDisksFilesystem      *filesystem,
                  GDBusMethodInvocation *invocation,
                  const gchar           *label,
                  GVariant              *options)
{
  UDisksBlock *block;
  UDisksObject *object = NULL;
  UDisksDaemon *daemon;
  UDisksState *state = NULL;
  const gchar *probed_fs_usage;
  const gchar *probed_fs_type;
  const gchar *action_id;
  const gchar *message;
  gchar *required_utility = NULL;
  uid_t caller_uid;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);
  block = udisks_object_peek_block (object);

  udisks_linux_block_object_lock_for_cleanup (UDISKS_LINUX_BLOCK_OBJECT (object));
  udisks_state_check_block (state, udisks_linux_block_object_get_device_number (UDISKS_LINUX_BLOCK_OBJECT (object)));

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  probed_fs_usage = udisks_block_get_id_usage (block);
  probed_fs_type = udisks_block_get_id_type (block);

  if (g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Cannot change label on device of type %s",
                                             probed_fs_usage);
      goto out;
    }

  if (! bd_fs_can_set_label (probed_fs_type, &required_utility, &error))
    {
      if (error != NULL)
        {
          g_dbus_method_invocation_return_error_literal (invocation,
                                                         UDISKS_ERROR,
                                                         UDISKS_ERROR_FAILED,
                                                         error->message);
          g_error_free (error);
        }
      else
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Cannot change %s filesystem label on %s: executable %s not found",
                                               probed_fs_type,
                                               udisks_block_get_device (block),
                                               required_utility);
      goto out;
    }

  if (! bd_fs_check_label (probed_fs_type, label, &error))
    {
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     UDISKS_ERROR,
                                                     UDISKS_ERROR_FAILED,
                                                     error->message);
      g_error_free (error);
      goto out;
    }

  action_id = "org.freedesktop.udisks2.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests changing the filesystem label.
   *
   * Do not translate $(device.name), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to change the filesystem label on $(device.name)");
  if (!udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (udisks_block_get_hint_system (block))
        {
          action_id = "org.freedesktop.udisks2.modify-device-system";
        }
      else if (!udisks_daemon_util_on_user_seat (daemon, UDISKS_OBJECT (object), caller_uid))
        {
          action_id = "org.freedesktop.udisks2.modify-device-other-seat";
        }
    }

  /* Check that the user is actually authorized to change the
   * filesystem label.
   */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  job = udisks_daemon_launch_simple_job (daemon,
                                         object,
                                         "filesystem-modify",
                                         caller_uid,
                                         FALSE,
                                         NULL /* cancellable */);
  if (job == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to create a job object");
      goto out;
    }

  if (! bd_fs_set_label (udisks_block_get_device (block), label, probed_fs_type, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     UDISKS_ERROR,
                                                     UDISKS_ERROR_FAILED,
                                                     error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

  /* Label property is automatically updated after an udev change
   * event for this device, but udev sometimes returns the old label
   * so just trigger the uevent again now to be sure the property
   * has been updated.
   */
  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);

  udisks_filesystem_complete_set_label (filesystem, invocation);

 out:
  if (object != NULL)
    udisks_linux_block_object_release_cleanup_lock (UDISKS_LINUX_BLOCK_OBJECT (object));
  if (state != NULL)
    udisks_state_check (state);
  g_free (required_utility);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling method call */
static gboolean
handle_resize (UDisksFilesystem      *filesystem,
               GDBusMethodInvocation *invocation,
               guint64                size,
               GVariant              *options)
{
  UDisksBlock *block = NULL;
  UDisksObject *object = NULL;
  UDisksDaemon *daemon = NULL;
  UDisksState *state = NULL;
  const gchar *probed_fs_usage = NULL;
  const gchar *probed_fs_type = NULL;
  BDFSResizeFlags mode = 0;
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  uid_t caller_uid;
  GError *error = NULL;
  UDisksBaseJob *job = NULL;
  gchar *required_utility = NULL;
  const gchar * const *existing_mount_points = NULL;

  g_mutex_lock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);

  object = udisks_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);
  block = udisks_object_peek_block (object);

  udisks_linux_block_object_lock_for_cleanup (UDISKS_LINUX_BLOCK_OBJECT (object));
  udisks_state_check_block (state, udisks_linux_block_object_get_device_number (UDISKS_LINUX_BLOCK_OBJECT (object)));

  if (! udisks_daemon_util_get_caller_uid_sync (daemon,
                                                invocation,
                                                NULL /* GCancellable */,
                                                &caller_uid,
                                                &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  probed_fs_usage = udisks_block_get_id_usage (block);
  if (g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Cannot resize %s filesystem on %s",
                                             probed_fs_usage,
                                             udisks_block_get_device (block));
      goto out;
    }

  probed_fs_type = udisks_block_get_id_type (block);
  if (! bd_fs_can_resize (probed_fs_type, &mode, &required_utility, &error))
    {
      if (error != NULL)
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Cannot resize %s filesystem on %s: %s",
                                               probed_fs_type,
                                               udisks_block_get_device (block),
                                               error->message);
      else
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Cannot resize %s filesystem on %s: executable %s not found",
                                               probed_fs_type,
                                               udisks_block_get_device (block),
                                               required_utility);
      goto out;
    }

  /* it can't be known if it's shrinking or growing but check at least the mount state */
  existing_mount_points = udisks_filesystem_get_mount_points (filesystem);
  if (existing_mount_points != NULL && g_strv_length ((gchar **) existing_mount_points) > 0)
    {
      if (! (mode & BD_FS_ONLINE_SHRINK) && ! (mode & BD_FS_ONLINE_GROW))
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_NOT_SUPPORTED,
                                               "Cannot resize %s filesystem on %s if mounted",
                                               probed_fs_usage,
                                               udisks_block_get_device (block));
    }
  else if (! (mode & BD_FS_OFFLINE_SHRINK) && ! (mode & BD_FS_OFFLINE_GROW))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Cannot resize %s filesystem on %s if unmounted",
                                             probed_fs_usage,
                                             udisks_block_get_device (block));
    }

  action_id = "org.freedesktop.udisks2.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests resizing the filesystem.
   *
   * Do not translate $(device.name), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to resize the filesystem on $(device.name)");
  if (! udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (udisks_block_get_hint_system (block))
        {
          action_id = "org.freedesktop.udisks2.modify-device-system";
        }
      else if (! udisks_daemon_util_on_user_seat (daemon, UDISKS_OBJECT (object), caller_uid))
        {
          action_id = "org.freedesktop.udisks2.modify-device-other-seat";
        }
    }

  /* Check that the user is actually authorized to resize the filesystem. */
  if (! udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         "filesystem-resize",
                                         caller_uid,
                                         FALSE,
                                         NULL);
  if (job == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to create a job object");
      goto out;
    }

  udisks_bd_thread_set_progress_for_job (UDISKS_JOB (job));
  if (! bd_fs_resize (udisks_block_get_device (block), size, probed_fs_type, &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error resizing filesystem on %s: %s",
                                             udisks_block_get_device (block),
                                             error->message);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      goto out;
    }

  /* At least resize2fs might need another uevent after it is done.
   */
  UDISKS_LINUX_FILESYSTEM (filesystem)->cached_fs_size = 0;
  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (filesystem));
  udisks_filesystem_complete_resize (filesystem, invocation);
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

 out:
  udisks_bd_thread_disable_progress ();
  if (object != NULL)
    udisks_linux_block_object_release_cleanup_lock (UDISKS_LINUX_BLOCK_OBJECT (object));
  if (state != NULL)
    udisks_state_check (state);
  g_clear_object (&object);
  g_free (required_utility);
  g_clear_error (&error);
  g_mutex_unlock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling method call */
static gboolean
handle_repair (UDisksFilesystem      *filesystem,
               GDBusMethodInvocation *invocation,
               GVariant              *options)
{
  UDisksBlock *block = NULL;
  UDisksObject *object = NULL;
  UDisksDaemon *daemon = NULL;
  UDisksState *state = NULL;
  const gchar *probed_fs_usage = NULL;
  const gchar *probed_fs_type = NULL;
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  uid_t caller_uid;
  GError *error = NULL;
  gboolean ret = FALSE;
  UDisksBaseJob *job = NULL;
  gchar *required_utility = NULL;
  const gchar * const *existing_mount_points = NULL;

  g_mutex_lock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);

  object = udisks_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);
  block = udisks_object_peek_block (object);

  udisks_linux_block_object_lock_for_cleanup (UDISKS_LINUX_BLOCK_OBJECT (object));
  udisks_state_check_block (state, udisks_linux_block_object_get_device_number (UDISKS_LINUX_BLOCK_OBJECT (object)));

  if (! udisks_daemon_util_get_caller_uid_sync (daemon,
                                                invocation,
                                                NULL /* GCancellable */,
                                                &caller_uid,
                                                &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  probed_fs_usage = udisks_block_get_id_usage (block);
  if (g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Cannot repair %s filesystem on %s",
                                             probed_fs_usage,
                                             udisks_block_get_device (block));
      goto out;
    }

  probed_fs_type = udisks_block_get_id_type (block);
  if (! bd_fs_can_repair (probed_fs_type, &required_utility, &error))
    {
      if (error != NULL)
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Cannot repair %s filesystem on %s: %s",
                                               probed_fs_type,
                                               udisks_block_get_device (block),
                                               error->message);
      else
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Cannot repair %s filesystem on %s: executable %s not found",
                                               probed_fs_type,
                                               udisks_block_get_device (block),
                                               required_utility);
      goto out;
    }

  /* check the mount state */
  existing_mount_points = udisks_filesystem_get_mount_points (filesystem);
  if (existing_mount_points != NULL && g_strv_length ((gchar **) existing_mount_points) > 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Cannot repair %s filesystem on %s if mounted",
                                             probed_fs_usage,
                                             udisks_block_get_device (block));
    }

  action_id = "org.freedesktop.udisks2.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests resizing the filesystem.
   *
   * Do not translate $(device.name), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to repair the filesystem on $(device.name)");
  if (! udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (udisks_block_get_hint_system (block))
        {
          action_id = "org.freedesktop.udisks2.modify-device-system";
        }
      else if (! udisks_daemon_util_on_user_seat (daemon, UDISKS_OBJECT (object), caller_uid))
        {
          action_id = "org.freedesktop.udisks2.modify-device-other-seat";
        }
    }

  /* Check that the user is actually authorized to repair the filesystem. */
  if (! udisks_daemon_util_check_authorization_sync (daemon,
                                                     object,
                                                     action_id,
                                                     options,
                                                     message,
                                                     invocation))
    goto out;

  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         "filesystem-repair",
                                         caller_uid,
                                         FALSE,
                                         NULL);
  if (job == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to create a job object");
      goto out;
    }

  udisks_bd_thread_set_progress_for_job (UDISKS_JOB (job));
  ret = bd_fs_repair (udisks_block_get_device (block), probed_fs_type, &error);
  if (error)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error reparing filesystem on %s: %s",
                                             udisks_block_get_device (block),
                                             error->message);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      goto out;
    }

  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  udisks_filesystem_complete_repair (filesystem, invocation, ret);
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

 out:
  udisks_bd_thread_disable_progress ();
  if (object != NULL)
    udisks_linux_block_object_release_cleanup_lock (UDISKS_LINUX_BLOCK_OBJECT (object));
  if (state != NULL)
    udisks_state_check (state);
  g_clear_object (&object);
  g_free (required_utility);
  g_clear_error (&error);
  g_mutex_unlock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling method call */
static gboolean
handle_check (UDisksFilesystem      *filesystem,
              GDBusMethodInvocation *invocation,
              GVariant              *options)
{
  UDisksBlock *block = NULL;
  UDisksObject *object = NULL;
  UDisksDaemon *daemon = NULL;
  UDisksState *state = NULL;
  const gchar *probed_fs_usage = NULL;
  const gchar *probed_fs_type = NULL;
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  uid_t caller_uid;
  GError *error = NULL;
  gboolean ret = FALSE;
  UDisksBaseJob *job = NULL;
  gchar *required_utility = NULL;
  const gchar * const *existing_mount_points = NULL;

  g_mutex_lock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);

  object = udisks_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);
  block = udisks_object_peek_block (object);

  udisks_linux_block_object_lock_for_cleanup (UDISKS_LINUX_BLOCK_OBJECT (object));
  udisks_state_check_block (state, udisks_linux_block_object_get_device_number (UDISKS_LINUX_BLOCK_OBJECT (object)));

  if (! udisks_daemon_util_get_caller_uid_sync (daemon,
                                                invocation,
                                                NULL /* GCancellable */,
                                                &caller_uid,
                                                &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  probed_fs_usage = udisks_block_get_id_usage (block);
  if (g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Cannot check %s filesystem on %s",
                                             probed_fs_usage,
                                             udisks_block_get_device (block));
      goto out;
    }

  probed_fs_type = udisks_block_get_id_type (block);
  if (! bd_fs_can_check (probed_fs_type, &required_utility, &error))
    {
      if (error != NULL)
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Cannot check %s filesystem on %s: %s",
                                               probed_fs_type,
                                               udisks_block_get_device (block),
                                               error->message);
      else
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Cannot check %s filesystem on %s: executable %s not found",
                                               probed_fs_type,
                                               udisks_block_get_device (block),
                                               required_utility);
      goto out;
    }

  /* check the mount state */
  existing_mount_points = udisks_filesystem_get_mount_points (filesystem);
  if (existing_mount_points != NULL && g_strv_length ((gchar **) existing_mount_points) > 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Cannot check %s filesystem on %s if mounted",
                                             probed_fs_usage,
                                             udisks_block_get_device (block));
    }

  action_id = "org.freedesktop.udisks2.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests resizing the filesystem.
   *
   * Do not translate $(device.name), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to check the filesystem on $(device.name)");
  if (! udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (udisks_block_get_hint_system (block))
        {
          action_id = "org.freedesktop.udisks2.modify-device-system";
        }
      else if (! udisks_daemon_util_on_user_seat (daemon, UDISKS_OBJECT (object), caller_uid))
        {
          action_id = "org.freedesktop.udisks2.modify-device-other-seat";
        }
    }

  /* Check that the user is actually authorized to check the filesystem. */
  if (! udisks_daemon_util_check_authorization_sync (daemon,
                                                     object,
                                                     action_id,
                                                     options,
                                                     message,
                                                     invocation))
    goto out;

  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         "filesystem-check",
                                         caller_uid,
                                         FALSE,
                                         NULL);
  if (job == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to create a job object");
      goto out;
    }

  udisks_bd_thread_set_progress_for_job (UDISKS_JOB (job));
  ret = bd_fs_check (udisks_block_get_device (block), probed_fs_type, &error);
  if (error)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error checking filesystem on %s: %s",
                                             udisks_block_get_device (block),
                                             error->message);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      goto out;
    }

  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  udisks_filesystem_complete_check (filesystem, invocation, ret);
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

 out:
  udisks_bd_thread_disable_progress ();
  if (object != NULL)
    udisks_linux_block_object_release_cleanup_lock (UDISKS_LINUX_BLOCK_OBJECT (object));
  if (state != NULL)
    udisks_state_check (state);
  g_clear_object (&object);
  g_free (required_utility);
  g_clear_error (&error);
  g_mutex_unlock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static gboolean
handle_take_ownership (UDisksFilesystem      *filesystem,
                       GDBusMethodInvocation *invocation,
                       GVariant              *options)
{
  UDisksBlock *block = NULL;
  UDisksObject *object = NULL;
  UDisksDaemon *daemon = NULL;
  UDisksState *state = NULL;
  const gchar *probed_fs_usage = NULL;
  const gchar *probed_fs_type = NULL;
  const gchar *action_id = NULL;
  const gchar *message = NULL;
  const BDFSFeatures *fs_features;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;
  gboolean recursive = FALSE;
  uid_t caller_uid;
  gid_t caller_gid;

  g_variant_lookup (options, "recursive", "b", &recursive);

  /* only allow a single call at a time */
  g_mutex_lock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);

  object = udisks_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  block = udisks_object_peek_block (object);
  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);

  udisks_linux_block_object_lock_for_cleanup (UDISKS_LINUX_BLOCK_OBJECT (object));
  udisks_state_check_block (state, udisks_linux_block_object_get_device_number (UDISKS_LINUX_BLOCK_OBJECT (object)));

  if (! udisks_daemon_util_get_caller_uid_sync (daemon,
                                                invocation,
                                                NULL /* GCancellable */,
                                                &caller_uid,
                                                &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_get_user_info (caller_uid, &caller_gid, NULL /* user name */, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  probed_fs_usage = udisks_block_get_id_usage (block);
  if (g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Cannot take ownership of %s filesystem on %s",
                                             probed_fs_usage,
                                             udisks_block_get_device (block));
      goto out;
    }

  probed_fs_type = udisks_block_get_id_type (block);
  fs_features = bd_fs_features (probed_fs_type, &error);
  if (fs_features == NULL)
    {
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     UDISKS_ERROR,
                                                     UDISKS_ERROR_NOT_SUPPORTED,
                                                     error->message);
      goto out;
    }
  if ((fs_features->features & BD_FS_FEATURE_OWNERS) != BD_FS_FEATURE_OWNERS)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Filesystem %s doesn't support ownership",
                                             probed_fs_type);
      goto out;
    }

  action_id = "org.freedesktop.udisks2.filesystem-take-ownership";
  /* Translators: Shown in authentication dialog when the user
   * requests taking ownership of the filesystem.
   *
   * Do not translate $(device.name), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to change ownership of the filesystem on $(device.name)");

  /* Check that the user is actually authorized to check the filesystem. */
  if (! udisks_daemon_util_check_authorization_sync (daemon,
                                                     object,
                                                     action_id,
                                                     options,
                                                     message,
                                                     invocation))
    goto out;

  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         "filesystem-modify",
                                         caller_uid,
                                         FALSE,
                                         NULL);
  if (job == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to create a job object");
      goto out;
    }

  if (! take_filesystem_ownership (udisks_block_get_device (block),
                                   probed_fs_type,
                                   caller_uid, caller_gid,
                                   recursive,
                                   &error))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error taking ownership of filesystem on %s: %s",
                                             udisks_block_get_device (block),
                                             error->message);
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      goto out;
    }


  udisks_filesystem_complete_take_ownership (filesystem, invocation);
  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);

  out:
   if (object != NULL)
     udisks_linux_block_object_release_cleanup_lock (UDISKS_LINUX_BLOCK_OBJECT (object));
   if (state != NULL)
     udisks_state_check (state);
   g_clear_object (&object);
   g_clear_error (&error);
   g_mutex_unlock (&UDISKS_LINUX_FILESYSTEM (filesystem)->lock);
   return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

/* remove dashes for certain filesystem types */
static gchar *
reformat_uuid_string (const gchar *arg_uuid, const gchar *fs_type)
{
  if (arg_uuid == NULL)
    return NULL;
  if (g_strcmp0 (fs_type, "vfat") == 0 ||
      g_strcmp0 (fs_type, "exfat") == 0 ||
      g_strcmp0 (fs_type, "ntfs") == 0 ||
      g_strcmp0 (fs_type, "udf") == 0)
      return udisks_daemon_util_subst_str (arg_uuid, "-", NULL);
  return g_strdup (arg_uuid);
}

static gboolean
handle_set_uuid (UDisksFilesystem      *filesystem,
                 GDBusMethodInvocation *invocation,
                 const gchar           *arg_uuid,
                 GVariant              *options)
{
  UDisksBlock *block;
  UDisksObject *object = NULL;
  UDisksDaemon *daemon;
  UDisksState *state = NULL;
  const gchar *probed_fs_usage;
  const gchar *probed_fs_type;
  const gchar *action_id;
  const gchar *message;
  gchar *required_utility = NULL;
  gchar *uuid = NULL;
  uid_t caller_uid;
  UDisksBaseJob *job = NULL;
  GError *error = NULL;

  object = udisks_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_block_object_get_daemon (UDISKS_LINUX_BLOCK_OBJECT (object));
  state = udisks_daemon_get_state (daemon);
  block = udisks_object_peek_block (object);

  udisks_linux_block_object_lock_for_cleanup (UDISKS_LINUX_BLOCK_OBJECT (object));
  udisks_state_check_block (state, udisks_linux_block_object_get_device_number (UDISKS_LINUX_BLOCK_OBJECT (object)));

  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_clear_error (&error);
      goto out;
    }

  probed_fs_usage = udisks_block_get_id_usage (block);
  probed_fs_type = udisks_block_get_id_type (block);

  if (g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_NOT_SUPPORTED,
                                             "Cannot change UUID on device of type %s",
                                             probed_fs_usage);
      goto out;
    }

  if (! bd_fs_can_set_uuid (probed_fs_type, &required_utility, &error))
    {
      if (error != NULL)
        {
          g_dbus_method_invocation_return_error_literal (invocation,
                                                         UDISKS_ERROR,
                                                         UDISKS_ERROR_FAILED,
                                                         error->message);
          g_error_free (error);
        }
      else
        g_dbus_method_invocation_return_error (invocation,
                                               UDISKS_ERROR,
                                               UDISKS_ERROR_FAILED,
                                               "Cannot change %s filesystem UUID on %s: executable %s not found",
                                               probed_fs_type,
                                               udisks_block_get_device (block),
                                               required_utility);
      goto out;
    }

  uuid = reformat_uuid_string (arg_uuid, probed_fs_type);
  if (! bd_fs_check_uuid (probed_fs_type, uuid, &error))
    {
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     UDISKS_ERROR,
                                                     UDISKS_ERROR_FAILED,
                                                     error->message);
      g_error_free (error);
      goto out;
    }

  action_id = "org.freedesktop.udisks2.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests changing the filesystem UUID.
   *
   * Do not translate $(device.name), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to change the filesystem UUID on $(device.name)");
  if (!udisks_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (udisks_block_get_hint_system (block))
        {
          action_id = "org.freedesktop.udisks2.modify-device-system";
        }
      else if (!udisks_daemon_util_on_user_seat (daemon, UDISKS_OBJECT (object), caller_uid))
        {
          action_id = "org.freedesktop.udisks2.modify-device-other-seat";
        }
    }

  /* Check that the user is actually authorized to change the
   * filesystem UUID.
   */
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    object,
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  job = udisks_daemon_launch_simple_job (daemon,
                                         object,
                                         "filesystem-modify",
                                         caller_uid,
                                         FALSE,
                                         NULL /* cancellable */);
  if (job == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Failed to create a job object");
      goto out;
    }

  if (! bd_fs_set_uuid (udisks_block_get_device (block), uuid, probed_fs_type, &error))
    {
      udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), FALSE, error->message);
      g_dbus_method_invocation_return_error_literal (invocation,
                                                     UDISKS_ERROR,
                                                     UDISKS_ERROR_FAILED,
                                                     error->message);
      g_clear_error (&error);
      goto out;
    }

  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, NULL);
  udisks_linux_block_object_trigger_uevent_sync (UDISKS_LINUX_BLOCK_OBJECT (object),
                                                 UDISKS_DEFAULT_WAIT_TIMEOUT);
  udisks_filesystem_complete_set_uuid (filesystem, invocation);

 out:
  if (object != NULL)
    udisks_linux_block_object_release_cleanup_lock (UDISKS_LINUX_BLOCK_OBJECT (object));
  if (state != NULL)
    udisks_state_check (state);
  g_free (required_utility);
  g_free (uuid);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
filesystem_iface_init (UDisksFilesystemIface *iface)
{
  iface->handle_mount     = handle_mount;
  iface->handle_unmount   = handle_unmount;
  iface->handle_set_label = handle_set_label;
  iface->handle_set_uuid  = handle_set_uuid;
  iface->handle_resize    = handle_resize;
  iface->handle_repair    = handle_repair;
  iface->handle_check     = handle_check;
  iface->handle_take_ownership = handle_take_ownership;
}
