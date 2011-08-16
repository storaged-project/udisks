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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <mntent.h>

#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>

#include "udiskslogging.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxblock.h"
#include "udisksmount.h"
#include "udisksmountmonitor.h"
#include "udiskslinuxdriveobject.h"
#include "udiskslinuxdrive.h"
#include "udiskslinuxfilesystem.h"
#include "udiskslinuxencrypted.h"
#include "udiskslinuxswapspace.h"
#include "udiskslinuxloop.h"
#include "udiskspersistentstore.h"
#include "udiskslinuxprovider.h"
#include "udisksfstabmonitor.h"
#include "udisksfstabentry.h"
#include "udiskscrypttabmonitor.h"
#include "udiskscrypttabentry.h"

/**
 * SECTION:udiskslinuxblock
 * @title: UDisksLinuxBlock
 * @short_description: Linux block devices
 *
 * Object corresponding to a Linux block device.
 */

typedef struct _UDisksLinuxBlockClass   UDisksLinuxBlockClass;

/**
 * UDisksLinuxBlock:
 *
 * The #UDisksLinuxBlock structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxBlock
{
  UDisksObjectSkeleton parent_instance;

  UDisksDaemon *daemon;
  UDisksMountMonitor *mount_monitor;

  GUdevDevice *device;

  /* interface */
  UDisksBlockDevice *iface_block_device;
  UDisksFilesystem *iface_filesystem;
  UDisksSwapspace *iface_swapspace;
  UDisksEncrypted *iface_encrypted;
  UDisksLoop *iface_loop;
};

struct _UDisksLinuxBlockClass
{
  UDisksObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

G_DEFINE_TYPE (UDisksLinuxBlock, udisks_linux_block, UDISKS_TYPE_OBJECT_SKELETON);

static void on_mount_monitor_mount_added   (UDisksMountMonitor  *monitor,
                                            UDisksMount         *mount,
                                            gpointer             user_data);
static void on_mount_monitor_mount_removed (UDisksMountMonitor  *monitor,
                                            UDisksMount         *mount,
                                            gpointer             user_data);

static void
udisks_linux_block_finalize (GObject *object)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (object);

  /* note: we don't hold a ref to block->daemon or block->mount_monitor */
  g_signal_handlers_disconnect_by_func (block->mount_monitor, on_mount_monitor_mount_added, block);
  g_signal_handlers_disconnect_by_func (block->mount_monitor, on_mount_monitor_mount_removed, block);

  g_object_unref (block->device);

  if (block->iface_block_device != NULL)
    g_object_unref (block->iface_block_device);
  if (block->iface_filesystem != NULL)
    g_object_unref (block->iface_filesystem);
  if (block->iface_swapspace != NULL)
    g_object_unref (block->iface_swapspace);
  if (block->iface_encrypted != NULL)
    g_object_unref (block->iface_encrypted);
  if (block->iface_loop != NULL)
    g_object_unref (block->iface_loop);

  if (G_OBJECT_CLASS (udisks_linux_block_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_block_parent_class)->finalize (object);
}

static void
udisks_linux_block_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_block_get_daemon (block));
      break;

    case PROP_DEVICE:
      g_value_set_object (value, udisks_linux_block_get_device (block));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_block_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (block->daemon == NULL);
      /* we don't take a reference to the daemon */
      block->daemon = g_value_get_object (value);
      break;

    case PROP_DEVICE:
      g_assert (block->device == NULL);
      block->device = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
udisks_linux_block_init (UDisksLinuxBlock *block)
{
}

static void
udisks_linux_block_constructed (GObject *object)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (object);
  GString *str;

  block->mount_monitor = udisks_daemon_get_mount_monitor (block->daemon);
  g_signal_connect (block->mount_monitor,
                    "mount-added",
                    G_CALLBACK (on_mount_monitor_mount_added),
                    block);
  g_signal_connect (block->mount_monitor,
                    "mount-removed",
                    G_CALLBACK (on_mount_monitor_mount_removed),
                    block);

  /* initial coldplug */
  udisks_linux_block_uevent (block, "add", NULL);

  /* compute the object path */
  str = g_string_new ("/org/freedesktop/UDisks2/block_devices/");
  udisks_safe_append_to_object_path (str, g_udev_device_get_name (block->device));
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (block), str->str);
  g_string_free (str, TRUE);

  if (G_OBJECT_CLASS (udisks_linux_block_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_block_parent_class)->constructed (object);
}

static void
udisks_linux_block_class_init (UDisksLinuxBlockClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_block_finalize;
  gobject_class->constructed  = udisks_linux_block_constructed;
  gobject_class->set_property = udisks_linux_block_set_property;
  gobject_class->get_property = udisks_linux_block_get_property;

  /**
   * UDisksLinuxBlock:daemon:
   *
   * The #UDisksDaemon the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the object is for",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksLinuxBlock:device:
   *
   * The #GUdevDevice for the object. Connect to the #GObject::notify
   * signal to get notified whenever this is updated.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DEVICE,
                                   g_param_spec_object ("device",
                                                        "Device",
                                                        "The device for the object",
                                                        G_UDEV_TYPE_DEVICE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

}

/**
 * udisks_linux_block_new:
 * @daemon: A #UDisksDaemon.
 * @device: The #GUdevDevice for the sysfs block device.
 *
 * Create a new block object.
 *
 * Returns: A #UDisksLinuxBlock object. Free with g_object_unref().
 */
UDisksLinuxBlock *
udisks_linux_block_new (UDisksDaemon  *daemon,
                        GUdevDevice   *device)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_LINUX_BLOCK (g_object_new (UDISKS_TYPE_LINUX_BLOCK,
                                           "daemon", daemon,
                                           "device", device,
                                           NULL));
}

/**
 * udisks_linux_block_get_daemon:
 * @block: A #UDisksLinuxBlock.
 *
 * Gets the daemon used by @block.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @block.
 */
UDisksDaemon *
udisks_linux_block_get_daemon (UDisksLinuxBlock *block)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK (block), NULL);
  return block->daemon;
}

/**
 * udisks_linux_block_get_device:
 * @block: A #UDisksLinuxBlock.
 *
 * Gets the current #GUdevDevice for @block. Connect to
 * #GObject::notify to track changes to the #UDisksLinuxBlock:device
 * property.
 *
 * Returns: A #GUdevDevice. Free with g_object_unref().
 */
GUdevDevice *
udisks_linux_block_get_device (UDisksLinuxBlock *block)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_BLOCK (block), NULL);
  return g_object_ref (block->device);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef gboolean (*HasInterfaceFunc)    (UDisksLinuxBlock     *block);
typedef void     (*ConnectInterfaceFunc) (UDisksLinuxBlock     *block);
typedef void     (*UpdateInterfaceFunc) (UDisksLinuxBlock     *block,
                                         const gchar    *uevent_action,
                                         GDBusInterface *interface);

