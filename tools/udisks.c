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

/* TODO: Temporary include */
#include <gdbusproxymanager.h>

static GDBusProxyManager *manager = NULL;
static GMainLoop *loop = NULL;

/* Uncomment to get debug traces in /tmp/udisks-completion-debug.txt - use tail(1) to
 * inspect this file
 */
/* #define COMPLETION_DEBUG */
//#define COMPLETION_DEBUG

/* ---------------------------------------------------------------------------------------------------- */

G_GNUC_UNUSED static void completion_debug (const gchar *format, ...);

static void remove_arg (gint num, gint *argc, gchar **argv[]);
static void modify_argv0_for_command (gint *argc, gchar **argv[], const gchar *command);

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

      g_print ("%*s%s: %*s%s\n",
               indent, "",
               property_name,
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

  g_print ("%*s%s:\n",
           indent, "", g_dbus_object_proxy_get_object_path (proxy));

  interface_proxies = g_dbus_object_proxy_get_all (proxy);

  /* We want to print the interfaces in order */
  interface_proxies = g_list_sort (interface_proxies, (GCompareFunc) if_proxy_cmp);

  for (l = interface_proxies; l != NULL; l = l->next)
    {
      GDBusProxy *iproxy = G_DBUS_PROXY (l->data);
      g_print ("%*s%s:\n",
               indent + 2, "", g_dbus_proxy_get_interface_name (iproxy));
      print_interface_properties (iproxy, indent + 4);
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

/* ---------------------------------------------------------------------------------------------------- */

static gchar *opt_info_object = NULL;
static gchar *opt_info_device = NULL;

static const GOptionEntry command_info_entries[] =
{
  { "object", 'o', 0, G_OPTION_ARG_STRING, &opt_info_object, "Object to get information about", NULL},
  { "device", 'd', 0, G_OPTION_ARG_STRING, &opt_info_device, "Device file to get information about", NULL},
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
  GList *l;
  GList *object_proxies;
  GDBusObjectProxy *object_proxy;
  UDisksBlockDevice *block;
  guint n;

  ret = 1;
  opt_info_object = NULL;
  opt_info_device = NULL;

  modify_argv0_for_command (argc, argv, "info");

  o = g_option_context_new (NULL);
  if (request_completion)
    g_option_context_set_ignore_unknown_options (o, TRUE);
  g_option_context_set_help_enabled (o, FALSE);
  g_option_context_set_summary (o, "Show information about an object.");
  g_option_context_add_main_entries (o, command_info_entries, NULL /* GETTEXT_PACKAGE*/);

  complete_objects = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--object") == 0 || g_strcmp0 (completion_prev, "-o") == 0))
    {
      complete_objects = TRUE;
      remove_arg ((*argc) - 1, argc, argv);
    }

  complete_devices = FALSE;
  if (request_completion && (g_strcmp0 (completion_prev, "--device") == 0 || g_strcmp0 (completion_prev, "-d") == 0))
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
      (opt_info_object == NULL && !complete_objects) &&
      (opt_info_device == NULL && !complete_devices))
    {
      g_print ("--object \n"
               "--device \n");
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
  else
    {
      s = g_option_context_get_help (o, FALSE, NULL);
      g_printerr ("%s", s);
      g_free (s);
      goto out;
    }

  print_object (object_proxy, 0);
  g_object_unref (object_proxy);

  ret = 0;

 out:
  g_option_context_free (o);
  g_free (opt_info_object);
  g_free (opt_info_device);
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
      print_object (object_proxy, 0);
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

  g_print ("%s.%03d: ", time_buf, (gint) now.tv_usec / 1000);
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
  g_print ("Added %s\n",
           g_dbus_object_proxy_get_object_path (object_proxy));
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
  g_print ("Removed %s\n",
           g_dbus_object_proxy_get_object_path (object_proxy));
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
  g_print ("%s: Added interface %s\n",
           g_dbus_object_proxy_get_object_path (object_proxy),
           g_dbus_proxy_get_interface_name (interface_proxy));

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
  g_print ("%s: Removed interface %s\n",
           g_dbus_object_proxy_get_object_path (object_proxy),
           g_dbus_proxy_get_interface_name (interface_proxy));
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
  g_print ("%s: %s: Properties Changed\n",
           g_dbus_object_proxy_get_object_path (object_proxy),
           g_dbus_proxy_get_interface_name (interface_proxy));

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

      g_print ("  %s: %*s%s\n",
               property_name,
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
  g_print ("%s: Received signal %s::%s %s\n",
           g_dbus_object_proxy_get_object_path (object_proxy),
           g_dbus_proxy_get_interface_name (interface_proxy),
           signal_name,
           param_str);
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
                       "  monitor      Monitor changes to objects\n"
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

/* TODO: should get this function from the codegen */
static GType
get_proxy_type_func (GDBusProxyManager *manager,
                     const gchar       *object_path,
                     const gchar       *interface_name,
                     gpointer           user_data)
{
  if (g_strcmp0 (interface_name, "org.freedesktop.UDisks.BlockDevice") == 0)
    return UDISKS_TYPE_BLOCK_DEVICE_PROXY;
  else
    return G_TYPE_DBUS_PROXY;
}

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
  manager = NULL;
  loop = NULL;

  g_type_init ();

  if (argc < 2)
    {
      usage (&argc, &argv, FALSE);
      goto out;
    }

  loop = g_main_loop_new (NULL, FALSE);

  error = NULL;
  manager = g_dbus_proxy_manager_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                   G_DBUS_PROXY_MANAGER_FLAGS_NONE,
                                                   "org.freedesktop.UDisks",
                                                   "/org/freedesktop/UDisks",
                                                   get_proxy_type_func, /* TODO: codegen */
                                                   NULL, /* user_data for get_proxy_func */
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
      f = fopen ("/tmp/udisks-completion-debug.txt", "a+");
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
