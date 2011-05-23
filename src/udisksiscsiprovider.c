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

/* TODO:
 *
 *  - instead of parsing /var/lib/iscsi, we should probably run the
 *    command 'iscsadm -m node -P 1' and parse the output
 *
 *  - need to somehow get reliable change notifications when
 *    iscsiadm's database has changed
 *
 *  - there is currently no way to get/set properties for each
 *    connection/path - this is really needed especially for
 *    e.g. setting up authentication
 *
 *  - there is no way to add/remove targets and add/remove paths -
 *    this should use a discovery mechanism
 *
 *  - should we expose node.discovery_address, node.discovery_port and
 *    node.discovery_type somehow so the UI can group targets
 *    discovered from a SendTargets server... ugh..
 *
 *  - apparently we don't get any uevent when the state sysfs
 *    attribute changes on an iscsi_connection - TODO: file a bug and
 *    poll until this is fixed
 */

#include <stdio.h>
#include <mntent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string.h>
#include <stdlib.h>

#include <gudev/gudev.h>

#include "udisksdaemon.h"
#include "udisksprovider.h"
#include "udisksmount.h"
#include "udisksmountmonitor.h"
#include "udisksiscsiprovider.h"
#include "udiskslinuxprovider.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udisksiscsiprovider
 * @title: UDisksIScsiProvider
 * @short_description: Provides UDisksIScsiTarget from the open-iscsi database
 *
 * This type provides #UDisksIScsiTarget objects for targets in the
 * open-iscsi database. Additionally, this information is tied
 * together with information to sysfs in order to convey the
 * connection state of each target.
 */

/* ---------------------------------------------------------------------------------------------------- */

