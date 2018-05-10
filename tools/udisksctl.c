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
#include <glib/gi18n.h>
#include <gio/gunixfdlist.h>

#include <stdio.h>
#include <stdlib.h>
#include <udisks/udisks.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <locale.h>

#include <polkit/polkit.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>

static UDisksClient *client = NULL;
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

  subject = polkit_unix_process_new_for_owner (getpid (), 0, getuid ());

  error = NULL;
  /* this will fail if we can't find a controlling terminal */
  local_polkit_agent = polkit_agent_text_listener_new (NULL, &error);
  if (local_polkit_agent == NULL)
    {
      g_printerr ("Error creating textual authentication agent: %s (%s, %d)\n",
                  error->message,
                  g_quark_to_string (error->domain),
                  error->code);
      g_clear_error (&error);
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
      g_clear_error (&error);
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
      fflush (stdout);
      dup2 (fileno(_color_pager_out), fileno(stdout));
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
print_object (UDisksObject *object,
              guint        indent)
{
  GList *interface_proxies;
  GList *l;

  g_return_if_fail (G_IS_DBUS_OBJECT (object));

  interface_proxies = g_dbus_object_get_interfaces (G_DBUS_OBJECT (object));

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
  g_list_free_full (interface_proxies, g_object_unref);
}

/* ---------------------------------------------------------------------------------------------------- */

static UDisksObject *
lookup_object_by_path (const gchar *path)
{
  GDBusObject *ret;
  gchar *s;

  s = g_strdup_printf ("/org/freedesktop/UDisks2/%s", path);
  ret = g_dbus_object_manager_get_object (udisks_client_get_object_manager (client), s);
  g_free (s);

  if (ret == NULL)
    return NULL;
  else
    return UDISKS_OBJECT (ret);
}

static UDisksObject *
lookup_object_by_device (const gchar *device)
{
  UDisksObject *ret;
  GList *objects;
  GList *l;

  ret = NULL;

  objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block != NULL)
        {
          const gchar * const *symlinks;
          guint n;

          if (g_strcmp0 (udisks_block_get_device (block), device) == 0)
            {
              ret = g_object_ref (object);
              goto out;
            }

          symlinks = udisks_block_get_symlinks (block);
          for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
            {
              if (g_strcmp0 (symlinks[n], device) == 0)
                {
                  ret = g_object_ref (object);
                  goto out;
                }
            }
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);

  return ret;
}

static UDisksObject *
lookup_object_by_drive (const gchar *drive)
{
  UDisksObject *ret;
  GList *objects;
  GList *l;
  gchar *full_drive_object_path;

  ret = NULL;

  full_drive_object_path = g_strdup_printf ("/org/freedesktop/UDisks2/drives/%s", drive);

  objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksDrive *drive_iface;

      if (g_strcmp0 (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), full_drive_object_path) != 0)
        continue;

      drive_iface = udisks_object_peek_drive (object);
      if (drive_iface != NULL)
        {
          ret = g_object_ref (object);
          goto out;
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  g_free (full_drive_object_path);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar   *opt_mount_unmount_object_path = NULL;
static gchar   *opt_mount_unmount_device = NULL;
static gchar   *opt_mount_options = NULL;
static gchar   *opt_mount_filesystem_type = NULL;
static gboolean opt_unmount_force = FALSE;
static gboolean opt_mount_unmount_no_user_interaction = FALSE;

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
    "options",
    'o',
    0,
    G_OPTION_ARG_STRING,
    &opt_mount_options,
    "Mount options",
    NULL
  },
  {
    "no-user-interaction",
    0, /* no short option */
    0,
    G_OPTION_ARG_NONE,
    &opt_mount_unmount_no_user_interaction,
    "Do not authenticate the user if needed",
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
    "force",
    'f',
    0,
    G_OPTION_ARG_NONE,
    &opt_unmount_force,
    "Force/lazy unmount",
    NULL
  },
  {
    "no-user-interaction",
    0, /* no short option */
    0,
    G_OPTION_ARG_NONE,
    &opt_mount_unmount_no_user_interaction,
    "Do not authenticate the user if needed",
    NULL
  },
  {
    NULL
  }
};

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
  GList *objects;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksFilesystem *filesystem;
  guint n;
  const gchar * const *mount_points;
  GVariant *options;
  GVariantBuilder builder;

  ret = 1;
  opt_mount_unmount_object_path = NULL;
  opt_mount_unmount_device = NULL;
  opt_mount_options = NULL;
  opt_mount_filesystem_type = NULL;
  opt_unmount_force = FALSE;
  object = NULL;
  options = NULL;

  if (is_mount)
    modify_argv0_for_command (argc, argv, "mount");
  else
    modify_argv0_for_command (argc, argv, "unmount");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  if (is_mount)
    g_option_context_set_summary (o, "Mount a filesystem.");
  else
    g_option_context_set_summary (o, "Unmount a filesystem.");
  g_option_context_add_main_entries (o,
                                     is_mount ? command_mount_entries : command_unmount_entries,
                                     NULL /* GETTEXT_PACKAGE*/);

  complete_objects = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--object-path") == 0 || g_strcmp0 (completion_prev, "-p") == 0 ||
                             g_strcmp0 (completion_cur, "--object-path") == 0 || g_strcmp0 (completion_cur, "-p") == 0))
    {
      complete_objects = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_devices = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--block-device") == 0 || g_strcmp0 (completion_prev, "-b") == 0 ||
                             g_strcmp0 (completion_cur, "--block-device") == 0 || g_strcmp0 (completion_cur, "-b") == 0))
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

      objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
      for (l = objects; l != NULL; l = l->next)
        {
          gboolean is_mounted;

          object = UDISKS_OBJECT (l->data);
          filesystem = udisks_object_peek_filesystem (object);

          if (filesystem == NULL)
            continue;

          is_mounted = FALSE;
          mount_points = udisks_filesystem_get_mount_points (filesystem);
          if (mount_points != NULL && g_strv_length ((gchar **) mount_points) > 0)
            is_mounted = TRUE;

          if ((is_mount && !is_mounted) || (!is_mount && is_mounted))
            {
              object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
              g_assert (g_str_has_prefix (object_path, "/org/freedesktop/UDisks2/"));
              g_print ("%s \n", object_path + sizeof ("/org/freedesktop/UDisks2/") - 1);
            }
        }
      g_list_free_full (objects, g_object_unref);
      goto out;
    }

  if (complete_devices)
    {
      objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
      for (l = objects; l != NULL; l = l->next)
        {
          object = UDISKS_OBJECT (l->data);
          block = udisks_object_peek_block (object);
          filesystem = udisks_object_peek_filesystem (object);

          if (block != NULL)
            {
              gboolean is_mounted;

              is_mounted = FALSE;
              if (filesystem != NULL)
                {
                  mount_points = udisks_filesystem_get_mount_points (filesystem);
                  if (mount_points != NULL && g_strv_length ((gchar **) mount_points) > 0)
                    is_mounted = TRUE;
                }

              if ((is_mount && !is_mounted) || (!is_mount && is_mounted))
                {
                  const gchar * const *symlinks;
                  g_print ("%s \n", udisks_block_get_device (block));
                  symlinks = udisks_block_get_symlinks (block);
                  for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
                    g_print ("%s \n", symlinks[n]);
                }
            }
        }
      g_list_free_full (objects, g_object_unref);
      goto out;
    }

  /* done with completion */
  if (request_completion)
    goto out;

  if (opt_mount_unmount_object_path != NULL)
    {
      object = lookup_object_by_path (opt_mount_unmount_object_path);
      if (object == NULL)
        {
          g_printerr ("Error looking up object with path %s\n", opt_mount_unmount_object_path);
          goto out;
        }
    }
  else if (opt_mount_unmount_device != NULL)
    {
      object = lookup_object_by_device (opt_mount_unmount_device);
      if (object == NULL)
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

  block = udisks_object_peek_block (object);
  filesystem = udisks_object_peek_filesystem (object);
  if (filesystem == NULL)
    {
      g_printerr ("Object %s is not a mountable filesystem.\n", g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      g_object_unref (object);
      goto out;
    }

  if (opt_mount_filesystem_type == NULL)
    opt_mount_filesystem_type = g_strdup ("");

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  if (opt_mount_unmount_no_user_interaction)
    {
      g_variant_builder_add (&builder,
                             "{sv}",
                             "auth.no_user_interaction", g_variant_new_boolean (TRUE));
    }
  if (is_mount)
    {
      if (opt_mount_options != NULL)
        {
          g_variant_builder_add (&builder,
                                 "{sv}",
                                 "options", g_variant_new_string (opt_mount_options));
        }
      if (opt_mount_filesystem_type != NULL)
        {
          g_variant_builder_add (&builder,
                                 "{sv}",
                                 "fstype", g_variant_new_string (opt_mount_filesystem_type));
        }
    }
  else
    {
      if (opt_unmount_force)
        {
          g_variant_builder_add (&builder,
                                 "{sv}",
                                 "force", g_variant_new_boolean (TRUE));
        }
    }
  options = g_variant_builder_end (&builder);
  g_variant_ref_sink (options);

 try_again:
  if (is_mount)
    {
      GError *error;
      gchar *mount_path;

      error = NULL;
      if (!udisks_filesystem_call_mount_sync (filesystem,
                                              options,
                                              &mount_path,
                                              NULL,                       /* GCancellable */
                                              &error))
        {
          if (error->domain == UDISKS_ERROR &&
              error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
              setup_local_polkit_agent ())
            {
              g_clear_error (&error);
              goto try_again;
            }
          g_printerr ("Error mounting %s: %s\n",
                      udisks_block_get_device (block),
                      error->message);
          g_clear_error (&error);
          g_object_unref (object);
          goto out;
        }
      g_print ("Mounted %s at %s.\n",
               udisks_block_get_device (block),
               mount_path);
      g_free (mount_path);
    }
  else
    {
      GError *error;

      error = NULL;
      if (!udisks_filesystem_call_unmount_sync (filesystem,
                                                options,
                                                NULL,         /* GCancellable */
                                                &error))
        {
          if (error->domain == UDISKS_ERROR &&
              error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
              setup_local_polkit_agent ())
            {
              g_clear_error (&error);
              goto try_again;
            }
          g_printerr ("Error unmounting %s: %s\n",
                      udisks_block_get_device (block),
                      error->message);
          g_clear_error (&error);
          g_object_unref (object);
          goto out;
        }
      g_print ("Unmounted %s.\n",
               udisks_block_get_device (block));
    }

  ret = 0;

  g_object_unref (object);

 out:
  if (options != NULL)
    g_variant_unref (options);
  g_option_context_free (o);
  g_free (opt_mount_unmount_object_path);
  g_free (opt_mount_unmount_device);
  g_free (opt_mount_options);
  g_free (opt_mount_filesystem_type);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
read_passphrase (void)
{
  struct termios ts, ots;
  GString *str = NULL;
  const gchar *tty_name = NULL;
  FILE *tty = NULL;
  gchar *ret = NULL;

  tty_name = ctermid (NULL);
  if (tty_name == NULL)
    {
      g_warning ("Cannot determine pathname for current controlling terminal for the process: %m");
      return NULL;
    }

  tty = fopen (tty_name, "r+");
  if (tty == NULL)
    {
      g_warning ("Error opening current controlling terminal %s: %m", tty_name);
      return NULL;
    }

  fprintf (tty, "Passphrase: ");
  fflush (tty);

  setbuf (tty, NULL);

  tcgetattr (fileno (tty), &ts);
  ots = ts;
  ts.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
  tcsetattr (fileno (tty), TCSAFLUSH, &ts);

  str = g_string_new (NULL);
  while (TRUE)
    {
      gint c;
      c = getc (tty);
      if (c == '\n')
        {
          /* ok, done */
          break;
        }
      else if (c == EOF)
        {
          tcsetattr (fileno (tty), TCSAFLUSH, &ots);
          g_error ("Unexpected EOF while reading from controlling terminal.");
          abort ();
          break;
        }
      else
        {
          g_string_append_c (str, c);
        }
    }
  tcsetattr (fileno (tty), TCSAFLUSH, &ots);
  putc ('\n', tty);

  ret = g_string_free (str, FALSE);
  str = NULL;

  fclose (tty);
  return ret;
}

static gboolean
encrypted_is_unlocked (UDisksObject *encrypted_object)
{
  GList *objects;
  GList *l;
  const gchar *encrypted_object_path;
  gboolean ret;

  ret = FALSE;

  encrypted_object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (encrypted_object));

  objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_peek_block (object);
      if (block != NULL)
        {
          if (g_strcmp0 (udisks_block_get_crypto_backing_device (block), encrypted_object_path) == 0)
            {
              ret = TRUE;
              goto out;
            }
        }
    }

 out:
  g_list_free_full (objects, g_object_unref);
  return ret;
}

