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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "udiskslogging.h"
#include "udisksdaemon.h"
#include "udisksdaemonutil.h"
#include "udiskslinuxprovider.h"
#include "udiskslinuxmdraidobject.h"
#include "udiskslinuxmdraid.h"
#include "udiskslinuxblockobject.h"

/**
 * SECTION:udiskslinuxmdraidobject
 * @title: UDisksLinuxMDRaidObject
 * @short_description: Object representing a Linux Software RAID array
 *
 * Object corresponding to a Linux Software RAID array.
 */

typedef struct _UDisksLinuxMDRaidObjectClass   UDisksLinuxMDRaidObjectClass;

/**
 * UDisksLinuxMDRaidObject:
 *
 * The #UDisksLinuxMDRaidObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksLinuxMDRaidObject
{
  UDisksObjectSkeleton parent_instance;

  UDisksDaemon *daemon;

  /* list of GUdevDevice objects for detected member devices */
  GList *devices;

  /* interfaces */
  UDisksMDRaid *iface_mdraid;

  /* watches for sysfs attr changes */
  GSource *sync_action_source;
  GSource *degraded_source;
};

struct _UDisksLinuxMDRaidObjectClass
{
  UDisksObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

static void
remove_watches (UDisksLinuxMDRaidObject *object)
{
  if (object->sync_action_source != NULL)
    {
      g_source_destroy (object->sync_action_source);
      object->sync_action_source = NULL;
    }
  if (object->degraded_source != NULL)
    {
      g_source_destroy (object->degraded_source);
      object->degraded_source = NULL;
    }
}

G_DEFINE_TYPE (UDisksLinuxMDRaidObject, udisks_linux_mdraid_object, UDISKS_TYPE_OBJECT_SKELETON);

static void
udisks_linux_mdraid_object_finalize (GObject *_object)
{
  UDisksLinuxMDRaidObject *object = UDISKS_LINUX_MDRAID_OBJECT (_object);

  /* note: we don't hold a ref to object->daemon */

  remove_watches (object);

  g_list_foreach (object->devices, (GFunc) g_object_unref, NULL);
  g_list_free (object->devices);

  if (object->iface_mdraid != NULL)
    g_object_unref (object->iface_mdraid);

  if (G_OBJECT_CLASS (udisks_linux_mdraid_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_linux_mdraid_object_parent_class)->finalize (_object);
}

static void
udisks_linux_mdraid_object_get_property (GObject    *__object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  UDisksLinuxMDRaidObject *object = UDISKS_LINUX_MDRAID_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_linux_mdraid_object_get_daemon (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_linux_mdraid_object_set_property (GObject      *__object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  UDisksLinuxMDRaidObject *object = UDISKS_LINUX_MDRAID_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (object->daemon == NULL);
      /* we don't take a reference to the daemon */
      object->daemon = g_value_get_object (value);
      break;

    case PROP_DEVICE:
      g_assert (object->devices == NULL);
      object->devices = g_list_prepend (NULL, g_value_dup_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
udisks_linux_mdraid_object_init (UDisksLinuxMDRaidObject *object)
{
}

static void
strip_and_replace_with_uscore (gchar *s)
{
  guint n;

  if (s == NULL)
    goto out;

  g_strstrip (s);

  for (n = 0; s != NULL && s[n] != '\0'; n++)
    {
      if (s[n] == ' ' || s[n] == '-' || s[n] == ':')
        s[n] = '_';
    }

 out:
  ;
}

static void
udisks_linux_mdraid_object_constructed (GObject *_object)
{
  UDisksLinuxMDRaidObject *object = UDISKS_LINUX_MDRAID_OBJECT (_object);
  gchar *uuid;
  gchar *s;

  /* initial coldplug */
  udisks_linux_mdraid_object_uevent (object, "add", object->devices->data);

  /* compute the object path */
  uuid = udisks_mdraid_dup_uuid (object->iface_mdraid);
  strip_and_replace_with_uscore (uuid);
  s = g_strdup_printf ("/org/freedesktop/UDisks2/mdraid/%s", uuid);
  g_free (uuid);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), s);
  g_free (s);

  if (G_OBJECT_CLASS (udisks_linux_mdraid_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (udisks_linux_mdraid_object_parent_class)->constructed (_object);
}

static void
udisks_linux_mdraid_object_class_init (UDisksLinuxMDRaidObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_linux_mdraid_object_finalize;
  gobject_class->constructed  = udisks_linux_mdraid_object_constructed;
  gobject_class->set_property = udisks_linux_mdraid_object_set_property;
  gobject_class->get_property = udisks_linux_mdraid_object_get_property;

  /**
   * UDisksLinuxMDRaidObject:daemon:
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
   * UDisksLinuxMDRaidObject:device:
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
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

}

/**
 * udisks_linux_mdraid_object_new:
 * @daemon: A #UDisksDaemon.
 * @device: The #GUdevDevice for the sysfs block device.
 *
 * Create a new mdraid object.
 *
 * Returns: A #UDisksLinuxMDRaidObject object. Free with g_object_unref().
 */
UDisksLinuxMDRaidObject *
udisks_linux_mdraid_object_new (UDisksDaemon  *daemon,
                               GUdevDevice   *device)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), NULL);
  return UDISKS_LINUX_MDRAID_OBJECT (g_object_new (UDISKS_TYPE_LINUX_MDRAID_OBJECT,
                                                   "daemon", daemon,
                                                   "device", device,
                                                   NULL));
}

/**
 * udisks_linux_mdraid_object_get_daemon:
 * @object: A #UDisksLinuxMDRaidObject.
 *
 * Gets the daemon used by @object.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @object.
 */
UDisksDaemon *
udisks_linux_mdraid_object_get_daemon (UDisksLinuxMDRaidObject *object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MDRAID_OBJECT (object), NULL);
  return object->daemon;
}

/**
 * udisks_linux_mdraid_object_get_members:
 * @object: A #UDisksLinuxMDRaidObject.
 *
 * Gets the current #GUdevDevice objects for the RAID members associated with @object.
 *
 * Returns: A list of #GUdevDevice objects. Free each element with
 * g_object_unref(), then free the list with g_list_free().
 */
GList *
udisks_linux_mdraid_object_get_members (UDisksLinuxMDRaidObject *object)
{
  GList *ret = NULL;
  GList *l;

  g_return_val_if_fail (UDISKS_IS_LINUX_MDRAID_OBJECT (object), NULL);

  for (l = object->devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      /* TODO: find a better way to distinguish member vs array ? */
      if (!g_str_has_prefix (g_udev_device_get_device_file (device), "/dev/md"))
        {
          ret = g_list_prepend (ret, g_object_ref (device));
          goto out;
        }
    }
 out:
  return ret;
}

/**
 * udisks_linux_mdraid_object_get_device:
 * @object: A #UDisksLinuxMDRaidObject.
 *
 * Gets the current #GUdevDevice object for the RAID device
 * (e.g. /dev/md0) associated with @object, if any.
 *
 * Returns: (transfer full): A #GUdevDevice or %NULL. Free with g_object_unref().
 */
GUdevDevice *
udisks_linux_mdraid_object_get_device (UDisksLinuxMDRaidObject   *object)
{
  GUdevDevice *ret = NULL;
  GList *l;

  g_return_val_if_fail (UDISKS_IS_LINUX_MDRAID_OBJECT (object), NULL);

  for (l = object->devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      /* TODO: find a better way to distinguish member vs array ? */
      if (g_str_has_prefix (g_udev_device_get_device_file (device), "/dev/md"))
        {
          ret = g_object_ref (device);
          goto out;
        }
    }
 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef gboolean (*HasInterfaceFunc)    (UDisksLinuxMDRaidObject     *object);
typedef void     (*ConnectInterfaceFunc) (UDisksLinuxMDRaidObject    *object);
typedef gboolean (*UpdateInterfaceFunc) (UDisksLinuxMDRaidObject     *object,
                                         const gchar    *uevent_action,
                                         GDBusInterface *interface);

static gboolean
update_iface (UDisksLinuxMDRaidObject   *object,
              const gchar              *uevent_action,
              HasInterfaceFunc          has_func,
              ConnectInterfaceFunc      connect_func,
              UpdateInterfaceFunc       update_func,
              GType                     skeleton_type,
              gpointer                  _interface_pointer)
{
  gboolean ret = FALSE;
  gboolean has;
  gboolean add;
  GDBusInterface **interface_pointer = _interface_pointer;

  g_return_val_if_fail (object != NULL, FALSE);
  g_return_val_if_fail (has_func != NULL, FALSE);
  g_return_val_if_fail (update_func != NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (skeleton_type, G_TYPE_OBJECT), FALSE);
  g_return_val_if_fail (g_type_is_a (skeleton_type, G_TYPE_DBUS_INTERFACE), FALSE);
  g_return_val_if_fail (interface_pointer != NULL, FALSE);
  g_return_val_if_fail (*interface_pointer == NULL || G_IS_DBUS_INTERFACE (*interface_pointer), FALSE);

  add = FALSE;
  has = has_func (object);
  if (*interface_pointer == NULL)
    {
      if (has)
        {
          *interface_pointer = g_object_new (skeleton_type, NULL);
          if (connect_func != NULL)
            connect_func (object);
          add = TRUE;
        }
    }
  else
    {
      if (!has)
        {
          g_dbus_object_skeleton_remove_interface (G_DBUS_OBJECT_SKELETON (object),
                                                   G_DBUS_INTERFACE_SKELETON (*interface_pointer));
          g_object_unref (*interface_pointer);
          *interface_pointer = NULL;
        }
    }

  if (*interface_pointer != NULL)
    {
      if (update_func (object, uevent_action, G_DBUS_INTERFACE (*interface_pointer)))
        ret = TRUE;
      if (add)
        g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (object),
                                              G_DBUS_INTERFACE_SKELETON (*interface_pointer));
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
mdraid_check (UDisksLinuxMDRaidObject *object)
{
  return TRUE;
}

static void
mdraid_connect (UDisksLinuxMDRaidObject *object)
{
}

static gboolean
mdraid_update (UDisksLinuxMDRaidObject  *object,
               const gchar              *uevent_action,
               GDBusInterface           *_iface)
{
  return udisks_linux_mdraid_update (UDISKS_LINUX_MDRAID (object->iface_mdraid), object);
}

/* ---------------------------------------------------------------------------------------------------- */

static GList *
find_link_for_sysfs_path (UDisksLinuxMDRaidObject *object,
                          const gchar            *sysfs_path)
{
  GList *l;
  GList *ret;
  ret = NULL;
  for (l = object->devices; l != NULL; l = l->next)
    {
      GUdevDevice *device = G_UDEV_DEVICE (l->data);
      if (g_strcmp0 (g_udev_device_get_sysfs_path (device), sysfs_path) == 0)
        {
          ret = l;
          goto out;
        }
    }
 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static GSource *
watch_attr (GUdevDevice *device,
            const gchar *attr,
            GSourceFunc  callback,
            gpointer     user_data)
{
  GError *error = NULL;
  gchar *path = NULL;
  GIOChannel *channel = NULL;
  GSource *ret = NULL;;

  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), NULL);

  path = g_strdup_printf ("%s/%s", g_udev_device_get_sysfs_path (device), attr);
  channel = g_io_channel_new_file (path, "r", &error);
  if (channel != NULL)
    {
      ret = g_io_create_watch (channel, G_IO_ERR);
      g_source_set_callback (ret, callback, user_data, NULL);
      g_source_attach (ret, g_main_context_get_thread_default ());
      g_source_unref (ret);
      g_io_channel_unref (channel); /* the keeps a reference to this object */
    }
  else
    {
      udisks_warning ("Error creating watch for file %s: %s (%s, %d)",
                      path, error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
    }
  g_free (path);

  return ret;
}

/* ----------------------------------------------------------------------------------------------------  */

static gboolean
attr_changed (GIOChannel   *channel,
              GIOCondition  cond,
              gpointer      user_data)
{
  UDisksLinuxMDRaidObject *object = UDISKS_LINUX_MDRAID_OBJECT (user_data);
  GError *error = NULL;
  gchar *str = NULL;
  gsize len = 0;

  if (cond & ~G_IO_ERR)
    goto out;

  if (g_io_channel_seek_position (channel, 0, G_SEEK_SET, &error) != G_IO_STATUS_NORMAL)
    {
      udisks_warning ("Error seeking in channel: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      goto out;
    }
  if (g_io_channel_read_to_end (channel, &str, &len, &error) != G_IO_STATUS_NORMAL)
    {
      udisks_warning ("Error reading: %s (%s, %d)",
                      error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      goto out;
    }

  /* synthesize uevent */
  udisks_linux_mdraid_object_uevent (object, "change", NULL);

 out:
  return TRUE; /* keep event source around */
}

/* ---------------------------------------------------------------------------------------------------- */

/* The md(4) driver does not use the usual uevent 'change' mechanism
 * for notification - instead it excepts user-space to select(2)-ish
 * on a fd for the sysfs attribute. Annoying. See
 *
 *  http://www.kernel.org/doc/Documentation/md.txt
 *
 * for more details.
 */

static void
md_device_added (UDisksLinuxMDRaidObject *object,
                 GUdevDevice             *device)
{
  g_assert (object->sync_action_source == NULL);
  g_assert (object->degraded_source == NULL);

  object->sync_action_source = watch_attr (device,
                                           "md/sync_action",
                                           (GSourceFunc) attr_changed,
                                           object);
  object->degraded_source = watch_attr (device,
                                        "md/degraded",
                                        (GSourceFunc) attr_changed,
                                        object);
}

static void
md_device_removed (UDisksLinuxMDRaidObject *object,
                   GUdevDevice             *device)
{
  remove_watches (object);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * udisks_linux_mdraid_object_uevent:
 * @object: A #UDisksLinuxMDRaidObject.
 * @action: Uevent action or %NULL
 * @device: A #GUdevDevice device object or %NULL if the device hasn't changed.
 *
 * Updates all information on interfaces on @mdraid.
 */
void
udisks_linux_mdraid_object_uevent (UDisksLinuxMDRaidObject *object,
                                   const gchar             *action,
                                   GUdevDevice             *device)
{
  GList *link;
  gboolean conf_changed = FALSE;

  g_return_if_fail (UDISKS_IS_LINUX_MDRAID_OBJECT (object));
  g_return_if_fail (device == NULL || G_UDEV_IS_DEVICE (device));

  link = NULL;
  if (device != NULL)
    link = find_link_for_sysfs_path (object, g_udev_device_get_sysfs_path (device));
  if (g_strcmp0 (action, "remove") == 0)
    {
      if (link != NULL)
        {
          if (g_str_has_prefix (g_udev_device_get_name (device), "md"))
            md_device_removed (object, device);
          g_object_unref (G_UDEV_DEVICE (link->data));
          object->devices = g_list_delete_link (object->devices, link);
        }
      else
        {
          udisks_warning ("MDRaid doesn't have device with sysfs path %s on remove event",
                          g_udev_device_get_sysfs_path (device));
        }
    }
  else
    {
      if (link != NULL)
        {
          g_object_unref (G_UDEV_DEVICE (link->data));
          link->data = g_object_ref (device);
        }
      else
        {
          if (device != NULL)
            {
              object->devices = g_list_append (object->devices, g_object_ref (device));
              if (g_str_has_prefix (g_udev_device_get_name (device), "md"))
                md_device_added (object, device);
            }
        }
    }

  conf_changed = FALSE;
  conf_changed |= update_iface (object, action, mdraid_check, mdraid_connect, mdraid_update,
                                UDISKS_TYPE_LINUX_MDRAID, &object->iface_mdraid);
}

/* ---------------------------------------------------------------------------------------------------- */