static void
diff_sorted_lists (GList *list1,
                   GList *list2,
                   GCompareFunc compare,
                   GList **added,
                   GList **removed)
{
  int order;

  *added = *removed = NULL;

  while (list1 != NULL && list2 != NULL)
    {
      order = (*compare) (list1->data, list2->data);
      if (order < 0)
        {
          *removed = g_list_prepend (*removed, list1->data);
          list1 = list1->next;
        }
      else if (order > 0)
        {
          *added = g_list_prepend (*added, list2->data);
          list2 = list2->next;
        }
      else
        { /* same item */
          list1 = list1->next;
          list2 = list2->next;
        }
    }

  while (list1 != NULL)
    {
      *removed = g_list_prepend (*removed, list1->data);
      list1 = list1->next;
    }
  while (list2 != NULL)
    {
      *added = g_list_prepend (*added, list2->data);
      list2 = list2->next;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
util_compute_object_path (const gchar *base,
                          const gchar *path)
{
  const gchar *basename;
  GString *s;
  guint n;

  g_return_val_if_fail (path != NULL, NULL);

  basename = strrchr (path, '/');
  if (basename != NULL)
    basename++;
  else
    basename = path;

  s = g_string_new (base);
  for (n = 0; basename[n] != '\0'; n++)
    {
      gint c = basename[n];

      /* D-Bus spec sez:
       *
       * Each element must only contain the ASCII characters "[A-Z][a-z][0-9]_"
       */
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
          g_string_append_c (s, c);
        }
      else
        {
          /* Escape bytes not in [A-Z][a-z][0-9] as _<hex-with-two-digits> */
          g_string_append_printf (s, "_%02x", c);
        }
    }

  return g_string_free (s, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  gchar *name;
} IScsiIface;

static void
iscsi_iface_free (IScsiIface *iface)
{
  g_free (iface->name);
  g_free (iface);
}

static gint
iscsi_iface_compare (const IScsiIface *a,
                     const IScsiIface *b)
{
  return g_strcmp0 (a->name, b->name);
}

typedef struct
{
  gchar *address;
  gint port;
  gint tpgt;
  GList *ifaces;
} IScsiPortal;

static void
iscsi_portal_free (IScsiPortal *portal)
{
  g_free (portal->address);
  g_list_foreach (portal->ifaces, (GFunc) iscsi_iface_free, NULL);
  g_list_free (portal->ifaces);
  g_free (portal);
}

static gint
iscsi_portal_compare (const IScsiPortal *a,
                      const IScsiPortal *b)
{
  gint ret;
  GList *l;
  GList *j;

  ret = g_strcmp0 (a->address, b->address);
  if (ret != 0)
    goto out;
  ret = a->port - b->port;
  if (ret != 0)
    goto out;
  ret = a->tpgt - b->tpgt;
  if (ret != 0)
    goto out;
  ret = g_list_length (a->ifaces) - g_list_length (b->ifaces);
  if (ret != 0)
    goto out;
  for (l = a->ifaces, j = b->ifaces; l != NULL; l = l->next, j = j->next)
    {
      IScsiIface *ia = l->data;
      IScsiIface *ib = j->data;
      ret = iscsi_iface_compare (ia, ib);
      if (ret != 0)
        goto out;
    }
  ret = 0;
 out:
  return ret;
}

typedef struct
{
  volatile gint ref_count;

  gchar *target_name;
  gchar *dir_name;

  gchar *object_path;
  UDisksObjectSkeleton *object;
  UDisksIScsiTarget *iface;

  GList *portals;
} IScsiTarget;

static IScsiTarget *
iscsi_target_ref (IScsiTarget *target)
{
  g_atomic_int_inc (&target->ref_count);
  return target;
}

static void
iscsi_target_unref (IScsiTarget *target)
{
  if (g_atomic_int_dec_and_test (&target->ref_count))
    {
      g_free (target->target_name);

      g_free (target->object_path);
      if (target->object != NULL)
        g_object_unref (target->object);
      if (target->iface != NULL)
        g_object_unref (target->iface);

      g_list_foreach (target->portals, (GFunc) iscsi_portal_free, NULL);
      g_list_free (target->portals);

      g_free (target);
    }
}

/* on purpose, this does not take portals/ifaces into account */
static gint
iscsi_target_compare (const IScsiTarget *a,
                      const IScsiTarget *b)
{
  gint ret;

  ret = g_strcmp0 (a->target_name, b->target_name);
  if (ret != 0)
    goto out;

 out:
  return ret;
}

static void load_and_process_iscsi (UDisksIScsiProvider *provider);

/* ---------------------------------------------------------------------------------------------------- */

typedef struct _UDisksIScsiProviderClass   UDisksIScsiProviderClass;

/**
 * UDisksIScsiProvider:
 *
 * The #UDisksIScsiProvider structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksIScsiProvider
{
  UDisksProvider parent_instance;

  UDisksDaemon *daemon;
  GUdevClient *udev_client;

  GFileMonitor *file_monitor;
  guint cool_off_timeout_id;

  GHashTable *sysfs_to_connection;
  GHashTable *id_to_connection;

  GList *targets;
};

struct _UDisksIScsiProviderClass
{
  UDisksProviderClass parent_class;
};

static void         connections_init      (UDisksIScsiProvider *provider);
static void         connections_finalize  (UDisksIScsiProvider *provider);
static const gchar *connections_get_state (UDisksIScsiProvider *provider,
                                           const gchar         *target_name,
                                           gint                 tpgt,
                                           const gchar         *portal_address,
                                           gint                 portal_port,
                                           const gchar         *iface_name);

static void
on_file_monitor_changed (GFileMonitor     *monitor,
                         GFile            *file,
                         GFile            *other_file,
                         GFileMonitorEvent event_type,
                         gpointer          user_data);

G_DEFINE_TYPE (UDisksIScsiProvider, udisks_iscsi_provider, UDISKS_TYPE_PROVIDER);

static void
udisks_iscsi_provider_finalize (GObject *object)
{
  UDisksIScsiProvider *provider = UDISKS_ISCSI_PROVIDER (object);
  GList *l;

  if (provider->cool_off_timeout_id != 0)
    g_source_remove (provider->cool_off_timeout_id);

  if (provider->file_monitor == NULL)
    {
      g_signal_handlers_disconnect_by_func (provider->file_monitor,
                                            G_CALLBACK (on_file_monitor_changed),
                                            provider);
      g_object_unref (provider->file_monitor);
    }

  for (l = provider->targets; l != NULL; l = l->next)
    {
      IScsiTarget *target = l->data;
      g_assert (target->object_path != NULL);
      g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (udisks_provider_get_daemon (UDISKS_PROVIDER (provider))),
                                             target->object_path);
      iscsi_target_unref (target);
    }
  g_list_free (provider->targets);

  connections_finalize (provider);

  if (G_OBJECT_CLASS (udisks_iscsi_provider_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_iscsi_provider_parent_class)->finalize (object);
}

static void
udisks_iscsi_provider_init (UDisksIScsiProvider *provider)
{
}

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_iscsi_provider_start (UDisksProvider *_provider)
{
  UDisksIScsiProvider *provider = UDISKS_ISCSI_PROVIDER (_provider);
  GFile *file;
  GError *error;
  const gchar *nodes_dir_name;

  if (UDISKS_PROVIDER_CLASS (udisks_iscsi_provider_parent_class)->start != NULL)
    UDISKS_PROVIDER_CLASS (udisks_iscsi_provider_parent_class)->start (_provider);

  provider->daemon = udisks_provider_get_daemon (UDISKS_PROVIDER (provider));
  provider->udev_client = udisks_linux_provider_get_udev_client (udisks_daemon_get_linux_provider (provider->daemon));

  /* TODO: this doesn't catch all changes but it's good enough for now */
  nodes_dir_name = "/var/lib/iscsi/nodes";
  file = g_file_new_for_path ("/var/lib/iscsi/nodes");
  error = NULL;
  provider->file_monitor = g_file_monitor_directory (file,
                                                     G_FILE_MONITOR_NONE,
                                                     NULL,
                                                     &error);
  if (provider->file_monitor == NULL)
    {
      udisks_daemon_log (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)),
                         UDISKS_LOG_LEVEL_WARNING,
                         "Error monitoring dir %s: %s",
                         nodes_dir_name,
                         error->message);
      g_error_free (error);
    }
  else
    {
      g_file_monitor_set_rate_limit (provider->file_monitor, 50 /* msec */);
      g_signal_connect (provider->file_monitor,
                        "changed",
                        G_CALLBACK (on_file_monitor_changed),
                        provider);
    }
  g_object_unref (file);

  connections_init (provider);

  load_and_process_iscsi (provider);
}