static gchar   *opt_unlock_lock_object_path = NULL;
static gchar   *opt_unlock_lock_device = NULL;
static gboolean opt_unlock_lock_no_user_interaction = FALSE;
static gchar   *opt_unlock_keyfile = NULL;

static const GOptionEntry command_unlock_entries[] =
{
  {
    "object-path",
    'p',
    0,
    G_OPTION_ARG_STRING,
    &opt_unlock_lock_object_path,
    "Object to unlock",
    NULL
  },
  {
    "block-device",
    'b',
    0,
    G_OPTION_ARG_STRING,
    &opt_unlock_lock_device,
    "Block device to unlock",
    NULL
  },
  {
    "no-user-interaction",
    0, /* no short option */
    0,
    G_OPTION_ARG_NONE,
    &opt_unlock_lock_no_user_interaction,
    "Do not authenticate the user if needed",
    NULL
  },
  {
    "key-file",
    0, /* no short option */
    0,
    G_OPTION_ARG_STRING,
    &opt_unlock_keyfile,
    "Keyfile for unlocking",
    NULL
  },
  {
    NULL
  }
};

static const GOptionEntry command_lock_entries[] =
{
  {
    "object-path",
    'p',
    0,
    G_OPTION_ARG_STRING,
    &opt_unlock_lock_object_path,
    "Object to lock",
    NULL
  },
  {
    "block-device",
    'b',
    0,
    G_OPTION_ARG_STRING,
    &opt_unlock_lock_device,
    "Block device to lock",
    NULL
  },
  {
    "no-user-interaction",
    0, /* no short option */
    0,
    G_OPTION_ARG_NONE,
    &opt_unlock_lock_no_user_interaction,
    "Do not authenticate the user if needed",
    NULL
  },
  {
    NULL
  }
};

static GVariant*
pack_binary_blob (const gchar *data,
                  gsize        size)
{
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("ay"));

  for (gsize i = 0; i < size; i++)
  {
    g_variant_builder_add (&builder, "y", data[i]);
  }

  return g_variant_builder_end (&builder);
}

