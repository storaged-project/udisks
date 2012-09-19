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
#include <stdio.h>
#include <mntent.h>

#include <glib/gstdio.h>

#include "udiskslogging.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxmdraidobject.h"
#include "udiskslinuxmdraid.h"
#include "udiskslinuxblockobject.h"
#include "udisksdaemon.h"
#include "udiskscleanup.h"
#include "udisksdaemonutil.h"

/**
 * SECTION:udiskslinuxmdraid
 * @title: UDisksLinuxMDRaid
 * @short_description: Linux implementation of #UDisksMDRaid
 *
 * This type provides an implementation of the #UDisksMDRaid interface
 * on Linux.
 */

typedef struct _UDisksLinuxMDRaidClass   UDisksLinuxMDRaidClass;

/**
 * UDisksLinuxMDRaid:
 *
 * The #UDisksLinuxMDRaid structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _UDisksLinuxMDRaid
{
  UDisksMDRaidSkeleton parent_instance;

  guint polling_timeout;
};

struct _UDisksLinuxMDRaidClass
{
  UDisksMDRaidSkeletonClass parent_class;
};

static void ensure_polling (UDisksLinuxMDRaid  *mdraid,
                            gboolean            polling_on);

static void mdraid_iface_init (UDisksMDRaidIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxMDRaid, udisks_linux_mdraid, UDISKS_TYPE_MDRAID_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MDRAID, mdraid_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
udisks_linux_mdraid_finalize (GObject *object)
{
  UDisksLinuxMDRaid *mdraid = UDISKS_LINUX_MDRAID (object);

  ensure_polling (mdraid, FALSE);

  if (G_OBJECT_CLASS (udisks_linux_mdraid_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_mdraid_parent_class)->finalize (object);
}

static void
udisks_linux_mdraid_init (UDisksLinuxMDRaid *mdraid)
{
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (mdraid),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
udisks_linux_mdraid_class_init (UDisksLinuxMDRaidClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_mdraid_finalize;
}

/**
 * udisks_linux_mdraid_new:
 *
 * Creates a new #UDisksLinuxMDRaid instance.
 *
 * Returns: A new #UDisksLinuxMDRaid. Free with g_object_unref().
 */