static void
udisks_iscsi_provider_class_init (UDisksIScsiProviderClass *klass)
{
  GObjectClass *gobject_class;
  UDisksProviderClass *provider_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_iscsi_provider_finalize;

  provider_class        = UDISKS_PROVIDER_CLASS (klass);
  provider_class->start = udisks_iscsi_provider_start;
}

/**
 * udisks_iscsi_provider_new:
 * @daemon: A #UDisksDaemon.
 *
 * Create a new provider object for iSCSI targets on the system.
 *
 * Returns: A #UDisksIScsiProvider object. Free with g_object_unref().
 */
UDisksIScsiProvider *
udisks_iscsi_provider_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_ISCSI_PROVIDER (g_object_new (UDISKS_TYPE_ISCSI_PROVIDER,
                                              "daemon", daemon,
                                              NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/* returns a new floating GVariant */
static GVariant *
portals_and_ifaces_to_gvariant (UDisksIScsiProvider *provider,
                                IScsiTarget         *target)
{
  GVariantBuilder portals_builder;
  GList *l, *ll;

  g_variant_builder_init (&portals_builder, G_VARIANT_TYPE ("a(ayiia(ays))"));
  for (l = target->portals; l != NULL; l = l->next)
    {
      IScsiPortal *portal = l->data;
      GVariantBuilder iface_builder;

      g_variant_builder_init (&iface_builder, G_VARIANT_TYPE ("a(ays)"));
      for (ll = portal->ifaces; ll != NULL; ll = ll->next)
        {
          IScsiIface *iface = ll->data;
          const gchar *state;
          state = connections_get_state (provider,
                                         target->target_name,
                                         portal->tpgt,
                                         portal->address,
                                         portal->port,
                                         iface->name);
          g_variant_builder_add (&iface_builder, "(^ays)",
                                 iface->name,
                                 state);
        }
      g_variant_builder_add (&portals_builder, "(^ayiia(ays))",
                             portal->address,
                             portal->port,
                             portal->tpgt,
                             &iface_builder);
    }
  return g_variant_builder_end (&portals_builder);
}

static void
update_portals_and_ifaces (UDisksIScsiProvider *provider,
                           IScsiTarget         *target)
{
  GDir *portals_dir;
  const gchar *portal_str;
  GError *error;
  GList *portals;

  portals = NULL;

  /* first load all portals and their interfaces, then sort them .. and then build
   * the variant. We have to do it in this order because g_dir_read_name() might
   * return items in a random order...
   */
  error = NULL;
  portals_dir = g_dir_open (target->dir_name, 0, &error);
  if (portals_dir == NULL)
    {
      udisks_daemon_log (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)),
                         UDISKS_LOG_LEVEL_WARNING,
                         "Error opening portal dir %s: %s",
                         target->dir_name,
                         error->message);
      g_error_free (error);
      goto out;
    }
  else
    {
      while ((portal_str = g_dir_read_name (portals_dir)) != NULL)
        {
          GDir *ifaces_dir;
          gchar *ifaces_dir_name;
          const gchar *iface_str;
          gchar *s;
          gchar *comma;
          gint port;
          gint tpgt;
          IScsiPortal *portal;

          ifaces_dir_name = NULL;

          s = g_strdup (portal_str);
          comma = g_strrstr (s, ",");
          if (comma == NULL)
            {
              udisks_daemon_log (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)),
                                 UDISKS_LOG_LEVEL_WARNING,
                                 "Error parsing tpgt from portal string %s",
                                 portal_str);
              goto portal_out;
            }
          tpgt = strtol (comma + 1, NULL, 0);
          *comma = '\0';
          comma = g_strrstr (s, ",");
          if (comma == NULL)
            {
              udisks_daemon_log (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)),
                                 UDISKS_LOG_LEVEL_WARNING,
                                 "Error parsing port part of portal string %s",
                                 portal_str);
              goto portal_out;
            }
          port = strtol (comma + 1, NULL, 0);
          *comma = '\0';

          portal = g_new0 (IScsiPortal, 1);
          portal->address = g_strdup (s);
          portal->port = port;
          portal->tpgt = tpgt;
          portals = g_list_prepend (portals, portal);

          ifaces_dir_name = g_strdup_printf ("%s/%s", target->dir_name, portal_str);
          ifaces_dir = g_dir_open (ifaces_dir_name, 0, &error);
          if (ifaces_dir == NULL)
            {
              udisks_daemon_log (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)),
                                 UDISKS_LOG_LEVEL_WARNING,
                                 "Error opening ifaces dir %s: %s",
                                 ifaces_dir_name,
                                 error->message);
              g_error_free (error);
            }
          else
            {
              while ((iface_str = g_dir_read_name (ifaces_dir)) != NULL)
                {
                  IScsiIface *iface;
                  iface = g_new0 (IScsiIface, 1);
                  iface->name = g_strdup (iface_str);
                  portal->ifaces = g_list_prepend (portal->ifaces, iface);
                } /* for all ifaces */
              portal->ifaces = g_list_sort (portal->ifaces, (GCompareFunc) iscsi_iface_compare);
              g_dir_close (ifaces_dir);
            }
        portal_out:
          g_free (ifaces_dir_name);
        } /* for all portals */

      g_dir_close (portals_dir);
    }
  portals = g_list_sort (portals, (GCompareFunc) iscsi_portal_compare);

 out:
  if (target->portals != NULL)
    {
      g_list_foreach (target->portals, (GFunc) iscsi_portal_free, NULL);
      g_list_free (target->portals);
    }
  target->portals = portals;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in dedicated thread */