static gint
handle_command_unlock_lock (gint        *argc,
                            gchar      **argv[],
                            gboolean     request_completion,
                            const gchar *completion_cur,
                            const gchar *completion_prev,
                            gboolean     is_unlock)
{
  gint ret;
  GOptionContext *o;
  gchar *s;
  gboolean complete_objects;
  gboolean complete_devices;
  GList *l;
  GList *objects;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksEncrypted *encrypted;
  guint n;
  GVariant *options;
  GVariantBuilder builder;
  gchar *passphrase;
  gchar *keyfile_contents = NULL;
  gsize  keyfile_size = 0;
  GError *error = NULL;

  ret = 1;
  opt_unlock_lock_object_path = NULL;
  opt_unlock_lock_device = NULL;
  object = NULL;
  options = NULL;
  passphrase = NULL;

  if (is_unlock)
    modify_argv0_for_command (argc, argv, "unlock");
  else
    modify_argv0_for_command (argc, argv, "lock");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  if (is_unlock)
    g_option_context_set_summary (o, "Unlock an encrypted device.");
  else
    g_option_context_set_summary (o, "Lock an encrypted device.");
  g_option_context_add_main_entries (o,
                                     is_unlock ? command_unlock_entries : command_lock_entries,
                                     NULL /* GETTEXT_PACKAGE*/);

  complete_objects = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--object-path") == 0 || g_strcmp0 (completion_prev, "-p") == 0 ||
                            g_strcmp0 (completion_cur, "--object-path") == 0 || g_strcmp0 (completion_cur, "-p") == 0))
    {
      complete_objects = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_devices = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--block-device") == 0 || g_strcmp0 (completion_prev, "-b") == 0 ||
                            g_strcmp0 (completion_cur, "--block-device") == 0 || g_strcmp0 (completion_cur, "-b") == 0))
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
      (opt_unlock_lock_object_path == NULL && !complete_objects) &&
      (opt_unlock_lock_device == NULL && !complete_devices))
    {
      g_print ("--object-path \n"
               "--block-device \n");
    }

  if (complete_objects)
    {
      const gchar *object_path;

      objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
      for (l = objects; l != NULL; l = l->next)
        {
          gboolean is_unlocked;

          object = UDISKS_OBJECT (l->data);
          encrypted = udisks_object_peek_encrypted (object);

          if (encrypted == NULL)
            continue;

          is_unlocked = encrypted_is_unlocked (object);

          if ((is_unlock && !is_unlocked) || (!is_unlock && is_unlocked))
            {
              object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
              g_assert (g_str_has_prefix (object_path, "/org/freedesktop/UDisks2/"));
              g_print ("%s \n", object_path + sizeof ("/org/freedesktop/UDisks2/") - 1);
            }
        }
      g_list_free_full (objects, g_object_unref);
      goto out;
    }

  if (complete_devices)
    {
      objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
      for (l = objects; l != NULL; l = l->next)
        {
          object = UDISKS_OBJECT (l->data);
          block = udisks_object_peek_block (object);

          if (block != NULL)
            {
              gboolean is_unlocked;

              is_unlocked = encrypted_is_unlocked (object);

              if ((is_unlock && !is_unlocked) || (!is_unlock && is_unlocked))
                {
                  const gchar * const *symlinks;
                  g_print ("%s \n", udisks_block_get_device (block));
                  symlinks = udisks_block_get_symlinks (block);
                  for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
                    g_print ("%s \n", symlinks[n]);
                }
            }
        }
      g_list_free_full (objects, g_object_unref);
      goto out;
    }

  /* done with completion */
  if (request_completion)
    goto out;

  if (opt_unlock_lock_object_path != NULL)
    {
      object = lookup_object_by_path (opt_unlock_lock_object_path);
      if (object == NULL)
        {
          g_printerr ("Error looking up object with path %s\n", opt_unlock_lock_object_path);
          goto out;
        }
    }
  else if (opt_unlock_lock_device != NULL)
    {
      object = lookup_object_by_device (opt_unlock_lock_device);
      if (object == NULL)
        {
          g_printerr ("Error looking up object for device %s\n", opt_unlock_lock_device);
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

  block = udisks_object_peek_block (object);
  encrypted = udisks_object_peek_encrypted (object);
  if (encrypted == NULL)
    {
      g_printerr ("Object %s is not an encrypted device.\n", g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      g_object_unref (object);
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  if (opt_unlock_lock_no_user_interaction)
    {
      g_variant_builder_add (&builder,
                             "{sv}",
                             "auth.no_user_interaction", g_variant_new_boolean (TRUE));
    }
  if (opt_unlock_keyfile)
    {
      if (!g_file_get_contents (opt_unlock_keyfile,
                                &keyfile_contents,
                                &keyfile_size,
                                &error))
      {
        g_printerr ("Error unlocking %s: %s\n",
                    udisks_block_get_device (block),
                    error->message);
        goto out;
      }
      g_variant_builder_add (&builder,
                             "{sv}",
                             "keyfile_contents",
                             pack_binary_blob (keyfile_contents, keyfile_size));
    }
  options = g_variant_builder_end (&builder);
  g_variant_ref_sink (options);

  if (is_unlock && !opt_unlock_keyfile)
    passphrase = read_passphrase ();

 try_again:
  if (is_unlock)
    {
      gchar *cleartext_object_path;
      UDisksObject *cleartext_object;

      if (!udisks_encrypted_call_unlock_sync (encrypted,
                                              passphrase ? passphrase : "",
                                              options,
                                              &cleartext_object_path,
                                              NULL,                       /* GCancellable */
                                              &error))
        {
          if (error->domain == UDISKS_ERROR &&
              error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
              setup_local_polkit_agent ())
            {
              g_clear_error (&error);
              goto try_again;
            }
          g_printerr ("Error unlocking %s: %s\n",
                      udisks_block_get_device (block),
                      error->message);
          g_clear_error (&error);
          g_object_unref (object);
          goto out;
        }
      udisks_client_settle (client);

      cleartext_object = UDISKS_OBJECT (g_dbus_object_manager_get_object (udisks_client_get_object_manager (client),
                                                                          (cleartext_object_path)));
      g_print ("Unlocked %s as %s.\n",
               udisks_block_get_device (block),
               udisks_block_get_device (udisks_object_get_block (cleartext_object)));
      g_object_unref (cleartext_object);
      g_free (cleartext_object_path);
    }
  else
    {
      if (!udisks_encrypted_call_lock_sync (encrypted,
                                            options,
                                            NULL,         /* GCancellable */
                                            &error))
        {
          if (error->domain == UDISKS_ERROR &&
              error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
              setup_local_polkit_agent ())
            {
              g_clear_error (&error);
              goto try_again;
            }
          g_printerr ("Error locking %s: %s\n",
                      udisks_block_get_device (block),
                      error->message);
          g_clear_error (&error);
          g_object_unref (object);
          goto out;
        }
      g_print ("Locked %s.\n",
               udisks_block_get_device (block));
    }

  ret = 0;

  g_object_unref (object);

 out:
  if (passphrase != NULL)
    {
      memset (passphrase, '\0', strlen (passphrase));
      g_free (passphrase);
    }
  if (keyfile_contents != NULL)
    {
      memset (keyfile_contents, '\0', keyfile_size);
      g_free (keyfile_contents);
    }
  if (options != NULL)
    g_variant_unref (options);
  g_option_context_free (o);
  g_free (opt_unlock_lock_object_path);
  g_free (opt_unlock_lock_device);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar   *opt_loop_file = NULL;
static gchar   *opt_loop_object_path = NULL;
static gchar   *opt_loop_device = NULL;
static gboolean opt_loop_no_user_interaction = FALSE;
static gboolean opt_loop_read_only = FALSE;
static gint64   opt_loop_offset = 0;
static gint64   opt_loop_size = 0;

static const GOptionEntry command_loop_setup_entries[] =
{
  {
    "file",
    'f',
    0,
    G_OPTION_ARG_FILENAME,
    &opt_loop_file,
    "File to set-up a loop device for",
    NULL
  },
  {
    "read-only",
    'r',
    0,
    G_OPTION_ARG_NONE,
    &opt_loop_read_only,
    "Setup read-only device",
    NULL
  },
  {
    "offset",
    'o',
    0,
    G_OPTION_ARG_INT64,
    &opt_loop_offset,
    "Start at <num> bytes into file",
    NULL
  },
  {
    "size",
    's',
    0,
    G_OPTION_ARG_INT64,
    &opt_loop_size,
    "Limit size to <num> bytes",
    NULL
  },
  {
    "no-user-interaction",
    0, /* no short option */
    0,
    G_OPTION_ARG_NONE,
    &opt_loop_no_user_interaction,
    "Do not authenticate the user if needed",
    NULL
  },
  {
    NULL
  }
};

static const GOptionEntry command_loop_delete_entries[] =
{
  {
    "object-path",
    'p',
    0,
    G_OPTION_ARG_STRING,
    &opt_loop_object_path,
    "Object for loop device to delete",
    NULL
  },
  {
    "block-device",
    'b',
    0,
    G_OPTION_ARG_STRING,
    &opt_loop_device,
    "Loop device to delete",
    NULL
  },
  {
    "no-user-interaction",
    0, /* no short option */
    0,
    G_OPTION_ARG_NONE,
    &opt_loop_no_user_interaction,
    "Do not authenticate the user if needed",
    NULL
  },
  {
    NULL
  }
};

static gint
handle_command_loop (gint        *argc,
                     gchar      **argv[],
                     gboolean     request_completion,
                     const gchar *completion_cur,
                     const gchar *completion_prev,
                     gboolean     is_setup)
{
  gint ret;
  GOptionContext *o;
  gchar *s;
  gboolean complete_objects;
  gboolean complete_devices;
  gboolean complete_files;
  GList *l;
  GList *objects;
  UDisksObject *object;
  UDisksBlock *block;
  guint n;
  GVariant *options;
  GVariantBuilder builder;
  GError *error;

  ret = 1;
  opt_loop_object_path = NULL;
  opt_loop_device = NULL;
  object = NULL;
  options = NULL;

  if (is_setup)
    modify_argv0_for_command (argc, argv, "loop-setup");
  else
    modify_argv0_for_command (argc, argv, "loop-delete");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  if (is_setup)
    g_option_context_set_summary (o, "Set up a loop device.");
  else
    g_option_context_set_summary (o, "Delete a loop device.");
  g_option_context_add_main_entries (o,
                                     is_setup ? command_loop_setup_entries : command_loop_delete_entries,
                                     NULL /* GETTEXT_PACKAGE*/);

  complete_objects = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--object-path") == 0 || g_strcmp0 (completion_prev, "-p") == 0 ||
                             g_strcmp0 (completion_cur, "--object-path") == 0 || g_strcmp0 (completion_cur, "-p") == 0))
    {
      complete_objects = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_devices = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--block-device") == 0 || g_strcmp0 (completion_prev, "-b") == 0 ||
                             g_strcmp0 (completion_cur, "--block-device") == 0 || g_strcmp0 (completion_cur, "-b") == 0))
    {
      complete_devices = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_files = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--file") == 0 || g_strcmp0 (completion_prev, "-f") == 0 ||
                             g_strcmp0 (completion_cur, "--file") == 0 || g_strcmp0 (completion_cur, "-f") == 0))
    {
      complete_files = TRUE;
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

  if (request_completion)
    {
      if (is_setup)
        {
          if (opt_loop_file == NULL && !complete_files)
            {
              g_print ("--file \n");
            }
          if (complete_files)
            {
              g_print ("@FILES@");
            }
        }
      else
        {
          if ((opt_loop_object_path == NULL && !complete_objects) &&
              (opt_loop_device == NULL && !complete_devices))
            {
              g_print ("--object-path \n"
                       "--block-device \n");
            }

          if (complete_objects)
            {
              const gchar *object_path;
              objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
              for (l = objects; l != NULL; l = l->next)
                {
                  object = UDISKS_OBJECT (l->data);
                  if (udisks_object_peek_loop (object) != NULL)
                    {
                      object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
                      g_assert (g_str_has_prefix (object_path, "/org/freedesktop/UDisks2/"));
                      g_print ("%s \n", object_path + sizeof ("/org/freedesktop/UDisks2/") - 1);
                    }
                }
              g_list_free_full (objects, g_object_unref);
              goto out;
            }
          if (complete_devices)
            {
              objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
              for (l = objects; l != NULL; l = l->next)
                {
                  object = UDISKS_OBJECT (l->data);
                  block = udisks_object_peek_block (object);
                  if (udisks_object_peek_loop (object) != NULL)
                    {
                      const gchar * const *symlinks;
                      g_print ("%s \n", udisks_block_get_device (block));
                      symlinks = udisks_block_get_symlinks (block);
                      for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
                        g_print ("%s \n", symlinks[n]);
                    }
                }
              g_list_free_full (objects, g_object_unref);
              goto out;
            }
        }

      /* done with completion */
      if (request_completion)
        goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  if (opt_loop_no_user_interaction)
    {
      g_variant_builder_add (&builder,
                             "{sv}",
                             "auth.no_user_interaction", g_variant_new_boolean (TRUE));
    }
  if (is_setup)
    {
      if (opt_loop_read_only)
        g_variant_builder_add (&builder,
                               "{sv}",
                               "read-only", g_variant_new_boolean (TRUE));
      if (opt_loop_offset > 0)
        g_variant_builder_add (&builder,
                               "{sv}",
                               "offset", g_variant_new_uint64 (opt_loop_offset));
      if (opt_loop_size > 0)
        g_variant_builder_add (&builder,
                               "{sv}",
                               "size", g_variant_new_uint64 (opt_loop_size));
    }
  options = g_variant_builder_end (&builder);
  g_variant_ref_sink (options);

  if (is_setup)
    {
      gchar *resulting_object_path;
      UDisksObject *resulting_object;
      GUnixFDList *fd_list;
      gint fd;
      gboolean rc;

      if (opt_loop_file == NULL)
        {
          s = g_option_context_get_help (o, FALSE, NULL);
          g_printerr ("%s", s);
          g_free (s);
          goto out;
        }

      fd = open (opt_loop_file, opt_loop_read_only ? O_RDONLY : O_RDWR);
      if (fd == -1)
        {
          g_printerr ("Error opening (%s) file %s: %m\n",
                      opt_loop_read_only ? "ro" : "rw",
                      opt_loop_file);
          goto out;
        }
      fd_list = g_unix_fd_list_new_from_array (&fd, 1); /* adopts the fd */

    setup_try_again:
      error = NULL;
      rc = udisks_manager_call_loop_setup_sync (udisks_client_get_manager (client),
                                                g_variant_new_handle (0),
                                                options,
                                                fd_list,
                                                &resulting_object_path,
                                                NULL,                       /* out_fd_list */
                                                NULL,                       /* GCancellable */
                                                &error);
      if (!rc)
        {
          if (error->domain == UDISKS_ERROR &&
              error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
              setup_local_polkit_agent ())
            {
              g_clear_error (&error);
              goto setup_try_again;
            }
          g_object_unref (fd_list);
          g_printerr ("Error setting up loop device for %s: %s\n",
                      opt_loop_file,
                      error->message);
          g_clear_error (&error);
          goto out;
        }
      g_object_unref (fd_list);
      udisks_client_settle (client);

      resulting_object = UDISKS_OBJECT (g_dbus_object_manager_get_object (udisks_client_get_object_manager (client),
                                                                          (resulting_object_path)));
      g_print ("Mapped file %s as %s.\n",
               opt_loop_file,
               udisks_block_get_device (udisks_object_get_block (resulting_object)));
      g_object_unref (resulting_object);
      g_free (resulting_object_path);
    }
  else
    {
      if (opt_loop_object_path != NULL)
        {
          object = lookup_object_by_path (opt_loop_object_path);
          if (object == NULL)
            {
              g_printerr ("Error looking up object with path %s\n", opt_loop_object_path);
              goto out;
            }
        }
      else if (opt_loop_device != NULL)
        {
          object = lookup_object_by_device (opt_loop_device);
          if (object == NULL)
            {
              g_printerr ("Error looking up object for device %s\n", opt_loop_device);
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

      if (udisks_object_peek_loop (object) == NULL)
        {
          g_printerr ("Error: specified object is not a loop device\n");
          goto out;
        }

    delete_try_again:
      error = NULL;
      if (!udisks_loop_call_delete_sync (udisks_object_peek_loop (object),
                                         options,
                                         NULL,                       /* GCancellable */
                                         &error))
        {
          if (error->domain == UDISKS_ERROR &&
              error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
              setup_local_polkit_agent ())
            {
              g_clear_error (&error);
              goto delete_try_again;
            }
          g_printerr ("Error deleting loop device %s: %s\n",
                      udisks_block_get_device (udisks_object_peek_block (object)),
                      error->message);
          g_clear_error (&error);
          goto out;
        }
      g_object_unref (object);
    }

  ret = 0;

 out:
  if (options != NULL)
    g_variant_unref (options);
  g_option_context_free (o);
  g_free (opt_loop_file);
  g_free (opt_loop_object_path);
  g_free (opt_loop_device);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar   *opt_smart_simulate_file = NULL;
static gchar   *opt_smart_simulate_object_path = NULL;
static gchar   *opt_smart_simulate_device = NULL;
static gboolean opt_smart_simulate_no_user_interaction = FALSE;

static const GOptionEntry command_smart_simulate_entries[] =
{
  {
    "file",
    'f',
    0,
    G_OPTION_ARG_FILENAME,
    &opt_smart_simulate_file,
    "File with libatasmart blob",
    NULL
  },
  {
    "object-path",
    'p',
    0,
    G_OPTION_ARG_STRING,
    &opt_smart_simulate_object_path,
    "Object path for ATA device",
    NULL
  },
  {
    "block-device",
    'b',
    0,
    G_OPTION_ARG_STRING,
    &opt_smart_simulate_device,
    "Device file for ATA device",
    NULL
  },
  {
    "no-user-interaction",
    0, /* no short option */
    0,
    G_OPTION_ARG_NONE,
    &opt_smart_simulate_no_user_interaction,
    "Do not authenticate the user if needed",
    NULL
  },
  {
    NULL
  }
};

static gint
handle_command_smart_simulate (gint        *argc,
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
  gboolean complete_files;
  GList *l;
  GList *objects;
  UDisksObject *object;
  UDisksDriveAta *ata;
  guint n;
  GVariant *options;
  GVariantBuilder builder;
  GError *error;

  ret = 1;
  opt_smart_simulate_object_path = NULL;
  opt_smart_simulate_device = NULL;
  object = NULL;
  options = NULL;

  modify_argv0_for_command (argc, argv, "smart-simulate");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, "Set SMART data for drive.");
  g_option_context_add_main_entries (o,
                                     command_smart_simulate_entries,
                                     NULL /* GETTEXT_PACKAGE*/);

  complete_objects = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--object-path") == 0 || g_strcmp0 (completion_prev, "-p") == 0 ||
                             g_strcmp0 (completion_cur, "--object-path") == 0 || g_strcmp0 (completion_cur, "-p") == 0))
    {
      complete_objects = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_devices = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--block-device") == 0 || g_strcmp0 (completion_prev, "-b") == 0 ||
                             g_strcmp0 (completion_cur, "--block-device") == 0 || g_strcmp0 (completion_cur, "-b") == 0))
    {
      complete_devices = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_files = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--file") == 0 || g_strcmp0 (completion_prev, "-f") == 0 ||
                             g_strcmp0 (completion_cur, "--file") == 0 || g_strcmp0 (completion_cur, "-f") == 0))
    {
      complete_files = TRUE;
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

  if (request_completion)
    {
      if (opt_smart_simulate_file == NULL && !complete_files && !complete_objects && !complete_devices)
        {
          g_print ("--file \n");
        }

      if (complete_files)
        {
          g_print ("@FILES@");
          goto out;
        }

      if ((opt_smart_simulate_object_path == NULL && !complete_objects) &&
          (opt_smart_simulate_device == NULL && !complete_devices))
        {
          g_print ("--object-path \n"
                   "--block-device \n");
        }

      if (complete_objects)
        {
          const gchar *object_path;
          objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
          for (l = objects; l != NULL; l = l->next)
            {
              object = UDISKS_OBJECT (l->data);
              ata = udisks_object_peek_drive_ata (object);
              if (ata != NULL)
                {
                  object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
                  g_assert (g_str_has_prefix (object_path, "/org/freedesktop/UDisks2/"));
                  g_print ("%s \n", object_path + sizeof ("/org/freedesktop/UDisks2/") - 1);
                }
            }
          g_list_free_full (objects, g_object_unref);
        }

      if (complete_devices)
        {
          objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
          for (l = objects; l != NULL; l = l->next)
            {
              object = UDISKS_OBJECT (l->data);
              ata = udisks_object_peek_drive_ata (object);
              if (ata != NULL)
                {
                  const gchar * const *symlinks;
                  UDisksBlock *block;
                  block = udisks_client_get_block_for_drive (client, udisks_object_peek_drive (object), TRUE);
                  g_print ("%s \n", udisks_block_get_device (block));
                  symlinks = udisks_block_get_symlinks (block);
                  for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
                    g_print ("%s \n", symlinks[n]);
                }
            }
          g_list_free_full (objects, g_object_unref);
        }
      goto out;
    }

  if (opt_smart_simulate_file == NULL)
    {
      s = g_option_context_get_help (o, FALSE, NULL);
      g_printerr ("%s", s);
      g_free (s);
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  if (opt_smart_simulate_no_user_interaction)
    {
      g_variant_builder_add (&builder,
                             "{sv}",
                             "auth.no_user_interaction", g_variant_new_boolean (TRUE));
    }
  g_variant_builder_add (&builder,
                         "{sv}",
                         "atasmart_blob", g_variant_new_string (opt_smart_simulate_file));
  options = g_variant_builder_end (&builder);
  g_variant_ref_sink (options);

  if (opt_smart_simulate_object_path != NULL)
    {
      object = lookup_object_by_path (opt_smart_simulate_object_path);
      if (object == NULL)
        {
          g_printerr ("Error looking up object with path %s\n", opt_smart_simulate_object_path);
          goto out;
        }
    }
  else if (opt_smart_simulate_device != NULL)
    {
      UDisksObject *block_object;
      UDisksDrive *drive;
      block_object = lookup_object_by_device (opt_smart_simulate_device);
      if (block_object == NULL)
        {
          g_printerr ("Error looking up object for device %s\n", opt_smart_simulate_device);
          goto out;
        }
      drive = udisks_client_get_drive_for_block (client, udisks_object_peek_block (block_object));
      object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (drive));
      g_object_unref (block_object);
    }
  else
    {
      s = g_option_context_get_help (o, FALSE, NULL);
      g_printerr ("%s", s);
      g_free (s);
      goto out;
    }

  if (udisks_object_peek_drive_ata (object) == NULL)
    {
      g_printerr ("Device %s is not an ATA device\n",
                  udisks_block_get_device (udisks_object_peek_block (object)));
      g_object_unref (object);
      goto out;
    }

 try_again:
  error = NULL;
  if (!udisks_drive_ata_call_smart_update_sync (udisks_object_peek_drive_ata (object),
                                                options,
                                                NULL,                       /* GCancellable */
                                                &error))
    {
      if (error->domain == UDISKS_ERROR &&
          error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
          setup_local_polkit_agent ())
        {
          g_clear_error (&error);
          goto try_again;
        }
      g_dbus_error_strip_remote_error (error);
      g_printerr ("Error updating SMART data: %s (%s, %d)\n",
                  error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      g_object_unref (object);
      goto out;
    }

  g_object_unref (object);


  ret = 0;

 out:
  if (options != NULL)
    g_variant_unref (options);
  g_option_context_free (o);
  g_free (opt_smart_simulate_file);
  g_free (opt_smart_simulate_object_path);
  g_free (opt_smart_simulate_device);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar   *opt_power_off_object_path = NULL;
static gchar   *opt_power_off_device = NULL;
static gboolean opt_power_off_no_user_interaction = FALSE;

static const GOptionEntry command_power_off_entries[] =
{
  {
    "object-path",
    'p',
    0,
    G_OPTION_ARG_STRING,
    &opt_power_off_object_path,
    "Object path for ATA device",
    NULL
  },
  {
    "block-device",
    'b',
    0,
    G_OPTION_ARG_STRING,
    &opt_power_off_device,
    "Device file for ATA device",
    NULL
  },
  {
    "no-user-interaction",
    0, /* no short option */
    0,
    G_OPTION_ARG_NONE,
    &opt_power_off_no_user_interaction,
    "Do not authenticate the user if needed",
    NULL
  },
  {
    NULL
  }
};

static gint
handle_command_power_off (gint        *argc,
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
  GList *l;
  GList *objects;
  UDisksObject *object;
  UDisksDriveAta *ata;
  guint n;
  GVariant *options;
  GVariantBuilder builder;
  GError *error;

  ret = 1;
  opt_power_off_object_path = NULL;
  opt_power_off_device = NULL;
  object = NULL;
  options = NULL;

  modify_argv0_for_command (argc, argv, "power-off");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, "Safely power off a drive.");
  g_option_context_add_main_entries (o,
                                     command_power_off_entries,
                                     NULL /* GETTEXT_PACKAGE*/);

  complete_objects = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--object-path") == 0 || g_strcmp0 (completion_prev, "-p") == 0 ||
                             g_strcmp0 (completion_cur, "--object-path") == 0 || g_strcmp0 (completion_cur, "-p") == 0))
    {
      complete_objects = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_devices = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--block-device") == 0 || g_strcmp0 (completion_prev, "-b") == 0 ||
                             g_strcmp0 (completion_cur, "--block-device") == 0 || g_strcmp0 (completion_cur, "-b") == 0))
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

  if (request_completion)
    {
      if ((opt_power_off_object_path == NULL && !complete_objects) &&
          (opt_power_off_device == NULL && !complete_devices))
        {
          g_print ("--object-path \n"
                   "--block-device \n");
        }

      if (complete_objects)
        {
          const gchar *object_path;
          objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
          for (l = objects; l != NULL; l = l->next)
            {
              object = UDISKS_OBJECT (l->data);
              ata = udisks_object_peek_drive_ata (object);
              if (ata != NULL)
                {
                  object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
                  g_assert (g_str_has_prefix (object_path, "/org/freedesktop/UDisks2/"));
                  g_print ("%s \n", object_path + sizeof ("/org/freedesktop/UDisks2/") - 1);
                }
            }
          g_list_free_full (objects, g_object_unref);
        }

      if (complete_devices)
        {
          objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
          for (l = objects; l != NULL; l = l->next)
            {
              object = UDISKS_OBJECT (l->data);
              ata = udisks_object_peek_drive_ata (object);
              if (ata != NULL)
                {
                  const gchar * const *symlinks;
                  UDisksBlock *block;
                  block = udisks_client_get_block_for_drive (client, udisks_object_peek_drive (object), TRUE);
                  g_print ("%s \n", udisks_block_get_device (block));
                  symlinks = udisks_block_get_symlinks (block);
                  for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
                    g_print ("%s \n", symlinks[n]);
                }
            }
          g_list_free_full (objects, g_object_unref);
        }
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  if (opt_power_off_no_user_interaction)
    {
      g_variant_builder_add (&builder,
                             "{sv}",
                             "auth.no_user_interaction", g_variant_new_boolean (TRUE));
    }
  options = g_variant_builder_end (&builder);
  g_variant_ref_sink (options);

  if (opt_power_off_object_path != NULL)
    {
      object = lookup_object_by_path (opt_power_off_object_path);
      if (object == NULL)
        {
          g_printerr ("Error looking up object with path %s\n", opt_power_off_object_path);
          goto out;
        }
    }
  else if (opt_power_off_device != NULL)
    {
      UDisksObject *block_object;
      UDisksDrive *drive;
      block_object = lookup_object_by_device (opt_power_off_device);
      if (block_object == NULL)
        {
          g_printerr ("Error looking up object for device %s\n", opt_power_off_device);
          goto out;
        }
      drive = udisks_client_get_drive_for_block (client, udisks_object_peek_block (block_object));
      object = (UDisksObject *) g_dbus_interface_dup_object (G_DBUS_INTERFACE (drive));
      g_object_unref (block_object);
    }
  else
    {
      s = g_option_context_get_help (o, FALSE, NULL);
      g_printerr ("%s", s);
      g_free (s);
      goto out;
    }

 try_again:
  error = NULL;
  if (!udisks_drive_call_power_off_sync (udisks_object_peek_drive (object),
                                         options,
                                         NULL,                       /* GCancellable */
                                         &error))
    {
      if (error->domain == UDISKS_ERROR &&
          error->code == UDISKS_ERROR_NOT_AUTHORIZED_CAN_OBTAIN &&
          setup_local_polkit_agent ())
        {
          g_clear_error (&error);
          goto try_again;
        }
      g_dbus_error_strip_remote_error (error);
      g_printerr ("Error powering off drive: %s (%s, %d)\n",
                  error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      g_object_unref (object);
      goto out;
    }

  g_object_unref (object);


  ret = 0;

 out:
  if (options != NULL)
    g_variant_unref (options);
  g_option_context_free (o);
  g_free (opt_power_off_object_path);
  g_free (opt_power_off_device);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *opt_info_object = NULL;
static gchar *opt_info_device = NULL;
static gchar *opt_info_drive = NULL;

static const GOptionEntry command_info_entries[] =
{
  { "object-path", 'p', 0, G_OPTION_ARG_STRING, &opt_info_object, "Object to get information about", NULL},
  { "block-device", 'b', 0, G_OPTION_ARG_STRING, &opt_info_device, "Block device to get information about", NULL},
  { "drive", 'd', 0, G_OPTION_ARG_STRING, &opt_info_drive, "Drive to get information about", NULL},
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
  GList *l;
  GList *objects;
  UDisksObject *object;
  UDisksBlock *block;
  UDisksDrive *drive;
  guint n;

  ret = 1;
  opt_info_object = NULL;
  opt_info_device = NULL;
  opt_info_drive = NULL;

  modify_argv0_for_command (argc, argv, "info");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, "Show information about an object.");
  g_option_context_add_main_entries (o, command_info_entries, NULL /* GETTEXT_PACKAGE*/);

  complete_objects = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--object-path") == 0 || g_strcmp0 (completion_prev, "-p") == 0 ||
                             g_strcmp0 (completion_cur, "--object-path") == 0 || g_strcmp0 (completion_cur, "-p") == 0))
    {
      complete_objects = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_devices = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--block-device") == 0 || g_strcmp0 (completion_prev, "-b") == 0 ||
                             g_strcmp0 (completion_cur, "--block-device") == 0 || g_strcmp0 (completion_cur, "-b") == 0))
    {
      complete_devices = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_drives = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--drive") == 0 || g_strcmp0 (completion_prev, "-d") == 0 ||
                             g_strcmp0 (completion_cur, "--drive") == 0 || g_strcmp0 (completion_cur, "-d") == 0))
    {
      complete_drives = TRUE;
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
      (opt_info_drive == NULL && !complete_drives))
    {
      g_print ("--object-path \n"
               "--block-device \n"
               "--drive \n");
    }

  if (complete_objects)
    {
      const gchar *object_path;

      objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
      for (l = objects; l != NULL; l = l->next)
        {
          object = UDISKS_OBJECT (l->data);

          object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
          g_assert (g_str_has_prefix (object_path, "/org/freedesktop/UDisks2/"));
          g_print ("%s \n", object_path + sizeof ("/org/freedesktop/UDisks2/") - 1);
        }
      g_list_free_full (objects, g_object_unref);
      goto out;
    }

  if (complete_devices)
    {
      objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
      for (l = objects; l != NULL; l = l->next)
        {
          object = UDISKS_OBJECT (l->data);
          block = udisks_object_peek_block (object);
          if (block != NULL)
            {
              const gchar * const *symlinks;
              g_print ("%s \n", udisks_block_get_device (block));
              symlinks = udisks_block_get_symlinks (block);
              for (n = 0; symlinks != NULL && symlinks[n] != NULL; n++)
                g_print ("%s \n", symlinks[n]);
            }
        }
      g_list_free_full (objects, g_object_unref);
      goto out;
    }

  if (complete_drives)
    {
      objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
      for (l = objects; l != NULL; l = l->next)
        {
          object = UDISKS_OBJECT (l->data);
          drive = udisks_object_peek_drive (object);
          if (drive != NULL)
            {
              const gchar *base;
              base = g_strrstr (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), "/") + 1;
              g_print ("%s \n", base);
            }
        }
      g_list_free_full (objects, g_object_unref);
      goto out;
    }

  /* done with completion */
  if (request_completion)
    goto out;


  if (opt_info_object != NULL)
    {
      object = lookup_object_by_path (opt_info_object);
      if (object == NULL)
        {
          g_printerr ("Error looking up object with path %s\n", opt_info_object);
          goto out;
        }
    }
  else if (opt_info_device != NULL)
    {
      object = lookup_object_by_device (opt_info_device);
      if (object == NULL)
        {
          g_printerr ("Error looking up object for device %s\n", opt_info_device);
          goto out;
        }
    }
  else if (opt_info_drive != NULL)
    {
      object = lookup_object_by_drive (opt_info_drive);
      if (object == NULL)
        {
          g_printerr ("Error looking up object for drive %s\n", opt_info_drive);
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
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE), g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), _color_get (_COLOR_RESET));
  print_object (object, 2);
  g_object_unref (object);

  ret = 0;

 out:
  g_option_context_free (o);
  g_free (opt_info_object);
  g_free (opt_info_device);
  g_free (opt_info_drive);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */


