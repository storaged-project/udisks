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
#include <sys/types.h>
#include <sys/acl.h>
#include <errno.h>

#include <glib/gstdio.h>

#include "storagedlogging.h"
#include "storagedlinuxfilesystem.h"
#include "storagedlinuxblockobject.h"
#include "storagedlinuxfsinfo.h"
#include "storageddaemon.h"
#include "storagedstate.h"
#include "storageddaemonutil.h"
#include "storagedmountmonitor.h"
#include "storagedmount.h"
#include "storagedlinuxdevice.h"

/**
 * SECTION:storagedlinuxfilesystem
 * @title: StoragedLinuxFilesystem
 * @short_description: Linux implementation of #StoragedFilesystem
 *
 * This type provides an implementation of the #StoragedFilesystem
 * interface on Linux.
 */

typedef struct _StoragedLinuxFilesystemClass   StoragedLinuxFilesystemClass;

/**
 * StoragedLinuxFilesystem:
 *
 * The #StoragedLinuxFilesystem structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _StoragedLinuxFilesystem
{
  StoragedFilesystemSkeleton parent_instance;
  GMutex lock;
};

struct _StoragedLinuxFilesystemClass
{
  StoragedFilesystemSkeletonClass parent_class;
};

static void filesystem_iface_init (StoragedFilesystemIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxFilesystem, storaged_linux_filesystem, STORAGED_TYPE_FILESYSTEM_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_FILESYSTEM, filesystem_iface_init));

#ifdef HAVE_FHS_MEDIA
#  define MOUNT_BASE "/media"
#else
#  define MOUNT_BASE "/run/media"
#endif

/* ---------------------------------------------------------------------------------------------------- */

