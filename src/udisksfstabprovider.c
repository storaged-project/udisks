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

#include <stdio.h>
#include <mntent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <gudev/gudev.h>

#include <string.h>

#include "udisksdaemon.h"
#include "udisksprovider.h"
#include "udisksmount.h"
#include "udisksmountmonitor.h"
#include "udisksfstabprovider.h"
#include "udiskslinuxprovider.h"

/**
 * SECTION:udisksfstabprovider
 * @title: UDisksFstabProvider
 * @short_description: Provides /etc/fstab configuration items
 *
 * This type provides #UDisksConfigurationItem objects for mount
 * points defined in the <file>/etc/fstab</file>.
 */

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  guint   line_no;

  gchar *device;
  gchar *path;
  gchar *type;
  gchar *options;
  gint   freq;
  gint   passno;

  gchar *object_path;
  GDBusObjectStub *object;
  UDisksConfigurationItem *item;
} FstabEntry;

static FstabEntry *
fstab_entry_new_from_mntent (struct mntent *m,
                             guint  line_no)
{
  FstabEntry *entry;
  entry          = g_new0 (FstabEntry, 1);
  entry->line_no = line_no;
  entry->device  = g_strdup (m->mnt_fsname);
  entry->path    = g_strdup (m->mnt_dir);
  entry->type    = g_strdup (m->mnt_type);
  entry->options = g_strdup (m->mnt_opts);
  entry->freq    = m->mnt_freq;
  entry->passno  = m->mnt_passno;
  return entry;
}

static void
fstab_entry_free (FstabEntry *entry)
{
  g_free (entry->device);
  g_free (entry->path);
  g_free (entry->type);
  g_free (entry->options);

  g_free (entry->object_path);
  if (entry->object != NULL)
    g_object_unref (entry->object);
  if (entry->item != NULL)
    g_object_unref (entry->item);

  g_free (entry);
}