static gint
obj_proxy_cmp (GDBusObject *a,
               GDBusObject *b)
{
  return g_strcmp0 (g_dbus_object_get_object_path (a), g_dbus_object_get_object_path (b));
}

static gint
obj_proxy_drive_sortkey_cmp (GDBusObject *a,
                             GDBusObject *b)
{
  UDisksDrive *da = udisks_object_peek_drive (UDISKS_OBJECT (a));
  UDisksDrive *db = udisks_object_peek_drive (UDISKS_OBJECT (b));
  if (da != NULL && db != NULL)
    return g_strcmp0 (udisks_drive_get_sort_key (da), udisks_drive_get_sort_key (db));
  else
    return obj_proxy_cmp (a, b);
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
  GList *objects;
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

  objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));
  /* We want to print the objects in order */
  objects = g_list_sort (objects, (GCompareFunc) obj_proxy_cmp);
  first = TRUE;
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      if (!first)
        g_print ("\n");
      first = FALSE;
      g_print ("%s%s%s:%s\n",
               _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE), g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), _color_get (_COLOR_RESET));
      print_object (object, 2);
    }
  g_list_free_full (objects, g_object_unref);

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
  name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (udisks_client_get_object_manager (client)));
  ret = (name_owner != NULL);
  g_free (name_owner);
  return ret;
}