static gboolean
on_iscsi_target_handle_login_logout (UDisksIScsiTarget     *iface,
                                     GDBusMethodInvocation *invocation,
                                     const gchar *const    *options,
                                     const gchar           *portal_address,
                                     gint                   portal_port,
                                     const gchar           *interface_name,
                                     UDisksIScsiProvider   *provider,
                                     gboolean               is_login)
{
  gboolean auth_no_user_interaction;
  guint n;
  gchar *error_message;
  GString *command_line;
  gchar *s;

  error_message = NULL;
  command_line = NULL;

  auth_no_user_interaction = FALSE;
  for (n = 0; options != NULL && options[n] != NULL; n++)
    if (g_strcmp0 (options[n], "auth_no_user_interaction") == 0)
      auth_no_user_interaction = TRUE;

  /* TODO: we want nicer authentication message */
  if (!udisks_daemon_util_check_authorization_sync (provider->daemon,
                                                    UDISKS_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (iface))),
                                                    "org.freedesktop.udisks2.iscsi",
                                                    auth_no_user_interaction,
                                                    is_login ?
                                                      N_("Authentication is required to login to an iSCSI target") :
                                                      N_("Authentication is required to logout of an iSCSI target"),
                                                    invocation))
    goto out;

  command_line = g_string_new ("iscsiadm --mode node");

  s = g_strescape (udisks_iscsi_target_get_name (iface), NULL);
  g_string_append_printf (command_line, " --target \"%s\"", s);
  g_free (s);
  if (strlen (portal_address) > 0)
    {
      s = g_strescape (portal_address, NULL);
      if (portal_port == 0)
        portal_port = 3260;
      g_string_append_printf (command_line, " --portal \"%s\":%d", s, portal_port);
      g_free (s);
    }
  if (strlen (interface_name) > 0)
    {
      s = g_strescape (interface_name, NULL);
      g_string_append_printf (command_line, " --interface \"%s\"", s);
      g_free (s);
    }

  if (is_login)
    g_string_append (command_line, " --login");
  else
    g_string_append (command_line, " --logout");

  if (!udisks_daemon_launch_spawned_job_sync (provider->daemon,
                                              NULL,  /* GCancellable */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "%s",
                                              command_line->str))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "iscsiadm(8) failed with: %s",
                                             error_message);
    }
  else
    {
      /* sometimes iscsiadm returns 0 when it fails but stderr is set...
       *
       * TODO: file a bug against iscsi-initiator-utils
       */
      if (error_message != NULL && strlen (error_message) > 0)
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "iscsiadm(8) failed with: %s",
                                                 error_message);
        }
      else
        {
          g_dbus_method_invocation_return_value (invocation, NULL);
        }
    }

 out:
  if (command_line != NULL)
    g_string_free (command_line, TRUE);
  g_free (error_message);
  return TRUE; /* call was handled */
}

