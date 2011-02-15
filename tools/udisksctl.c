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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <udisks/udisks.h>

#include <string.h>

#include <locale.h>

#include <polkit/polkit.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>

/* TODO: Temporary include */
#include <gdbusproxymanager.h>

static GDBusProxyManager *manager = NULL;
static GMainLoop *loop = NULL;

/* Uncomment to get debug traces in /tmp/udisksctl-completion-debug.txt - use tail(1) to
 * inspect this file
 */
/* #define COMPLETION_DEBUG */
//#define COMPLETION_DEBUG

/* ---------------------------------------------------------------------------------------------------- */

G_GNUC_UNUSED static void completion_debug (const gchar *format, ...);

static void remove_arg (gint num, gint *argc, gchar **argv[]);
static void modify_argv0_for_command (gint *argc, gchar **argv[], const gchar *command);


/* ---------------------------------------------------------------------------------------------------- */

static PolkitAgentListener *local_polkit_agent = NULL;
static gpointer local_agent_handle = NULL;

static gboolean
setup_local_polkit_agent (void)
{
  gboolean ret;
  GError *error;
  PolkitSubject *subject;

  ret = FALSE;
  subject = NULL;

  if (local_polkit_agent != NULL)
    goto out;

  subject = polkit_unix_process_new (getpid ());

  error = NULL;
  /* this will fail if we can't find a controlling terminal */
  local_polkit_agent = polkit_agent_text_listener_new (NULL, &error);
  if (local_polkit_agent == NULL)
    {
      g_printerr ("Error creating textual authentication agent: %s (%s, %d)\n",
                  error->message,
                  g_quark_to_string (error->domain),
                  error->code);
      g_error_free (error);
      goto out;
    }
  local_agent_handle = polkit_agent_listener_register (local_polkit_agent,
                                                       POLKIT_AGENT_REGISTER_FLAGS_RUN_IN_THREAD,
                                                       subject,
                                                       NULL, /* object_path */
                                                       NULL, /* GCancellable */
                                                       &error);
  if (local_agent_handle == NULL)
    {
      g_printerr ("Error registering local authentication agent: %s (%s, %d)\n",
                  error->message,
                  g_quark_to_string (error->domain),
                  error->code);
      g_error_free (error);
      goto out;
    }

  ret = TRUE;

 out:
  if (subject != NULL)
    g_object_unref (subject);
  return ret;
}

static void
shutdown_local_polkit_agent (void)
{
  if (local_agent_handle != NULL)
    polkit_agent_listener_unregister (local_agent_handle);
  if (local_polkit_agent != NULL)
    g_object_unref (local_polkit_agent);
}


/* ---------------------------------------------------------------------------------------------------- */

typedef enum
{
  _COLOR_RESET,
  _COLOR_BOLD_ON,
  _COLOR_INVERSE_ON,
  _COLOR_BOLD_OFF,
  _COLOR_FG_BLACK,
  _COLOR_FG_RED,
  _COLOR_FG_GREEN,
  _COLOR_FG_YELLOW,
  _COLOR_FG_BLUE,
  _COLOR_FG_MAGENTA,
  _COLOR_FG_CYAN,
  _COLOR_FG_WHITE,
  _COLOR_BG_RED,
  _COLOR_BG_GREEN,
  _COLOR_BG_YELLOW,
  _COLOR_BG_BLUE,
  _COLOR_BG_MAGENTA,
  _COLOR_BG_CYAN,
  _COLOR_BG_WHITE
} _Color;

static gboolean _color_stdin_is_tty = FALSE;
static gboolean _color_initialized = FALSE;
static FILE *_color_pager_out = NULL;

static void
_color_init (void)
{
  if (_color_initialized)
    return;
  _color_initialized = TRUE;
  _color_stdin_is_tty = (isatty (STDIN_FILENO) != 0 && isatty (STDOUT_FILENO) != 0);
}

static void
_color_shutdown (void)
{
  if (!_color_initialized)
    return;
  _color_initialized = FALSE;
  if (_color_pager_out != NULL)
    pclose (_color_pager_out);
}

static const gchar *
_color_get (_Color color)
{
  const gchar *str;

  _color_init ();

  if (!_color_stdin_is_tty)
    return "";

  str = NULL;
  switch (color)
    {
    case _COLOR_RESET:      str="\x1b[0m"; break;
    case _COLOR_BOLD_ON:    str="\x1b[1m"; break;
    case _COLOR_INVERSE_ON: str="\x1b[7m"; break;
    case _COLOR_BOLD_OFF:   str="\x1b[22m"; break;
    case _COLOR_FG_BLACK:   str="\x1b[30m"; break;
    case _COLOR_FG_RED:     str="\x1b[31m"; break;
    case _COLOR_FG_GREEN:   str="\x1b[32m"; break;
    case _COLOR_FG_YELLOW:  str="\x1b[33m"; break;
    case _COLOR_FG_BLUE:    str="\x1b[34m"; break;
    case _COLOR_FG_MAGENTA: str="\x1b[35m"; break;
    case _COLOR_FG_CYAN:    str="\x1b[36m"; break;
    case _COLOR_FG_WHITE:   str="\x1b[37m"; break;
    case _COLOR_BG_RED:     str="\x1b[41m"; break;
    case _COLOR_BG_GREEN:   str="\x1b[42m"; break;
    case _COLOR_BG_YELLOW:  str="\x1b[43m"; break;
    case _COLOR_BG_BLUE:    str="\x1b[44m"; break;
    case _COLOR_BG_MAGENTA: str="\x1b[45m"; break;
    case _COLOR_BG_CYAN:    str="\x1b[46m"; break;
    case _COLOR_BG_WHITE:   str="\x1b[47m"; break;
    default:
      g_assert_not_reached ();
      break;
    }
  return str;
}

static void
_color_run_pager (void)
{
  const gchar *pager_program;

  _color_init ();
  if (!_color_stdin_is_tty)
    goto out;

  pager_program = g_getenv ("PAGER");
  if (pager_program == NULL)
    pager_program = "less -R";

  _color_pager_out = popen (pager_program, "w");
  if (_color_pager_out == NULL)
    {
      g_printerr ("Error spawning pager `%s': %m\n", pager_program);
    }
  else
    {
      fclose (stdout);
      stdout = _color_pager_out;
    }

 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
variant_to_string_with_indent (GVariant *value,
                               guint     indent)
{
  gchar *value_str;

  if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
    {
      value_str = g_variant_dup_string (value, NULL);
    }
  else if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTESTRING))
    {
      value_str = g_variant_dup_bytestring (value, NULL);
    }
  else if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING_ARRAY) ||
           g_variant_is_of_type (value, G_VARIANT_TYPE_BYTESTRING_ARRAY))
    {
      const gchar **strv;
      guint m;
      GString *str;
      if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTESTRING_ARRAY))
        strv = g_variant_get_bytestring_array (value, NULL);
      else
        strv = g_variant_get_strv (value, NULL);
      str = g_string_new (NULL);
      for (m = 0; strv != NULL && strv[m] != NULL; m++)
        {
          if (m > 0)
            g_string_append_printf (str, "\n%*s",
                                    indent, "");
          g_string_append (str, strv[m]);
        }
      value_str = g_string_free (str, FALSE);
      g_free (strv);
    }
  else
    {
      value_str = g_variant_print (value, FALSE);
    }
  return value_str;
}