static void
monitor_print_name_owner (void)
{
  gchar *name_owner;
  name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (udisks_client_get_object_manager (client)));
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
monitor_on_object_added (GDBusObjectManager  *manager,
                         GDBusObject         *object,
                         gpointer             user_data)
{
  if (!monitor_has_name_owner ())
    goto out;
  monitor_print_timestamp ();
  g_print ("%s%sAdded %s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_GREEN),
             g_dbus_object_get_object_path (object),
           _color_get (_COLOR_RESET));
  print_object (UDISKS_OBJECT (object), 2);
 out:
  ;
}

static void
monitor_on_object_removed (GDBusObjectManager *manager,
                           GDBusObject        *object,
                           gpointer            user_data)
{
  if (!monitor_has_name_owner ())
    goto out;
  monitor_print_timestamp ();
  g_print ("%s%sRemoved %s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_RED),
             g_dbus_object_get_object_path (object),
           _color_get (_COLOR_RESET));
 out:
  ;
}

static void
monitor_on_interface_proxy_added (GDBusObjectManager  *manager,
                                  GDBusObject         *object,
                                  GDBusInterface      *interface,
                                  gpointer             user_data)
{
  if (!monitor_has_name_owner ())
    goto out;
  monitor_print_timestamp ();
  g_print ("%s%s%s:%s %s%sAdded interface %s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE),
             g_dbus_object_get_object_path (object),
           _color_get (_COLOR_RESET),
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_GREEN),
             g_dbus_proxy_get_interface_name (G_DBUS_PROXY (interface)),
           _color_get (_COLOR_RESET));

  print_interface_properties (G_DBUS_PROXY (interface), 2);
 out:
  ;
}