static gboolean
on_iscsi_target_handle_login (UDisksIScsiTarget     *iface,
                              GDBusMethodInvocation *invocation,
                              const gchar *const    *options,
                              const gchar           *portal_address,
                              gint                   portal_port,
                              const gchar           *interface_name,
                              UDisksIScsiProvider   *provider)
{
  return on_iscsi_target_handle_login_logout (iface, invocation, options, portal_address, portal_port, interface_name, provider, TRUE);
}

static gboolean
on_iscsi_target_handle_logout (UDisksIScsiTarget     *iface,
                               GDBusMethodInvocation *invocation,
                               const gchar *const    *options,
                               const gchar           *portal_address,
                               gint                   portal_port,
                               const gchar           *interface_name,
                               UDisksIScsiProvider   *provider)
{
  return on_iscsi_target_handle_login_logout (iface, invocation, options, portal_address, portal_port, interface_name, provider, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
load_and_process_iscsi (UDisksIScsiProvider *provider)
{
  GDir *nodes_dir;
  GError *error;
  GList *parsed_targets;
  GList *l;
  const gchar *nodes_dir_name;
  GList *added;
  GList *removed;

  parsed_targets = NULL;

  error = NULL;
  nodes_dir_name = "/var/lib/iscsi/nodes";
  nodes_dir = g_dir_open (nodes_dir_name, 0, &error);
  if (nodes_dir == NULL)
    {
      udisks_daemon_log (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)),
                         UDISKS_LOG_LEVEL_WARNING,
                         "Error opening node dir %s: %s",
                         nodes_dir_name,
                         error->message);
      g_error_free (error);
    }
  else
    {
      const gchar *node_name;

      while ((node_name = g_dir_read_name (nodes_dir)) != NULL)
        {
          IScsiTarget *target;
          target = g_new0 (IScsiTarget, 1);
          target->ref_count = 1;
          target->target_name = g_strdup (node_name);
          target->dir_name = g_strdup_printf ("%s/%s", nodes_dir_name, node_name);
          parsed_targets = g_list_prepend (parsed_targets, target);
        } /* for all nodes */
      parsed_targets = g_list_sort (parsed_targets, (GCompareFunc) iscsi_target_compare);
      g_dir_close (nodes_dir);
    }

  provider->targets = g_list_sort (provider->targets, (GCompareFunc) iscsi_target_compare);
  diff_sorted_lists (provider->targets,
                     parsed_targets,
                     (GCompareFunc) iscsi_target_compare,
                     &added,
                     &removed);

  for (l = removed; l != NULL; l = l->next)
    {
      IScsiTarget *target = l->data;
      g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (udisks_provider_get_daemon (UDISKS_PROVIDER (provider))),
                                             target->object_path);
      provider->targets = g_list_remove (provider->targets, target);
      iscsi_target_unref (target);
    }
  for (l = added; l != NULL; l = l->next)
    {
      IScsiTarget *target = l->data;
      target->object_path = util_compute_object_path ("/org/freedesktop/UDisks2/iSCSI/", target->target_name);
      target->iface = udisks_iscsi_target_skeleton_new ();
      g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (target->iface),
                                           G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
      g_signal_connect (target->iface,
                        "handle-login",
                        G_CALLBACK (on_iscsi_target_handle_login),
                        provider);
      g_signal_connect (target->iface,
                        "handle-logout",
                        G_CALLBACK (on_iscsi_target_handle_logout),
                        provider);
      udisks_iscsi_target_set_name (target->iface, target->target_name);
      provider->targets = g_list_prepend (provider->targets, iscsi_target_ref (target));
    }

  /* update all known targets since portals/interfaces might have changed */
  for (l = provider->targets; l != NULL; l = l->next)
    {
      IScsiTarget *target = l->data;
      update_portals_and_ifaces (provider, target);
      udisks_iscsi_target_set_portals_and_interfaces (target->iface,
                                                      portals_and_ifaces_to_gvariant (provider, target));
    }

  /* finally export added targets */
  for (l = added; l != NULL; l = l->next)
    {
      IScsiTarget *target = l->data;
      target->object = udisks_object_skeleton_new (target->object_path);
      udisks_object_skeleton_set_iscsi_target (target->object, target->iface);
      g_dbus_object_manager_server_export_uniquely (udisks_daemon_get_object_manager (udisks_provider_get_daemon (UDISKS_PROVIDER (provider))),
                                                    G_DBUS_OBJECT_SKELETON (target->object));
    }

  g_list_foreach (parsed_targets, (GFunc) iscsi_target_unref, NULL);
  g_list_free (parsed_targets);
  g_list_free (removed);
  g_list_free (added);
}