UDisksMDRaid *
udisks_linux_mdraid_new (void)
{
  return UDISKS_MDRAID (g_object_new (UDISKS_TYPE_LINUX_MDRAID,
                                          NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
read_sysfs_attr (GUdevDevice *device,
                 const gchar *attr)
{
  gchar *ret = NULL;
  gchar *path = NULL;
  GError *error = NULL;

  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), NULL);

  path = g_strdup_printf ("%s/%s", g_udev_device_get_sysfs_path (device), attr);
  if (!g_file_get_contents (path, &ret, NULL /* size */, &error))
    {
      udisks_warning ("Error reading sysfs attr `%s': %s (%s, %d)",
                      path, error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      goto out;
    }

 out:
  g_free (path);
  return ret;
}

static gint
read_sysfs_attr_as_int (GUdevDevice *device,
                        const gchar *attr)
{
  gint ret = 0;
  gchar *str = NULL;

  str = read_sysfs_attr (device, attr);
  if (str == NULL)
    goto out;

  ret = atoi (str);
  g_free (str);

 out:
  return ret;
}

static guint64
read_sysfs_attr_as_uint64 (GUdevDevice *device,
                           const gchar *attr)
{
  guint64 ret = 0;
  gchar *str = NULL;

  str = read_sysfs_attr (device, attr);
  if (str == NULL)
    goto out;

  ret = atoll (str);
  g_free (str);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_polling_timout (gpointer user_data)
{
  UDisksLinuxMDRaid *mdraid = UDISKS_LINUX_MDRAID (user_data);
  UDisksLinuxMDRaidObject *object = NULL;

  udisks_debug ("polling timeout");

  object = udisks_daemon_util_dup_object (mdraid, NULL);
  if (object == NULL)
    goto out;

  /* synthesize uevent */
  udisks_linux_mdraid_object_uevent (object, "change", NULL);

 out:
  g_clear_object (&object);
  return TRUE; /* keep timeout around */
}

static void
ensure_polling (UDisksLinuxMDRaid  *mdraid,
                gboolean            polling_on)
{
  if (polling_on)
    {
      if (mdraid->polling_timeout == 0)
        {
          mdraid->polling_timeout = g_timeout_add_seconds (1,
                                                           on_polling_timout,
                                                           mdraid);
        }
    }
  else
    {
      if (mdraid->polling_timeout != 0)
        {
          g_source_remove (mdraid->polling_timeout);
          mdraid->polling_timeout = 0;
        }
    }
}

static gint
member_cmpfunc (GVariant **a,
                GVariant **b)
{
  gint slot_a;
  gint slot_b;
  const gchar *objpath_a;
  const gchar *objpath_b;

  g_return_val_if_fail (a != NULL, 0);
  g_return_val_if_fail (b != NULL, 0);

  g_variant_get (*a, "(&oiasta{sv})", &objpath_a, &slot_a, NULL, NULL, NULL);
  g_variant_get (*b, "(&oiasta{sv})", &objpath_b, &slot_b, NULL, NULL, NULL);

  if (slot_a == slot_b)
    return g_strcmp0 (objpath_a, objpath_b);

  return slot_a - slot_b;
}


/**
 * udisks_linux_mdraid_update:
 * @mdraid: A #UDisksLinuxMDRaid.
 * @object: The enclosing #UDisksLinuxMDRaidObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
udisks_linux_mdraid_update (UDisksLinuxMDRaid       *mdraid,
                            UDisksLinuxMDRaidObject *object)
{
  UDisksMDRaid *iface = UDISKS_MDRAID (mdraid);
  gboolean ret = FALSE;
  guint num_devices = 0;
  guint num_members = 0;
  guint64 size = 0;
  GUdevDevice *raid_device = NULL;
  GList *member_devices = NULL;
  GUdevDevice *device = NULL;
  const gchar *level = NULL;
  gchar *sync_action = NULL;
  gchar *sync_completed = NULL;
  guint degraded = 0;
  gdouble sync_completed_val = 0.0;
  GVariantBuilder builder;
  UDisksDaemon *daemon = NULL;
  gboolean can_start = FALSE, can_start_degraded = FALSE;

  daemon = udisks_linux_mdraid_object_get_daemon (object);

  member_devices = udisks_linux_mdraid_object_get_members (object);
  raid_device = udisks_linux_mdraid_object_get_device (object);

  if (member_devices == NULL && raid_device == NULL)
    {
      /* this should never happen */
      udisks_warning ("No members and no RAID device - bailing");
      goto out;
    }

  num_members = g_list_length (member_devices);

  /* it doesn't matter where we get the MD_ properties from - it can be
   * either a member device or the raid device (/dev/md*) - prefer the
   * former, if available
   */
  if (member_devices != NULL)
    device = G_UDEV_DEVICE (member_devices->data);
  else
    device = G_UDEV_DEVICE (raid_device);

  num_devices = g_udev_device_get_property_as_int (device, "MD_DEVICES");
  level = g_udev_device_get_property (device, "MD_LEVEL");

  /* figure out size */
  if (raid_device != NULL)
    {
      size = 512 * g_udev_device_get_sysfs_attr_as_uint64 (raid_device, "size");
    }
  else
    {
      /* TODO: need MD_ARRAY_SIZE, see https://bugs.freedesktop.org/show_bug.cgi?id=53239#c5 */
    }

  udisks_mdraid_set_uuid (iface, g_udev_device_get_property (device, "MD_UUID"));
  udisks_mdraid_set_name (iface, g_udev_device_get_property (device, "MD_NAME"));
  udisks_mdraid_set_level (iface, level);
  udisks_mdraid_set_num_devices (iface, num_devices);
  udisks_mdraid_set_size (iface, size);

  /* Figure out CanStart[Degraded]
   *
   * We ignore corner-cases RAID-10 can start with 2, 3, N/2 missing drives...
   *
   * TODO: We probably should ignore devices marked as spares...
   */
  if (num_members >= num_devices)
    can_start = TRUE;
  if (g_strcmp0 (level, "raid1") == 0 ||
      g_strcmp0 (level, "raid4") == 0 ||
      g_strcmp0 (level, "raid5") == 0 ||
      g_strcmp0 (level, "raid10") == 0)
    {
      if (num_members >= num_devices - 1)
        can_start_degraded = TRUE;
    }
  else if (g_strcmp0 (level, "raid6") == 0)
    {
      if (num_members >= num_devices - 2)
        can_start_degraded = TRUE;
    }
  udisks_mdraid_set_can_start (iface, can_start);
  udisks_mdraid_set_can_start_degraded (iface, can_start_degraded);

  if (raid_device != NULL)
    {
      /* Can't use GUdevDevice methods as they cache the result and these variables vary */
      degraded = read_sysfs_attr_as_int (raid_device, "md/degraded");
      sync_action = read_sysfs_attr (raid_device, "md/sync_action");
      if (sync_action != NULL)
        g_strstrip (sync_action);
      sync_completed = read_sysfs_attr (raid_device, "md/sync_completed");
      if (sync_completed != NULL)
        g_strstrip (sync_completed);
    }
  udisks_mdraid_set_degraded (iface, degraded);
  udisks_mdraid_set_sync_action (iface, sync_action);

  if (sync_completed != NULL && g_strcmp0 (sync_completed, "none") != 0)
    {
      guint64 completed_sectors = 0;
      guint64 num_sectors = 1;
      if (sscanf (sync_completed, "%" G_GUINT64_FORMAT " / %" G_GUINT64_FORMAT,
                  &completed_sectors, &num_sectors) == 2)
        {
          if (num_sectors != 0)
            sync_completed_val = ((gdouble) completed_sectors) / ((gdouble) num_sectors);
        }
    }
  udisks_mdraid_set_sync_completed (iface, sync_completed_val);

  /* ensure we poll, exactly when we need to */
  if (g_strcmp0 (sync_action, "resync") == 0 ||
      g_strcmp0 (sync_action, "recover") == 0 ||
      g_strcmp0 (sync_action, "check") == 0 ||
      g_strcmp0 (sync_action, "repair") == 0)
    {
      ensure_polling (mdraid, TRUE);
    }
  else
    {
      ensure_polling (mdraid, FALSE);
    }

  /* figure out active devices */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(oiasta{sv})"));
  if (raid_device != NULL)
    {
      gchar *md_dir_name = NULL;
      GDir *md_dir;
      GPtrArray *p;
      guint n;

      /* First build an array of variants, then sort it, then build
       *
       * Why sort it? Because directory traversal does not preserve
       * the order and we want the same order every time to avoid
       * spurious property changes on MDRaid:ActiveDevices
       */
      p = g_ptr_array_new ();
      md_dir_name = g_strdup_printf ("%s/md", g_udev_device_get_sysfs_path (raid_device));
      md_dir = g_dir_open (md_dir_name, 0, NULL);
      if (md_dir != NULL)
        {
          gchar buf[256];
          const gchar *name;
          while ((name = g_dir_read_name (md_dir)) != NULL)
            {
              gchar *block_sysfs_path = NULL;
              UDisksObject *member_object = NULL;
              gchar *member_state = NULL;
              gchar **member_state_elements = NULL;
              gchar *member_slot = NULL;
              gint member_slot_as_int = -1;
              guint64 member_errors = 0;

              if (!g_str_has_prefix (name, "dev-"))
                goto member_done;

              snprintf (buf, sizeof (buf), "%s/block", name);
              block_sysfs_path = udisks_daemon_util_resolve_link (md_dir_name, buf);
              if (block_sysfs_path == NULL)
                {
                  udisks_warning ("Unable to resolve %s/%s symlink", md_dir_name, buf);
                  goto member_done;
                }

              member_object = udisks_daemon_find_block_by_sysfs_path (daemon, block_sysfs_path);
              if (member_object == NULL)
                {
                  /* TODO: only warn on !coldplug */
                  /* udisks_warning ("No object for block device with sysfs path %s", block_sysfs_path); */
                  goto member_done;
                }

              snprintf (buf, sizeof (buf), "md/%s/state", name);
              member_state = read_sysfs_attr (raid_device, buf);
              if (member_state != NULL)
                {
                  g_strstrip (member_state);
                  member_state_elements = g_strsplit (member_state, ",", 0);
                }

              snprintf (buf, sizeof (buf), "md/%s/slot", name);
              member_slot = read_sysfs_attr (raid_device, buf);
              if (member_slot != NULL)
                {
                  g_strstrip (member_slot);
                  if (g_strcmp0 (member_slot, "none") != 0)
                    member_slot_as_int = atoi (member_slot);
                }

              snprintf (buf, sizeof (buf), "md/%s/errors", name);
              member_errors = read_sysfs_attr_as_uint64 (raid_device, buf);

              g_ptr_array_add (p,
                               g_variant_new ("(oi^asta{sv})",
                                              g_dbus_object_get_object_path (G_DBUS_OBJECT (member_object)),
                                              member_slot_as_int,
                                              member_state_elements,
                                              member_errors,
                                              NULL)); /* expansion, unused for now */

            member_done:
              g_free (member_slot);
              g_free (member_state);
              g_strfreev (member_state_elements);
              g_clear_object (&member_object);
              g_free (block_sysfs_path);

            } /* for all dev- directories */

          /* ... and sort */
          g_ptr_array_sort (p, (GCompareFunc) member_cmpfunc);

          /* ... and finally build (builder consumes each GVariant instance) */
          for (n = 0; n < p->len; n++)
            g_variant_builder_add_value (&builder, p->pdata[n]);
          g_ptr_array_free (p, TRUE);

          g_dir_close (md_dir);
        }
      g_free (md_dir_name);

    }
  udisks_mdraid_set_active_devices (iface, g_variant_builder_end (&builder));

  /* TODO: set other stuff */

 out:
  g_free (sync_completed);
  g_free (sync_action);
  g_list_free_full (member_devices, g_object_unref);
  g_clear_object (&raid_device);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
calculate_mdname_and_escape_and_quote (UDisksLinuxMDRaid *mdraid)
{
  gchar *ret;
  gchar *name;

  name = udisks_mdraid_dup_name (UDISKS_MDRAID (mdraid));
  if (name != NULL)
    {
      /* skip homehost */
      const gchar *s = strstr (name, ":");
      if (s != NULL && strlen (s) > 1)
        {
          ret = udisks_daemon_util_escape_and_quote (s + 1);
          g_free (name);
        }
      else
        {
          ret = name;
          name = NULL;
        }
    }
  else
    {
      gchar *uuid = udisks_mdraid_dup_uuid (UDISKS_MDRAID (mdraid));
      ret = udisks_daemon_util_escape_and_quote (uuid);
      g_free (uuid);
    }

  return ret;
}

static gboolean
handle_start (UDisksMDRaid           *_mdraid,
              GDBusMethodInvocation  *invocation,
              GVariant               *options)
{
  UDisksLinuxMDRaid *mdraid = UDISKS_LINUX_MDRAID (_mdraid);
  UDisksDaemon *daemon;
  UDisksLinuxMDRaidObject *object;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  GUdevDevice *raid_device = NULL;
  GString *str = NULL;
  gchar *escaped_name = NULL;
  gchar *escaped_devices = NULL;
  GError *error = NULL;
  gchar *error_message = NULL;
  GList *member_devices = NULL, *l;
  gboolean opt_start_degraded = FALSE;

  object = udisks_daemon_util_dup_object (mdraid, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_mdraid_object_get_daemon (object);

  g_variant_lookup (options, "start-degraded", "b", &opt_start_degraded);

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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

  raid_device = udisks_linux_mdraid_object_get_device (object);
  if (raid_device != NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "RAID Array is already running");
      goto out;
    }

  member_devices = udisks_linux_mdraid_object_get_members (object);
  if (member_devices == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No member devices");
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * attempts to start a RAID Array.
   */
  /* TODO: variables */
  message = N_("Authentication is required to start a RAID array");
  action_id = "org.freedesktop.udisks2.manage-md-raid";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  /* figure out the name and member devices */
  escaped_name = calculate_mdname_and_escape_and_quote (mdraid);
  str = g_string_new (NULL);
  for (l = member_devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      gchar *escaped_device_file;
      if (str->len > 0)
        g_string_append_c (str, ' ');
      escaped_device_file = udisks_daemon_util_escape_and_quote (g_udev_device_get_device_file (device));
      g_string_append (str, escaped_device_file);
      g_free (escaped_device_file);
    }
  escaped_devices = g_string_free (str, FALSE);

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
                                              "md-raid-start", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "mdadm --assemble%s %s %s",
                                              opt_start_degraded ? " --run" : " ",
                                              escaped_name,
                                              escaped_devices))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error starting RAID array: %s",
                                             error_message);
      goto out;
    }

  /* TODO: wait for array to actually show up? */

  udisks_mdraid_complete_stop (_mdraid, invocation);

 out:
  g_list_free_full (member_devices, g_object_unref);
  g_free (error_message);
  g_free (escaped_name);
  g_free (escaped_devices);
  g_clear_object (&raid_device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_stop (UDisksMDRaid           *_mdraid,
             GDBusMethodInvocation  *invocation,
             GVariant               *options)
{
  UDisksLinuxMDRaid *mdraid = UDISKS_LINUX_MDRAID (_mdraid);
  UDisksDaemon *daemon;
  UDisksLinuxMDRaidObject *object;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  GUdevDevice *raid_device = NULL;
  const gchar *device_file = NULL;
  gchar *escaped_device_file = NULL;
  GError *error = NULL;
  gchar *error_message = NULL;

  object = udisks_daemon_util_dup_object (mdraid, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_mdraid_object_get_daemon (object);

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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

  raid_device = udisks_linux_mdraid_object_get_device (object);
  if (raid_device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "RAID Array is not running");
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * attempts to stop a RAID Array.
   */
  /* TODO: variables */
  message = N_("Authentication is required to stop a RAID array");
  action_id = "org.freedesktop.udisks2.manage-md-raid";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  device_file = g_udev_device_get_device_file (raid_device);
  escaped_device_file = udisks_daemon_util_escape_and_quote (g_udev_device_get_device_file (raid_device));

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
                                              "md-raid-stop", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "mdadm --stop %s",
                                              escaped_device_file))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             UDISKS_ERROR,
                                             UDISKS_ERROR_FAILED,
                                             "Error stopping RAID array %s: %s",
                                             device_file,
                                             error_message);
      goto out;
    }

  udisks_mdraid_complete_stop (_mdraid, invocation);

 out:
  g_free (error_message);
  g_free (escaped_device_file);
  g_clear_object (&raid_device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar **
find_member_states (UDisksLinuxMDRaid *mdraid,
                    const gchar       *member_device_objpath)
{
  GVariantIter iter;
  GVariant *active_devices = NULL;
  const gchar *iter_objpath;
  const gchar **iter_state;
  gchar **ret = NULL;

  active_devices = udisks_mdraid_dup_active_devices (UDISKS_MDRAID (mdraid));
  if (active_devices == NULL)
    goto out;

  g_variant_iter_init (&iter, active_devices);
  while (g_variant_iter_next (&iter, "(&oi^a&sta{sv})", &iter_objpath, NULL, &iter_state, NULL, NULL))
    {
      if (g_strcmp0 (iter_objpath, member_device_objpath) == 0)
        {
          guint n;
          /* we only own the container, so need to dup the values */
          ret = (gchar **) iter_state;
          for (n = 0; ret[n] != NULL; n++)
            ret[n] = g_strdup (ret[n]);
          goto out;
        }
      else
        {
          g_free (iter_state);
        }
    }

 out:
  if (active_devices != NULL)
    g_variant_unref (active_devices);
  return ret;
}

static gboolean
has_state (gchar **states, const gchar *state)
{
  gboolean ret = FALSE;
  guint n;

  if (states == NULL)
    goto out;

  for (n = 0; states[n] != NULL; n++)
    {
      if (g_strcmp0 (states[n], state) == 0)
        {
          ret = TRUE;
          goto out;
        }
    }
 out:
  return ret;
}

static gboolean
handle_remove_device (UDisksMDRaid           *_mdraid,
                      GDBusMethodInvocation  *invocation,
                      const gchar            *member_device_objpath,
                      GVariant               *options)
{
  UDisksLinuxMDRaid *mdraid = UDISKS_LINUX_MDRAID (_mdraid);
  UDisksDaemon *daemon;
  UDisksLinuxMDRaidObject *object;
  const gchar *action_id;
  const gchar *message;
  uid_t caller_uid;
  gid_t caller_gid;
  GUdevDevice *raid_device = NULL;
  const gchar *device_file = NULL;
  gchar *escaped_device_file = NULL;
  const gchar *member_device_file = NULL;
  gchar *escaped_member_device_file = NULL;
  GError *error = NULL;
  gchar *error_message = NULL;
  UDisksObject *member_device_object = NULL;
  UDisksBlock *member_device = NULL;
  gchar **member_states = NULL;
  gboolean opt_wipe = FALSE;

  object = udisks_daemon_util_dup_object (mdraid, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  daemon = udisks_linux_mdraid_object_get_daemon (object);

  g_variant_lookup (options, "wipe", "b", &opt_wipe);

  error = NULL;
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
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

  raid_device = udisks_linux_mdraid_object_get_device (object);
  if (raid_device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "RAID Array is not running");
      goto out;
    }

  member_device_object = udisks_daemon_find_object (daemon, member_device_objpath);
  if (member_device_object == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No device for given object path");
      goto out;
    }

  member_device = udisks_object_get_block (member_device_object);
  if (member_device == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "No block interface on given object");
      goto out;
    }

  member_states = find_member_states (mdraid, member_device_objpath);
  if (member_states == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, UDISKS_ERROR, UDISKS_ERROR_FAILED,
                                             "Cannot determine member state of given object");
      goto out;
    }

  /* Translators: Shown in authentication dialog when the user
   * attempts to remove a device from a RAID Array.
   */
  /* TODO: variables */
  message = N_("Authentication is required to remove a device from a RAID array");
  action_id = "org.freedesktop.udisks2.manage-md-raid";
  if (!udisks_daemon_util_check_authorization_sync (daemon,
                                                    UDISKS_OBJECT (object),
                                                    action_id,
                                                    options,
                                                    message,
                                                    invocation))
    goto out;

  device_file = g_udev_device_get_device_file (raid_device);
  escaped_device_file = udisks_daemon_util_escape_and_quote (device_file);

  member_device_file = udisks_block_get_device (member_device);
  escaped_member_device_file = udisks_daemon_util_escape_and_quote (member_device_file);

  /* if necessary, mark as faulty first */
  if (has_state (member_states, "in_sync"))
    {
      if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                                  UDISKS_OBJECT (object),
                                                  "md-raid-fault-device", caller_uid,
                                                  NULL, /* GCancellable */
                                                  0,    /* uid_t run_as_uid */
                                                  0,    /* uid_t run_as_euid */
                                                  NULL, /* gint *out_status */
                                                  &error_message,
                                                  NULL,  /* input_string */
                                                  "mdadm --manage %s --set-faulty %s",
                                                  escaped_device_file,
                                                  escaped_member_device_file))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error marking %s as faulty in RAID array %s: %s",
                                                 member_device_file,
                                                 device_file,
                                                 error_message);
          goto out;
        }
    }

  if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                              UDISKS_OBJECT (object),
                                              "md-raid-remove-device", caller_uid,
                                              NULL, /* GCancellable */
                                              0,    /* uid_t run_as_uid */
                                              0,    /* uid_t run_as_euid */
                                              NULL, /* gint *out_status */
                                              &error_message,
                                              NULL,  /* input_string */
                                              "mdadm --manage %s --remove %s",
                                              escaped_device_file,
                                              escaped_member_device_file))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error remove %s from RAID array %s: %s",
                                                 member_device_file,
                                                 device_file,
                                                 error_message);
          goto out;
        }

  if (opt_wipe)
    {
      if (!udisks_daemon_launch_spawned_job_sync (daemon,
                                                  UDISKS_OBJECT (member_device_object),
                                                  "format-erase", caller_uid,
                                                  NULL, /* GCancellable */
                                                  0,    /* uid_t run_as_uid */
                                                  0,    /* uid_t run_as_euid */
                                                  NULL, /* gint *out_status */
                                                  &error_message,
                                                  NULL,  /* input_string */
                                                  "wipefs -a %s",
                                                  escaped_member_device_file))
        {
          g_dbus_method_invocation_return_error (invocation,
                                                 UDISKS_ERROR,
                                                 UDISKS_ERROR_FAILED,
                                                 "Error wiping  %s after removal from RAID array %s: %s",
                                                 member_device_file,
                                                 device_file,
                                                 error_message);
          goto out;
        }
    }

  udisks_mdraid_complete_remove_device (_mdraid, invocation);

 out:
  g_free (error_message);
  g_free (escaped_device_file);
  g_free (escaped_member_device_file);
  g_free (member_states);
  g_clear_object (&member_device_object);
  g_clear_object (&member_device);
  g_clear_object (&raid_device);
  g_clear_object (&object);
  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mdraid_iface_init (UDisksMDRaidIface *iface)
{
  iface->handle_start = handle_start;
  iface->handle_stop = handle_stop;
  iface->handle_remove_device = handle_remove_device;
}