static void
monitor_on_interface_proxy_removed (GDBusObjectManager  *manager,
                                    GDBusObject         *object,
                                    GDBusInterface      *interface,
                                    gpointer             user_data)
{
  if (!monitor_has_name_owner ())
    goto out;
  monitor_print_timestamp ();
  g_print ("%s%s%s:%s %s%sRemoved interface %s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE),
             g_dbus_object_get_object_path (object),
           _color_get (_COLOR_RESET),
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_RED),
             g_dbus_proxy_get_interface_name (G_DBUS_PROXY (interface)),
           _color_get (_COLOR_RESET));
 out:
  ;
}

static void
monitor_on_interface_proxy_properties_changed (GDBusObjectManagerClient *manager,
                                               GDBusObjectProxy         *object_proxy,
                                               GDBusProxy               *interface_proxy,
                                               GVariant                 *changed_properties,
                                               const gchar* const       *invalidated_properties,
                                               gpointer                  user_data)
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
             g_dbus_object_get_object_path (G_DBUS_OBJECT (object_proxy)),
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
monitor_on_interface_proxy_signal (GDBusObjectManagerClient  *manager,
                                   GDBusObjectProxy          *object_proxy,
                                   GDBusProxy                *interface_proxy,
                                   const gchar               *sender_name,
                                   const gchar               *signal_name,
                                   GVariant                  *parameters,
                                   gpointer                   user_data)
{
  gchar *param_str;
  if (!monitor_has_name_owner ())
    goto out;

  param_str = g_variant_print (parameters, TRUE);
  monitor_print_timestamp ();

  g_print ("%s%s%s:%s %s%s%s%s%s%s::%s%s %s%s%s%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_BLUE),
           g_dbus_object_get_object_path (G_DBUS_OBJECT (object_proxy)),
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
  GDBusObjectManager *manager;

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

  manager = udisks_client_get_object_manager (client);
  g_signal_connect (manager,
                    "notify::name-owner",
                    G_CALLBACK (monitor_on_notify_name_owner),
                    NULL);

  g_signal_connect (manager,
                    "object-added",
                    G_CALLBACK (monitor_on_object_added),
                    NULL);
  g_signal_connect (manager,
                    "object-removed",
                    G_CALLBACK (monitor_on_object_removed),
                    NULL);
  g_signal_connect (manager,
                    "interface-added",
                    G_CALLBACK (monitor_on_interface_proxy_added),
                    NULL);
  g_signal_connect (manager,
                    "interface-removed",
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

static GList *
find_blocks_for_drive (GList       *objects,
                       const gchar *drive_object_path)
{
  GList *ret;
  GList *l;

  ret = NULL;
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksBlock *block;

      block = udisks_object_get_block (object);
      if (block == NULL)
        continue;

      if (g_strcmp0 (udisks_block_get_drive (block), drive_object_path) == 0)
        {
          ret = g_list_append (ret, g_object_ref (block));
        }
      g_object_unref (block);
    }
  return ret;
}

static const GOptionEntry command_status_entries[] =
{
  { NULL }
};

#if 0
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
#endif

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
  GList *objects;

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

  objects = g_dbus_object_manager_get_objects (udisks_client_get_object_manager (client));

  /* print all drives
   *
   * We are guaranteed that, usually,
   *
   *  - model     <= 16   (SCSI: 16, ATA: 40)
   *  - vendor    <= 8    (SCSI: 8, ATA: 0)
   *  - revision  <= 8    (SCSI: 6, ATA: 8)
   *  - serial    <= 20   (SCSI: 16, ATA: 20)
   */
  g_print ("MODEL                     REVISION  SERIAL               DEVICE\n"
           "--------------------------------------------------------------------------\n");
         /* SEAGATE ST3300657SS       0006      3SJ1QNMQ00009052NECM sdaa sdab dm-32   */
         /* 01234567890123456789012345678901234567890123456789012345678901234567890123456789 */

  /* sort on Drive:SortKey */
  objects = g_list_sort (objects, (GCompareFunc) obj_proxy_drive_sortkey_cmp);
  for (l = objects; l != NULL; l = l->next)
    {
      UDisksObject *object = UDISKS_OBJECT (l->data);
      UDisksDrive *drive;
      GList *blocks;
      const gchar *vendor;
      const gchar *model;
      const gchar *revision;
      const gchar *serial;
      gchar *vendor_model;
      GString *str;
      gchar *block;
      GList *j;

      drive = udisks_object_peek_drive (object);
      if (drive == NULL)
        continue;

      str = g_string_new (NULL);
      blocks = find_blocks_for_drive (objects, g_dbus_object_get_object_path (G_DBUS_OBJECT (object)));
      for (j = blocks; j != NULL; j = j->next)
        {
          UDisksBlock *block_iface = UDISKS_BLOCK (j->data);
          GDBusObject *block_object;
          UDisksPartition *partition;
          block_object = g_dbus_interface_get_object (G_DBUS_INTERFACE (block_iface));
          partition = block_object == NULL ? NULL : udisks_object_peek_partition (UDISKS_OBJECT (block_object));
          if (partition == NULL)
            {
              const gchar *device_file;
              if (str->len > 0)
                g_string_append (str, " ");
              device_file = udisks_block_get_device (block_iface);
              if (g_str_has_prefix (device_file, "/dev/"))
                g_string_append (str, device_file + 5);
              else
                g_string_append (str, device_file);
            }
        }
      if (str->len == 0)
        g_string_append (str, "-");
      block = g_string_free (str, FALSE);
      g_list_free_full (blocks, g_object_unref);

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

      /* TODO: would be nice to show the port/slot if disk is in a SES-2 enclosure */
      g_print ("%-25s %-9s %-20s %-8s\n",
               vendor_model,
               revision,
               serial,
               block);
      g_free (block);
    }


  g_list_free_full (objects, g_object_unref);

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
  static GOptionEntry entries[] = { { NULL } };
  gchar *description;
  gchar *s;
  gchar *program_name;

  o = g_option_context_new ("COMMAND");
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_add_main_entries (o, entries, NULL);
  /* Ignore parsing result */
  /* coverity[check_return] */
  g_option_context_parse (o, argc, argv, NULL);
  program_name = g_path_get_basename ((*argv)[0]);
  description = g_strdup_printf ("Commands:\n"
                       "  help            Shows this information\n"
                       "  info            Shows information about an object\n"
                       "  dump            Shows information about all objects\n"
                       "  status          Shows high-level status\n"
                       "  monitor         Monitor changes to objects\n"
                       "  mount           Mount a filesystem\n"
                       "  unmount         Unmount a filesystem\n"
                       "  unlock          Unlock an encrypted device\n"
                       "  lock            Lock an encrypted device\n"
                       "  loop-setup      Set-up a loop device\n"
                       "  loop-delete     Delete a loop device\n"
                       "  power-off       Safely power off a drive\n"
                       "  smart-simulate  Set SMART data for a drive\n"
                       "\n"
                       "Use \"%s COMMAND --help\" to get help on each command.\n",
                       program_name);
  g_free (program_name);
  g_option_context_set_description (o, description);
  s = g_option_context_get_help (o, FALSE, NULL);
  if (use_stdout)
    g_print ("%s", s);
  else
    g_printerr ("%s", s);
  g_free (s);
  g_free (description);
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
  size_t len = strlen(s);

  if (cursor < 0)
    cursor = 0;
  else if ((size_t) cursor >= len)
    cursor = len - 1;

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

  ret = 1;
  completion_cur = NULL;
  completion_prev = NULL;
  loop = NULL;

  _color_init ();

  setlocale (LC_ALL, "");

  if (argc < 2)
    {
      usage (&argc, &argv, FALSE);
      goto out;
    }

  loop = g_main_loop_new (NULL, FALSE);

  error = NULL;
  client = udisks_client_new_sync (NULL, /* GCancellable */
                                   &error);
  if (client == NULL)
    {
      g_printerr ("Error connecting to the udisks daemon: %s\n", error->message);
      g_clear_error (&error);
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
  else if (g_strcmp0 (command, "unlock") == 0 || g_strcmp0 (command, "lock") == 0)
    {
      ret = handle_command_unlock_lock (&argc,
                                        &argv,
                                        request_completion,
                                        completion_cur,
                                        completion_prev,
                                        g_strcmp0 (command, "unlock") == 0);
      goto out;
    }
  else if (g_strcmp0 (command, "loop-setup") == 0 || g_strcmp0 (command, "loop-delete") == 0)
    {
      ret = handle_command_loop (&argc,
                                 &argv,
                                 request_completion,
                                 completion_cur,
                                 completion_prev,
                                 g_strcmp0 (command, "loop-setup") == 0);
      goto out;
    }
  else if (g_strcmp0 (command, "smart-simulate") == 0)
    {
      ret = handle_command_smart_simulate (&argc,
                                           &argv,
                                           request_completion,
                                           completion_cur,
                                           completion_prev);
      goto out;
    }
  else if (g_strcmp0 (command, "power-off") == 0)
    {
      ret = handle_command_power_off (&argc,
                                      &argv,
                                      request_completion,
                                      completion_cur,
                                      completion_prev);
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
                   "lock \n"
                   "unlock \n"
                   "loop-setup \n"
                   "loop-delete \n"
                   "power-off \n"
                   "smart-simulate \n"
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
  if (client != NULL)
    g_object_unref (client);
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