static void
storaged_linux_filesystem_finalize (GObject *object)
{
  StoragedLinuxFilesystem *filesystem = STORAGED_LINUX_FILESYSTEM (object);

  g_mutex_clear (&(filesystem->lock));

  if (G_OBJECT_CLASS (storaged_linux_filesystem_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_filesystem_parent_class)->finalize (object);
}

static void
storaged_linux_filesystem_init (StoragedLinuxFilesystem *filesystem)
{
  g_mutex_init (&filesystem->lock);
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (filesystem),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
storaged_linux_filesystem_class_init (StoragedLinuxFilesystemClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_linux_filesystem_finalize;
}

/**
 * storaged_linux_filesystem_new:
 *
 * Creates a new #StoragedLinuxFilesystem instance.
 *
 * Returns: A new #StoragedLinuxFilesystem. Free with g_object_unref().
 */
StoragedFilesystem *
storaged_linux_filesystem_new (void)
{
  return STORAGED_FILESYSTEM (g_object_new (STORAGED_TYPE_LINUX_FILESYSTEM,
                              NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_filesystem_update:
 * @filesystem: A #StoragedLinuxFilesystem.
 * @object: The enclosing #StoragedLinuxBlockObject instance.
 *
 * Updates the interface.
 */
void
storaged_linux_filesystem_update (StoragedLinuxFilesystem  *filesystem,
                                  StoragedLinuxBlockObject *object)
{
  StoragedMountMonitor *mount_monitor;
  StoragedLinuxDevice *device;
  GPtrArray *p;
  GList *mounts;
  GList *l;

  mount_monitor = storaged_daemon_get_mount_monitor (storaged_linux_block_object_get_daemon (object));
  device = storaged_linux_block_object_get_device (object);

  p = g_ptr_array_new ();
  mounts = storaged_mount_monitor_get_mounts_for_dev (mount_monitor, g_udev_device_get_device_number (device->udev_device));
  /* we are guaranteed that the list is sorted so if there are
   * multiple mounts we'll always get the same order
   */
  for (l = mounts; l != NULL; l = l->next)
    {
      StoragedMount *mount = STORAGED_MOUNT (l->data);
      if (storaged_mount_get_mount_type (mount) == STORAGED_MOUNT_TYPE_FILESYSTEM)
        g_ptr_array_add (p, (gpointer) storaged_mount_get_mount_path (mount));
    }
  g_ptr_array_add (p, NULL);
  storaged_filesystem_set_mount_points (STORAGED_FILESYSTEM (filesystem),
                                      (const gchar *const *) p->pdata);
  g_ptr_array_free (p, TRUE);
  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);
  g_object_unref (device);
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *well_known_filesystems[] =
{
  "btrfs",
  "ext2",
  "ext3",
  "ext4",
  "udf",
  "iso9660",
  "xfs",
  "jfs",
  "nilfs",
  "reiserfs",
  "reiser4",
  "msdos",
  "umsdos",
  "vfat",
  "exfat",
  "ntfs",
  NULL,
};

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
      storaged_warning ("Error reading %s: %s (%s %d)",
                        filesystems_file,
                        error->message,
                        g_quark_to_string (error->domain),
                        error->code);
      g_error_free (error);
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
  gboolean ret = FALSE;
  guint n;

  for (n = 0; well_known_filesystems[n] != NULL; n++)
    {
      if (g_strcmp0 (well_known_filesystems[n], fstype) == 0)
        {
          ret = TRUE;
          goto out;
        }
    }
 out:
  return ret;
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

typedef struct
{
  const gchar *fstype;
  const gchar * const *defaults;
  const gchar * const *allow;
  const gchar * const *allow_uid_self;
  const gchar * const *allow_gid_self;
} FSMountOptions;

/* ---------------------- vfat -------------------- */

static const gchar *vfat_defaults[] = { "uid=", "gid=", "shortname=mixed", "dmask=0077", "utf8=1", "showexec", "flush", NULL };
static const gchar *vfat_allow[] = { "flush", "utf8=", "shortname=", "umask=", "dmask=", "fmask=", "codepage=", "iocharset=", "usefree", "showexec", NULL };
static const gchar *vfat_allow_uid_self[] = { "uid=", NULL };
static const gchar *vfat_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- ntfs -------------------- */
/* this is assuming that ntfs-3g is used */

static const gchar *ntfs_defaults[] = { "uid=", "gid=", "dmask=0077", "fmask=0177", NULL };
static const gchar *ntfs_allow[] = { "umask=", "dmask=", "fmask=", "locale=", "norecover", "ignore_case", "windows_names", "compression", "nocompression", NULL };
static const gchar *ntfs_allow_uid_self[] = { "uid=", NULL };
static const gchar *ntfs_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- iso9660 -------------------- */

static const gchar *iso9660_defaults[] = { "uid=", "gid=", "iocharset=utf8", "mode=0400", "dmode=0500", NULL };
static const gchar *iso9660_allow[] = { "norock", "nojoliet", "iocharset=", "mode=", "dmode=", NULL };
static const gchar *iso9660_allow_uid_self[] = { "uid=", NULL };
static const gchar *iso9660_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- udf -------------------- */

static const gchar *udf_defaults[] = { "uid=", "gid=", "iocharset=utf8", "umask=0077", NULL };
static const gchar *udf_allow[] = { "iocharset=", "umask=", NULL };
static const gchar *udf_allow_uid_self[] = { "uid=", NULL };
static const gchar *udf_allow_gid_self[] = { "gid=", NULL };

/* ---------------------- exfat -------------------- */

static const gchar *exfat_defaults[] = { "uid=", "gid=", "iocharset=utf8", "namecase=0", "errors=remount-ro", "umask=0077", NULL };
static const gchar *exfat_allow[] = { "dmask=", "errors=", "fmask=", "iocharset=", "namecase=", "umask=", NULL };
static const gchar *exfat_allow_uid_self[] = { "uid=", NULL };
static const gchar *exfat_allow_gid_self[] = { "gid=", NULL };

/* ------------------------------------------------ */
/* TODO: support context= */

static const gchar *any_allow[] = { "exec", "noexec", "nodev", "nosuid", "atime", "noatime", "nodiratime", "ro", "rw", "sync", "dirsync", NULL };

static const FSMountOptions fs_mount_options[] =
  {
    { "vfat", vfat_defaults, vfat_allow, vfat_allow_uid_self, vfat_allow_gid_self },
    { "ntfs", ntfs_defaults, ntfs_allow, ntfs_allow_uid_self, ntfs_allow_gid_self },
    { "iso9660", iso9660_defaults, iso9660_allow, iso9660_allow_uid_self, iso9660_allow_gid_self },
    { "udf", udf_defaults, udf_allow, udf_allow_uid_self, udf_allow_gid_self },
    { "exfat", exfat_defaults, exfat_allow, exfat_allow_uid_self, exfat_allow_gid_self },
  };

/* ------------------------------------------------ */

static int num_fs_mount_options = sizeof(fs_mount_options) / sizeof(FSMountOptions);

static const FSMountOptions *
find_mount_options_for_fs (const gchar *fstype)
{
  int n;
  const FSMountOptions *fsmo;

  for (n = 0; n < num_fs_mount_options; n++)
    {
      fsmo = fs_mount_options + n;
      if (g_strcmp0 (fsmo->fstype, fstype) == 0)
        goto out;
    }

  fsmo = NULL;
 out:
  return fsmo;
}

static gid_t
find_primary_gid (uid_t uid)
{
  struct passwd *pw = NULL;
  struct passwd pwstruct;
  gchar pwbuf[8192];
  int rc;
  gid_t gid;

  gid = (gid_t) - 1;

  rc = getpwuid_r (uid, &pwstruct, pwbuf, sizeof pwbuf, &pw);
  if (rc != 0 || pw == NULL)
    {
      storaged_warning ("Error looking up uid %u: %m", uid);
      goto out;
    }
  gid = pw->pw_gid;

 out:
  return gid;
}

static gboolean
is_uid_in_gid (uid_t uid,
               gid_t gid)
{
  gboolean ret;
  struct passwd *pw = NULL;
  struct passwd pwstruct;
  gchar pwbuf[8192];
  int rc;
  static gid_t supplementary_groups[128];
  int num_supplementary_groups = 128;
  int n;

  /* TODO: use some #define instead of harcoding some random number like 128 */

  ret = FALSE;

  rc = getpwuid_r (uid, &pwstruct, pwbuf, sizeof pwbuf, &pw);
  if (rc != 0 || pw == NULL)
    {
      storaged_warning ("Error looking up uid %u: %m", uid);
      goto out;
    }
  if (pw->pw_gid == gid)
    {
      ret = TRUE;
      goto out;
    }

  if (getgrouplist (pw->pw_name, pw->pw_gid, supplementary_groups, &num_supplementary_groups) < 0)
    {
      storaged_warning ("Error getting supplementary groups for uid %u: %m", uid);
      goto out;
    }

  for (n = 0; n < num_supplementary_groups; n++)
    {
      if (supplementary_groups[n] == gid)
        {
          ret = TRUE;
          goto out;
        }
    }

 out:
  return ret;
}

static gboolean
is_mount_option_allowed (const FSMountOptions *fsmo,
                         const gchar          *option,
                         uid_t                 caller_uid)
{
  int n;
  gchar *endp;
  uid_t uid;
  gid_t gid;
  gboolean allowed;
  const gchar *ep;
  gsize ep_len;

  allowed = FALSE;

  /* first run through the allowed mount options */
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->allow != NULL && fsmo->allow[n] != NULL; n++)
        {
          ep = strstr (fsmo->allow[n], "=");
          if (ep != NULL && ep[1] == '\0')
            {
              ep_len = ep - fsmo->allow[n] + 1;
              if (strncmp (fsmo->allow[n], option, ep_len) == 0)
                {
                  allowed = TRUE;
                  goto out;
                }
            }
          else
            {
              if (strcmp (fsmo->allow[n], option) == 0)
                {
                  allowed = TRUE;
                  goto out;
                }
            }
        }
    }
  for (n = 0; any_allow[n] != NULL; n++)
    {
      ep = strstr (any_allow[n], "=");
      if (ep != NULL && ep[1] == '\0')
        {
          ep_len = ep - any_allow[n] + 1;
          if (strncmp (any_allow[n], option, ep_len) == 0)
            {
              allowed = TRUE;
              goto out;
            }
        }
      else
        {
          if (strcmp (any_allow[n], option) == 0)
            {
              allowed = TRUE;
              goto out;
            }
        }
    }

  /* .. then check for mount options where the caller is allowed to pass
   * in his own uid
   */
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->allow_uid_self != NULL && fsmo->allow_uid_self[n] != NULL; n++)
        {
          const gchar *r_mount_option = fsmo->allow_uid_self[n];
          if (g_str_has_prefix (option, r_mount_option))
            {
              uid = strtol (option + strlen (r_mount_option), &endp, 10);
              if (*endp != '\0')
                continue;
              if (uid == caller_uid)
                {
                  allowed = TRUE;
                  goto out;
                }
            }
        }
    }

  /* .. ditto for gid
   */
  if (fsmo != NULL)
    {
      for (n = 0; fsmo->allow_gid_self != NULL && fsmo->allow_gid_self[n] != NULL; n++)
        {
          const gchar *r_mount_option = fsmo->allow_gid_self[n];
          if (g_str_has_prefix (option, r_mount_option))
            {
              gid = strtol (option + strlen (r_mount_option), &endp, 10);
              if (*endp != '\0')
                continue;
              if (is_uid_in_gid (caller_uid, gid))
                {
                  allowed = TRUE;
                  goto out;
                }
            }
        }
    }

 out:
  return allowed;
}

static gchar **
prepend_default_mount_options (const FSMountOptions *fsmo,
                               uid_t                 caller_uid,
                               GVariant             *given_options)
{
  GPtrArray *options;
  gint n;
  gchar *s;
  gid_t gid;
  const gchar *option_string;

  options = g_ptr_array_new ();
  if (fsmo != NULL)
    {
      const gchar *const *defaults = fsmo->defaults;

      for (n = 0; defaults != NULL && defaults[n] != NULL; n++)
        {
          const gchar *option = defaults[n];

          if (strcmp (option, "uid=") == 0)
            {
              s = g_strdup_printf ("uid=%u", caller_uid);
              g_ptr_array_add (options, s);
            }
          else if (strcmp (option, "gid=") == 0)
            {
              gid = find_primary_gid (caller_uid);
              if (gid != (gid_t) - 1)
                {
                  s = g_strdup_printf ("gid=%u", gid);
                  g_ptr_array_add (options, s);
                }
            }
          else
            {
              g_ptr_array_add (options, g_strdup (option));
            }
        }
    }

  if (g_variant_lookup (given_options,
                        "options",
                        "&s", &option_string))
    {
      gchar **split_option_string;
      split_option_string = g_strsplit (option_string, ",", -1);
      for (n = 0; split_option_string[n] != NULL; n++)
        g_ptr_array_add (options, split_option_string[n]); /* steals string */
      g_free (split_option_string);
    }
  g_ptr_array_add (options, NULL);

  return (char **) g_ptr_array_free (options, FALSE);
}

static gchar *
subst_str (const gchar *str,
           const gchar *from,
           const gchar *to)
{
    gchar **parts;
    gchar *result;

    parts = g_strsplit (str, from, 0);
    result = g_strjoinv (to, parts);
    g_strfreev (parts);
    return result;
}

static gchar *
subst_str_and_escape (const gchar *str,
                      const gchar *from,
                      const gchar *to)
{
  gchar *quoted_and_escaped;
  gchar *ret;
  quoted_and_escaped = storaged_daemon_util_escape_and_quote (to);
  ret = subst_str (str, from, quoted_and_escaped);
  g_free (quoted_and_escaped);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/*
 * calculate_fs_type: <internal>
 * @block: A #StoragedBlock.
 * @given_options: The a{sv} #GVariant.
 * @error: Return location for error or %NULL.
 *
 * Calculates the file system type to use.
 *
 * Returns: A valid UTF-8 string with the filesystem type (may be "auto") or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_fs_type (StoragedBlock               *block,
                   GVariant                    *given_options,
                   GError                     **error)
{
  gchar *fs_type_to_use = NULL;
  const gchar *probed_fs_type = NULL;
  const gchar *requested_fs_type;

  probed_fs_type = NULL;
  if (block != NULL)
    probed_fs_type = storaged_block_get_id_type (block);

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
                           STORAGED_ERROR,
                           STORAGED_ERROR_OPTION_NOT_PERMITTED,
                           "Requested filesystem type `%s' is neither well-known nor "
                           "in /proc/filesystems nor in /etc/filesystems",
                           requested_fs_type);
              goto out;
            }
        }

      /* TODO: maybe check that it's compatible with probed_fs_type */
      fs_type_to_use = g_strdup (requested_fs_type);
    }
  else
    {
      if (probed_fs_type != NULL && strlen (probed_fs_type) > 0)
        fs_type_to_use = g_strdup (probed_fs_type);
      else
        fs_type_to_use = g_strdup ("auto");
    }

 out:
  g_assert (fs_type_to_use == NULL || g_utf8_validate (fs_type_to_use, -1, NULL));

  return fs_type_to_use;
}