static void
update_state (UDisksIScsiProvider *provider)
{
  GList *l;
  for (l = provider->targets; l != NULL; l = l->next)
    {
      IScsiTarget *target = l->data;
      update_portals_and_ifaces (provider, target);
      udisks_iscsi_target_set_portals_and_interfaces (target->iface,
                                                      portals_and_ifaces_to_gvariant (provider, target));
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_cool_off_timeout_cb (gpointer user_data)
{
  UDisksIScsiProvider *provider = UDISKS_ISCSI_PROVIDER (user_data);

  //udisks_daemon_log (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)), UDISKS_LOG_LEVEL_INFO, "iscsi refresh..");
  load_and_process_iscsi (provider);
  provider->cool_off_timeout_id = 0;
  return FALSE;
}

static void
on_file_monitor_changed (GFileMonitor     *monitor,
                         GFile            *file,
                         GFile            *other_file,
                         GFileMonitorEvent event_type,
                         gpointer          user_data)
{
  UDisksIScsiProvider *provider = UDISKS_ISCSI_PROVIDER (user_data);
  //udisks_daemon_log (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)), UDISKS_LOG_LEVEL_INFO, "iscsi file monitor event..");
  /* coalesce many events into one */
  if (provider->cool_off_timeout_id == 0)
    provider->cool_off_timeout_id = g_timeout_add (250, on_cool_off_timeout_cb, provider);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  /* from iscsi_session */
  gchar *target_name;
  gchar *iface_name;
  gint tpgt;
  gchar *state;
  gchar *session_sysfs_path;

  /* from iscsi_connection */
  gchar *address;
  gint port;

  gchar *id;
} Connection;

static void
connection_free (Connection *connection)
{
  g_free (connection->target_name);
  g_free (connection->iface_name);
  g_free (connection->state);
  g_free (connection->session_sysfs_path);
  g_free (connection->address);
  g_free (connection->id);
  g_free (connection);
}

/* ---------------------------------------------------------------------------------------------------- */

/* believe it or not, sometimes the kernel returns a sysfs attr with content "(null)" */
static gboolean
is_null (const gchar *str)
{
  return str == NULL || g_strcmp0 (str, "(null)") == 0;
}

static void
handle_iscsi_connection_uevent (UDisksIScsiProvider *provider,
                                const gchar         *uevent,
                                GUdevDevice         *device)
{
  const gchar *sysfs_path;
  Connection *connection;

  sysfs_path = g_udev_device_get_sysfs_path (device);
  connection = g_hash_table_lookup (provider->sysfs_to_connection, sysfs_path);
  if (g_strcmp0 (uevent, "remove") == 0)
    {
      if (connection != NULL)
        {
          /*g_debug ("removed %s %s", sysfs_path, connection->id);*/
          g_warn_if_fail (g_hash_table_remove (provider->id_to_connection, connection->id));
          g_hash_table_remove (provider->sysfs_to_connection, sysfs_path);
        }
      else
        {
          g_warning ("no object for connection %s", sysfs_path);
        }
    }
  else
    {
      /* This is a bit sketchy and includes assumptions about what sysfs
       * currently looks like...
       */
      if (connection == NULL)
        {
          gchar *session_sysfs_dir;
          GDir *session_dir;
          gchar *session_sysfs_path;
          GUdevDevice *session_device;
          const gchar *name;

          session_sysfs_dir = NULL;
          session_dir = NULL;
          session_sysfs_path = NULL;
          session_device = NULL;

          session_sysfs_dir = g_strdup_printf ("%s/device/../iscsi_session", sysfs_path);
          if (!g_file_test (session_sysfs_dir, G_FILE_TEST_IS_DIR))
            goto skip_connection;
          session_dir = g_dir_open (session_sysfs_dir, 0, NULL);
          if (session_dir == NULL)
            goto skip_connection;
          while ((name = g_dir_read_name (session_dir)) != NULL)
            {
              gint session_num;
              if (sscanf (name, "session%d", &session_num) == 1)
                {
                  session_sysfs_path = g_strdup_printf ("%s/%s", session_sysfs_dir, name);
                  break;
                }
            }
          if (session_sysfs_path == NULL)
            goto skip_connection;
          session_device = g_udev_client_query_by_sysfs_path (provider->udev_client, session_sysfs_path);
          if (session_device == NULL)
            goto skip_connection;

          connection = g_new0 (Connection, 1);
          connection->target_name = g_strdup (g_udev_device_get_sysfs_attr (session_device, "targetname"));
          connection->iface_name = g_strdup (g_udev_device_get_sysfs_attr (session_device, "ifacename"));
          connection->tpgt = g_udev_device_get_sysfs_attr_as_int (session_device, "tpgt");
          connection->address = g_strdup (g_udev_device_get_sysfs_attr (device, "persistent_address"));
          connection->port = g_udev_device_get_sysfs_attr_as_int (device, "persistent_port");
          connection->session_sysfs_path = g_strdup (g_udev_device_get_sysfs_path (session_device));

          if (is_null (connection->target_name) ||
              is_null (connection->iface_name) ||
              is_null (connection->address) ||
              connection->port == 0)
            {
              /*udisks_daemon_log (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)),
                                 UDISKS_LOG_LEVEL_WARNING,
                                 "Abandoning incomplete iscsi_connection object at %s",
                                 sysfs_path);
              */
              connection_free (connection);
              connection = NULL;
              goto skip_connection;
            }

          connection->id = g_strdup_printf ("%d,%s:%d,%s,%s",
                                            connection->tpgt,
                                            connection->address,
                                            connection->port,
                                            connection->iface_name,
                                            connection->target_name);

          /*g_debug ("added %s %s", sysfs_path, connection->id);*/
          g_hash_table_insert (provider->sysfs_to_connection, g_strdup (sysfs_path), connection);
          g_hash_table_insert (provider->id_to_connection, connection->id, connection);

        skip_connection:
          g_free (session_sysfs_dir);
          if (session_dir != NULL)
            g_dir_close (session_dir);
          g_free (session_sysfs_path);
          if (session_device != NULL)
            g_object_unref (session_device);
        }

      /* update the Connection object */
      if (connection != NULL)
        {
          GUdevDevice *session_device;
          session_device = g_udev_client_query_by_sysfs_path (provider->udev_client,
                                                              connection->session_sysfs_path);
          if (session_device != NULL)
            {
              g_free (connection->state);
              connection->state = g_strdup (g_udev_device_get_sysfs_attr (session_device, "state"));
              g_object_unref (session_device);
            }
          else
            {
              g_warning ("no session device for %s", connection->session_sysfs_path);
            }
        }
    }
}

static void
handle_scsi_target_uevent (UDisksIScsiProvider *provider,
                           const gchar         *uevent,
                           GUdevDevice         *device)
{
  const gchar *sysfs_path;
  gchar *parent_sysfs_dir;
  GDir *parent_dir;
  gchar *connection_sysfs_path;
  GUdevDevice *connection_device;
  const gchar *name;
  gchar connection_canonical_sysfs_path[PATH_MAX];

  /* Also sketchy and also includes assumptions about what sysfs
   * currently looks like...
   */

  parent_sysfs_dir = NULL;
  parent_dir = NULL;
  connection_sysfs_path = NULL;
  connection_device = NULL;

  if (g_strcmp0 (uevent, "remove") == 0)
    goto skip;

  sysfs_path = g_udev_device_get_sysfs_path (device);

  parent_sysfs_dir = g_strdup_printf ("%s/..", sysfs_path);
  parent_dir = g_dir_open (parent_sysfs_dir, 0, NULL);
  if (parent_dir == NULL)
    goto skip;
  while ((name = g_dir_read_name (parent_dir)) != NULL)
    {
      gint connection_num;
      if (sscanf (name, "connection%d", &connection_num) == 1)
        {
          connection_sysfs_path = g_strdup_printf ("%s/%s/iscsi_connection/%s", parent_sysfs_dir, name, name);
          break;
        }
    }
  if (connection_sysfs_path == NULL)
    goto skip;
  if (realpath (connection_sysfs_path, connection_canonical_sysfs_path) == NULL)
    goto skip;
  connection_device = g_udev_client_query_by_sysfs_path (provider->udev_client, connection_canonical_sysfs_path);
  if (connection_device == NULL)
    goto skip;

  handle_iscsi_connection_uevent (provider, "change", connection_device);
  update_state (provider);

 skip:
  g_free (parent_sysfs_dir);
  if (parent_dir != NULL)
    g_dir_close (parent_dir);
  g_free (connection_sysfs_path);
  if (connection_device != NULL)
    g_object_unref (connection_device);
}

static void
connections_on_uevent (GUdevClient   *udev_client,
                       const gchar   *uevent,
                       GUdevDevice   *device,
                       gpointer       user_data)
{
  UDisksIScsiProvider *provider = UDISKS_ISCSI_PROVIDER (user_data);
  const gchar *subsystem;
  const gchar *devtype;

  subsystem = g_udev_device_get_subsystem (device);
  devtype = g_udev_device_get_devtype (device);
  if (g_strcmp0 (subsystem, "iscsi_connection") == 0)
    {
      handle_iscsi_connection_uevent (provider, uevent, device);
      update_state (provider);
    }
  else if (g_strcmp0 (subsystem, "scsi") == 0 && g_strcmp0 (devtype, "scsi_target") == 0)
    {
      handle_scsi_target_uevent (provider, uevent, device);
    }
}

static void
connections_init (UDisksIScsiProvider *provider)
{
  GList *devices;
  GList *l;

  provider->sysfs_to_connection = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                         (GDestroyNotify) connection_free);
  provider->id_to_connection = g_hash_table_new (g_str_hash, g_str_equal);

  /* hotplug */
  g_signal_connect (provider->udev_client,
                    "uevent",
                    G_CALLBACK (connections_on_uevent),
                    provider);

  /* coldplug */
  devices = g_udev_client_query_by_subsystem (provider->udev_client, "iscsi_connection");
  for (l = devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      handle_iscsi_connection_uevent (provider, "add", device);
    }
  g_list_foreach (devices, (GFunc) g_object_unref, NULL);
  g_list_free (devices);
}

static void
connections_finalize (UDisksIScsiProvider *provider)
{
  g_signal_handlers_disconnect_by_func (provider->udev_client,
                                        G_CALLBACK (connections_on_uevent),
                                        provider);
  g_hash_table_unref (provider->id_to_connection);
  g_hash_table_unref (provider->sysfs_to_connection);
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
connections_get_state (UDisksIScsiProvider *provider,
                       const gchar         *target_name,
                       gint                 tpgt,
                       const gchar         *portal_address,
                       gint                 portal_port,
                       const gchar         *iface_name)
{
  const gchar *ret;
  gchar *id;
  Connection *connection;

  ret = "";

  id = g_strdup_printf ("%d,%s:%d,%s,%s",
                        tpgt,
                        portal_address,
                        portal_port,
                        iface_name,
                        target_name);
  connection = g_hash_table_lookup (provider->id_to_connection, id);
  if (connection != NULL)
    ret = connection->state;

  g_free (id);
  return ret;
}


/* ---------------------------------------------------------------------------------------------------- */