static gint
fstab_entry_compare (gconstpointer a, gconstpointer b)
{
  const FstabEntry *ea;
  const FstabEntry *eb;
  gint ret;

  ea = a;
  eb = b;

  ret = g_strcmp0 (ea->device, eb->device);
  if (ret != 0)
    goto out;
  ret = g_strcmp0 (ea->path, eb->path);
  if (ret != 0)
    goto out;
  ret = g_strcmp0 (ea->type, eb->type);
  if (ret != 0)
    goto out;
  ret = g_strcmp0 (ea->options, eb->options);
  if (ret != 0)
    goto out;
  ret = ea->freq - eb->freq;
  if (ret != 0)
    goto out;
  ret = ea->passno - eb->passno;
  if (ret != 0)
    goto out;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct _UDisksFstabProviderClass   UDisksFstabProviderClass;

/**
 * UDisksFstabProvider:
 *
 * The #UDisksFstabProvider structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksFstabProvider
{
  UDisksProvider parent_instance;

  UDisksMountMonitor *mount_monitor;

  GFileMonitor *fstab_monitor;

  GUdevClient *gudev_client;

  GList *entries;
};

struct _UDisksFstabProviderClass
{
  UDisksProviderClass parent_class;
};

G_DEFINE_TYPE (UDisksFstabProvider, udisks_fstab_provider, UDISKS_TYPE_PROVIDER);

static void on_file_monitor_changed (GFileMonitor     *monitor,
                                     GFile            *file,
                                     GFile            *other_file,
                                     GFileMonitorEvent event_type,
                                     gpointer          user_data);
static void load_and_process_fstab  (UDisksFstabProvider *provider);
static gboolean export_entry (UDisksFstabProvider *provider,
                              FstabEntry          *entry);
static void unexport_entry (UDisksFstabProvider *provider,
                            FstabEntry          *entry);

static void update_entry (UDisksFstabProvider  *provider,
                          FstabEntry           *entry);

static void on_mount_monitor_mount_added   (UDisksMountMonitor  *monitor,
                                            UDisksMount         *mount,
                                            gpointer             user_data);
static void on_mount_monitor_mount_removed (UDisksMountMonitor  *monitor,
                                            UDisksMount         *mount,
                                            gpointer             user_data);

static void on_uevent (GUdevClient  *client,
                       const gchar  *action,
                       GUdevDevice  *device,
                       gpointer      user_data);

static void
udisks_fstab_provider_finalize (GObject *object)
{
  UDisksFstabProvider *provider = UDISKS_FSTAB_PROVIDER (object);
  GList *l;

  /* note: provider->gudev_client is owned by the Linux provider */

  /* note: we don't hold a ref to provider->mount_monitor */
  g_signal_handlers_disconnect_by_func (provider->mount_monitor, on_mount_monitor_mount_added, provider);
  g_signal_handlers_disconnect_by_func (provider->mount_monitor, on_mount_monitor_mount_removed, provider);

  for (l = provider->entries; l != NULL; l = l->next)
    {
      FstabEntry *entry = l->data;
      unexport_entry (provider, entry);
      fstab_entry_free (entry);
    }
  g_list_free (provider->entries);

  if (provider->fstab_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (provider->fstab_monitor,
                                            G_CALLBACK (on_file_monitor_changed),
                                            provider);
      g_object_unref (provider->fstab_monitor);
    }

  if (G_OBJECT_CLASS (udisks_fstab_provider_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_fstab_provider_parent_class)->finalize (object);
}

static void
udisks_fstab_provider_init (UDisksFstabProvider *provider)
{
}

static void
udisks_fstab_provider_constructed (GObject *object)
{
  UDisksFstabProvider *provider = UDISKS_FSTAB_PROVIDER (object);
  GFile *file;
  GError *error;

  /* use the same GUdevClient as the Linux provider */
  provider->gudev_client = udisks_linux_provider_get_udev_client (udisks_daemon_get_linux_provider (udisks_provider_get_daemon (UDISKS_PROVIDER (provider))));
  g_signal_connect (provider->gudev_client,
                    "uevent",
                    G_CALLBACK (on_uevent),
                    provider);

  provider->mount_monitor = udisks_daemon_get_mount_monitor (udisks_provider_get_daemon (UDISKS_PROVIDER (provider)));
  g_signal_connect (provider->mount_monitor,
                    "mount-added",
                    G_CALLBACK (on_mount_monitor_mount_added),
                    provider);
  g_signal_connect (provider->mount_monitor,
                    "mount-removed",
                    G_CALLBACK (on_mount_monitor_mount_removed),
                    provider);

  file = g_file_new_for_path ("/etc/fstab");
  error = NULL;
  provider->fstab_monitor = g_file_monitor_file (file,
                                                 G_FILE_MONITOR_SEND_MOVED,
                                                 NULL,
                                                 &error);
  if (provider->fstab_monitor == NULL)
    {
      g_warning ("Failed to monitor /etc/fstab: %s", error->message);
    }
  else
    {
      g_signal_connect (provider->fstab_monitor,
                        "changed",
                        G_CALLBACK (on_file_monitor_changed),
                        provider);
    }
  g_object_unref (file);

  load_and_process_fstab (provider);

  if (G_OBJECT_CLASS (udisks_fstab_provider_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_fstab_provider_parent_class)->constructed (object);
}


static void
udisks_fstab_provider_class_init (UDisksFstabProviderClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_fstab_provider_finalize;
  gobject_class->constructed  = udisks_fstab_provider_constructed;
}

/**
 * udisks_fstab_provider_new:
 * @daemon: A #UDisksDaemon.
 *
 * Create a new provider object for <file>/etc/fstab</file> configuration entries.
 *
 * Returns: A #UDisksFstabProvider object. Free with g_object_unref().
 */
UDisksFstabProvider *
udisks_fstab_provider_new (UDisksDaemon *daemon)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  return UDISKS_FSTAB_PROVIDER (g_object_new (UDISKS_TYPE_FSTAB_PROVIDER,
                                              "daemon", daemon,
                                              NULL));
}

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

static void
on_file_monitor_changed (GFileMonitor     *monitor,
                         GFile            *file,
                         GFile            *other_file,
                         GFileMonitorEvent event_type,
                         gpointer          user_data)
{
  UDisksFstabProvider *provider = UDISKS_FSTAB_PROVIDER (user_data);
  load_and_process_fstab (provider);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
export_entry (UDisksFstabProvider *provider,
              FstabEntry          *entry)
{
  gboolean exported;
  gchar *device;
  gchar *name;
  gchar *target;
  GList *l;
  GVariantBuilder options_builder;

  exported = FALSE;
  device = NULL;
  name = NULL;

  g_assert (entry->object_path == NULL);

  if (g_str_has_prefix (entry->device, "UUID="))
    {
      device = g_strdup_printf ("/dev/disk/by-uuid/%s", entry->device + 5);
    }
  else if (g_str_has_prefix (entry->device, "LABEL="))
    {
      device = g_strdup_printf ("/dev/disk/by-label/%s", entry->device + 6);
    }
  else if (g_str_has_prefix (entry->device, "/dev/"))
    {
      device = g_strdup (entry->device);
    }
  else
    {
      /* for now we only consider real block devices */
      goto out;
    }

  /* compute a nice name */
  if (g_str_has_prefix (device, "/dev/disk/by-uuid/"))
    {
      name = g_strdup_printf ("UUID_%s", device + sizeof "/dev/disk/by-uuid/" - 1);
    }
  else if (g_str_has_prefix (device, "/dev/disk/by-label/"))
    {
      name = g_strdup_printf ("Label_%s", device + sizeof "/dev/disk/by-label/" - 1);
    }
  else
    {
      name = g_path_get_basename (device);
    }

  /* compute a pleasant object path and handle possible collisions */
  entry->object_path = util_compute_object_path ("/org/freedesktop/UDisks2/configuration/fstab/", name);
 again:
  for (l = provider->entries; l != NULL; l = l->next)
    {
      FstabEntry *other = l->data;
      if (g_strcmp0 (other->object_path, entry->object_path) == 0)
        {
          gchar *tmp;
          tmp = entry->object_path;
          entry->object_path = g_strdup_printf ("%s_", entry->object_path);
          g_free (tmp);
          goto again;
        }
    }

  entry->item = udisks_configuration_item_stub_new ();

  udisks_configuration_item_set_type (entry->item, "fsmount");

  target = g_strdup_printf ("block:%s", device);
  udisks_configuration_item_set_target (entry->item, target);
  g_free (target);

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&options_builder,
                         "{sv}",
                         "fsmount:path",
                         g_variant_new_variant (g_variant_new_bytestring (entry->path)));
  g_variant_builder_add (&options_builder,
                         "{sv}",
                         "fsmount:type",
                         g_variant_new_variant (g_variant_new_bytestring (entry->type)));
  g_variant_builder_add (&options_builder,
                         "{sv}",
                         "fsmount:options",
                         g_variant_new_variant (g_variant_new_bytestring (entry->options)));
  udisks_configuration_item_set_options (entry->item, g_variant_builder_end (&options_builder));

  udisks_configuration_item_set_origin (entry->item, "fstab");
  udisks_configuration_item_set_origin_detail (entry->item, g_variant_new_variant (g_variant_new_int32 (entry->line_no)));

  /* set the transient fields */
  update_entry (provider, entry);

  entry->object = g_dbus_object_stub_new (entry->object_path);
  g_dbus_object_stub_add_interface (entry->object, G_DBUS_INTERFACE_STUB (entry->item));
  g_dbus_object_manager_server_export (udisks_daemon_get_object_manager (udisks_provider_get_daemon (UDISKS_PROVIDER (provider))),
                                       entry->object);

  exported = TRUE;

 out:

  g_free (device);
  g_free (name);

  return exported;
}

static void
unexport_entry (UDisksFstabProvider *provider,
                FstabEntry          *entry)
{
  g_assert (entry->object_path != NULL);
  g_dbus_object_manager_server_unexport (udisks_daemon_get_object_manager (udisks_provider_get_daemon (UDISKS_PROVIDER (provider))),
                                         entry->object_path);
}

/* ---------------------------------------------------------------------------------------------------- */

/* updates transient fields such as CanApply and IsApplied */
static void
update_entry (UDisksFstabProvider  *provider,
              FstabEntry           *entry)
{
  gboolean can_apply;
  gboolean is_applied;
  const gchar *target;
  dev_t dev;
  GList *mounts;
  GList *l;

  dev = 0;

  can_apply = FALSE;
  is_applied = FALSE;

  target = udisks_configuration_item_get_target (entry->item);
  if (g_str_has_prefix (target, "block:"))
    {
      const gchar *device = target + sizeof "block:" - 1;
      struct stat statbuf;

      if (stat (device, &statbuf) == 0)
        {
          if (statbuf.st_mode & S_IFBLK)
            {
              can_apply = TRUE;
              dev = statbuf.st_rdev;
            }
        }
    }

  if (!can_apply)
    goto out;

  mounts = udisks_mount_monitor_get_mounts_for_dev (provider->mount_monitor, dev);
  for (l = mounts; l != NULL; l = l->next)
    {
      UDisksMount *mount = UDISKS_MOUNT (l->data);

      if (g_strcmp0 (udisks_mount_get_mount_path (mount), entry->path) == 0)
        {
          is_applied = TRUE;
          break;
        }
    }
  g_list_foreach (mounts, (GFunc) g_object_unref, NULL);
  g_list_free (mounts);

 out:
  udisks_configuration_item_set_can_apply (entry->item, can_apply);
  udisks_configuration_item_set_is_applied (entry->item, is_applied);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
load_and_process_fstab (UDisksFstabProvider *provider)
{
  GList *new_entries;
  FILE *f;
  struct mntent mntent;
  gchar buf[4096];
  GList *added;
  GList *removed;
  GList *l;
  guint n;

  f = NULL;
  new_entries = NULL;
  added = NULL;
  removed = NULL;

  f = fopen ("/etc/fstab", "r");
  if (f == NULL)
    {
      g_warning ("Error opening /etc/fstab: %m");
      goto out;
    }

  n = 0;
  while (getmntent_r (f, &mntent, buf, sizeof buf) != NULL)
    {
      FstabEntry *entry;
      entry = fstab_entry_new_from_mntent (&mntent, n++);
      new_entries = g_list_prepend (new_entries, entry);
    }

  new_entries = g_list_sort (new_entries, fstab_entry_compare);
  provider->entries = g_list_sort (provider->entries, fstab_entry_compare);

  diff_sorted_lists (provider->entries,
                     new_entries,
                     fstab_entry_compare,
                     &added,
                     &removed);


  for (l = removed; l != NULL; l = l->next)
    {
      FstabEntry *entry = l->data;
      g_assert (g_list_find (provider->entries, entry) != NULL);
      provider->entries = g_list_remove (provider->entries, entry);
      unexport_entry (provider, entry);
      fstab_entry_free (entry);
    }

  for (l = added; l != NULL; l = l->next)
    {
      FstabEntry *entry = l->data;
      g_assert (g_list_find (provider->entries, entry) == NULL);
      if (export_entry (provider, entry))
        {
          provider->entries = g_list_prepend (provider->entries, entry);
        }
      else
        {
          fstab_entry_free (entry);
        }
    }

 out:
  g_list_free (new_entries);
  g_list_free (added);
  g_list_free (removed);
  if (f != NULL)
    fclose (f);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_mount_monitor_mount_added (UDisksMountMonitor  *monitor,
                              UDisksMount         *mount,
                              gpointer             user_data)
{
  UDisksFstabProvider *provider = UDISKS_FSTAB_PROVIDER (user_data);
  GList *l;

  for (l = provider->entries; l != NULL; l = l->next)
    {
      FstabEntry *entry = l->data;
      update_entry (provider, entry);
    }
}

static void
on_mount_monitor_mount_removed (UDisksMountMonitor  *monitor,
                                UDisksMount         *mount,
                                gpointer             user_data)
{
  UDisksFstabProvider *provider = UDISKS_FSTAB_PROVIDER (user_data);
  GList *l;
  for (l = provider->entries; l != NULL; l = l->next)
    {
      FstabEntry *entry = l->data;
      update_entry (provider, entry);
    }
}

static void
on_uevent (GUdevClient  *client,
           const gchar  *action,
           GUdevDevice  *device,
           gpointer      user_data)
{
  UDisksFstabProvider *provider = UDISKS_FSTAB_PROVIDER (user_data);
  GList *l;
  for (l = provider->entries; l != NULL; l = l->next)
    {
      FstabEntry *entry = l->data;
      update_entry (provider, entry);
    }
}