static void
update_iface (UDisksLinuxBlock           *block,
              const gchar          *uevent_action,
              HasInterfaceFunc      has_func,
              ConnectInterfaceFunc   connect_func,
              UpdateInterfaceFunc   update_func,
              GType                 skeleton_type,
              gpointer              _interface_pointer)
{
  gboolean has;
  gboolean add;
  GDBusInterface **interface_pointer = _interface_pointer;

  g_return_if_fail (block != NULL);
  g_return_if_fail (has_func != NULL);
  g_return_if_fail (update_func != NULL);
  g_return_if_fail (g_type_is_a (skeleton_type, G_TYPE_OBJECT));
  g_return_if_fail (g_type_is_a (skeleton_type, G_TYPE_DBUS_INTERFACE));
  g_return_if_fail (interface_pointer != NULL);
  g_return_if_fail (*interface_pointer == NULL || G_IS_DBUS_INTERFACE (*interface_pointer));

  add = FALSE;
  has = has_func (block);
  if (*interface_pointer == NULL)
    {
      if (has)
        {
          *interface_pointer = g_object_new (skeleton_type, NULL);
          if (connect_func != NULL)
            connect_func (block);
          add = TRUE;
        }
    }
  else
    {
      if (!has)
        {
          g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (block), G_DBUS_INTERFACE_SKELETON (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      update_func (block, uevent_action, G_DBUS_INTERFACE (*interface_pointer));
      if (add)
        g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (block), G_DBUS_INTERFACE_SKELETON (*interface_pointer));
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static GVariant *calculate_configuration (UDisksLinuxBlock  *block,
                                          gboolean           include_secrets,
                                          GError           **error);

static gboolean
on_get_secret_configuration (UDisksBlockDevice     *block,
                             GDBusMethodInvocation *invocation,
                             GVariant              *options,
                             gpointer               user_data)
{
  UDisksLinuxBlock *object = UDISKS_LINUX_BLOCK (user_data);
  GVariant *configuration;
  GError *error;

  error = NULL;
  configuration = calculate_configuration (object, TRUE, &error);
  if (configuration == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!udisks_daemon_util_check_authorization_sync (object->daemon,
                                                    NULL,
                                                    "org.freedesktop.udisks2.read-system-configuration-secrets",
                                                    options,
                                                    N_("Authentication is required to read system-level secrets"),
                                                    invocation))
    {
      g_variant_unref (configuration);
      goto out;
    }

  udisks_block_device_complete_get_secret_configuration (object->iface_block_device, invocation,
                                                         configuration); /* consumes floating ref */

 out:
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
escape_fstab (const gchar *source)
{
  GString *s;
  guint n;
  s = g_string_new (NULL);
  for (n = 0; source[n] != '\0'; n++)
    {
      switch (source[n])
        {
        case ' ':
        case '\t':
        case '\n':
        case '\\':
          g_string_append_printf (s, "\\%03o", source[n]);
          break;

        default:
          g_string_append_c (s, source[n]);
          break;
        }
    }
  return g_string_free (s, FALSE);
}

/* based on g_strcompress() */
static gchar *
unescape_fstab (const gchar *source)
{
  const gchar *p = source, *octal;
  gchar *dest = g_malloc (strlen (source) + 1);
  gchar *q = dest;

  while (*p)
    {
      if (*p == '\\')
        {
          p++;
          switch (*p)
            {
            case '\0':
              g_warning ("unescape_fstab: trailing \\");
              goto out;
            case '0':  case '1':  case '2':  case '3':  case '4':
            case '5':  case '6':  case '7':
              *q = 0;
              octal = p;
              while ((p < octal + 3) && (*p >= '0') && (*p <= '7'))
                {
                  *q = (*q * 8) + (*p - '0');
                  p++;
                }
              q++;
              p--;
              break;
            default:            /* Also handles \" and \\ */
              *q++ = *p;
              break;
            }
        }
      else
        *q++ = *p;
      p++;
    }
out:
  *q = 0;

  return dest;
}

/* ---------------------------------------------------------------------------------------------------- */

/* TODO: maybe move to GLib */
static gboolean
_g_file_set_contents_full (const gchar  *filename,
                           const gchar  *contents,
                           gssize        contents_len,
                           gint          mode_for_new_file,
                           GError      **error)
{
  gboolean ret;
  struct stat statbuf;
  gint mode;
  gchar *tmpl;
  gint fd;
  FILE *f;

  ret = FALSE;
  tmpl = NULL;

  if (stat (filename, &statbuf) != 0)
    {
      if (errno == ENOENT)
        {
          mode = mode_for_new_file;
        }
      else
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "Error stat(2)'ing %s: %m",
                       filename);
          goto out;
        }
    }
  else
    {
      mode = statbuf.st_mode;
    }

  tmpl = g_strdup_printf ("%s.XXXXXX", filename);
  fd = g_mkstemp_full (tmpl, O_RDWR, mode);
  if (fd == -1)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error creating temporary file: %m");
      goto out;
    }

  f = fdopen (fd, "w");
  if (f == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error calling fdopen: %m");
      g_unlink (tmpl);
      goto out;
    }

  if (contents_len < 0 )
    contents_len = strlen (contents);
  if (fwrite (contents, 1, contents_len, f) != contents_len)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error calling fwrite on temp file: %m");
      fclose (f);
      g_unlink (tmpl);
      goto out;
    }

  if (fsync (fileno (f)) != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error calling fsync on temp file: %m");
      fclose (f);
      g_unlink (tmpl);
      goto out;
    }
  fclose (f);

  if (rename (tmpl, filename) != 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Error renaming temp file to final file: %m");
      g_unlink (tmpl);
      goto out;
    }

  ret = TRUE;

 out:
  g_free (tmpl);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
add_remove_fstab_entry (GVariant  *remove,
                        GVariant  *add,
                        GError   **error)
{
  struct mntent mntent_remove;
  struct mntent mntent_add;
  gboolean ret;
  gchar *contents;
  gchar **lines;
  GString *str;
  gboolean removed;
  guint n;

  contents = NULL;
  lines = NULL;
  str = NULL;
  ret = FALSE;

  if (remove != NULL)
    {
      if (!g_variant_lookup (remove, "fsname", "^&ay", &mntent_remove.mnt_fsname) ||
          !g_variant_lookup (remove, "dir", "^&ay", &mntent_remove.mnt_dir) ||
          !g_variant_lookup (remove, "type", "^&ay", &mntent_remove.mnt_type) ||
          !g_variant_lookup (remove, "opts", "^&ay", &mntent_remove.mnt_opts) ||
          !g_variant_lookup (remove, "freq", "i", &mntent_remove.mnt_freq) ||
          !g_variant_lookup (remove, "passno", "i", &mntent_remove.mnt_passno))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Missing fsname, dir, type, opts, freq or passno parameter in entry to remove");
          goto out;
        }
    }

  if (add != NULL)
    {
      if (!g_variant_lookup (add, "fsname", "^&ay", &mntent_add.mnt_fsname) ||
          !g_variant_lookup (add, "dir", "^&ay", &mntent_add.mnt_dir) ||
          !g_variant_lookup (add, "type", "^&ay", &mntent_add.mnt_type) ||
          !g_variant_lookup (add, "opts", "^&ay", &mntent_add.mnt_opts) ||
          !g_variant_lookup (add, "freq", "i", &mntent_add.mnt_freq) ||
          !g_variant_lookup (add, "passno", "i", &mntent_add.mnt_passno))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Missing fsname, dir, type, opts, freq or passno parameter in entry to add");
          goto out;
        }
    }

  if (!g_file_get_contents ("/etc/fstab",
                            &contents,
                            NULL,
                            error))
    goto out;

  lines = g_strsplit (contents, "\n", 0);

  str = g_string_new (NULL);
  removed = FALSE;
  for (n = 0; lines != NULL && lines[n] != NULL; n++)
    {
      const gchar *line = lines[n];
      if (strlen (line) == 0 && lines[n+1] == NULL)
        break;
      if (remove != NULL && !removed)
        {
          gchar parsed_fsname[512];
          gchar parsed_dir[512];
          gchar parsed_type[512];
          gchar parsed_opts[512];
          gint parsed_freq;
          gint parsed_passno;
          if (sscanf (line, "%511s %511s %511s %511s %d %d",
                      parsed_fsname,
                      parsed_dir,
                      parsed_type,
                      parsed_opts,
                      &parsed_freq,
                      &parsed_passno) == 6)
            {
              gchar *unescaped_fsname = unescape_fstab (parsed_fsname);
              gchar *unescaped_dir = unescape_fstab (parsed_dir);
              gchar *unescaped_type = unescape_fstab (parsed_type);
              gchar *unescaped_opts = unescape_fstab (parsed_opts);
              gboolean matches = FALSE;
              if (g_strcmp0 (unescaped_fsname,   mntent_remove.mnt_fsname) == 0 &&
                  g_strcmp0 (unescaped_dir,      mntent_remove.mnt_dir) == 0 &&
                  g_strcmp0 (unescaped_type,     mntent_remove.mnt_type) == 0 &&
                  g_strcmp0 (unescaped_opts,     mntent_remove.mnt_opts) == 0 &&
                  parsed_freq ==      mntent_remove.mnt_freq &&
                  parsed_passno ==    mntent_remove.mnt_passno)
                {
                  matches = TRUE;
                }
              g_free (unescaped_fsname);
              g_free (unescaped_dir);
              g_free (unescaped_type);
              g_free (unescaped_opts);
              if (matches)
                {
                  removed = TRUE;
                  continue;
                }
            }
        }
      g_string_append (str, line);
      g_string_append_c (str, '\n');
    }

  if (remove != NULL && !removed)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Didn't find entry to remove");
      goto out;
    }

  if (add != NULL)
    {
      gchar *escaped_fsname = escape_fstab (mntent_add.mnt_fsname);
      gchar *escaped_dir = escape_fstab (mntent_add.mnt_dir);
      gchar *escaped_type = escape_fstab (mntent_add.mnt_type);
      gchar *escaped_opts = escape_fstab (mntent_add.mnt_opts);
      g_string_append_printf (str, "%s %s %s %s %d %d\n",
                              escaped_fsname,
                              escaped_dir,
                              escaped_type,
                              escaped_opts,
                              mntent_add.mnt_freq,
                              mntent_add.mnt_passno);
      g_free (escaped_fsname);
      g_free (escaped_dir);
      g_free (escaped_type);
      g_free (escaped_opts);
    }

  if (!_g_file_set_contents_full ("/etc/fstab",
                                  str->str,
                                  -1,
                                  0644, /* mode to use if non-existant */
                                  error) != 0)
    goto out;

  ret = TRUE;

 out:
  g_strfreev (lines);
  g_free (contents);
  if (str != NULL)
    g_string_free (str, TRUE);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
