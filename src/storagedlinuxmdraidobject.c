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

#include "storagedlogging.h"
#include "storageddaemon.h"
#include "storageddaemonutil.h"
#include "storagedlinuxprovider.h"
#include "storagedlinuxmdraidobject.h"
#include "storagedlinuxmdraid.h"
#include "storagedlinuxblockobject.h"
#include "storagedlinuxdevice.h"
#include "storagedsimplejob.h"

/**
 * SECTION:storagedlinuxmdraidobject
 * @title: StoragedLinuxMDRaidObject
 * @short_description: Object representing a Linux Software RAID array
 *
 * Object corresponding to a Linux Software RAID array.
 */

typedef struct _StoragedLinuxMDRaidObjectClass   StoragedLinuxMDRaidObjectClass;

/**
 * StoragedLinuxMDRaidObject:
 *
 * The #StoragedLinuxMDRaidObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _StoragedLinuxMDRaidObject
{
  StoragedObjectSkeleton parent_instance;

  StoragedDaemon *daemon;

  /* The UUID for the object */
  gchar *uuid;

  /* The StoragedLinuxDevice for the RAID device (e.g. /dev/md0), if any */
  StoragedLinuxDevice *raid_device;

  /* list of StoragedLinuxDevice objects for detected member devices */
  GList *member_devices;

  /* interfaces */
  StoragedMDRaid *iface_mdraid;

  /* watches for sysfs attr changes */
  GSource *sync_action_source;
  GSource *degraded_source;

  /* sync job */
  StoragedBaseJob *sync_job;
  GMutex sync_job_mutex;
};

struct _StoragedLinuxMDRaidObjectClass
{
  StoragedObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_UUID,
  PROP_DAEMON,
  PROP_SYNC_JOB,
};

static void
remove_watches (StoragedLinuxMDRaidObject *object)
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

G_DEFINE_TYPE (StoragedLinuxMDRaidObject, storaged_linux_mdraid_object, STORAGED_TYPE_OBJECT_SKELETON);