/*
 * calculate_mount_options: <internal>
 * @daemon: A #StoragedDaemon.
 * @block: A #StoragedBlock.
 * @caller_uid: The uid of the caller making the request.
 * @fs_type: The filesystem type to use or %NULL.
 * @options: Options requested by the caller.
 * @error: Return location for error or %NULL.
 *
 * Calculates the mount option string to use. Ensures (by returning an
 * error) that only safe options are used.
 *
 * Returns: A string with mount options or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_mount_options (StoragedDaemon              *daemon,
                         StoragedBlock               *block,
                         uid_t                        caller_uid,
                         const gchar                 *fs_type,
                         GVariant                    *options,
                         GError                     **error)
{
  const FSMountOptions *fsmo;
  gchar **options_to_use;
  gchar *options_to_use_str;
  GString *str;
  guint n;

  options_to_use = NULL;
  options_to_use_str = NULL;

  fsmo = find_mount_options_for_fs (fs_type);

  /* always prepend some reasonable default mount options; these are
   * chosen here; the user can override them if he wants to
   */
  options_to_use = prepend_default_mount_options (fsmo, caller_uid, options);

  /* validate mount options */
  str = g_string_new ("uhelper=storaged,nodev,nosuid");
  for (n = 0; options_to_use[n] != NULL; n++)
    {
      const gchar *option = options_to_use[n];

      /* avoid attacks like passing "shortname=lower,uid=0" as a single mount option */
      if (strstr (option, ",") != NULL)
        {
          g_set_error (error,
                       STORAGED_ERROR,
                       STORAGED_ERROR_OPTION_NOT_PERMITTED,
                       "Malformed mount option `%s'",
                       option);
          g_string_free (str, TRUE);
          goto out;
        }

      /* first check if the mount option is allowed */
      if (!is_mount_option_allowed (fsmo, option, caller_uid))
        {
          g_set_error (error,
                       STORAGED_ERROR,
                       STORAGED_ERROR_OPTION_NOT_PERMITTED,
                       "Mount option `%s' is not allowed",
                       option);
          g_string_free (str, TRUE);
          goto out;
        }

      g_string_append_c (str, ',');
      g_string_append (str, option);
    }
  options_to_use_str = g_string_free (str, FALSE);

 out:
  g_strfreev (options_to_use);

  g_assert (options_to_use_str == NULL || g_utf8_validate (options_to_use_str, -1, NULL));

  return options_to_use_str;
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