static gint
if_proxy_cmp (GDBusProxy *a,
              GDBusProxy *b)
{
  return g_strcmp0 (g_dbus_proxy_get_interface_name (a), g_dbus_proxy_get_interface_name (b));
}

static void
print_interface_properties (GDBusProxy *proxy,
                            guint       indent)
{
  gchar **cached_properties;
  guint n;
  guint value_column;
  guint max_property_name_len;

  /* note: this is guaranteed to be sorted */
  cached_properties = g_dbus_proxy_get_cached_property_names (proxy);

  max_property_name_len = 0;
  for (n = 0; cached_properties != NULL && cached_properties[n] != NULL; n++)
    {
      const gchar *property_name = cached_properties[n];
      guint property_name_len;
      property_name_len = strlen (property_name);
      if (max_property_name_len < property_name_len)
        max_property_name_len = property_name_len;
    }

  value_column = ((max_property_name_len + 7) / 8) * 8 + 8;
  if (value_column < 24)
    value_column = 24;
  else if (value_column > 64)
    value_column = 64;

  for (n = 0; cached_properties != NULL && cached_properties[n] != NULL; n++)
    {
      const gchar *property_name = cached_properties[n];
      GVariant *value;
      gchar *value_str;
      guint rightmost;
      gint value_indent;

      rightmost = indent + strlen (property_name) + 2;
      value_indent = value_column - rightmost;
      if (value_indent < 0)
        value_indent = 0;

      value = g_dbus_proxy_get_cached_property (proxy, property_name);

      value_str = variant_to_string_with_indent (value, indent + strlen (property_name) + 2 + value_indent);

      g_print ("%*s%s%s:%s %*s%s\n",
               indent, "",
               _color_get (_COLOR_FG_WHITE), property_name, _color_get (_COLOR_RESET),
               value_indent, "",
               value_str);

      g_free (value_str);
      g_variant_unref (value);
    }
  g_strfreev (cached_properties);
}

static void
print_object (GDBusObjectProxy *proxy,
              guint             indent)
{
  GList *interface_proxies;
  GList *l;

  g_return_if_fail (G_IS_DBUS_OBJECT_PROXY (proxy));

  interface_proxies = g_dbus_object_proxy_get_all (proxy);

  /* We want to print the interfaces in order */
  interface_proxies = g_list_sort (interface_proxies, (GCompareFunc) if_proxy_cmp);

  for (l = interface_proxies; l != NULL; l = l->next)
    {
      GDBusProxy *iproxy = G_DBUS_PROXY (l->data);
      g_print ("%*s%s%s%s:%s\n",
               indent, "",
               _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_MAGENTA), g_dbus_proxy_get_interface_name (iproxy), _color_get (_COLOR_RESET));
      print_interface_properties (iproxy, indent + 2);
    }
  g_list_foreach (interface_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (interface_proxies);
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusObjectProxy *
lookup_object_proxy_by_path (const gchar *path)
{
  GDBusObjectProxy *ret;
  gchar *s;

  s = g_strdup_printf ("/org/freedesktop/UDisks/%s", path);
  ret = g_dbus_proxy_manager_lookup (manager, s);
  g_free (s);

  return ret;
}

static GDBusObjectProxy *
lookup_object_proxy_by_device (const gchar *device)
{
  GDBusObjectProxy *ret;
  GList *object_proxies;
  GList *l;

  ret = NULL;

  object_proxies = g_dbus_proxy_manager_get_all (manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksBlockDevice *block;

      block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
      if (block != NULL)
        {
          const gchar * const *symlinks;
          guint n;

          if (g_strcmp0 (udisks_block_device_get_device (block), device) == 0)
            {
              ret = g_object_ref (object_proxy);
              goto out;
            }

          symlinks = udisks_block_device_get_symlinks (block);
          for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
            {
              if (g_strcmp0 (symlinks[n], device) == 0)
                {
                  ret = g_object_ref (object_proxy);
                  goto out;
                }
            }
        }
    }

 out:
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);

  return ret;
}

static GDBusObjectProxy *
lookup_object_proxy_by_drive (const gchar *drive)
{
  GDBusObjectProxy *ret;
  GList *object_proxies;
  GList *l;
  gchar *full_drive_object_path;

  ret = NULL;

  full_drive_object_path = g_strdup_printf ("/org/freedesktop/UDisks/drives/%s", drive);

  object_proxies = g_dbus_proxy_manager_get_all (manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksDrive *drive;

      if (g_strcmp0 (g_dbus_object_proxy_get_object_path (object_proxy), full_drive_object_path) != 0)
        continue;

      drive = UDISKS_PEEK_DRIVE (object_proxy);
      if (drive != NULL)
        {
          ret = g_object_ref (object_proxy);
          goto out;
        }
    }

 out:
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  g_free (full_drive_object_path);

  return ret;
}

static GDBusObjectProxy *
lookup_object_proxy_by_controller (const gchar *controller)
{
  GDBusObjectProxy *ret;
  GList *object_proxies;
  GList *l;
  gchar *full_controller_object_path;

  ret = NULL;

  full_controller_object_path = g_strdup_printf ("/org/freedesktop/UDisks/controllers/%s", controller);

  object_proxies = g_dbus_proxy_manager_get_all (manager);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksController *controller;

      if (g_strcmp0 (g_dbus_object_proxy_get_object_path (object_proxy), full_controller_object_path) != 0)
        continue;

      controller = UDISKS_PEEK_CONTROLLER (object_proxy);
      if (controller != NULL)
        {
          ret = g_object_ref (object_proxy);
          goto out;
        }
    }

 out:
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);
  g_free (full_controller_object_path);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar  *opt_mount_unmount_object_path = NULL;
static gchar  *opt_mount_unmount_device = NULL;
static gchar **opt_mount_unmount_options = NULL;
static gchar  *opt_mount_filesystem_type = NULL;

static const GOptionEntry command_mount_entries[] =
{
  {
    "object-path",
    'p',
    0,
    G_OPTION_ARG_STRING,
    &opt_mount_unmount_object_path,
    "Object to mount",
    NULL
  },
  {
    "block-device",
    'b',
    0,
    G_OPTION_ARG_STRING,
    &opt_mount_unmount_device,
    "Block device to mount",
    NULL
  },
  {
    "filesystem-type",
    't',
    0,
    G_OPTION_ARG_STRING,
    &opt_mount_filesystem_type,
    "Filesystem type to use",
    NULL
  },
  {
    "option",
    'o',
    0,
    G_OPTION_ARG_STRING_ARRAY,
    &opt_mount_unmount_options,
    "Mount option (can be used several times)",
    NULL
  },
  {
    NULL
  }
};