static void
storaged_linux_mdraid_object_finalize (GObject *_object)
{
  StoragedLinuxMDRaidObject *object = STORAGED_LINUX_MDRAID_OBJECT (_object);

  /* note: we don't hold a ref to object->daemon */

  remove_watches (object);

  if (object->iface_mdraid != NULL)
    g_object_unref (object->iface_mdraid);

  g_clear_object (&object->raid_device);

  g_list_foreach (object->member_devices, (GFunc) g_object_unref, NULL);
  g_list_free (object->member_devices);

  g_free (object->uuid);

  if (G_OBJECT_CLASS (storaged_linux_mdraid_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (storaged_linux_mdraid_object_parent_class)->finalize (_object);
}

static void
storaged_linux_mdraid_object_get_property (GObject    *__object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  StoragedLinuxMDRaidObject *object = STORAGED_LINUX_MDRAID_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, storaged_linux_mdraid_object_get_daemon (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
storaged_linux_mdraid_object_set_property (GObject      *__object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  StoragedLinuxMDRaidObject *object = STORAGED_LINUX_MDRAID_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (object->daemon == NULL);
      /* we don't take a reference to the daemon */
      object->daemon = g_value_get_object (value);
      break;

    case PROP_UUID:
      object->uuid = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
storaged_linux_mdraid_object_init (StoragedLinuxMDRaidObject *object)
{
  g_mutex_init (&object->sync_job_mutex);
  object->sync_job = NULL;
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
storaged_linux_mdraid_object_constructed (GObject *_object)
{
  StoragedLinuxMDRaidObject *object = STORAGED_LINUX_MDRAID_OBJECT (_object);
  gchar *uuid;
  gchar *s;

  /* compute the object path */
  uuid = g_strdup (object->uuid);
  strip_and_replace_with_uscore (uuid);
  s = g_strdup_printf ("/org/storaged/Storaged/mdraid/%s", uuid);
  g_free (uuid);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), s);
  g_free (s);

  if (G_OBJECT_CLASS (storaged_linux_mdraid_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (storaged_linux_mdraid_object_parent_class)->constructed (_object);
}

static void
storaged_linux_mdraid_object_class_init (StoragedLinuxMDRaidObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = storaged_linux_mdraid_object_finalize;
  gobject_class->constructed  = storaged_linux_mdraid_object_constructed;
  gobject_class->set_property = storaged_linux_mdraid_object_set_property;
  gobject_class->get_property = storaged_linux_mdraid_object_get_property;

  /**
   * StoragedLinuxMDRaidObject:daemon:
   *
   * The #StoragedDaemon the object is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the object is for",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedLinuxMDRaidObject:uuid:
   *
   * The UUID for the array.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_UUID,
                                   g_param_spec_string ("uuid",
                                                        "UUID",
                                                        "The UUID for the array",
                                                        NULL,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * storaged_linux_mdraid_object_new:
 * @daemon: A #StoragedDaemon.
 * @uuid: The UUID for the array.
 *
 * Create a new MDRaid object.
 *
 * Returns: A #StoragedLinuxMDRaidObject object. Free with g_object_unref().
 */
StoragedLinuxMDRaidObject *
storaged_linux_mdraid_object_new (StoragedDaemon  *daemon,
                                  const gchar     *uuid)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (uuid != NULL, NULL);
  return STORAGED_LINUX_MDRAID_OBJECT (g_object_new (STORAGED_TYPE_LINUX_MDRAID_OBJECT,
                                                   "daemon", daemon,
                                                   "uuid", uuid,
                                                   NULL));
}

/**
 * storaged_linux_mdraid_object_get_daemon:
 * @object: A #StoragedLinuxMDRaidObject.
 *
 * Gets the daemon used by @object.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @object.
 */
StoragedDaemon *
storaged_linux_mdraid_object_get_daemon (StoragedLinuxMDRaidObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object), NULL);
  return object->daemon;
}

/**
 * storaged_linux_mdraid_object_get_members:
 * @object: A #StoragedLinuxMDRaidObject.
 *
 * Gets the current #StoragedLinuxDevice objects for the RAID members associated with @object.
 *
 * Returns: A list of #StoragedLinuxDevice objects. Free each element with
 * g_object_unref(), then free the list with g_list_free().
 */
GList *
storaged_linux_mdraid_object_get_members (StoragedLinuxMDRaidObject *object)
{
  GList *ret = NULL;

  g_return_val_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object), NULL);

  ret = g_list_copy (object->member_devices);
  g_list_foreach (ret, (GFunc) g_object_ref, NULL);

  return ret;
}

/**
 * storaged_linux_mdraid_object_get_device:
 * @object: A #StoragedLinuxMDRaidObject.
 *
 * Gets the current #StoragedLinuxDevice object for the RAID device
 * (e.g. /dev/md0) associated with @object, if any.
 *
 * Returns: (transfer full): A #StoragedLinuxDevice or %NULL. Free with g_object_unref().
 */
StoragedLinuxDevice *
storaged_linux_mdraid_object_get_device (StoragedLinuxMDRaidObject   *object)
{
  StoragedLinuxDevice *ret = NULL;

  g_return_val_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object), NULL);

  ret = object->raid_device != NULL ? g_object_ref (object->raid_device) : NULL;

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef gboolean (*HasInterfaceFunc)    (StoragedLinuxMDRaidObject     *object);
typedef void     (*ConnectInterfaceFunc) (StoragedLinuxMDRaidObject    *object);
typedef gboolean (*UpdateInterfaceFunc) (StoragedLinuxMDRaidObject     *object,
                                         const gchar                   *uevent_action,
                                         GDBusInterface                *interface);

static gboolean
update_iface (StoragedLinuxMDRaidObject   *object,
              const gchar                 *uevent_action,
              HasInterfaceFunc             has_func,
              ConnectInterfaceFunc         connect_func,
              UpdateInterfaceFunc          update_func,
              GType                        skeleton_type,
              gpointer                     _interface_pointer)
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
mdraid_check (StoragedLinuxMDRaidObject *object)
{
  return TRUE;
}

static void
mdraid_connect (StoragedLinuxMDRaidObject *object)
{
}

static gboolean
mdraid_update (StoragedLinuxMDRaidObject  *object,
               const gchar                *uevent_action,
               GDBusInterface             *_iface)
{
  return storaged_linux_mdraid_update (STORAGED_LINUX_MDRAID (object->iface_mdraid), object);
}

/* ---------------------------------------------------------------------------------------------------- */

static GList *
find_link_for_sysfs_path_for_member (StoragedLinuxMDRaidObject *object,
                                     const gchar               *sysfs_path)
{
  GList *l;
  GList *ret;
  ret = NULL;

  for (l = object->member_devices; l != NULL; l = l->next)
    {
      StoragedLinuxDevice *device = STORAGED_LINUX_DEVICE (l->data);
      if (g_strcmp0 (g_udev_device_get_sysfs_path (device->udev_device), sysfs_path) == 0)
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
watch_attr (StoragedLinuxDevice *device,
            const gchar         *attr,
            GSourceFunc          callback,
            gpointer             user_data)
{
  GError *error = NULL;
  gchar *path = NULL;
  GIOChannel *channel = NULL;
  GSource *ret = NULL;;

  g_return_val_if_fail (STORAGED_IS_LINUX_DEVICE (device), NULL);

  path = g_strdup_printf ("%s/%s", g_udev_device_get_sysfs_path (device->udev_device), attr);
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
      storaged_warning ("Error creating watch for file %s: %s (%s, %d)",
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
  StoragedLinuxMDRaidObject *object = STORAGED_LINUX_MDRAID_OBJECT (user_data);
  gboolean bail = FALSE;
  GError *error = NULL;
  gchar *str = NULL;
  gsize len = 0;

  if (cond & ~G_IO_ERR)
    goto out;

  if (g_io_channel_seek_position (channel, 0, G_SEEK_SET, &error) != G_IO_STATUS_NORMAL)
    {
      storaged_debug ("Error seeking in channel (uuid %s): %s (%s, %d)",
                      object->uuid, error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      bail = TRUE;
      goto out;
    }

  if (g_io_channel_read_to_end (channel, &str, &len, &error) != G_IO_STATUS_NORMAL)
    {
      storaged_debug ("Error reading (uuid %s): %s (%s, %d)",
                      object->uuid, error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      bail = TRUE;
      goto out;
    }

  g_free (str);

  /* synthesize uevent */
  if (object->raid_device != NULL)
    storaged_linux_mdraid_object_uevent (object, "change", object->raid_device, FALSE);

 out:
  if (bail)
    remove_watches (object);
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
raid_device_added (StoragedLinuxMDRaidObject *object,
                   StoragedLinuxDevice       *device)
{
  g_assert (object->sync_action_source == NULL);
  g_assert (object->degraded_source == NULL);

  /* storaged_debug ("start watching %s", g_udev_device_get_sysfs_path (device->udev_device)); */
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
raid_device_removed (StoragedLinuxMDRaidObject *object,
                     StoragedLinuxDevice       *device)
{
  /* storaged_debug ("stop watching %s", g_udev_device_get_sysfs_path (device->udev_device)); */
  remove_watches (object);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_mdraid_object_uevent:
 * @object: A #StoragedLinuxMDRaidObject.
 * @action: Uevent action or %NULL
 * @device: A #StoragedLinuxDevice device object or %NULL if the device hasn't changed.
 * @is_member: %TRUE if @device is a member, %FALSE if it's the raid device.
 *
 * Updates all information on interfaces on @mdraid.
 */
void
storaged_linux_mdraid_object_uevent (StoragedLinuxMDRaidObject *object,
                                     const gchar               *action,
                                     StoragedLinuxDevice       *device,
                                     gboolean                   is_member)
{
  gboolean conf_changed = FALSE;

  g_return_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object));
  g_return_if_fail (STORAGED_IS_LINUX_DEVICE (device));

  /* storaged_debug ("is_member=%d for uuid %s and device %s", is_member, object->uuid, g_udev_device_get_device_file (device->udev_device)); */

  if (is_member)
    {
      GList *link = NULL;
      link = NULL;
      if (device != NULL)
        link = find_link_for_sysfs_path_for_member (object, g_udev_device_get_sysfs_path (device->udev_device));

      if (g_strcmp0 (action, "remove") == 0)
        {
          if (link != NULL)
            {
              g_object_unref (STORAGED_LINUX_DEVICE (link->data));
              object->member_devices = g_list_delete_link (object->member_devices, link);
            }
          else
            {
              storaged_warning ("MDRaid with UUID %s doesn't have member device with sysfs path %s on remove event",
                                object->uuid,
                                g_udev_device_get_sysfs_path (device->udev_device));
            }
        }
      else
        {
          if (link != NULL)
            {
              if (device != link->data)
                {
                  g_object_unref (STORAGED_LINUX_DEVICE (link->data));
                  link->data = g_object_ref (device);
                }
            }
          else
            {
              if (device != NULL)
                {
                  object->member_devices = g_list_append (object->member_devices, g_object_ref (device));
                }
            }
        }
    }
  else
    {
      /* Skip partitions of raid devices */
      if (g_strcmp0 (g_udev_device_get_devtype (device->udev_device), "disk") != 0)
        goto out;

      if (g_strcmp0 (action, "remove") == 0)
        {
          if (object->raid_device != NULL)
            if (g_strcmp0 (g_udev_device_get_sysfs_path (object->raid_device->udev_device),
                           g_udev_device_get_sysfs_path (device->udev_device)) == 0)
              {
                g_clear_object (&object->raid_device);
                raid_device_removed (object, object->raid_device);
              }
            else
              {
                storaged_warning ("MDRaid with UUID %s doesn't have raid device with sysfs path %s on remove event (it has %s)",
                                  object->uuid,
                                  g_udev_device_get_sysfs_path (device->udev_device),
                                  g_udev_device_get_sysfs_path (object->raid_device->udev_device));
              }
          else
            {
              storaged_warning ("MDRaid with UUID %s doesn't have raid device with sysfs path %s on remove event",
                                object->uuid,
                                g_udev_device_get_sysfs_path (device->udev_device));
            }
        }
      else
        {
          if (object->raid_device == NULL)
            {
              object->raid_device = g_object_ref (device);
              raid_device_added (object, object->raid_device);
            }
          else
            {
              if (device != object->raid_device)
                {
                  g_clear_object (&object->raid_device);
                  object->raid_device = g_object_ref (device);
                }
            }
        }
    }

  /* if we don't have any devices, no point in updating (we should get nuked soon anyway) */
  if (storaged_linux_mdraid_object_have_devices (object))
    {
      conf_changed = FALSE;
      conf_changed |= update_iface (object, action, mdraid_check, mdraid_connect, mdraid_update,
                                    STORAGED_TYPE_LINUX_MDRAID, &object->iface_mdraid);
    }
 out:
  ;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * storaged_linux_mdraid_object_have_devices:
 * @object: A #StoragedLinuxMDRaidObject.
 *
 * Checks if there are any devices associated with @object at
 * all. This includes both member devices and the raid device.
 *
 * Returns: %TRUE if at least one device is associated with @object, %FALSE otherwise.
 */
gboolean
storaged_linux_mdraid_object_have_devices (StoragedLinuxMDRaidObject   *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object), FALSE);

  return g_list_length (object->member_devices) > 0 || object->raid_device != NULL;
}

StoragedBaseJob *
storaged_linux_mdraid_object_get_sync_job (StoragedLinuxMDRaidObject *object)
{
  StoragedBaseJob *rval = NULL;

  g_return_val_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object), NULL);

  g_mutex_lock (&object->sync_job_mutex);
  rval = object->sync_job;
  g_mutex_unlock (&object->sync_job_mutex);

  return rval;
}

gboolean
storaged_linux_mdraid_object_set_sync_job  (StoragedLinuxMDRaidObject   *object,
                                            StoragedBaseJob             *job)
{
  gboolean rval = TRUE;

  g_return_val_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object), FALSE);

  g_mutex_lock (&object->sync_job_mutex);
  if (! object->sync_job)
    object->sync_job = g_object_ref (job);
  else
    rval = FALSE;
  g_mutex_unlock (&object->sync_job_mutex);

  return rval;
}

gboolean
storaged_linux_mdraid_object_complete_sync_job (StoragedLinuxMDRaidObject *object,
                                                gboolean                   success,
                                                const gchar               *message)
{
  gboolean rval = TRUE;

  g_return_val_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object), FALSE);

  g_mutex_lock (&object->sync_job_mutex);

  if (! object->sync_job)
    {
      rval = FALSE;
    }
  else
    {
      storaged_simple_job_complete (STORAGED_SIMPLE_JOB (object->sync_job),
                                    success,
                                    message);

      g_clear_object (&object->sync_job);
    }

  g_mutex_unlock (&object->sync_job_mutex);

  return rval;
}

gboolean
storaged_linux_mdraid_object_has_sync_job (StoragedLinuxMDRaidObject *object)
{
  gboolean rval = FALSE;

  g_return_val_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object), FALSE);

  g_mutex_lock (&object->sync_job_mutex);
  rval = object->sync_job != NULL;
  g_mutex_unlock (&object->sync_job_mutex);

  return rval;
}

/**
 * storaged_linux_mdraid_object_get_uuid:
 * @object: A #StoragedLinuxMDRaidObject.
 *
 * Gets the UUID for @object.
 *
 * Returns: (transfer none): The UUID for object. Do not free, the string belongs to @object.
 */
const gchar *
storaged_linux_mdraid_object_get_uuid (StoragedLinuxMDRaidObject *object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_MDRAID_OBJECT (object), NULL);
  return object->uuid;
}