static gboolean
add_acl (const gchar  *path,
         uid_t         uid,
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
      storaged_warning(
                   "Adding read ACL for uid %d to `%s' failed: %m",
                   (gint) uid, path);
      chown(path, uid, -1);
    }

  ret = TRUE;

  if (acl != NULL)
    acl_free (acl);
  return ret;
}

/*
 * calculate_mount_point: <internal>
 * @dameon: A #StoragedDaemon.
 * @block: A #StoragedBlock.
 * @uid: user id of the calling user
 * @gid: group id of the calling user
 * @user_name: user name of the calling user
 * @fs_type: The file system type to mount with
 * @error: Return location for error or %NULL.
 *
 * Calculates the mount point to use.
 *
 * Returns: A UTF-8 string with the mount point to use or %NULL if @error is set. Free with g_free().
 */
static gchar *
calculate_mount_point (StoragedDaemon              *daemon,
                       StoragedBlock               *block,
                       uid_t                        uid,
                       gid_t                        gid,
                       const gchar                 *user_name,
                       const gchar                 *fs_type,
                       GError                     **error)
{
  StoragedLinuxBlockObject *object = NULL;
  gboolean fs_shared = FALSE;
  const gchar *label = NULL;
  const gchar *uuid = NULL;
  gchar *escaped_user_name = NULL;
  gchar *mount_dir = NULL;
  gchar *mount_point = NULL;
  gchar *orig_mount_point;
  GString *str;
  gchar *s;
  guint n;

  label = NULL;
  uuid = NULL;
  if (block != NULL)
    {
      label = storaged_block_get_id_label (block);
      uuid = storaged_block_get_id_uuid (block);
    }

  object = storaged_daemon_util_dup_object (block, NULL);
  if (object != NULL)
    {
      StoragedLinuxDevice *device = storaged_linux_block_object_get_device (object);
      if (device != NULL)
        {
          if (device->udev_device != NULL)
            {
              /* TODO: maybe introduce Block:HintFilesystemShared instead of pulling it directly from the udev device */
              fs_shared = g_udev_device_get_property_as_boolean (device->udev_device, "STORAGED_FILESYSTEM_SHARED");
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
      if (!g_file_test (mount_dir, G_FILE_TEST_EXISTS))
        {
          /* First ensure that MOUNT_BASE exists */
          if (g_mkdir (MOUNT_BASE, 0755) != 0 && errno != EEXIST)
            {
              g_set_error (error,
                           STORAGED_ERROR,
                           STORAGED_ERROR_FAILED,
                           "Error creating directory " MOUNT_BASE ": %m");
              goto out;
            }
          /* Then create the per-user MOUNT_BASE/$USER */
          if (g_mkdir (mount_dir, 0700) != 0 && errno != EEXIST)
            {
              g_set_error (error,
                           STORAGED_ERROR,
                           STORAGED_ERROR_FAILED,
                           "Error creating directory `%s': %m",
                           mount_dir);
              goto out;
            }
          /* Finally, add the read+execute ACL for $USER */
          if (!add_acl (mount_dir, uid, error))
            {
              if (rmdir (mount_dir) != 0)
                storaged_warning ("Error calling rmdir() on %s: %m", mount_dir);
              goto out;
            }
        }
    }
  /* otherwise fall back to mounting in /media */
  if (mount_dir == NULL)
    mount_dir = g_strdup ("/media");

  /* NOTE: UTF-8 has the nice property that valid UTF-8 strings only contains
   *       the byte 0x2F if it's for the '/' character (U+002F SOLIDUS).
   *
   *       See http://en.wikipedia.org/wiki/UTF-8 for details.
   */
  if (label != NULL && strlen (label) > 0)
    {
      str = g_string_new (NULL);
      g_string_append_printf (str, "%s/", mount_dir);
      s = ensure_utf8 (label);
      for (n = 0; s[n] != '\0'; n++)
        {
          gint c = s[n];
          if (c == '/')
            g_string_append_c (str, '_');
          else
            g_string_append_c (str, c);
        }
      mount_point = g_string_free (str, FALSE);
      g_free (s);
    }
  else if (uuid != NULL && strlen (uuid) > 0)
    {
      str = g_string_new (NULL);
      g_string_append_printf (str, "%s/", mount_dir);
      s = ensure_utf8 (uuid);
      for (n = 0; s[n] != '\0'; n++)
        {
          gint c = s[n];
          if (c == '/')
            g_string_append_c (str, '_');
          else
            g_string_append_c (str, c);
        }
      mount_point = g_string_free (str, FALSE);
      g_free (s);
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
  g_free (mount_dir);

 out:
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
  g_strfreev (tokens);

 out:
  return ret;
}

static gboolean
is_in_fstab (StoragedBlock        *block,
             const gchar          *fstab_path,
             gchar               **out_mount_point,
             gchar               **out_mount_options)
{
  gboolean ret;
  FILE *f;
  char buf[8192];
  struct mntent mbuf;
  struct mntent *m;

  ret = FALSE;
  f = fopen (fstab_path, "r");
  if (f == NULL)
    {
      storaged_warning ("Error opening fstab file %s: %m", fstab_path);
      goto out;
    }

  while ((m = getmntent_r (f, &mbuf, buf, sizeof (buf))) != NULL && !ret)
    {
      gchar *device;
      struct stat sb;

      device = NULL;
      if (g_str_has_prefix (m->mnt_fsname, "UUID="))
        {
          device = g_strdup_printf ("/dev/disk/by-uuid/%s", m->mnt_fsname + 5);
        }
      else if (g_str_has_prefix (m->mnt_fsname, "LABEL="))
        {
          device = g_strdup_printf ("/dev/disk/by-label/%s", m->mnt_fsname + 6);
        }
      else if (g_str_has_prefix (m->mnt_fsname, "PARTUUID="))
        {
          device = g_strdup_printf ("/dev/disk/by-partuuid/%s", m->mnt_fsname + 9);
        }
      else if (g_str_has_prefix (m->mnt_fsname, "PARTLABEL="))
        {
          device = g_strdup_printf ("/dev/disk/by-partlabel/%s", m->mnt_fsname + 10);
        }
      else if (g_str_has_prefix (m->mnt_fsname, "/dev"))
        {
          device = g_strdup (m->mnt_fsname);
        }
      else
        {
          /* ignore non-device entries */
          goto continue_loop;
        }

      if (stat (device, &sb) != 0)
        {
          storaged_debug ("Error statting %s (for entry %s): %m", device, m->mnt_fsname);
          goto continue_loop;
        }
      if (!S_ISBLK (sb.st_mode))
        {
          storaged_debug ("Device %s (for entry %s) is not a block device", device, m->mnt_fsname);
          goto continue_loop;
        }

      /* storaged_debug ("device %d:%d for entry %s", major (sb.st_rdev), minor (sb.st_rdev), m->mnt_fsname); */

      if (storaged_block_get_device_number (block) == sb.st_rdev)
        {
          ret = TRUE;
          if (out_mount_point != NULL)
            *out_mount_point = g_strdup (m->mnt_dir);
          if (out_mount_options != NULL)
            *out_mount_options = g_strdup (m->mnt_opts);
        }

    continue_loop:
      g_free (device);
    }

 out:
  if (f != NULL)
    fclose (f);
  return ret;
}

/* returns TRUE if, and only if, device is referenced in e.g. /etc/fstab
 *
 * TODO: check all files in /etc/fstab.d (it's a non-standard Linux extension)
 * TODO: check if systemd has a specific "unit" for the device
 */
static gboolean
is_system_managed (StoragedBlock        *block,
                   gchar               **out_mount_point,
                   gchar               **out_mount_options)
{
  gboolean ret;

  ret = TRUE;

  /* First, check /etc/fstab */
  if (is_in_fstab (block, "/etc/fstab", out_mount_point, out_mount_options))
    goto out;

  ret = FALSE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_mount (StoragedFilesystem       *filesystem,
              GDBusMethodInvocation    *invocation,
              GVariant                 *options)
{
  StoragedObject *object;
  StoragedBlock *block;
  StoragedDaemon *daemon;
  StoragedState *state;
  uid_t caller_uid;
  gid_t caller_gid;
  pid_t caller_pid;
  const gchar * const *existing_mount_points;
  const gchar *probed_fs_usage;
  gchar *fs_type_to_use;
  gchar *mount_options_to_use;
  gchar *mount_point_to_use;
  gchar *fstab_mount_options;
  gchar *escaped_fs_type_to_use;
  gchar *escaped_mount_options_to_use;
  gchar *escaped_mount_point_to_use;
  gchar *error_message;
  gchar *caller_user_name;
  GError *error;
  const gchar *action_id;
  const gchar *message;
  gboolean system_managed;
  gchar *escaped_device = NULL;

  object = NULL;
  error_message = NULL;
  fs_type_to_use = NULL;
  mount_options_to_use = NULL;
  mount_point_to_use = NULL;
  fstab_mount_options = NULL;
  escaped_fs_type_to_use = NULL;
  escaped_mount_options_to_use = NULL;
  escaped_mount_point_to_use = NULL;
  caller_user_name = NULL;
  system_managed = FALSE;

  /* only allow a single call at a time */
  g_mutex_lock (&STORAGED_LINUX_FILESYSTEM (filesystem)->lock);

  error = NULL;
  object = storaged_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = storaged_object_peek_block (object);
  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  state = storaged_daemon_get_state (daemon);

  /* check if mount point is managed by e.g. /etc/fstab or similar */
  if (is_system_managed (block, &mount_point_to_use, &fstab_mount_options))
    {
      system_managed = TRUE;
    }

  /* First, fail if the device is already mounted */
  existing_mount_points = storaged_filesystem_get_mount_points (filesystem);
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
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_ALREADY_MOUNTED,
                                             "Device %s is already mounted at %s.\n",
                                             storaged_block_get_device (block),
                                             str->str);
      g_string_free (str, TRUE);
      goto out;
    }

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon,
                                                 invocation,
                                                 NULL /* GCancellable */,
                                                 &caller_uid,
                                                 &caller_gid,
                                                 &caller_user_name,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
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

  if (system_managed)
    {
      gint status;
      gboolean mount_fstab_as_root = FALSE;

      if (!has_option (fstab_mount_options, "x-storaged-auth"))
        {
          action_id = "org.storaged.Storaged.filesystem-mount";
          /* Translators: Shown in authentication dialog when the user
           * requests mounting a filesystem.
           *
           * Do not translate $(drive), it's a placeholder and
           * will be replaced by the name of the drive/device in question
           */
          message = N_("Authentication is required to mount $(drive)");
          if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
            {
              if (storaged_block_get_hint_system (block))
                {
                  action_id = "org.storaged.Storaged.filesystem-mount-system";
                }
              else if (!storaged_daemon_util_on_same_seat (daemon, object, caller_pid))
                {
                  action_id = "org.storaged.Storaged.filesystem-mount-other-seat";
                }
            }

          if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                              object,
                                                              action_id,
                                                              options,
                                                              message,
                                                              invocation))
            goto out;
          mount_fstab_as_root = TRUE;
        }

      if (!g_file_test (mount_point_to_use, G_FILE_TEST_IS_DIR))
        {
          if (g_mkdir_with_parents (mount_point_to_use, 0755) != 0)
            {
              g_dbus_method_invocation_return_error (invocation,
                                                     STORAGED_ERROR,
                                                     STORAGED_ERROR_FAILED,
                                                     "Error creating directory `%s' to be used for mounting %s: %m",
                                                     mount_point_to_use,
                                                     storaged_block_get_device (block));
              goto out;
            }
        }

      escaped_mount_point_to_use   = storaged_daemon_util_escape_and_quote (mount_point_to_use);
    mount_fstab_again:
      if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                    object,
                                                    "filesystem-mount", caller_uid,
                                                    NULL,  /* GCancellable */
                                                    mount_fstab_as_root ? 0 : caller_uid, /* uid_t run_as_uid */
                                                    mount_fstab_as_root ? 0 : caller_uid, /* uid_t run_as_euid */
                                                    &status,
                                                    &error_message,
                                                    NULL,  /* input_string */
                                                    "mount %s",
                                                    escaped_mount_point_to_use))
        {
          /* mount(8) exits with status 1 on "incorrect invocation or permissions" - if this is
           * is so, try as as root */
          if (!mount_fstab_as_root && WIFEXITED (status) && WEXITSTATUS (status) == 1)
            {
              if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                                  object,
                                                                  "org.storaged.Storaged.filesystem-fstab",
                                                                  options,
                                                                  /* Translators: Shown in authentication dialog when the
                                                                   * user requests mounting a filesystem that is in
                                                                   * /etc/fstab file with the x-storaged-auth option.
                                                                   *
                                                                   * Do not translate $(drive), it's a
                                                                   * placeholder and will be replaced by the name of
                                                                   * the drive/device in question
                                                                   *
                                                                   * Do not translate /etc/fstab
                                                                   */
                                                                  N_("Authentication is required to mount $(drive) referenced in the /etc/fstab file"),
                                                                  invocation))
                goto out;
              mount_fstab_as_root = TRUE;
              goto mount_fstab_again;
            }

          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_FAILED,
                                                 "Error mounting system-managed device %s: %s",
                                                 storaged_block_get_device (block),
                                                 error_message);
          goto out;
        }
      storaged_notice ("Mounted %s (system) at %s on behalf of uid %u",
                       storaged_block_get_device (block),
                       mount_point_to_use,
                       caller_uid);

      /* update the mounted-fs file */
      storaged_state_add_mounted_fs (state,
                                     mount_point_to_use,
                                     storaged_block_get_device_number (block),
                                     caller_uid,
                                     TRUE); /* fstab_mounted */

      storaged_filesystem_complete_mount (filesystem, invocation, mount_point_to_use);
      goto out;
    }

  /* Then fail if the device is not mountable - we actually allow mounting
   * devices that are not probed since since it could be that we just
   * don't have the data in the udev database but the device has a
   * filesystem *anyway*...
   *
   * For example, this applies to PC floppy devices - automatically
   * probing for media them creates annoying noise. So they won't
   * appear in the udev database.
   */
  probed_fs_usage = NULL;
  if (block != NULL)
    probed_fs_usage = storaged_block_get_id_usage (block);
  if (probed_fs_usage != NULL && strlen (probed_fs_usage) > 0 &&
      g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Cannot mount block device %s with probed usage `%s' - expected `filesystem'",
                                             storaged_block_get_device (block),
                                             probed_fs_usage);
      goto out;
    }

  /* calculate filesystem type (guaranteed to be valid UTF-8) */
  error = NULL;
  fs_type_to_use = calculate_fs_type (block,
                                      options,
                                      &error);
  if (fs_type_to_use == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* calculate mount options (guaranteed to be valid UTF-8) */
  error = NULL;
  mount_options_to_use = calculate_mount_options (daemon,
                                                  block,
                                                  caller_uid,
                                                  fs_type_to_use,
                                                  options,
                                                  &error);
  if (mount_options_to_use == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* Now, check that the user is actually authorized to mount the
   * device. Need to do this before calculating a mount point since we
   * may be racing with other threads...
   */
  action_id = "org.storaged.Storaged.filesystem-mount";
  /* Translators: Shown in authentication dialog when the user
   * requests mounting a filesystem.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to mount $(drive)");
  if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (storaged_block_get_hint_system (block))
        {
          action_id = "org.storaged.Storaged.filesystem-mount-system";
        }
      else if (!storaged_daemon_util_on_same_seat (daemon, object, caller_pid))
        {
          action_id = "org.storaged.Storaged.filesystem-mount-other-seat";
        }
    }

  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      object,
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  /* calculate mount point (guaranteed to be valid UTF-8) */
  error = NULL;
  mount_point_to_use = calculate_mount_point (daemon,
                                              block,
                                              caller_uid,
                                              caller_gid,
                                              caller_user_name,
                                              fs_type_to_use,
                                              &error);
  if (mount_point_to_use == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* create the mount point */
  if (g_mkdir (mount_point_to_use, 0700) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error creating mount point `%s': %m",
                                             mount_point_to_use);
      goto out;
    }

  escaped_fs_type_to_use       = storaged_daemon_util_escape_and_quote (fs_type_to_use);
  escaped_mount_options_to_use = storaged_daemon_util_escape_and_quote (mount_options_to_use);
  escaped_mount_point_to_use   = storaged_daemon_util_escape_and_quote (mount_point_to_use);
  escaped_device               = storaged_daemon_util_escape_and_quote (storaged_block_get_device (block));

  /* run mount(8) */
  if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                object,
                                                "filesystem-mount", caller_uid,
                                                NULL, /* GCancellable */
                                                0,    /* uid_t run_as_uid */
                                                0,    /* uid_t run_as_euid */
                                                NULL, /* gint *out_status */
                                                &error_message,
                                                NULL,  /* input_string */
                                                "mount -t %s -o %s %s %s",
                                                escaped_fs_type_to_use,
                                                escaped_mount_options_to_use,
                                                escaped_device,
                                                escaped_mount_point_to_use))
    {
      /* ugh, something went wrong.. we need to clean up the created mount point */
      if (g_rmdir (mount_point_to_use) != 0)
        storaged_warning ("Error removing directory %s: %m", mount_point_to_use);
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_FAILED,
                                             "Error mounting %s at %s: %s",
                                             storaged_block_get_device (block),
                                             mount_point_to_use,
                                             error_message);
      goto out;
    }

  /* update the mounted-fs file */
  storaged_state_add_mounted_fs (state,
                                 mount_point_to_use,
                                 storaged_block_get_device_number (block),
                                 caller_uid,
                                 FALSE); /* fstab_mounted */

  storaged_notice ("Mounted %s at %s on behalf of uid %u",
                   storaged_block_get_device (block),
                   mount_point_to_use,
                   caller_uid);

  storaged_filesystem_complete_mount (filesystem, invocation, mount_point_to_use);

 out:
  g_free (escaped_device);
  g_free (error_message);
  g_free (escaped_fs_type_to_use);
  g_free (escaped_mount_options_to_use);
  g_free (escaped_mount_point_to_use);
  g_free (fs_type_to_use);
  g_free (mount_options_to_use);
  g_free (mount_point_to_use);
  g_free (fstab_mount_options);
  g_free (caller_user_name);
  g_clear_object (&object);

  /* only allow a single call at a time */
  g_mutex_unlock (&STORAGED_LINUX_FILESYSTEM (filesystem)->lock);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static guint
get_error_code_for_umount (gint         exit_status,
                           const gchar *error_message)
{
  if (strstr (error_message, "device is busy") != NULL ||
      strstr (error_message, "target is busy") != NULL)
    return STORAGED_ERROR_DEVICE_BUSY;
  else
    return STORAGED_ERROR_FAILED;
}

/* runs in thread dedicated to handling @invocation */
static gboolean
handle_unmount (StoragedFilesystem       *filesystem,
                GDBusMethodInvocation    *invocation,
                GVariant                 *options)
{
  StoragedObject *object;
  StoragedBlock *block;
  StoragedDaemon *daemon;
  StoragedState *state;
  gchar *mount_point;
  gchar *fstab_mount_options;
  gchar *escaped_mount_point;
  GError *error;
  uid_t mounted_by_uid;
  uid_t caller_uid;
  gint status = 0;
  gchar *error_message;
  const gchar *const *mount_points;
  gboolean opt_force;
  gboolean rc;
  gboolean system_managed;
  gboolean fstab_mounted;
  gchar *escaped_device = NULL;

  mount_point = NULL;
  fstab_mount_options = NULL;
  escaped_mount_point = NULL;
  error_message = NULL;
  opt_force = FALSE;

  /* only allow a single call at a time */
  g_mutex_lock (&STORAGED_LINUX_FILESYSTEM (filesystem)->lock);

  error = NULL;
  object = storaged_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  block = storaged_object_peek_block (object);
  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  state = storaged_daemon_get_state (daemon);
  system_managed = FALSE;

  if (options != NULL)
    {
      g_variant_lookup (options,
                        "force",
                        "b",
                        &opt_force);
    }

  mount_points = storaged_filesystem_get_mount_points (filesystem);
  if (mount_points == NULL || g_strv_length ((gchar **) mount_points) == 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_NOT_MOUNTED,
                                             "Device `%s' is not mounted",
                                             storaged_block_get_device (block));
      goto out;
    }

  error = NULL;
  if (!storaged_daemon_util_get_caller_uid_sync (daemon, invocation, NULL, &caller_uid, NULL, NULL, &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  /* check if mount point is managed by e.g. /etc/fstab or similar */
  if (is_system_managed (block, &mount_point, &fstab_mount_options))
    {
      system_managed = TRUE;
    }

  /* if system-managed (e.g. referenced in /etc/fstab or similar) and
   * with the option x-storaged-auth, just run umount(8) as the
   * calling user
   */
  if (system_managed && has_option (fstab_mount_options, "x-storaged-auth"))
    {
      gboolean unmount_fstab_as_root;

      unmount_fstab_as_root = FALSE;
    unmount_fstab_again:
      escaped_mount_point = storaged_daemon_util_escape_and_quote (mount_point);
      /* right now -l is the only way to "force unmount" file systems... */
      if (!storaged_daemon_launch_spawned_job_sync (daemon,
                                                    object,
                                                    "filesystem-unmount", caller_uid,
                                                    NULL, /* GCancellable */
                                                    unmount_fstab_as_root ? 0 : caller_uid, /* uid_t run_as_uid */
                                                    unmount_fstab_as_root ? 0 : caller_uid, /* uid_t run_as_euid */
                                                    &status,
                                                    &error_message,
                                                    NULL,  /* input_string */
                                                    "umount %s %s",
                                                    opt_force ? "-l" : "",
                                                    escaped_mount_point))
        {
          /* umount(8) does not (yet) have a specific exits status for
           * "insufficient permissions" so just try again as root
           *
           * TODO: file bug asking for such an exit status
           */
          if (!unmount_fstab_as_root && WIFEXITED (status) && WEXITSTATUS (status) != 0)
            {
              if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                                  object,
                                                                  "org.storaged.Storaged.filesystem-fstab",
                                                                  options,
                                                                  /* Translators: Shown in authentication dialog when the
                                                                   * user requests unmounting a filesystem that is in
                                                                   * /etc/fstab file with the x-storaged-auth option.
                                                                   *
                                                                   * Do not translate $(drive), it's a
                                                                   * placeholder and will be replaced by the name of
                                                                   * the drive/device in question
                                                                   *
                                                                   * Do not translate /etc/fstab
                                                                   */
                                                                  N_("Authentication is required to unmount $(drive) referenced in the /etc/fstab file"),
                                                                  invocation))
                goto out;
              unmount_fstab_as_root = TRUE;
              goto unmount_fstab_again;
            }

          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 get_error_code_for_umount (status, error_message),
                                                 "Error unmounting system-managed device %s: %s",
                                                 storaged_block_get_device (block),
                                                 error_message);
          goto out;
        }
      storaged_notice ("Unmounted %s (system) from %s on behalf of uid %u",
                       storaged_block_get_device (block),
                       mount_point,
                       caller_uid);
      storaged_filesystem_complete_unmount (filesystem, invocation);
      goto out;
    }

  error = NULL;
  mount_point = storaged_state_find_mounted_fs (state,
                                                storaged_block_get_device_number (block),
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

      action_id = "org.storaged.Storaged.filesystem-unmount-others";
      /* Translators: Shown in authentication dialog when the user
       * requests unmounting a filesystem previously mounted by
       * another user.
       *
       * Do not translate $(drive), it's a placeholder and
       * will be replaced by the name of the drive/device in question
       */
      message = N_("Authentication is required to unmount $(drive) mounted by another user");

      if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                          object,
                                                          action_id,
                                                          options,
                                                          message,
                                                          invocation))
        goto out;
    }

  escaped_device = storaged_daemon_util_escape_and_quote (storaged_block_get_device (block));

  /* otherwise go ahead and unmount the filesystem */
  if (mount_point != NULL)
    {
      escaped_mount_point = storaged_daemon_util_escape_and_quote (mount_point);
      rc = storaged_daemon_launch_spawned_job_sync (daemon,
                                                    object,
                                                    "filesystem-unmount", caller_uid,
                                                    NULL, /* GCancellable */
                                                    0,    /* uid_t run_as_uid */
                                                    0,    /* uid_t run_as_euid */
                                                    NULL, /* gint *out_status */
                                                    &error_message,
                                                    NULL,  /* input_string */
                                                    "umount %s %s",
                                                    opt_force ? "-l" : "",
                                                    escaped_mount_point);
    }
  else
    {
      /* mount_point == NULL */
      rc = storaged_daemon_launch_spawned_job_sync (daemon,
                                                    object,
                                                    "filesystem-unmount", caller_uid,
                                                    NULL, /* GCancellable */
                                                    0,    /* uid_t run_as_uid */
                                                    0,    /* uid_t run_as_euid */
                                                    &status,
                                                    &error_message,
                                                    NULL,  /* input_string */
                                                    "umount %s %s",
                                                    opt_force ? "-l" : "",
                                                    escaped_device);
    }

  if (!rc)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             get_error_code_for_umount (status, error_message),
                                             "Error unmounting %s: %s",
                                             storaged_block_get_device (block),
                                             error_message);
      goto out;
    }

  /* OK, filesystem unmounted.. the state/cleanup routines will remove the mountpoint for us */

  storaged_notice ("Unmounted %s on behalf of uid %u",
                   storaged_block_get_device (block),
                   caller_uid);

  storaged_filesystem_complete_unmount (filesystem, invocation);

 out:
  g_free (escaped_device);
  g_free (error_message);
  g_free (escaped_mount_point);
  g_free (mount_point);
  g_free (fstab_mount_options);
  g_clear_object (&object);

  g_mutex_unlock (&STORAGED_LINUX_FILESYSTEM (filesystem)->lock);

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_set_label_job_completed (StoragedJob   *job,
                            gboolean       success,
                            const gchar   *message,
                            gpointer       user_data)
{
  GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (user_data);
  StoragedFilesystem *filesystem;

  filesystem = STORAGED_FILESYSTEM (g_dbus_method_invocation_get_user_data (invocation));

  if (success)
    storaged_filesystem_complete_set_label (filesystem, invocation);
  else
    g_dbus_method_invocation_return_error (invocation,
                                           STORAGED_ERROR,
                                           STORAGED_ERROR_FAILED,
                                           "Error setting label: %s",
                                           message);
}