static const GOptionEntry command_unmount_entries[] =
{
  {
    "object-path",
    'p',
    0,
    G_OPTION_ARG_STRING,
    &opt_mount_unmount_object_path,
    "Object to unmount",
    NULL
  },
  {
    "block-device",
    'b',
    0,
    G_OPTION_ARG_STRING,
    &opt_mount_unmount_device,
    "Block device to unmount",
    NULL
  },
  {
    "option",
    'o',
    0,
    G_OPTION_ARG_STRING_ARRAY,
    &opt_mount_unmount_options,
    "Unmount option (can be used several times)",
    NULL
  },
  {
    NULL
  }
};

/* TODO: make 'mount' and 'unmount' take options? Probably... */

static gint
handle_command_mount_unmount (gint        *argc,
                              gchar      **argv[],
                              gboolean     request_completion,
                              const gchar *completion_cur,
                              const gchar *completion_prev,
                              gboolean     is_mount)
{
  gint ret;
  GOptionContext *o;
  gchar *s;
  gboolean complete_objects;
  gboolean complete_devices;
  GList *l;
  GList *object_proxies;
  GDBusObjectProxy *object_proxy;
  UDisksBlockDevice *block;
  UDisksFilesystem *filesystem;
  guint n;

  ret = 1;
  opt_mount_unmount_object_path = NULL;
  opt_mount_unmount_device = NULL;
  opt_mount_unmount_options = NULL;
  opt_mount_filesystem_type = NULL;
  object_proxy = NULL;

  if (is_mount)
    modify_argv0_for_command (argc, argv, "mount");
  else
    modify_argv0_for_command (argc, argv, "unmount");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  if (is_mount)
    g_option_context_set_summary (o, "Mount a device.");
  else
    g_option_context_set_summary (o, "Unmount a device.");
  g_option_context_add_main_entries (o,
                                     is_mount ? command_mount_entries : command_unmount_entries,
                                     NULL /* GETTEXT_PACKAGE*/);

  complete_objects = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--object-path") == 0 || g_strcmp0 (completion_prev, "-p") == 0))
    {
      complete_objects = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_devices = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--block-device") == 0 || g_strcmp0 (completion_prev, "-b") == 0))
    {
      complete_devices = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  if (!g_option_context_parse (o, argc, argv, NULL))
    {
      if (!request_completion)
        {
          s = g_option_context_get_help (o, FALSE, NULL);
          g_printerr ("%s", s);
          g_free (s);
          goto out;
        }
    }

  if (request_completion &&
      (opt_mount_unmount_object_path == NULL && !complete_objects) &&
      (opt_mount_unmount_device == NULL && !complete_devices))
    {
      g_print ("--object-path \n"
               "--block-device \n");
    }

  if (complete_objects)
    {
      const gchar *object_path;

      object_proxies = g_dbus_proxy_manager_get_all (manager);
      for (l = object_proxies; l != NULL; l = l->next)
        {
          const gchar * const *mount_points;
          gboolean is_mounted;

          object_proxy = G_DBUS_OBJECT_PROXY (l->data);
          filesystem = UDISKS_PEEK_FILESYSTEM (object_proxy);

          if (filesystem == NULL)
            continue;

          is_mounted = FALSE;
          mount_points = udisks_filesystem_get_mount_points (filesystem);
          if (mount_points != NULL && g_strv_length ((gchar **) mount_points) > 0)
            is_mounted = TRUE;

          if ((is_mount && !is_mounted) || (!is_mount && is_mounted))
            {
              object_path = g_dbus_object_proxy_get_object_path (object_proxy);
              g_assert (g_str_has_prefix (object_path, "/org/freedesktop/UDisks/"));
              g_print ("%s \n", object_path + sizeof ("/org/freedesktop/UDisks/") - 1);
            }
        }
      g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
      g_list_free (object_proxies);
      goto out;
    }

  if (complete_devices)
    {
      object_proxies = g_dbus_proxy_manager_get_all (manager);
      for (l = object_proxies; l != NULL; l = l->next)
        {
          object_proxy = G_DBUS_OBJECT_PROXY (l->data);
          block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
          filesystem = UDISKS_PEEK_FILESYSTEM (object_proxy);

          if (block != NULL && filesystem != NULL)
            {
              const gchar * const *mount_points;
              gboolean is_mounted;

              is_mounted = FALSE;
              mount_points = udisks_filesystem_get_mount_points (filesystem);
              if (mount_points != NULL && g_strv_length ((gchar **) mount_points) > 0)
                is_mounted = TRUE;

              if ((is_mount && !is_mounted) || (!is_mount && is_mounted))
                {
                  const gchar * const *symlinks;
                  g_print ("%s \n", udisks_block_device_get_device (block));
                  symlinks = udisks_block_device_get_symlinks (block);
                  for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
                    g_print ("%s \n", symlinks[n]);
                }
            }
        }
      g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
      g_list_free (object_proxies);
      goto out;
    }

  /* done with completion */
  if (request_completion)
    goto out;

  if (opt_mount_unmount_object_path != NULL)
    {
      object_proxy = lookup_object_proxy_by_path (opt_mount_unmount_object_path);
      if (object_proxy == NULL)
        {
          g_printerr ("Error looking up object with path %s\n", opt_mount_unmount_object_path);
          goto out;
        }
    }
  else if (opt_mount_unmount_device != NULL)
    {
      object_proxy = lookup_object_proxy_by_device (opt_mount_unmount_device);
      if (object_proxy == NULL)
        {
          g_printerr ("Error looking up object for device %s\n", opt_mount_unmount_device);
          goto out;
        }
    }
  else
    {
      s = g_option_context_get_help (o, FALSE, NULL);
      g_printerr ("%s", s);
      g_free (s);
      goto out;
    }

  block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
  if (block == NULL)
    {
      g_printerr ("Object %s is not a block device.\n", g_dbus_object_proxy_get_object_path (object_proxy));
      g_object_unref (object_proxy);
      goto out;
    }

  filesystem = UDISKS_PEEK_FILESYSTEM (object_proxy);
  if (filesystem == NULL)
    {
      g_printerr ("Device %s is not a filesystem.\n", udisks_block_device_get_device (block));
      g_object_unref (object_proxy);
      goto out;
    }

  if (opt_mount_filesystem_type == NULL)
    opt_mount_filesystem_type = g_strdup ("");
  if (opt_mount_unmount_options == NULL)
    opt_mount_unmount_options = g_new0 (gchar *, 1);

 try_again:
  if (is_mount)
    {
      GError *error;
      gchar *mount_path;

      error = NULL;
      if (!udisks_filesystem_call_mount_sync (filesystem,
                                              opt_mount_filesystem_type,
                                              (const gchar *const *) opt_mount_unmount_options,
                                              &mount_path,
                                              NULL,                       /* GCancellable */
                                              &error))
        {
          if (error->domain == UDISKS_ERROR &&
              error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
              setup_local_polkit_agent ())
            {
              g_error_free (error);
              goto try_again;
            }
          g_printerr ("Error mounting %s: %s\n",
                      udisks_block_device_get_device (block),
                      error->message);
          g_error_free (error);
          g_object_unref (object_proxy);
          goto out;
        }
      g_print ("Mounted %s at %s.\n",
               udisks_block_device_get_device (block),
               mount_path);
      g_free (mount_path);
    }
  else
    {
      GError *error;

      error = NULL;
      if (!udisks_filesystem_call_unmount_sync (filesystem,
                                                (const gchar *const *) opt_mount_unmount_options,
                                                NULL,         /* GCancellable */
                                                &error))
        {
          if (error->domain == UDISKS_ERROR &&
              error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
              setup_local_polkit_agent ())
            {
              g_error_free (error);
              goto try_again;
            }
          g_printerr ("Error unmounting %s: %s\n",
                      udisks_block_device_get_device (block),
                      error->message);
          g_error_free (error);
          g_object_unref (object_proxy);
          goto out;
        }
      g_print ("Unmounted %s.\n",
               udisks_block_device_get_device (block));
    }

  ret = 0;

  g_object_unref (object_proxy);

 out:
  g_option_context_free (o);
  g_free (opt_mount_unmount_object_path);
  g_free (opt_mount_unmount_device);
  g_strfreev (opt_mount_unmount_options);
  g_free (opt_mount_filesystem_type);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *opt_info_object = NULL;
static gchar *opt_info_device = NULL;
static gchar *opt_info_drive = NULL;
static gchar *opt_info_controller = NULL;

static const GOptionEntry command_info_entries[] =
{
  { "object-path", 'p', 0, G_OPTION_ARG_STRING, &opt_info_object, "Object to get information about", NULL},
  { "block-device", 'b', 0, G_OPTION_ARG_STRING, &opt_info_device, "Block device to get information about", NULL},
  { "drive", 'd', 0, G_OPTION_ARG_STRING, &opt_info_drive, "Drive to get information about", NULL},
  { "controller", 'c', 0, G_OPTION_ARG_STRING, &opt_info_controller, "Controller to get information about", NULL},
  { NULL }
};

static gint
handle_command_info (gint        *argc,
                     gchar      **argv[],
                     gboolean     request_completion,
                     const gchar *completion_cur,
                     const gchar *completion_prev)
{
  gint ret;
  GOptionContext *o;
  gchar *s;
  gboolean complete_objects;
  gboolean complete_devices;
  gboolean complete_drives;
  gboolean complete_controllers;
  GList *l;
  GList *object_proxies;
  GDBusObjectProxy *object_proxy;
  UDisksBlockDevice *block;
  UDisksDrive *drive;
  UDisksController *controller;
  guint n;

  ret = 1;
  opt_info_object = NULL;
  opt_info_device = NULL;
  opt_info_drive = NULL;
  opt_info_controller = NULL;

  modify_argv0_for_command (argc, argv, "info");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, "Show information about an object.");
  g_option_context_add_main_entries (o, command_info_entries, NULL /* GETTEXT_PACKAGE*/);

  complete_objects = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--object-path") == 0 || g_strcmp0 (completion_prev, "-p") == 0))
    {
      complete_objects = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_devices = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--block-device") == 0 || g_strcmp0 (completion_prev, "-b") == 0))
    {
      complete_devices = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_drives = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--drive") == 0 || g_strcmp0 (completion_prev, "-d") == 0))
    {
      complete_drives = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_controllers = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--controller") == 0 || g_strcmp0 (completion_prev, "-c") == 0))
    {
      complete_controllers = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  if (!g_option_context_parse (o, argc, argv, NULL))
    {
      if (!request_completion)
        {
          s = g_option_context_get_help (o, FALSE, NULL);
          g_printerr ("%s", s);
          g_free (s);
          goto out;
        }
    }

  if (request_completion &&
      (opt_info_object == NULL && !complete_objects) &&
      (opt_info_device == NULL && !complete_devices) &&
      (opt_info_drive == NULL && !complete_drives) &&
      (opt_info_controller == NULL && !complete_controllers))
    {
      g_print ("--object-path \n"
               "--block-device \n"
               "--drive \n"
               "--controller \n");
    }

  if (complete_objects)
    {
      const gchar *object_path;

      object_proxies = g_dbus_proxy_manager_get_all (manager);
      for (l = object_proxies; l != NULL; l = l->next)
        {
          object_proxy = G_DBUS_OBJECT_PROXY (l->data);

          object_path = g_dbus_object_proxy_get_object_path (object_proxy);
          g_assert (g_str_has_prefix (object_path, "/org/freedesktop/UDisks/"));
          g_print ("%s \n", object_path + sizeof ("/org/freedesktop/UDisks/") - 1);
        }
      g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
      g_list_free (object_proxies);
      goto out;
    }

  if (complete_devices)
    {
      object_proxies = g_dbus_proxy_manager_get_all (manager);
      for (l = object_proxies; l != NULL; l = l->next)
        {
          object_proxy = G_DBUS_OBJECT_PROXY (l->data);
          block = UDISKS_PEEK_BLOCK_DEVICE (object_proxy);
          if (block != NULL)
            {
              const gchar * const *symlinks;
              g_print ("%s \n", udisks_block_device_get_device (block));
              symlinks = udisks_block_device_get_symlinks (block);
              for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
                g_print ("%s \n", symlinks[n]);
            }
        }
      g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
      g_list_free (object_proxies);
      goto out;
    }

  if (complete_drives)
    {
      object_proxies = g_dbus_proxy_manager_get_all (manager);
      for (l = object_proxies; l != NULL; l = l->next)
        {
          object_proxy = G_DBUS_OBJECT_PROXY (l->data);
          drive = UDISKS_PEEK_DRIVE (object_proxy);
          if (drive != NULL)
            {
              const gchar *base;
              base = g_strrstr (g_dbus_object_proxy_get_object_path (object_proxy), "/") + 1;
              g_print ("%s \n", base);
            }
        }
      g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
      g_list_free (object_proxies);
      goto out;
    }

  if (complete_controllers)
    {
      object_proxies = g_dbus_proxy_manager_get_all (manager);
      for (l = object_proxies; l != NULL; l = l->next)
        {
          object_proxy = G_DBUS_OBJECT_PROXY (l->data);
          controller = UDISKS_PEEK_CONTROLLER (object_proxy);
          if (controller != NULL)
            {
              const gchar *base;
              base = g_strrstr (g_dbus_object_proxy_get_object_path (object_proxy), "/") + 1;
              g_print ("%s \n", base);
            }
        }
      g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
      g_list_free (object_proxies);
      goto out;
    }

  /* done with completion */
  if (request_completion)
    goto out;


  if (opt_info_object != NULL)
    {
      object_proxy = lookup_object_proxy_by_path (opt_info_object);
      if (object_proxy == NULL)
        {
          g_printerr ("Error looking up object with path %s\n", opt_info_object);
          goto out;
        }
    }
  else if (opt_info_device != NULL)
    {
      object_proxy = lookup_object_proxy_by_device (opt_info_device);
      if (object_proxy == NULL)
        {
          g_printerr ("Error looking up object for device %s\n", opt_info_device);
          goto out;
        }
    }
  else if (opt_info_drive != NULL)
    {
      object_proxy = lookup_object_proxy_by_drive (opt_info_drive);
      if (object_proxy == NULL)
        {
          g_printerr ("Error looking up object for drive %s\n", opt_info_drive);
          goto out;
        }
    }
  else if (opt_info_controller != NULL)
    {
      object_proxy = lookup_object_proxy_by_controller (opt_info_controller);
      if (object_proxy == NULL)
        {
          g_printerr ("Error looking up object for controller %s\n", opt_info_controller);
          goto out;
        }
    }
  else
    {
      s = g_option_context_get_help (o, FALSE, NULL);
      g_printerr ("%s", s);
      g_free (s);
      goto out;
    }

  g_print ("%s%s%s:%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE), g_dbus_object_proxy_get_object_path (object_proxy), _color_get (_COLOR_RESET));
  print_object (object_proxy, 2);
  g_object_unref (object_proxy);

  ret = 0;

 out:
  g_option_context_free (o);
  g_free (opt_info_object);
  g_free (opt_info_device);
  g_free (opt_info_drive);
  g_free (opt_info_controller);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */


static gint
obj_proxy_cmp (GDBusObjectProxy *a,
               GDBusObjectProxy *b)
{
  return g_strcmp0 (g_dbus_object_proxy_get_object_path (a), g_dbus_object_proxy_get_object_path (b));
}


static const GOptionEntry command_dump_entries[] =
{
  { NULL }
};

static gint
handle_command_dump (gint        *argc,
                     gchar      **argv[],
                     gboolean     request_completion,
                     const gchar *completion_cur,
                     const gchar *completion_prev)
{
  gint ret;
  GOptionContext *o;
  GList *l;
  GList *object_proxies;
  gchar *s;
  gboolean first;

  ret = 1;

  modify_argv0_for_command (argc, argv, "dump");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, "Show information about all objects.");
  g_option_context_add_main_entries (o, command_dump_entries, NULL /* GETTEXT_PACKAGE*/);

  if (!g_option_context_parse (o, argc, argv, NULL))
    {
      if (!request_completion)
        {
          s = g_option_context_get_help (o, FALSE, NULL);
          g_printerr ("%s", s);
          g_free (s);
          goto out;
        }
    }

  /* done with completion */
  if (request_completion)
    goto out;

  _color_run_pager ();

  object_proxies = g_dbus_proxy_manager_get_all (manager);
  /* We want to print the objects in order */
  object_proxies = g_list_sort (object_proxies, (GCompareFunc) obj_proxy_cmp);
  first = TRUE;
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      if (!first)
        g_print ("\n");
      first = FALSE;
      g_print ("%s%s%s:%s\n",
               _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE), g_dbus_object_proxy_get_object_path (object_proxy), _color_get (_COLOR_RESET));
      print_object (object_proxy, 2);
    }
  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);

  ret = 0;

 out:
  g_option_context_free (o);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
monitor_print_timestamp (void)
{
  GTimeVal now;
  time_t now_time;
  struct tm *now_tm;
  gchar time_buf[128];

  g_get_current_time (&now);
  now_time = (time_t) now.tv_sec;
  now_tm = localtime (&now_time);
  strftime (time_buf, sizeof time_buf, "%H:%M:%S", now_tm);

  g_print ("%s%s%s.%03d:%s ",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_YELLOW),
           time_buf, (gint) now.tv_usec / 1000,
           _color_get (_COLOR_RESET));
}

static gboolean
monitor_has_name_owner (void)
{
  gchar *name_owner;
  gboolean ret;
  name_owner = g_dbus_proxy_manager_get_name_owner (manager);
  ret = (name_owner != NULL);
  g_free (name_owner);
  return ret;
}

static void
monitor_print_name_owner (void)
{
  gchar *name_owner;
  name_owner = g_dbus_proxy_manager_get_name_owner (manager);
  monitor_print_timestamp ();
  if (name_owner != NULL)
    g_print ("The udisks-daemon is running (name-owner %s).\n", name_owner);
  else
    g_print ("The udisks-daemon is not running.\n");
  g_free (name_owner);
}

static void
monitor_on_notify_name_owner (GObject    *object,
                              GParamSpec *pspec,
                              gpointer    user_data)
{
  monitor_print_name_owner ();
}

static void
monitor_on_object_proxy_added (GDBusProxyManager  *manager,
                               GDBusObjectProxy   *object_proxy,
                               gpointer            user_data)
{
  if (!monitor_has_name_owner ())
    goto out;
  monitor_print_timestamp ();
  g_print ("%s%sAdded %s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_GREEN),
             g_dbus_object_proxy_get_object_path (object_proxy),
           _color_get (_COLOR_RESET));
  print_object (object_proxy, 2);
 out:
  ;
}

static void
monitor_on_object_proxy_removed (GDBusProxyManager  *manager,
                                 GDBusObjectProxy   *object_proxy,
                                 gpointer            user_data)
{
  if (!monitor_has_name_owner ())
    goto out;
  monitor_print_timestamp ();
  g_print ("%s%sRemoved %s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_RED),
             g_dbus_object_proxy_get_object_path (object_proxy),
           _color_get (_COLOR_RESET));
 out:
  ;
}

static void
monitor_on_interface_proxy_added (GDBusProxyManager  *manager,
                                  GDBusObjectProxy   *object_proxy,
                                  GDBusProxy         *interface_proxy,
                                  gpointer            user_data)
{
  if (!monitor_has_name_owner ())
    goto out;
  monitor_print_timestamp ();
  g_print ("%s%s%s:%s %s%sAdded interface %s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE),
             g_dbus_object_proxy_get_object_path (object_proxy),
           _color_get (_COLOR_RESET),
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_GREEN),
             g_dbus_proxy_get_interface_name (interface_proxy),
           _color_get (_COLOR_RESET));

  print_interface_properties (interface_proxy, 2);
 out:
  ;
}

static void
monitor_on_interface_proxy_removed (GDBusProxyManager  *manager,
                                    GDBusObjectProxy   *object_proxy,
                                    GDBusProxy         *interface_proxy,
                                    gpointer            user_data)
{
  if (!monitor_has_name_owner ())
    goto out;
  monitor_print_timestamp ();
  g_print ("%s%s%s:%s %s%sRemoved interface %s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE),
             g_dbus_object_proxy_get_object_path (object_proxy),
           _color_get (_COLOR_RESET),
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_RED),
             g_dbus_proxy_get_interface_name (interface_proxy),
           _color_get (_COLOR_RESET));
 out:
  ;
}

static void
monitor_on_interface_proxy_properties_changed (GDBusProxyManager  *manager,
                                               GDBusObjectProxy   *object_proxy,
                                               GDBusProxy         *interface_proxy,
                                               GVariant           *changed_properties,
                                               const gchar* const *invalidated_properties,
                                               gpointer            user_data)
{
  GVariantIter *iter;
  const gchar *property_name;
  GVariant *value;
  guint max_property_name_len;
  guint value_column;

  if (!monitor_has_name_owner ())
    goto out;

  monitor_print_timestamp ();

  g_print ("%s%s%s:%s %s%s%s:%s %s%sProperties Changed%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE),
             g_dbus_object_proxy_get_object_path (object_proxy),
           _color_get (_COLOR_RESET),
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_MAGENTA),
             g_dbus_proxy_get_interface_name (interface_proxy),
           _color_get (_COLOR_RESET),
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_YELLOW),
           _color_get (_COLOR_RESET));

  /* the daemon doesn't use the invalidated properties feature */
  g_warn_if_fail (g_strv_length ((gchar **) invalidated_properties) == 0);

  g_variant_get (changed_properties, "a{sv}", &iter);
  max_property_name_len = 0;
  while (g_variant_iter_next (iter, "{&sv}", &property_name, NULL))
    {
      guint property_name_len;
      property_name_len = strlen (property_name);
      if (max_property_name_len < property_name_len)
        max_property_name_len = property_name_len;
    }

  value_column = ((max_property_name_len + 7) / 8) * 8 + 8;
  if (value_column < 24)
    value_column = 24;
  else if (value_column > 64)
    value_column = 64;

  g_variant_get (changed_properties, "a{sv}", &iter);
  while (g_variant_iter_next (iter, "{&sv}", &property_name, &value))
    {
      gchar *value_str;
      guint rightmost;
      gint value_indent;

      rightmost = 2 + strlen (property_name) + 2;
      value_indent = value_column - rightmost;
      if (value_indent < 0)
        value_indent = 0;

      value_str = variant_to_string_with_indent (value, 2 + strlen (property_name) + 2 + value_indent);

      g_print ("  %s%s:%s %*s%s\n",
               _color_get (_COLOR_FG_WHITE), property_name, _color_get (_COLOR_RESET),
               value_indent, "",
               value_str);

      g_free (value_str);
      g_variant_unref (value);
    }
 out:
  ;
}