has_whitespace (const gchar *s)
{
  guint n;
  g_return_val_if_fail (s != NULL, TRUE);
  for (n = 0; s[n] != '\0'; n++)
    if (g_ascii_isspace (s[n]))
      return TRUE;
  return FALSE;
}

static gboolean
add_remove_crypttab_entry (GVariant  *remove,
                           GVariant  *add,
                           GError   **error)
{
  const gchar *remove_name = NULL;
  const gchar *remove_device = NULL;
  const gchar *remove_passphrase_path = NULL;
  const gchar *remove_options = NULL;
  const gchar *add_name = NULL;
  const gchar *add_device = NULL;
  const gchar *add_passphrase_path = NULL;
  const gchar *add_options = NULL;
  const gchar *add_passphrase_contents = NULL;
  gboolean ret;
  gchar *contents;
  gchar **lines;
  GString *str;
  gboolean removed;
  guint n;

  contents = NULL;
  lines = NULL;
  str = NULL;
  ret = FALSE;

  if (remove != NULL)
    {
      if (!g_variant_lookup (remove, "name", "^&ay", &remove_name) ||
          !g_variant_lookup (remove, "device", "^&ay", &remove_device) ||
          !g_variant_lookup (remove, "passphrase-path", "^&ay", &remove_passphrase_path) ||
          !g_variant_lookup (remove, "options", "^&ay", &remove_options))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Missing name, device, passphrase-path, options or parameter in entry to remove");
          goto out;
        }
    }

  if (add != NULL)
    {
      if (!g_variant_lookup (add, "name", "^&ay", &add_name) ||
          !g_variant_lookup (add, "device", "^&ay", &add_device) ||
          !g_variant_lookup (add, "passphrase-path", "^&ay", &add_passphrase_path) ||
          !g_variant_lookup (add, "options", "^&ay", &add_options) ||
          !g_variant_lookup (add, "passphrase-contents", "^&ay", &add_passphrase_contents))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "Missing name, device, passphrase-path, options or passphrase-contents parameter in entry to add");
          goto out;
        }

      /* reject strings with whitespace in them */
      if (has_whitespace (add_name) ||
          has_whitespace (add_device) ||
          has_whitespace (add_passphrase_path) ||
          has_whitespace (add_options))
        {
          g_set_error (error,
                       UDISKS_ERROR,
                       UDISKS_ERROR_FAILED,
                       "One of name, device, passphrase-path or options parameter are invalid (whitespace)");
          goto out;
        }
    }

  if (!g_file_get_contents ("/etc/crypttab",
                            &contents,
                            NULL,
                            error))
    goto out;

  lines = g_strsplit (contents, "\n", 0);

  str = g_string_new (NULL);
  removed = FALSE;
  for (n = 0; lines != NULL && lines[n] != NULL; n++)
    {
      const gchar *line = lines[n];
      if (strlen (line) == 0 && lines[n+1] == NULL)
        break;
      if (remove != NULL && !removed)
        {
          gchar parsed_name[512];
          gchar parsed_device[512];
          gchar parsed_passphrase_path[512];
          gchar parsed_options[512];
          guint num_parsed;

          num_parsed = sscanf (line, "%511s %511s %511s %511s",
                               parsed_name, parsed_device, parsed_passphrase_path, parsed_options);
          if (num_parsed >= 2)
            {
              if (num_parsed < 3 || g_strcmp0 (parsed_passphrase_path, "none") == 0)
                strcpy (parsed_passphrase_path, "");
              if (num_parsed < 4)
                strcpy (parsed_options, "");
              if (g_strcmp0 (parsed_name,            remove_name) == 0 &&
                  g_strcmp0 (parsed_device,          remove_device) == 0 &&
                  g_strcmp0 (parsed_passphrase_path, remove_passphrase_path) == 0 &&
                  g_strcmp0 (parsed_options,         remove_options) == 0)
                {
                  /* Nuke passphrase file */
                  if (strlen (remove_passphrase_path) > 0 && !g_str_has_prefix (remove_passphrase_path, "/dev"))
                    {
                      /* Is this exploitable? No, 1. the user would have to control
                       * the /etc/crypttab file for us to delete it; and 2. editing the
                       * /etc/crypttab file requires a polkit authorization that can't
                       * be retained (e.g. the user is always asked for the password)..
                       */
                      if (unlink (remove_passphrase_path) != 0)
                        {
                          g_set_error (error,
                                       UDISKS_ERROR,
                                       UDISKS_ERROR_FAILED,
                                       "Error deleting file `%s' with passphrase",
                                       remove_passphrase_path);
                          goto out;
                        }
                    }
                  removed = TRUE;
                  continue;
                }
            }
        }
      g_string_append (str, line);
      g_string_append_c (str, '\n');
    }

  if (remove != NULL && !removed)
    {
      g_set_error (error,
                   UDISKS_ERROR,
                   UDISKS_ERROR_FAILED,
                   "Didn't find entry to remove");
      goto out;
    }

  if (add != NULL)
    {
      /* First write add_passphrase_content to add_passphrase_path,
       * if applicable..
       *
       * Is this exploitable? No, because editing the /etc/crypttab
       * file requires a polkit authorization that can't be retained
       * (e.g. the user is always asked for the password)...
       *
       * Just to be on the safe side we only allow writing into the
       * directory /etc/luks-keys if create a _new_ entry.
       */
      if (strlen (add_passphrase_path) > 0)
        {
          gchar *filename;
          if (g_strcmp0 (add_passphrase_path, remove_passphrase_path) == 0)
            {
              filename = g_strdup (add_passphrase_path);
            }
          else
            {
              if (!g_str_has_prefix (add_passphrase_path, "/etc/luks-keys/"))
                {
                  g_set_error (error,
                               UDISKS_ERROR,
                               UDISKS_ERROR_FAILED,
                               "Crypttab passphrase file can only be created in the /etc/luks-keys directory");
                  goto out;
                }
              /* ensure the directory exists */
              if (g_mkdir_with_parents ("/etc/luks-keys", 0700) != 0)
                {
                  g_set_error (error,
                               UDISKS_ERROR,
                               UDISKS_ERROR_FAILED,
                               "Error creating /etc/luks-keys directory: %m");
                  goto out;
                }
              /* avoid symlink attacks */
              filename = g_strdup_printf ("/etc/luks-keys/%s", strrchr (add_passphrase_path, '/') + 1);
            }

          /* Bail if the requested file already exists */
          if (g_file_test (filename, G_FILE_TEST_EXISTS))
            {
                  g_set_error (error,
                               UDISKS_ERROR,
                               UDISKS_ERROR_FAILED,
                               "Refusing to overwrite existing file %s",
                               filename);
                  g_free (filename);
                  goto out;
            }

          if (!_g_file_set_contents_full (filename,
                                          add_passphrase_contents,
                                          -1,
                                          0600, /* mode to use if non-existant */
                                          error))
            {
              g_free (filename);
              goto out;
            }
          g_free (filename);
        }
      g_string_append_printf (str, "%s %s %s %s\n",
                              add_name,
                              add_device,
                              strlen (add_passphrase_path) > 0 ? add_passphrase_path : "none",
                              add_options);
    }

  if (!_g_file_set_contents_full ("/etc/crypttab",
                                  str->str,
                                  -1,
                                  0600, /* mode to use if non-existant */
                                  error) != 0)
    goto out;

  ret = TRUE;

 out:
  g_strfreev (lines);
  g_free (contents);
  if (str != NULL)
    g_string_free (str, TRUE);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_add_configuration_item (UDisksBlockDevice     *block,
                           GDBusMethodInvocation *invocation,
                           GVariant              *item,
                           GVariant              *options,
                           gpointer               user_data)
{
  UDisksLinuxBlock *object = UDISKS_LINUX_BLOCK (user_data);
  const gchar *type;
  GVariant *details;
  GError *error;

  g_variant_get (item, "(&s@a{sv})", &type, &details);

  if (g_strcmp0 (type, "fstab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (object->daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        N_("Authentication is required to add an entry to the /etc/fstab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_fstab_entry (NULL, details, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_device_complete_add_configuration_item (block, invocation);
    }
  else if (g_strcmp0 (type, "crypttab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (object->daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        N_("Authentication is required to add an entry to the /etc/crypttab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_crypttab_entry (NULL, details, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_device_complete_add_configuration_item (block, invocation);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Only /etc/fstab or /etc/crypttab items can be added");
      goto out;
    }

 out:
  g_variant_unref (details);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static gboolean
on_remove_configuration_item (UDisksBlockDevice     *block,
                              GDBusMethodInvocation *invocation,
                              GVariant              *item,
                              GVariant              *options,
                              gpointer               user_data)
{
  UDisksLinuxBlock *object = UDISKS_LINUX_BLOCK (user_data);
  const gchar *type;
  GVariant *details;
  GError *error;

  g_variant_get (item, "(&s@a{sv})", &type, &details);

  if (g_strcmp0 (type, "fstab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (object->daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        N_("Authentication is required to remove an entry from /etc/fstab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_fstab_entry (details, NULL, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_device_complete_add_configuration_item (block, invocation);
    }
  else if (g_strcmp0 (type, "crypttab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (object->daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        N_("Authentication is required to remove an entry from the /etc/crypttab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_crypttab_entry (details, NULL, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_device_complete_add_configuration_item (block, invocation);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Only fstab or crypttab items can be removed");
      goto out;
    }

 out:
  g_variant_unref (details);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

static gboolean
on_update_configuration_item (UDisksBlockDevice     *block,
                              GDBusMethodInvocation *invocation,
                              GVariant              *old_item,
                              GVariant              *new_item,
                              GVariant              *options,
                              gpointer               user_data)
{
  UDisksLinuxBlock *object = UDISKS_LINUX_BLOCK (user_data);
  const gchar *old_type;
  const gchar *new_type;
  GVariant *old_details;
  GVariant *new_details;
  GError *error;

  g_variant_get (old_item, "(&s@a{sv})", &old_type, &old_details);
  g_variant_get (new_item, "(&s@a{sv})", &new_type, &new_details);
  if (g_strcmp0 (old_type, new_type) != 0)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "old and new item are not of the same type");
      goto out;
    }

  if (g_strcmp0 (old_type, "fstab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (object->daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        N_("Authentication is required to modify the /etc/fstab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_fstab_entry (old_details, new_details, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_device_complete_add_configuration_item (block, invocation);
    }
  else if (g_strcmp0 (old_type, "crypttab") == 0)
    {
      if (!udisks_daemon_util_check_authorization_sync (object->daemon,
                                                        NULL,
                                                        "org.freedesktop.udisks2.modify-system-configuration",
                                                        options,
                                                        N_("Authentication is required to modify the /etc/crypttab file"),
                                                        invocation))
        goto out;
      error = NULL;
      if (!add_remove_crypttab_entry (old_details, new_details, &error))
        {
          g_dbus_method_invocation_take_error (invocation, error);
          goto out;
        }
      udisks_block_device_complete_add_configuration_item (block, invocation);
    }
  else
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Only fstab or crypttab items can be updated");
      goto out;
    }

 out:
  g_variant_unref (new_details);
  g_variant_unref (old_details);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.BlockDevice */

static gboolean
block_device_check (UDisksLinuxBlock *block)
{
  return TRUE;
}

static void
block_device_connect (UDisksLinuxBlock *block)
{
  g_signal_connect (block->iface_block_device,
                    "handle-get-secret-configuration",
                    G_CALLBACK (on_get_secret_configuration),
                    block);
  g_signal_connect (block->iface_block_device,
                    "handle-add-configuration-item",
                    G_CALLBACK (on_add_configuration_item),
                    block);
  g_signal_connect (block->iface_block_device,
                    "handle-remove-configuration-item",
                    G_CALLBACK (on_remove_configuration_item),
                    block);
  g_signal_connect (block->iface_block_device,
                    "handle-update-configuration-item",
                    G_CALLBACK (on_update_configuration_item),
                    block);
}

static gchar *
find_drive (GDBusObjectManagerServer  *object_manager,
            GUdevDevice               *block_device,
            UDisksDrive              **out_drive)
{
  GUdevDevice *whole_disk_block_device;
  const gchar *whole_disk_block_device_sysfs_path;
  gchar *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  if (g_strcmp0 (g_udev_device_get_devtype (block_device), "disk") == 0)
    whole_disk_block_device = g_object_ref (block_device);
  else
    whole_disk_block_device = g_udev_device_get_parent_with_subsystem (block_device, "block", "disk");
  whole_disk_block_device_sysfs_path = g_udev_device_get_sysfs_path (whole_disk_block_device);

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectSkeleton *object = G_DBUS_OBJECT_SKELETON (l->data);
      GList *drive_devices;
      GList *j;

      if (!UDISKS_IS_LINUX_DRIVE_OBJECT (object))
        continue;

      drive_devices = udisks_linux_drive_object_get_devices (UDISKS_LINUX_DRIVE_OBJECT (object));
      for (j = drive_devices; j != NULL; j = j->next)
        {
          GUdevDevice *drive_device = G_UDEV_DEVICE (j->data);
          const gchar *drive_sysfs_path;

          drive_sysfs_path = g_udev_device_get_sysfs_path (drive_device);
          if (g_strcmp0 (whole_disk_block_device_sysfs_path, drive_sysfs_path) == 0)
            {
              if (out_drive != NULL)
                *out_drive = udisks_object_get_drive (UDISKS_OBJECT (object));
              ret = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
              g_list_foreach (drive_devices, (GFunc) g_object_unref, NULL);
              g_list_free (drive_devices);
              goto out;
            }
        }
      g_list_foreach (drive_devices, (GFunc) g_object_unref, NULL);
      g_list_free (drive_devices);
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  g_object_unref (whole_disk_block_device);
  return ret;
}

static gchar *
find_block_device_by_sysfs_path (GDBusObjectManagerServer *object_manager,
                                 const gchar              *sysfs_path)
{
  gchar *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GDBusObjectSkeleton *object = G_DBUS_OBJECT_SKELETON (l->data);
      UDisksLinuxBlock *block;

      if (!UDISKS_IS_LINUX_BLOCK (object))
        continue;

      block = UDISKS_LINUX_BLOCK (object);

      if (g_strcmp0 (sysfs_path, g_udev_device_get_sysfs_path (block->device)) == 0)
        {
          ret = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
          goto out;
        }
    }

 out:
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);
  return ret;
}

static gchar *
get_sysfs_attr (GUdevDevice *device,
                const gchar *attr)
{
  gchar *filename;
  gchar *value;
  filename = g_strconcat (g_udev_device_get_sysfs_path (device),
                          "/",
                          attr,
                          NULL);
  value = NULL;
  /* don't care about errors */
  g_file_get_contents (filename,
                       &value,
                       NULL,
                       NULL);
  g_free (filename);
  return value;
}

static void
block_device_update_hints (UDisksLinuxBlock  *block,
                           const gchar       *uevent_action,
                           UDisksBlockDevice *iface,
                           const gchar       *device_file,
                           UDisksDrive       *drive)
{
  gboolean hint_system;
  gboolean hint_ignore;
  gboolean hint_auto;
  const gchar *hint_name;
  const gchar *hint_icon_name;

  /* very conservative defaults */
  hint_system = TRUE;
  hint_ignore = FALSE;
  hint_auto = FALSE;
  hint_name = NULL;
  hint_icon_name = NULL;

  /* Provide easy access to _only_ the following devices
   *
   *  - anything connected via known local buses (e.g. USB or Firewire, MMC or MemoryStick)
   *  - any device with removable media
   *
   * Be careful when extending this list as we don't want to automount
   * the world when (inadvertently) connecting to a SAN.
   */
  if (drive != NULL)
    {
      const gchar *connection_bus;
      gboolean removable;
      connection_bus = udisks_drive_get_connection_bus (drive);
      removable = udisks_drive_get_media_removable (drive);
      if (removable ||
          (g_strcmp0 (connection_bus, "usb") == 0 || g_strcmp0 (connection_bus, "firewire") == 0) ||
          (g_str_has_prefix (device_file, "/dev/mmcblk") || g_str_has_prefix (device_file, "/dev/mspblk")))
        {
          hint_system = FALSE;
          hint_auto = TRUE;
        }
    }

  /* TODO: set ignore to TRUE for physical paths belonging to a drive with multiple paths */

  /* override from udev properties */
  if (g_udev_device_has_property (block->device, "UDISKS_SYSTEM"))
    hint_system = g_udev_device_get_property_as_boolean (block->device, "UDISKS_SYSTEM");
  if (g_udev_device_has_property (block->device, "UDISKS_IGNORE"))
    hint_ignore = g_udev_device_get_property_as_boolean (block->device, "UDISKS_IGNORE");
  if (g_udev_device_has_property (block->device, "UDISKS_AUTO"))
    hint_auto = g_udev_device_get_property_as_boolean (block->device, "UDISKS_AUTO");
  if (g_udev_device_has_property (block->device, "UDISKS_NAME"))
    hint_name = g_udev_device_get_property (block->device, "UDISKS_NAME");
  if (g_udev_device_has_property (block->device, "UDISKS_ICON_NAME"))
    hint_icon_name = g_udev_device_get_property (block->device, "UDISKS_ICON_NAME");

  /* ... and scene! */
  udisks_block_device_set_hint_system (iface, hint_system);
  udisks_block_device_set_hint_ignore (iface, hint_ignore);
  udisks_block_device_set_hint_auto (iface, hint_auto);
  udisks_block_device_set_hint_name (iface, hint_name);
  udisks_block_device_set_hint_icon_name (iface, hint_icon_name);
}

static GList *
find_fstab_entries_for_device (UDisksLinuxBlock *block)
{
  GList *entries;
  GList *l;
  GList *ret;

  ret = NULL;

  /* if this is too slow, we could add lookup methods to UDisksFstabMonitor... */
  entries = udisks_fstab_monitor_get_entries (udisks_daemon_get_fstab_monitor (block->daemon));
  for (l = entries; l != NULL; l = l->next)
    {
      UDisksFstabEntry *entry = UDISKS_FSTAB_ENTRY (l->data);
      const gchar *const *symlinks;
      const gchar *fsname;
      gchar *device;
      guint n;

      fsname = udisks_fstab_entry_get_fsname (entry);
      device = NULL;
      if (g_str_has_prefix (fsname, "UUID="))
        {
          device = g_strdup_printf ("/dev/disk/by-uuid/%s", fsname + 5);
        }
      else if (g_str_has_prefix (fsname, "LABEL="))
        {
          device = g_strdup_printf ("/dev/disk/by-label/%s", fsname + 6);
        }
      else if (g_str_has_prefix (fsname, "/dev"))
        {
          device = g_strdup (fsname);
        }
      else
        {
          /* ignore non-device entries */
          goto continue_loop;
        }

      if (g_strcmp0 (device, udisks_block_device_get_device (block->iface_block_device)) == 0)
        {
          ret = g_list_prepend (ret, g_object_ref (entry));
        }
      else
        {
          symlinks = udisks_block_device_get_symlinks (block->iface_block_device);
          if (symlinks != NULL)
            {
              for (n = 0; symlinks[n] != NULL; n++)
                {
                  if (g_strcmp0 (device, symlinks[n]) == 0)
                    {
                      ret = g_list_prepend (ret, g_object_ref (entry));
                    }
                }
            }
        }

    continue_loop:
      g_free (device);
    }

  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
  g_list_free (entries);
  return ret;
}

static GList *
find_crypttab_entries_for_device (UDisksLinuxBlock *block)
{
  GList *entries;
  GList *l;
  GList *ret;

  ret = NULL;

  /* if this is too slow, we could add lookup methods to UDisksCrypttabMonitor... */
  entries = udisks_crypttab_monitor_get_entries (udisks_daemon_get_crypttab_monitor (block->daemon));
  for (l = entries; l != NULL; l = l->next)
    {
      UDisksCrypttabEntry *entry = UDISKS_CRYPTTAB_ENTRY (l->data);
      const gchar *const *symlinks;
      const gchar *device_in_entry;
      gchar *device;
      guint n;

      device_in_entry = udisks_crypttab_entry_get_device (entry);
      device = NULL;
      if (g_str_has_prefix (device_in_entry, "UUID="))
        {
          device = g_strdup_printf ("/dev/disk/by-uuid/%s", device_in_entry + 5);
        }
      else if (g_str_has_prefix (device_in_entry, "LABEL="))
        {
          device = g_strdup_printf ("/dev/disk/by-label/%s", device_in_entry + 6);
        }
      else if (g_str_has_prefix (device_in_entry, "/dev"))
        {
          device = g_strdup (device_in_entry);
        }
      else
        {
          /* ignore non-device entries */
          goto continue_loop;
        }

      if (g_strcmp0 (device, udisks_block_device_get_device (block->iface_block_device)) == 0)
        {
          ret = g_list_prepend (ret, g_object_ref (entry));
        }
      else
        {
          symlinks = udisks_block_device_get_symlinks (block->iface_block_device);
          if (symlinks != NULL)
            {
              for (n = 0; symlinks[n] != NULL; n++)
                {
                  if (g_strcmp0 (device, symlinks[n]) == 0)
                    {
                      ret = g_list_prepend (ret, g_object_ref (entry));
                    }
                }
            }
        }

    continue_loop:
      g_free (device);
    }

  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
  g_list_free (entries);
  return ret;
}

/* returns a floating GVariant */
static GVariant *
calculate_configuration (UDisksLinuxBlock  *block,
                         gboolean           include_secrets,
                         GError           **error)
{
  GList *entries;
  GList *l;
  GVariantBuilder builder;
  GVariant *ret;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sa{sv})"));
  /* First the /etc/fstab entries */
  entries = find_fstab_entries_for_device (block);
  for (l = entries; l != NULL; l = l->next)
    {
      UDisksFstabEntry *entry = UDISKS_FSTAB_ENTRY (l->data);
      GVariantBuilder dict_builder;
      g_variant_builder_init (&dict_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&dict_builder, "{sv}", "fsname",
                             g_variant_new_bytestring (udisks_fstab_entry_get_fsname (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "dir",
                             g_variant_new_bytestring (udisks_fstab_entry_get_dir (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "type",
                             g_variant_new_bytestring (udisks_fstab_entry_get_fstype (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "opts",
                             g_variant_new_bytestring (udisks_fstab_entry_get_opts (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "freq",
                             g_variant_new_int32 (udisks_fstab_entry_get_freq (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "passno",
                             g_variant_new_int32 (udisks_fstab_entry_get_passno (entry)));
      g_variant_builder_add (&builder,
                             "(sa{sv})",
                             "fstab", &dict_builder);
    }
  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
  g_list_free (entries);

  /* Then the /etc/crypttab entries */
  entries = find_crypttab_entries_for_device (block);
  for (l = entries; l != NULL; l = l->next)
    {
      UDisksCrypttabEntry *entry = UDISKS_CRYPTTAB_ENTRY (l->data);
      GVariantBuilder dict_builder;
      const gchar *passphrase_path;
      const gchar *options;
      gchar *passphrase_contents;
      gsize passphrase_contents_length;

      passphrase_path = udisks_crypttab_entry_get_passphrase_path (entry);
      if (passphrase_path == NULL || g_strcmp0 (passphrase_path, "none") == 0)
        passphrase_path = "";
      passphrase_contents = NULL;
      if (!(g_strcmp0 (passphrase_path, "") == 0 || g_str_has_prefix (passphrase_path, "/dev")))
        {
          if (include_secrets)
            {
              if (!g_file_get_contents (passphrase_path,
                                        &passphrase_contents,
                                        &passphrase_contents_length,
                                        error))
                {
                  g_prefix_error (error,
                                  "Error loading secrets from file `%s' referenced in /etc/crypttab entry: ",
                                  passphrase_path);
                  g_variant_builder_clear (&builder);
                  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
                  g_list_free (entries);
                  goto out;
                }
            }
        }

      options = udisks_crypttab_entry_get_options (entry);
      if (options == NULL)
        options = "";

      g_variant_builder_init (&dict_builder, G_VARIANT_TYPE_VARDICT);
      g_variant_builder_add (&dict_builder, "{sv}", "name",
                             g_variant_new_bytestring (udisks_crypttab_entry_get_name (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "device",
                             g_variant_new_bytestring (udisks_crypttab_entry_get_device (entry)));
      g_variant_builder_add (&dict_builder, "{sv}", "passphrase-path",
                             g_variant_new_bytestring (passphrase_path));
      if (passphrase_contents != NULL)
        {
          g_variant_builder_add (&dict_builder, "{sv}", "passphrase-contents",
                                 g_variant_new_bytestring (passphrase_contents));
        }
      g_variant_builder_add (&dict_builder, "{sv}", "options",
                             g_variant_new_bytestring (options));
      g_variant_builder_add (&builder,
                             "(sa{sv})",
                             "crypttab", &dict_builder);
      if (passphrase_contents != NULL)
        {
          memset (passphrase_contents, '\0', passphrase_contents_length);
          g_free (passphrase_contents);
        }
    }
  g_list_foreach (entries, (GFunc) g_object_unref, NULL);
  g_list_free (entries);

  ret = g_variant_builder_end (&builder);

 out:
  return ret;
}

static void
block_device_update_configuration (UDisksLinuxBlock  *block,
                                   const gchar       *uevent_action,
                                   UDisksBlockDevice *iface,
                                   const gchar       *device_file,
                                   UDisksDrive       *drive)
{
  GVariant *configuration;
  GError *error;

  error = NULL;
  configuration = calculate_configuration (block, FALSE, &error);
  if (configuration == NULL)
    {
      udisks_warning ("Error loading configuration: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      configuration = g_variant_new ("a(sa{sv})", NULL);
    }
  udisks_block_device_set_configuration (block->iface_block_device, configuration);
}

static void
block_device_update (UDisksLinuxBlock *block,
                     const gchar      *uevent_action,
                     GDBusInterface   *_iface)
{
  UDisksBlockDevice *iface = UDISKS_BLOCK_DEVICE (_iface);
  GUdevDeviceNumber dev;
  GDBusObjectManagerServer *object_manager;
  gchar *drive_object_path;
  UDisksDrive *drive;
  gchar *s;
  gboolean is_partition_table;
  gboolean is_partition_entry;
  const gchar *device_file;
  const gchar *const *symlinks;
  const gchar *preferred_device_file;

  drive = NULL;

  dev = g_udev_device_get_device_number (block->device);
  device_file = g_udev_device_get_device_file (block->device);
  symlinks = g_udev_device_get_device_file_symlinks (block->device);

  udisks_block_device_set_device (iface, device_file);
  udisks_block_device_set_symlinks (iface, symlinks);
  udisks_block_device_set_major (iface, major (dev));
  udisks_block_device_set_minor (iface, minor (dev));
  udisks_block_device_set_size (iface, udisks_daemon_util_block_get_size (block->device));

  /* dm-crypt
   *
   * TODO: this might not be the best way to determine if the device-mapper device
   *       is a dm-crypt device.. but unfortunately device-mapper keeps all this stuff
   *       in user-space and wants you to use libdevmapper to obtain it...
   */
  udisks_block_device_set_crypto_backing_device (iface, "/");
  if (g_str_has_prefix (g_udev_device_get_name (block->device), "dm-"))
    {
      gchar *dm_uuid;
      dm_uuid = get_sysfs_attr (block->device, "dm/uuid");
      if (dm_uuid != NULL && g_str_has_prefix (dm_uuid, "CRYPT-LUKS1"))
        {
          gchar **slaves;
          slaves = udisks_daemon_util_resolve_links (g_udev_device_get_sysfs_path (block->device),
                                                     "slaves");
          if (g_strv_length (slaves) == 1)
            {
              gchar *slave_object_path;
              slave_object_path = find_block_device_by_sysfs_path (udisks_daemon_get_object_manager (block->daemon),
                                                                   slaves[0]);
              if (slave_object_path != NULL)
                {
                  udisks_block_device_set_crypto_backing_device (iface, slave_object_path);
                }
              g_free (slave_object_path);
            }
          g_strfreev (slaves);
        }
      g_free (dm_uuid);
    }

  /* Sort out preferred device... this is what UI shells should
   * display. We default to the block device name.
   *
   * This is mostly for things like device-mapper where device file is
   * a name of the form dm-%d and a symlink name conveys more
   * information.
   */
  preferred_device_file = NULL;
  if (g_str_has_prefix (device_file, "/dev/dm-"))
    {
      guint n;
      const gchar *dm_name;
      gchar *dm_name_dev_file = NULL;
      const gchar *dm_name_dev_file_as_symlink = NULL;

      dm_name = g_udev_device_get_property (block->device, "DM_NAME");
      if (dm_name != NULL)
        dm_name_dev_file = g_strdup_printf ("/dev/mapper/%s", dm_name);
      for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
        {
          if (g_str_has_prefix (symlinks[n], "/dev/vg_"))
            {
              /* LVM2 */
              preferred_device_file = symlinks[n];
              break;
            }
          else if (g_strcmp0 (symlinks[n], dm_name_dev_file) == 0)
            {
              dm_name_dev_file_as_symlink = symlinks[n];
            }
        }
      /* fall back to /dev/mapper/$DM_NAME, if available as a symlink */
      if (preferred_device_file == NULL && dm_name_dev_file_as_symlink != NULL)
        preferred_device_file = dm_name_dev_file_as_symlink;
      g_free (dm_name_dev_file);
    }
  /* fallback to the device name */
  if (preferred_device_file == NULL)
    preferred_device_file = g_udev_device_get_device_file (block->device);
  udisks_block_device_set_preferred_device (iface, preferred_device_file);

  /* Determine the drive this block device belongs to
   *
   * TODO: if this is slow we could have a cache or ensure that we
   * only do this once or something else
   */
  object_manager = udisks_daemon_get_object_manager (block->daemon);
  drive_object_path = find_drive (object_manager, block->device, &drive);
  if (drive_object_path != NULL)
    {
      udisks_block_device_set_drive (iface, drive_object_path);
      g_free (drive_object_path);
    }
  else
    {
      udisks_block_device_set_drive (iface, "/");
    }

  udisks_block_device_set_id_usage (iface, g_udev_device_get_property (block->device, "ID_FS_USAGE"));
  udisks_block_device_set_id_type (iface, g_udev_device_get_property (block->device, "ID_FS_TYPE"));
  s = udisks_decode_udev_string (g_udev_device_get_property (block->device, "ID_FS_VERSION"));
  udisks_block_device_set_id_version (iface, s);
  g_free (s);
  s = udisks_decode_udev_string (g_udev_device_get_property (block->device, "ID_FS_LABEL_ENC"));
  udisks_block_device_set_id_label (iface, s);
  g_free (s);
  s = udisks_decode_udev_string (g_udev_device_get_property (block->device, "ID_FS_UUID_ENC"));
  udisks_block_device_set_id_uuid (iface, s);
  g_free (s);

  /* TODO: port this to blkid properties */

  /* Update the partition table and partition entry properties */
  is_partition_table = FALSE;
  is_partition_entry = FALSE;
  if (g_strcmp0 (g_udev_device_get_devtype (block->device), "partition") == 0 ||
      g_udev_device_get_property_as_boolean (block->device, "UDISKS_PARTITION"))
    {
      is_partition_entry = TRUE;
    }
  else if (g_udev_device_get_property_as_boolean (block->device, "UDISKS_PARTITION_TABLE"))
    {
      is_partition_table = TRUE;
    }

  /* partition table */
  if (is_partition_table)
    {
      udisks_block_device_set_part_table (iface, TRUE);
      udisks_block_device_set_part_table_scheme (iface,
                                                 g_udev_device_get_property (block->device,
                                                                             "UDISKS_PARTITION_TABLE_SCHEME"));
    }
  else
    {
      udisks_block_device_set_part_table (iface, FALSE);
      udisks_block_device_set_part_table_scheme (iface, "");
    }

  /* partition entry */
  if (is_partition_entry)
    {
      gchar *slave_sysfs_path;
      udisks_block_device_set_part_entry (iface, TRUE);
      udisks_block_device_set_part_entry_scheme (iface,
                                                 g_udev_device_get_property (block->device,
                                                                             "UDISKS_PARTITION_SCHEME"));
      udisks_block_device_set_part_entry_number (iface,
                                                 g_udev_device_get_property_as_int (block->device,
                                                                                    "UDISKS_PARTITION_NUMBER"));
      udisks_block_device_set_part_entry_type (iface,
                                               g_udev_device_get_property (block->device,
                                                                           "UDISKS_PARTITION_TYPE"));
      udisks_block_device_set_part_entry_flags (iface,
                                                g_udev_device_get_property (block->device,
                                                                            "UDISKS_PARTITION_FLAGS"));
      udisks_block_device_set_part_entry_label (iface,
                                                g_udev_device_get_property (block->device,
                                                                            "UDISKS_PARTITION_LABEL"));
      udisks_block_device_set_part_entry_uuid (iface,
                                               g_udev_device_get_property (block->device,
                                                                           "UDISKS_PARTITION_UUID"));
      slave_sysfs_path = g_strdup (g_udev_device_get_property (block->device, "UDISKS_PARTITION_SLAVE"));
      if (slave_sysfs_path == NULL)
        {
          if (g_strcmp0 (g_udev_device_get_devtype (block->device), "partition") == 0)
            {
              GUdevDevice *parent;
              parent = g_udev_device_get_parent (block->device);
              slave_sysfs_path = g_strdup (g_udev_device_get_sysfs_path (parent));
              g_object_unref (parent);
            }
          else
            {
              g_warning ("No UDISKS_PARTITION_SLAVE property and DEVTYPE is not partition for block device %s",
                         g_udev_device_get_sysfs_path (block->device));
            }
        }
      if (slave_sysfs_path != NULL)
        {
          gchar *slave_object_path;
          slave_object_path = find_block_device_by_sysfs_path (udisks_daemon_get_object_manager (block->daemon),
                                                               slave_sysfs_path);
          if (slave_object_path != NULL)
            udisks_block_device_set_part_entry_table (iface, slave_object_path);
          else
            udisks_block_device_set_part_entry_table (iface, "/");
          g_free (slave_object_path);
          g_free (slave_sysfs_path);
        }
      else
        {
          udisks_block_device_set_part_entry_table (iface, "/");
        }
      udisks_block_device_set_part_entry_offset (iface,
                                                 g_udev_device_get_property_as_uint64 (block->device,
                                                                                       "UDISKS_PARTITION_OFFSET"));
      udisks_block_device_set_part_entry_size (iface,
                                               g_udev_device_get_property_as_uint64 (block->device,
                                                                                     "UDISKS_PARTITION_SIZE"));
    }
  else
    {
      udisks_block_device_set_part_entry (iface, FALSE);
      udisks_block_device_set_part_entry_scheme (iface, "");
      udisks_block_device_set_part_entry_type (iface, "");
      udisks_block_device_set_part_entry_flags (iface, "");
      udisks_block_device_set_part_entry_table (iface, "/");
      udisks_block_device_set_part_entry_offset (iface, 0);
      udisks_block_device_set_part_entry_size (iface, 0);
    }

  block_device_update_hints (block, uevent_action, iface, device_file, drive);
  block_device_update_configuration (block, uevent_action, iface, device_file, drive);

  if (drive != NULL)
    g_object_unref (drive);
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.Filesystem */

static gboolean
filesystem_check (UDisksLinuxBlock *block)
{
  gboolean ret;
  UDisksMountType mount_type;

  ret = FALSE;
  if (g_strcmp0 (udisks_block_device_get_id_usage (block->iface_block_device), "filesystem") == 0 ||
      (udisks_mount_monitor_is_dev_in_use (block->mount_monitor,
                                           g_udev_device_get_device_number (block->device),
                                           &mount_type) &&
       mount_type == UDISKS_MOUNT_TYPE_FILESYSTEM))
    ret = TRUE;

  return ret;
}

static void
filesystem_update (UDisksLinuxBlock  *block,
                   const gchar       *uevent_action,
                   GDBusInterface    *_iface)
{
  UDisksFilesystem *iface = UDISKS_FILESYSTEM (_iface);
  GPtrArray *p;
  GList *mounts;
  GList *l;

  p = g_ptr_array_new ();
  mounts = udisks_mount_monitor_get_mounts_for_dev (block->mount_monitor,
                                                    g_udev_device_get_device_number (block->device));
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
  udisks_filesystem_set_mount_points (iface, (const gchar *const *) p->pdata);
  g_ptr_array_free (p, TRUE);
  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);
}

/* ---------------------------------------------------------------------------------------------------- */
/* org.freedesktop.UDisks.Swapspace */

static gboolean
swapspace_check (UDisksLinuxBlock *block)
{
  gboolean ret;
  UDisksMountType mount_type;

  ret = FALSE;
  if ((g_strcmp0 (udisks_block_device_get_id_usage (block->iface_block_device), "other") == 0 &&
       g_strcmp0 (udisks_block_device_get_id_type (block->iface_block_device), "swap") == 0)
      || (udisks_mount_monitor_is_dev_in_use (block->mount_monitor,
                                              g_udev_device_get_device_number (block->device),
                                              &mount_type)
          && mount_type == UDISKS_MOUNT_TYPE_SWAP))
    ret = TRUE;

  return ret;
}

static void
swapspace_connect (UDisksLinuxBlock *block)
{
  /* do nothing */
}

static void
swapspace_update (UDisksLinuxBlock  *block,
                  const gchar       *uevent_action,
                  GDBusInterface    *_iface)
{
  UDisksSwapspace *iface = UDISKS_SWAPSPACE (_iface);
  UDisksMountType mount_type;
  gboolean active;

  active = FALSE;
  if (udisks_mount_monitor_is_dev_in_use (block->mount_monitor,
                                          g_udev_device_get_device_number (block->device),
                                          &mount_type)
      && mount_type == UDISKS_MOUNT_TYPE_SWAP)
    active = TRUE;
  udisks_swapspace_set_active (iface, active);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
encrypted_check (UDisksLinuxBlock *block)
{
  gboolean ret;

  ret = FALSE;
  if (g_strcmp0 (udisks_block_device_get_id_usage (block->iface_block_device), "crypto") == 0 &&
      g_strcmp0 (udisks_block_device_get_id_type (block->iface_block_device), "crypto_LUKS") == 0)
    ret = TRUE;

  return ret;
}

static void
encrypted_connect (UDisksLinuxBlock *block)
{
  /* do nothing */
}

static void
encrypted_update (UDisksLinuxBlock  *block,
                  const gchar       *uevent_action,
                  GDBusInterface    *_iface)
{
  /* do nothing */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
loop_check (UDisksLinuxBlock *block)
{
  gboolean ret;

  ret = FALSE;
  if (g_str_has_prefix (g_udev_device_get_name (block->device), "loop"))
    ret = TRUE;

  return ret;
}

static void
loop_connect (UDisksLinuxBlock *block)
{
  /* do nothing */
}

static void
loop_update (UDisksLinuxBlock  *block,
             const gchar       *uevent_action,
             GDBusInterface    *_iface)
{
  UDisksLoop *iface = UDISKS_LOOP (_iface);

  if (g_str_has_prefix (g_udev_device_get_name (block->device), "loop"))
    {
      gchar *filename;
      gchar *backing_file;
      GError *error;
      filename = g_strconcat (g_udev_device_get_sysfs_path (block->device),
                              "/loop/backing_file",
                              NULL);
      error = NULL;
      if (!g_file_get_contents (filename,
                               &backing_file,
                               NULL,
                               &error))
        {
          /* ENOENT is not unexpected */
          if (!(error->domain == G_FILE_ERROR && error->code == G_FILE_ERROR_NOENT))
            {
              udisks_warning ("Error loading %s: %s (%s, %d)",
                              filename,
                              error->message,
                              g_quark_to_string (error->domain),
                              error->code);
            }
          g_error_free (error);
          udisks_loop_set_backing_file (iface, "");
        }
      else
        {
          /* TODO: validate UTF-8 */
          g_strstrip (backing_file);
          udisks_loop_set_backing_file (iface, backing_file);
          g_free (backing_file);
        }
      g_free (filename);
    }
  else
    {
      udisks_loop_set_backing_file (iface, "");
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_block_uevent:
 * @block: A #UDisksLinuxBlock.
 * @action: Uevent action or %NULL
 * @device: A new #GUdevDevice device object or %NULL if the device hasn't changed.
 *
 * Updates all information on interfaces on @block.
 */
void
udisks_linux_block_uevent (UDisksLinuxBlock *block,
                           const gchar      *action,
                           GUdevDevice      *device)
{
  g_return_if_fail (UDISKS_IS_LINUX_BLOCK (block));
  g_return_if_fail (device == NULL || G_UDEV_IS_DEVICE (device));

  if (device != NULL)
    {
      g_object_unref (block->device);
      block->device = g_object_ref (device);
      g_object_notify (G_OBJECT (block), "device");
    }

  update_iface (block, action, block_device_check, block_device_connect, block_device_update,
                UDISKS_TYPE_BLOCK_DEVICE_SKELETON, &block->iface_block_device);
  update_iface (block, action, filesystem_check, NULL, filesystem_update,
                UDISKS_TYPE_LINUX_FILESYSTEM, &block->iface_filesystem);
  update_iface (block, action, swapspace_check, swapspace_connect, swapspace_update,
                UDISKS_TYPE_LINUX_SWAPSPACE, &block->iface_swapspace);
  update_iface (block, action, encrypted_check, encrypted_connect, encrypted_update,
                UDISKS_TYPE_LINUX_ENCRYPTED, &block->iface_encrypted);
  update_iface (block, action, loop_check, loop_connect, loop_update,
                UDISKS_TYPE_LINUX_LOOP, &block->iface_loop);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_mount_monitor_mount_added (UDisksMountMonitor  *monitor,
                              UDisksMount         *mount,
                              gpointer             user_data)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (user_data);
  if (udisks_mount_get_dev (mount) == g_udev_device_get_device_number (block->device))
    udisks_linux_block_uevent (block, NULL, NULL);
}

static void
on_mount_monitor_mount_removed (UDisksMountMonitor  *monitor,
                                UDisksMount         *mount,
                                gpointer             user_data)
{
  UDisksLinuxBlock *block = UDISKS_LINUX_BLOCK (user_data);
  if (udisks_mount_get_dev (mount) == g_udev_device_get_device_number (block->device))
    udisks_linux_block_uevent (block, NULL, NULL);
}

/* ---------------------------------------------------------------------------------------------------- */