/* runs in thread dedicated to handling method call */
static gboolean
handle_set_label (StoragedFilesystem       *filesystem,
                  GDBusMethodInvocation    *invocation,
                  const gchar              *label,
                  GVariant                 *options)
{
  StoragedBlock *block;
  StoragedObject *object;
  StoragedDaemon *daemon;
  const gchar *probed_fs_usage;
  const gchar *probed_fs_type;
  const FSInfo *fs_info;
  StoragedBaseJob *job;
  const gchar *action_id;
  const gchar *message;
  gchar *real_label = NULL;
  uid_t caller_uid;
  gid_t caller_gid;
  pid_t caller_pid;
  gchar *command;
  gchar *tmp;
  GError *error;

  object = NULL;
  daemon = NULL;
  command = NULL;

  error = NULL;
  object = storaged_daemon_util_dup_object (filesystem, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = storaged_linux_block_object_get_daemon (STORAGED_LINUX_BLOCK_OBJECT (object));
  block = storaged_object_peek_block (object);

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

  probed_fs_usage = storaged_block_get_id_usage (block);
  probed_fs_type = storaged_block_get_id_type (block);

  if (g_strcmp0 (probed_fs_usage, "filesystem") != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_NOT_SUPPORTED,
                                             "Cannot change label on device of type %s",
                                             probed_fs_usage);
      goto out;
    }

  fs_info = get_fs_info (probed_fs_type);

  if (fs_info == NULL || fs_info->command_change_label == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             STORAGED_ERROR,
                                             STORAGED_ERROR_NOT_SUPPORTED,
                                             "Don't know how to change label on device of type %s:%s",
                                             probed_fs_usage,
                                             probed_fs_type);
      goto out;
    }

  /* VFAT does not allow some characters; as dosfslabel does not enforce this,
   * check in advance; also, VFAT only knows upper-case characters, dosfslabel
   * enforces this */
  if (g_strcmp0 (probed_fs_type, "vfat") == 0)
    {
      const gchar *forbidden = "\"*/:<>?\\|";
      guint n;
      for (n = 0; forbidden[n] != 0; n++)
        {
          if (strchr (label, forbidden[n]) != NULL)
            {
              g_dbus_method_invocation_return_error (invocation,
                                                     STORAGED_ERROR,
                                                     STORAGED_ERROR_NOT_SUPPORTED,
                                                     "character '%c' not supported in VFAT labels",
                                                     forbidden[n]);
               goto out;
            }
        }

      /* we need to remember that we make a copy, so assign it to a new
       * variable, too */
      real_label = g_ascii_strup (label, -1);
      label = real_label;
    }

  /* Fail if the device is already mounted and the tools/drivers doesn't
   * support changing the label in that case
   */
  if (filesystem != NULL && !fs_info->supports_online_label_rename)
    {
      const gchar * const *existing_mount_points;
      existing_mount_points = storaged_filesystem_get_mount_points (filesystem);
      if (existing_mount_points != NULL && g_strv_length ((gchar **) existing_mount_points) > 0)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 STORAGED_ERROR,
                                                 STORAGED_ERROR_NOT_SUPPORTED,
                                                 "Cannot change label on mounted device of type %s:%s.\n",
                                                 probed_fs_usage,
                                                 probed_fs_type);
          goto out;
        }
    }

  action_id = "org.storaged.Storaged.modify-device";
  /* Translators: Shown in authentication dialog when the user
   * requests changing the filesystem label.
   *
   * Do not translate $(drive), it's a placeholder and
   * will be replaced by the name of the drive/device in question
   */
  message = N_("Authentication is required to change the filesystem label on $(drive)");
  if (!storaged_daemon_util_setup_by_user (daemon, object, caller_uid))
    {
      if (storaged_block_get_hint_system (block))
        {
          action_id = "org.storaged.Storaged.modify-device-system";
        }
      else if (!storaged_daemon_util_on_same_seat (daemon, STORAGED_OBJECT (object), caller_pid))
        {
          action_id = "org.storaged.Storaged.modify-device-other-seat";
        }
    }

  /* Check that the user is actually authorized to change the
   * filesystem label.
   */
  if (!storaged_daemon_util_check_authorization_sync (daemon,
                                                      object,
                                                      action_id,
                                                      options,
                                                      message,
                                                      invocation))
    goto out;

  if (fs_info->command_clear_label != NULL && strlen (label) == 0)
    {
      command = subst_str_and_escape (fs_info->command_clear_label, "$DEVICE", storaged_block_get_device (block));
    }
  else
    {
      tmp = subst_str_and_escape (fs_info->command_change_label, "$DEVICE", storaged_block_get_device (block));
      command = subst_str_and_escape (tmp, "$LABEL", label);
      g_free (tmp);
    }

  job = storaged_daemon_launch_spawned_job (daemon,
                                            object,
                                            "filesystem-modify", caller_uid,
                                            NULL, /* cancellable */
                                            0,    /* uid_t run_as_uid */
                                            0,    /* uid_t run_as_euid */
                                            NULL, /* input_string */
                                            "%s", command);
  g_signal_connect (job,
                    "completed",
                    G_CALLBACK (on_set_label_job_completed),
                    invocation);

 out:
  /* for some FSes we need to copy and modify label; free our copy */
  g_free (real_label);
  g_free (command);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
filesystem_iface_init (StoragedFilesystemIface *iface)
{
  iface->handle_mount     = handle_mount;
  iface->handle_unmount   = handle_unmount;
  iface->handle_set_label = handle_set_label;
}