static void
monitor_on_interface_proxy_signal (GDBusProxyManager  *manager,
                                   GDBusObjectProxy   *object_proxy,
                                   GDBusProxy         *interface_proxy,
                                   const gchar        *sender_name,
                                   const gchar        *signal_name,
                                   GVariant           *parameters,
                                   gpointer            user_data)
{
  gchar *param_str;
  if (!monitor_has_name_owner ())
    goto out;

  param_str = g_variant_print (parameters, TRUE);
  monitor_print_timestamp ();

  g_print ("%s%s%s:%s %s%s%s%s%s%s::%s%s %s%s%s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE),
             g_dbus_object_proxy_get_object_path (object_proxy),
           _color_get (_COLOR_RESET),
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_MAGENTA),
             g_dbus_proxy_get_interface_name (interface_proxy),
           _color_get (_COLOR_RESET),
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_YELLOW),
           signal_name,
           _color_get (_COLOR_RESET),
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_WHITE),
           param_str,
           _color_get (_COLOR_RESET));
  g_free (param_str);
 out:
  ;
}


static const GOptionEntry command_monitor_entries[] =
{
  { NULL }
};

static gint
handle_command_monitor (gint        *argc,
                        gchar      **argv[],
                        gboolean     request_completion,
                        const gchar *completion_cur,
                        const gchar *completion_prev)
{
  gint ret;
  GOptionContext *o;
  gchar *s;

  ret = 1;

  modify_argv0_for_command (argc, argv, "monitor");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, "Monitor changes to objects.");
  g_option_context_add_main_entries (o, command_monitor_entries, NULL /* GETTEXT_PACKAGE*/);

  if (!g_option_context_parse (o, argc, argv, NULL))
    {
      if (!request_completion)
        {
          s = g_option_context_get_help (o, FALSE, NULL);
          g_printerr ("%s", s);
          g_free (s);
          goto out;
        }
    }

  /* done with completion */
  if (request_completion)
    goto out;

  g_print ("Monitoring the udisks daemon. Press Ctrl+C to exit.\n");

  g_signal_connect (manager,
                    "notify::name-owner",
                    G_CALLBACK (monitor_on_notify_name_owner),
                    NULL);

  g_signal_connect (manager,
                    "object-proxy-added",
                    G_CALLBACK (monitor_on_object_proxy_added),
                    NULL);
  g_signal_connect (manager,
                    "object-proxy-removed",
                    G_CALLBACK (monitor_on_object_proxy_removed),
                    NULL);
  g_signal_connect (manager,
                    "interface-proxy-added",
                    G_CALLBACK (monitor_on_interface_proxy_added),
                    NULL);
  g_signal_connect (manager,
                    "interface-proxy-removed",
                    G_CALLBACK (monitor_on_interface_proxy_removed),
                    NULL);
  g_signal_connect (manager,
                    "interface-proxy-properties-changed",
                    G_CALLBACK (monitor_on_interface_proxy_properties_changed),
                    NULL);
  g_signal_connect (manager,
                    "interface-proxy-signal",
                    G_CALLBACK (monitor_on_interface_proxy_signal),
                    NULL);

  monitor_print_name_owner ();

  g_main_loop_run (loop);

  ret = 0;

 out:
  g_option_context_free (o);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
