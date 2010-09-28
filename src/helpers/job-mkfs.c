/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mount.h>

#include <glib.h>

#include "job-shared.h"

int
main (int argc,
      char **argv)
{
  int fd;
  int ret;
  int exit_status;
  GError *error;
  const char *fstype;
  const char *device;
  char *command_line;
  char *standard_error;
  char **options;
  GString *s;
  char *label;
  int n;
  gboolean is_kernel_partitioned;
  GIOChannel *stdin_channel;
  GPtrArray *options_array;
  char *option;
  gsize option_len;
  char *endp;
  uid_t take_ownership_uid;
  gid_t take_ownership_gid;

  ret = 1;
  command_line = NULL;
  standard_error = NULL;
  take_ownership_uid = 0;
  take_ownership_gid = 0;
  label = NULL;

  if (argc != 4)
    {
      g_printerr ("wrong usage\n");
      goto out;
    }
  fstype = argv[1];
  device = argv[2];
  is_kernel_partitioned = (strcmp (argv[3], "1") == 0);

  options_array = g_ptr_array_new ();
  stdin_channel = g_io_channel_unix_new (0);
  if (stdin_channel == NULL)
    {
      g_printerr ("cannot open stdin\n");
      goto out;
    }
  while (g_io_channel_read_line (stdin_channel, &option, &option_len, NULL, NULL) == G_IO_STATUS_NORMAL)
    {
      option[option_len - 1] = '\0';
      if (strlen (option) == 0)
        break;
      g_ptr_array_add (options_array, option);
    }
  g_io_channel_unref (stdin_channel);
  g_ptr_array_add (options_array, NULL);
  options = (char **) g_ptr_array_free (options_array, FALSE);

  if (strcmp (fstype, "vfat") == 0)
    {

      /* allow to create an fs on the main block device */
      s = g_string_new ("mkfs.vfat -I");
      for (n = 0; options[n] != NULL; n++)
        {
          if (g_str_has_prefix (options[n], "label="))
            {
              label = strdup (options[n] + sizeof("label=") - 1);
              if (!validate_and_escape_label (&label, 254))
                {
                  g_string_free (s, TRUE);
                  goto out;
                }
              if (strlen (label) <= 11)
                {
                  g_string_append_printf (s, " -n \"%s\"", label);
                  g_free (label);
                  label = NULL;
                }
            }
          else
            {
              g_printerr ("option %s not supported\n", options[n]);
              goto out;
            }
        }
      g_string_append_printf (s, " %s", device);
      command_line = g_string_free (s, FALSE);

    }
  else if (strcmp (fstype, "ext2") == 0 || strcmp (fstype, "ext3") == 0 || strcmp (fstype, "ext4") == 0)
    {

      s = g_string_new ("mkfs.");
      g_string_append (s, fstype);
      g_string_append (s, " -F ");
      for (n = 0; options[n] != NULL; n++)
        {
          if (g_str_has_prefix (options[n], "label="))
            {
              label = strdup (options[n] + sizeof("label=") - 1);
              if (!validate_and_escape_label (&label, 16))
                {
                  g_string_free (s, TRUE);
                  goto out;
                }
              g_string_append_printf (s, " -L \"%s\"", label);
              g_free (label);
              label = NULL;
            }
          else if (g_str_has_prefix (options[n], "take_ownership_uid="))
            {
              take_ownership_uid = strtol (options[n] + sizeof("take_ownership_uid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else if (g_str_has_prefix (options[n], "take_ownership_gid="))
            {
              take_ownership_gid = strtol (options[n] + sizeof("take_ownership_gid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else
            {
              g_printerr ("option %s not supported\n", options[n]);
              goto out;
            }
        }
      g_string_append_printf (s, " %s", device);
      command_line = g_string_free (s, FALSE);

    }
  else if (strcmp (fstype, "btrfs") == 0)
    {

      s = g_string_new ("mkfs.btrfs");
      for (n = 0; options[n] != NULL; n++)
        {
          if (g_str_has_prefix (options[n], "label="))
            {
              label = strdup (options[n] + sizeof("label=") - 1);
              if (!validate_and_escape_label (&label, 12))
                {
                  g_string_free (s, TRUE);
                  goto out;
                }
              g_string_append_printf (s, " -L \"%s\"", label);
              g_free (label);
              label = NULL;
            }
          else if (g_str_has_prefix (options[n], "take_ownership_uid="))
            {
              take_ownership_uid = strtol (options[n] + sizeof("take_ownership_uid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else if (g_str_has_prefix (options[n], "take_ownership_gid="))
            {
              take_ownership_gid = strtol (options[n] + sizeof("take_ownership_gid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else
            {
              g_printerr ("option %s not supported\n", options[n]);
              goto out;
            }
        }
      g_string_append_printf (s, " %s", device);
      command_line = g_string_free (s, FALSE);

    }
  else if (strcmp (fstype, "xfs") == 0)
    {

      s = g_string_new ("mkfs.xfs");
      for (n = 0; options[n] != NULL; n++)
        {
          if (g_str_has_prefix (options[n], "label="))
            {
              label = strdup (options[n] + sizeof("label=") - 1);
              if (!validate_and_escape_label (&label, 12))
                {
                  g_string_free (s, TRUE);
                  goto out;
                }
              g_string_append_printf (s, " -L \"%s\"", label);
              g_free (label);
              label = NULL;
            }
          else if (g_str_has_prefix (options[n], "take_ownership_uid="))
            {
              take_ownership_uid = strtol (options[n] + sizeof("take_ownership_uid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else if (g_str_has_prefix (options[n], "take_ownership_gid="))
            {
              take_ownership_gid = strtol (options[n] + sizeof("take_ownership_gid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else
            {
              g_printerr ("option %s not supported\n", options[n]);
              goto out;
            }
        }
      g_string_append_printf (s, " %s", device);
      command_line = g_string_free (s, FALSE);

    }
  else if (strcmp (fstype, "ntfs") == 0)
    {

      /* skip zeroing (we do that ourselves) and bad sector checking (will 
       * eventually be handled on a higher level)
       */
      s = g_string_new ("mkntfs -f -F");
      for (n = 0; options[n] != NULL; n++)
        {
          if (g_str_has_prefix (options[n], "label="))
            {
              label = strdup (options[n] + sizeof("label=") - 1);
              if (!validate_and_escape_label (&label, 255))
                {
                  g_string_free (s, TRUE);
                  goto out;
                }
              g_string_append_printf (s, " -L \"%s\"", label);
              g_free (label);
              label = NULL;
            }
          else
            {
              g_printerr ("option %s not supported\n", options[n]);
              goto out;
            }
        }
      g_string_append_printf (s, " %s", device);
      command_line = g_string_free (s, FALSE);

    }
  else if (strcmp (fstype, "reiserfs") == 0)
    {

      s = g_string_new ("mkfs.reiserfs -q");
      for (n = 0; options[n] != NULL; n++)
        {
          if (g_str_has_prefix (options[n], "label="))
            {
              label = strdup (options[n] + sizeof("label=") - 1);
              if (!validate_and_escape_label (&label, 16))
                {
                  g_string_free (s, TRUE);
                  goto out;
                }
              g_string_append_printf (s, " -l \"%s\"", label);
              g_free (label);
              label = NULL;
            }
          else if (g_str_has_prefix (options[n], "take_ownership_uid="))
            {
              take_ownership_uid = strtol (options[n] + sizeof("take_ownership_uid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else if (g_str_has_prefix (options[n], "take_ownership_gid="))
            {
              take_ownership_gid = strtol (options[n] + sizeof("take_ownership_gid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else
            {
              g_printerr ("option %s not supported\n", options[n]);
              goto out;
            }
        }
      g_string_append_printf (s, " %s", device);
      command_line = g_string_free (s, FALSE);

    }
  else if (strcmp (fstype, "minix") == 0)
    {

      s = g_string_new ("mkfs.minix");
      /* minix does not support labels */
      for (n = 0; options[n] != NULL; n++)
        {
          if (g_str_has_prefix (options[n], "take_ownership_uid="))
            {
              take_ownership_uid = strtol (options[n] + sizeof("take_ownership_uid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else if (g_str_has_prefix (options[n], "take_ownership_gid="))
            {
              take_ownership_gid = strtol (options[n] + sizeof("take_ownership_gid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else
            {
              g_printerr ("option %s not supported\n", options[n]);
              goto out;
            }
        }
      g_string_append_printf (s, " %s", device);
      command_line = g_string_free (s, FALSE);

    }
  else if (strcmp (fstype, "nilfs2") == 0)
    {

      s = g_string_new ("mkfs.nilfs2");
      for (n = 0; options[n] != NULL; n++)
        {
          if (g_str_has_prefix (options[n], "label="))
            {
              label = strdup (options[n] + sizeof("label=") - 1);
              if (!validate_and_escape_label (&label, 80))
                {
                  g_string_free (s, TRUE);
                  goto out;
                }
              g_string_append_printf (s, " -L \"%s\"", label);
              g_free (label);
              label = NULL;
            }
          else if (g_str_has_prefix (options[n], "take_ownership_uid="))
            {
              take_ownership_uid = strtol (options[n] + sizeof("take_ownership_uid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else if (g_str_has_prefix (options[n], "take_ownership_gid="))
            {
              take_ownership_gid = strtol (options[n] + sizeof("take_ownership_gid=") - 1, &endp, 10);
              if (endp == NULL || *endp != '\0')
                {
                  g_printerr ("option %s is malformed\n", options[n]);
                  goto out;
                }
            }
          else
            {
              g_printerr ("option %s not supported\n", options[n]);
              goto out;
            }
        }
      g_string_append_printf (s, " %s", device);
      command_line = g_string_free (s, FALSE);
    }
  else if (strcmp (fstype, "swap") == 0)
    {

      s = g_string_new ("mkswap");
      for (n = 0; options[n] != NULL; n++)
        {
          if (g_str_has_prefix (options[n], "label="))
            {
              label = strdup (options[n] + sizeof("label=") - 1);
              if (!validate_and_escape_label (&label, 15))
                {
                  g_string_free (s, TRUE);
                  goto out;
                }
              g_string_append_printf (s, " -L \"%s\"", label);
              g_free (label);
              label = NULL;
            }
          else
            {
              g_printerr ("option %s not supported\n", options[n]);
              goto out;
            }
        }
      g_string_append_printf (s, " %s", device);
      command_line = g_string_free (s, FALSE);

    }
  else if (strcmp (fstype, "empty") == 0)
    {
      command_line = NULL;
      for (n = 0; options[n] != NULL; n++)
        {
          g_printerr ("option %s not supported\n", options[n]);
          goto out;
        }
    }
  else
    {
      g_printerr ("fstype %s not supported\n", fstype);
      goto out;
    }

  /* scrub signatures */
  if (!scrub_signatures (device, 0, 0))
    goto out;

  if (command_line != NULL)
    {
      error = NULL;
      if (!g_spawn_command_line_sync (command_line, NULL, &standard_error, &exit_status, &error))
        {
          g_printerr ("cannot spawn '%s': %s\n", command_line, error->message);
          ret = 3; /* indicate FilesystemToolsMissing error */
          g_error_free (error);
          goto out;
        }
      if (WEXITSTATUS (exit_status) != 0)
        {
          g_printerr ("helper failed with:\n%s", standard_error);
          goto out;
        }
    }

  if (label != NULL)
    {
      g_free (command_line);

      if (strcmp (fstype, "vfat") == 0)
        {
          command_line = g_strdup_printf ("mlabel -i %s \"::%s\"", device, label);
        }
      else
        {
          g_printerr ("label change for fstype '%s' requested but not implemented", fstype);
          goto out;
        }

      error = NULL;
      if (!g_spawn_command_line_sync (command_line, NULL, &standard_error, &exit_status, &error))
        {
          g_printerr ("cannot spawn '%s': %s\n", command_line, error->message);
          g_error_free (error);
          ret = 3; /* indicate FilesystemToolsMissing error */
          goto out;
        }
      if (WEXITSTATUS (exit_status) != 0)
        {
          g_printerr ("helper failed with:\n%s", standard_error);
          goto out;
        }

      g_free (label);
    }

  /* If we've created an fs on a partitioned device, then signal the
   * kernel to reread the (now missing) partition table.
   */
  if (is_kernel_partitioned)
    {
      fd = open (device, O_RDONLY);
      if (fd < 0)
        {
          g_printerr ("cannot open %s (for BLKRRPART): %m\n", device);
          goto out;
        }
      if (ioctl (fd, BLKRRPART) != 0)
        {
          close (fd);
          g_printerr ("BLKRRPART ioctl failed for %s: %m\n", device);
          goto out;
        }
      close (fd);
    }

  /* take ownership of the device if requested */
  if (take_ownership_uid != 0 || take_ownership_gid != 0)
    {
      char dir[256] = PACKAGE_LOCALSTATE_DIR "/run/udisks/job-mkfs-XXXXXX";

      if (mkdtemp (dir) == NULL)
        {
          g_printerr ("cannot create directory %s: %m\n", dir);
          goto out;
        }

      if (mount (device, dir, fstype, 0, NULL) != 0)
        {
          g_printerr ("cannot mount %s at %s: %m\n", device, dir);
          ret = 2;
          goto tos_err0;
        }

      if (chown (dir, take_ownership_uid, take_ownership_gid) != 0)
        {
          g_printerr ("cannot chown %s to uid=%d and gid=%d: %m\n", dir, take_ownership_uid, take_ownership_gid);
          ret = 2;
        }

      if (chmod (dir, 0700) != 0)
        {
          g_printerr ("cannot chmod %s to mode 0700: %m\n", dir);
          ret = 2;
        }

      if (umount (dir) != 0)
        {
          g_printerr ("cannot unmount %s: %m\n", dir);
          ret = 2;
          goto tos_err0;
        }

    tos_err0:
      if (rmdir (dir) != 0)
        {
          g_printerr ("cannot remove directory %s: %m\n", dir);
          goto out;
        }
    }

  if (ret == 2)
    ret = 1;
  else
    ret = 0;

 out:
  g_free (standard_error);
  g_free (command_line);
  return ret;
}