parse_ctds (const gchar *ctds,
            guint       *c,
            guint       *d,
            guint       *t,
            guint       *s)
{
  if (sscanf (ctds, "%d:%d:%d:%d", c, d, t, s) != 4)
    {
      g_warning ("Error parsing `%s'", ctds);
      *c = 0;
      *d = 0;
      *t = 0;
      *s = 0;
    }
}

static gint
obj_proxy_cmp_controller (GDBusObjectProxy *a,
                          GDBusObjectProxy *b)
{
  UDisksController *ca;
  UDisksController *cb;

  ca = UDISKS_PEEK_CONTROLLER (a);
  cb = UDISKS_PEEK_CONTROLLER (b);

  if (ca != NULL && cb != NULL)
    {
      return g_strcmp0 (udisks_controller_get_address (ca), udisks_controller_get_address (cb));
    }
  else
    {
      return obj_proxy_cmp (a, b);
    }
}

static gint
obj_proxy_cmp_ctds (GDBusObjectProxy *a,
                    GDBusObjectProxy *b)
{
  UDisksDrive *da;
  UDisksDrive *db;

  da = UDISKS_PEEK_DRIVE (a);
  db = UDISKS_PEEK_DRIVE (b);

  if (da != NULL && db != NULL)
    {
      guint c_a, t_a, d_a, s_a;
      guint c_b, t_b, d_b, s_b;

      parse_ctds (udisks_drive_get_ctds (da), &c_a, &t_a, &d_a, &s_a);
      parse_ctds (udisks_drive_get_ctds (db), &c_b, &t_b, &d_b, &s_b);

      if (c_a > c_b)
        return 1;
      else if (c_a < c_b)
        return -1;

      if (t_a > t_b)
        return 1;
      else if (t_a < t_b)
        return -1;

      if (d_a > d_b)
        return 1;
      else if (d_a < d_b)
        return -1;

      if (s_a > s_b)
        return 1;
      else if (s_a < s_b)
        return -1;

      return 0;
    }
  else
    return obj_proxy_cmp (a, b);
}

/* built-in assumption: there is only one block device per drive */
static UDisksBlockDevice *
find_block_for_drive (GList       *object_proxies,
                      const gchar *drive_object_path)
{
  UDisksBlockDevice *ret;
  GList *l;

  ret = NULL;
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksBlockDevice *block;

      block = UDISKS_GET_BLOCK_DEVICE (object_proxy);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_device_get_drive (block), drive_object_path) == 0)
        {
          ret = block;
          goto out;
        }
      g_object_unref (block);
    }
 out:
  return ret;
}

static const GOptionEntry command_status_entries[] =
{
  { NULL }
};

static void
print_with_padding_and_ellipsis (const gchar *str,
                                 gint         max_len)
{
  gint len;
  len = str != NULL ? strlen (str) : 0;
  if (len == 0)
    {
      g_print ("%-*s", max_len, "-");
    }
  else if (len <= max_len - 1)
    {
      g_print ("%-*s", max_len, str);
    }
  else
    {
      gchar *s;
      s = g_strndup (str, max_len - 2);
      /* … U+2026 HORIZONTAL ELLIPSIS
       * UTF-8: 0xE2 0x80 0xA6
       */
      g_print ("%s… ", s);
      g_free (s);
    }
}

static gint
handle_command_status (gint        *argc,
                        gchar      **argv[],
                        gboolean     request_completion,
                        const gchar *completion_cur,
                        const gchar *completion_prev)
{
  gint ret;
  GOptionContext *o;
  gchar *s;
  GList *l;
  GList *object_proxies;
  guint n;

  ret = 1;

  modify_argv0_for_command (argc, argv, "status");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, "Shows high-level status.");
  g_option_context_add_main_entries (o, command_status_entries, NULL /* GETTEXT_PACKAGE*/);

  if (!g_option_context_parse (o, argc, argv, NULL))
    {
      if (!request_completion)
        {
          s = g_option_context_get_help (o, FALSE, NULL);
          g_printerr ("%s", s);
          g_free (s);
          goto out;
        }
    }

  /* done with completion */
  if (request_completion)
    goto out;

  object_proxies = g_dbus_proxy_manager_get_all (manager);

  /* first, print all controllers
   */

  g_print ("NUM ADDRESS       SLOT     VENDOR      MODEL  \n"
           "--------------------------------------------------------------------------------\n");
         /*   1 0000:00:1f.1  SLOT 1   Intel Corp… 82801HBM/HEM (ICH8M/ICH8M-E) SATA AH…    */
         /* 01234567890123456789012345678901234567890123456789012345678901234567890123456789 */

  /* sort according to e.g. PCI address */
  object_proxies = g_list_sort (object_proxies, (GCompareFunc) obj_proxy_cmp_controller);
  for (l = object_proxies, n = 0; l != NULL; l = l->next, n++)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksController *controller;

      controller = UDISKS_PEEK_CONTROLLER (object_proxy);
      if (controller == NULL)
        continue;

      g_print ("% 3d ", n);
      print_with_padding_and_ellipsis (udisks_controller_get_address (controller), 14);
      print_with_padding_and_ellipsis (udisks_controller_get_physical_slot (controller), 9);
      print_with_padding_and_ellipsis (udisks_controller_get_vendor (controller), 12);
      print_with_padding_and_ellipsis (udisks_controller_get_model (controller), 40);
      g_print ("\n");
    }
  g_print ("\n");

  /* then, print all drives
   *
   * We are guaranteed that, usually,
   *
   *  - CTDS      <= 12
   *  - model     <= 16   (SCSI: 16, ATA: 40)
   *  - vendor    <= 8    (SCSI: 8, ATA: 0)
   *  - revision  <= 8    (SCSI: 6, ATA: 8)
   *  - serial    <= 20   (SCSI: 16, ATA: 20)
   */
  g_print ("CTDS          MODEL                     REVISION  SERIAL               BLOCK\n"
           "--------------------------------------------------------------------------------\n");
         /* (10,11,12,0)  SEAGATE ST3300657SS       0006      3SJ1QNMQ00009052NECM sdaa     */
         /* 01234567890123456789012345678901234567890123456789012345678901234567890123456789 */

  /* sort according to Controller-Target-Drive */
  object_proxies = g_list_sort (object_proxies, (GCompareFunc) obj_proxy_cmp_ctds);
  for (l = object_proxies; l != NULL; l = l->next)
    {
      GDBusObjectProxy *object_proxy = G_DBUS_OBJECT_PROXY (l->data);
      UDisksDrive *drive;
      UDisksBlockDevice *block;
      const gchar *block_device;
      const gchar *ctds;
      const gchar *vendor;
      const gchar *model;
      const gchar *revision;
      const gchar *serial;
      gchar *vendor_model;

      drive = UDISKS_PEEK_DRIVE (object_proxy);
      if (drive == NULL)
        continue;

      block = find_block_for_drive (object_proxies, g_dbus_object_proxy_get_object_path (object_proxy));
      if (block != NULL)
        {
          block_device = udisks_block_device_get_device (block);
          g_object_unref (block);
        }
      else
        {
          block_device = "-";
        }

      ctds = udisks_drive_get_ctds (drive);
      vendor = udisks_drive_get_vendor (drive);
      model = udisks_drive_get_model (drive);
      revision = udisks_drive_get_revision (drive);
      serial = udisks_drive_get_serial (drive);

      if (strlen (vendor) == 0)
        vendor = NULL;
      if (strlen (model) == 0)
        model = NULL;
      if (vendor != NULL && model != NULL)
        vendor_model = g_strdup_printf ("%s %s", vendor, model);
      else if (model != NULL)
        vendor_model = g_strdup (model);
      else if (vendor != NULL)
        vendor_model = g_strdup (vendor);
      else
        vendor_model = g_strdup ("-");

      g_print ("%-13s %-25s %-9s %-20s %-8s\n",
               ctds,
               vendor_model,
               revision,
               serial,
               block_device);
    }


  g_list_foreach (object_proxies, (GFunc) g_object_unref, NULL);
  g_list_free (object_proxies);

  ret = 0;

 out:
  g_option_context_free (o);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
usage (gint *argc, gchar **argv[], gboolean use_stdout)
{
  GOptionContext *o;
  gchar *s;
  gchar *program_name;

  o = g_option_context_new ("COMMAND");
  g_option_context_set_help_enabled (o, FALSE);
  /* Ignore parsing result */
  g_option_context_parse (o, argc, argv, NULL);
  program_name = g_path_get_basename ((*argv)[0]);
  s = g_strdup_printf ("Commands:\n"
                       "  help         Shows this information\n"
                       "  info         Shows information about an object\n"
                       "  dump         Shows information about all objects\n"
                       "  status       Shows high-level status\n"
                       "  monitor      Monitor changes to objects\n"
                       "  mount        Mount a device\n"
                       "  unmount      Unmount a device\n"
                       "\n"
                       "Use \"%s COMMAND --help\" to get help on each command.\n",
                       program_name);
  g_free (program_name);
  g_option_context_set_description (o, s);
  g_free (s);
  s = g_option_context_get_help (o, FALSE, NULL);
  if (use_stdout)
    g_print ("%s", s);
  else
    g_printerr ("%s", s);
  g_free (s);
  g_option_context_free (o);
}

static void
remove_arg (gint num, gint *argc, gchar **argv[])
{
  gint n;

  g_assert (num <= (*argc));

  for (n = num; (*argv)[n] != NULL; n++)
    (*argv)[n] = (*argv)[n+1];
  (*argv)[n] = NULL;
  (*argc) = (*argc) - 1;
}

static void
modify_argv0_for_command (gint *argc, gchar **argv[], const gchar *command)
{
  gchar *s;
  gchar *program_name;

  /* TODO:
   *  1. get a g_set_prgname() ?; or
   *  2. save old argv[0] and restore later
   */

  g_assert (g_strcmp0 ((*argv)[1], command) == 0);
  remove_arg (1, argc, argv);

  program_name = g_path_get_basename ((*argv)[0]);
  s = g_strdup_printf ("%s %s", (*argv)[0], command);
  (*argv)[0] = s;
  g_free (program_name);
}

static gchar *
pick_word_at (const gchar  *s,
              gint          cursor,
              gint         *out_word_begins_at)
{
  gint begin;
  gint end;

  if (s[0] == '\0')
    {
      if (out_word_begins_at != NULL)
        *out_word_begins_at = -1;
      return NULL;
    }

  if (g_ascii_isspace (s[cursor]) && ((cursor > 0 && g_ascii_isspace(s[cursor-1])) || cursor == 0))
    {
      if (out_word_begins_at != NULL)
        *out_word_begins_at = cursor;
      return g_strdup ("");
    }

  while (!g_ascii_isspace (s[cursor - 1]) && cursor > 0)
    cursor--;
  begin = cursor;

  end = begin;
  while (!g_ascii_isspace (s[end]) && s[end] != '\0')
    end++;

  if (out_word_begins_at != NULL)
    *out_word_begins_at = begin;

  return g_strndup (s + begin, end - begin);
}

/* TODO: would be nice with generic options that can be used before any verb such as
 *
 *  -n, --no-color   Turn colorization off always.
 *  -C, --color      Turn colorization on always.
 */

int
main (int argc,
      char **argv)
{
  gint ret;
  const gchar *command;
  gboolean request_completion;
  gchar *completion_cur;
  gchar *completion_prev;
  GError *error;
  static volatile GQuark gdbus_error_domain = 0;

  ret = 1;
  completion_cur = NULL;
  completion_prev = NULL;
  manager = NULL;
  loop = NULL;

  g_type_init ();
  _color_init ();

  setlocale (LC_ALL, "");

  /* ensure that the D-Bus error mapping is initialized */
  gdbus_error_domain = UDISKS_ERROR;

  if (argc < 2)
    {
      usage (&argc, &argv, FALSE);
      goto out;
    }

  loop = g_main_loop_new (NULL, FALSE);

  error = NULL;
  manager = udisks_proxy_manager_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                   G_DBUS_PROXY_MANAGER_FLAGS_NONE,
                                                   "org.freedesktop.UDisks2",
                                                   "/org/freedesktop/UDisks",
                                                   NULL, /* GCancellable */
                                                   &error);
  if (manager == NULL)
    {
      g_printerr ("Error connecting to the udisks daemon: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  request_completion = FALSE;

  completion_debug ("========================================================================");
  completion_debug ("---- argc=%d --------------------------------------------------------", argc);

 again:
  command = argv[1];
  if (g_strcmp0 (command, "help") == 0)
    {
      if (request_completion)
        {
          /* do nothing */
        }
      else
        {
          usage (&argc, &argv, TRUE);
          ret = 0;
        }
      goto out;
    }
  else if (g_strcmp0 (command, "info") == 0)
    {
      ret = handle_command_info (&argc,
                                 &argv,
                                 request_completion,
                                 completion_cur,
                                 completion_prev);
      goto out;
    }
  else if (g_strcmp0 (command, "mount") == 0 || g_strcmp0 (command, "unmount") == 0)
    {
      ret = handle_command_mount_unmount (&argc,
                                          &argv,
                                          request_completion,
                                          completion_cur,
                                          completion_prev,
                                          g_strcmp0 (command, "mount") == 0);
      goto out;
    }
  else if (g_strcmp0 (command, "dump") == 0)
    {
      ret = handle_command_dump (&argc,
                                 &argv,
                                 request_completion,
                                 completion_cur,
                                 completion_prev);
      goto out;
    }
  else if (g_strcmp0 (command, "monitor") == 0)
    {
      ret = handle_command_monitor (&argc,
                                    &argv,
                                    request_completion,
                                    completion_cur,
                                    completion_prev);
      goto out;
    }
  else if (g_strcmp0 (command, "status") == 0)
    {
      ret = handle_command_status (&argc,
                                   &argv,
                                   request_completion,
                                   completion_cur,
                                   completion_prev);
      goto out;
    }
  else if (g_strcmp0 (command, "complete") == 0 && argc == 4 && !request_completion)
    {
      const gchar *completion_line;
      gchar **completion_argv;
      gint completion_argc;
      gint completion_point;
      gchar *endp;
      gint cur_begin;

      request_completion = TRUE;

      completion_line = argv[2];
      completion_point = strtol (argv[3], &endp, 10);
      if (endp == argv[3] || *endp != '\0')
        goto out;

      completion_debug ("completion_point=%d", completion_point);
      completion_debug ("----");
      completion_debug (" 0123456789012345678901234567890123456789012345678901234567890123456789");
      completion_debug ("`%s'", completion_line);
      completion_debug (" %*s^",
                         completion_point, "");
      completion_debug ("----");

      if (!g_shell_parse_argv (completion_line,
                               &completion_argc,
                               &completion_argv,
                               NULL))
        {
          /* it's very possible the command line can't be parsed (for
           * example, missing quotes etc) - in that case, we just
           * don't autocomplete at all
           */
          goto out;
        }

      /* compute cur and prev */
      completion_prev = NULL;
      completion_cur = pick_word_at (completion_line, completion_point, &cur_begin);
      if (cur_begin > 0)
        {
          gint prev_end;
          for (prev_end = cur_begin - 1; prev_end >= 0; prev_end--)
            {
              if (!g_ascii_isspace (completion_line[prev_end]))
                {
                  completion_prev = pick_word_at (completion_line, prev_end, NULL);
                  break;
                }
            }
        }
      completion_debug (" cur=`%s'", completion_cur);
      completion_debug ("prev=`%s'", completion_prev);

      argc = completion_argc;
      argv = completion_argv;

      ret = 0;

      goto again;
    }
  else
    {
      if (request_completion)
        {
          g_print ("help \n"
                   "info \n"
                   "dump \n"
                   "monitor \n"
                   "status \n"
                   "mount \n"
                   "unmount \n"
                   );
          ret = 0;
          goto out;
        }
      else
        {
          g_printerr ("Unknown command `%s'\n", command);
          usage (&argc, &argv, FALSE);
          goto out;
        }
    }


 out:
  if (loop != NULL)
    g_main_loop_unref (loop);
  if (manager != NULL)
    g_object_unref (manager);
  _color_shutdown ();
  shutdown_local_polkit_agent ();
  return ret;
}

#ifdef COMPLETION_DEBUG
G_GNUC_UNUSED static void
completion_debug (const gchar *format, ...)
{
  va_list var_args;
  gchar *s;
  static FILE *f = NULL;

  va_start (var_args, format);
  s = g_strdup_vprintf (format, var_args);
  if (f == NULL)
    {
      f = fopen ("/tmp/udisksctl-completion-debug.txt", "a+");
    }
  fprintf (f, "%s\n", s);
  g_free (s);
}
#else
static void
completion_debug (const gchar *format, ...)
{
}
#endif
