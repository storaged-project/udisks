/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014 Tomas Bzatek <tbzatek@redhat.com>
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

#include <src/storagedlogging.h>
#include <src/storageddaemon.h>
#include <src/storageddaemonutil.h>
#include <src/storagedlinuxprovider.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedmodulemanager.h>

#include "dummyloopobject.h"
#include "dummylinuxloop.h"
#include "dummy-generated.h"

#include <modules/storagedmoduleobject.h>


/**
 * SECTION:dummyloopobject
 * @title: DummyLoopObject
 * @short_description: Object representing loop devices on Linux
 *
 * Object corresponding to a loop block devices manager on Linux.
 */

typedef struct _DummyLoopObjectClass   DummyLoopObjectClass;

/**
 * DummyLoopObject:
 *
 * The #DummyLoopObject structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _DummyLoopObject
{
  StoragedObjectSkeleton parent_instance;

  StoragedDaemon *daemon;

  /* list of StoragedLinuxDevice objects for block objects */
  GList *devices;

  /* interfaces */
  DummyDummyLoop *iface_loop;
};

struct _DummyLoopObjectClass
{
  StoragedObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_DEVICE
};

static gboolean
dummy_loop_object_process_uevent (StoragedModuleObject  *object,
                                  const gchar           *action,
                                  StoragedLinuxDevice   *device);


static void dummy_loop_object_iface_init (StoragedModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (DummyLoopObject, dummy_loop_object, STORAGED_TYPE_OBJECT_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MODULE_OBJECT, dummy_loop_object_iface_init));

static void
dummy_loop_object_finalize (GObject *_object)
{
  DummyLoopObject *object = DUMMY_LOOP_OBJECT (_object);

  /* note: we don't hold a ref to object->daemon or object->mount_monitor */
  g_list_foreach (object->devices, (GFunc) g_object_unref, NULL);
  g_list_free (object->devices);

  if (object->iface_loop != NULL)
    g_object_unref (object->iface_loop);

  if (G_OBJECT_CLASS (dummy_loop_object_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (dummy_loop_object_parent_class)->finalize (_object);
}

static void
dummy_loop_object_get_property (GObject    *__object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  DummyLoopObject *object = DUMMY_LOOP_OBJECT (__object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, dummy_loop_object_get_daemon (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
dummy_loop_object_set_property (GObject      *__object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  DummyLoopObject *object = DUMMY_LOOP_OBJECT (__object);

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
dummy_loop_object_init (DummyLoopObject *object)
{
}

static void
dummy_loop_object_constructed (GObject *_object)
{
  DummyLoopObject *object = DUMMY_LOOP_OBJECT (_object);

  /* initial coldplug */
  dummy_loop_object_process_uevent (STORAGED_MODULE_OBJECT (_object), "add", object->devices->data);

  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (object), "/org/storaged/Storaged/dummy/loops");

  if (G_OBJECT_CLASS (dummy_loop_object_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (dummy_loop_object_parent_class)->constructed (_object);
}

static void
dummy_loop_object_class_init (DummyLoopObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = dummy_loop_object_finalize;
  gobject_class->constructed  = dummy_loop_object_constructed;
  gobject_class->set_property = dummy_loop_object_set_property;
  gobject_class->get_property = dummy_loop_object_get_property;

  /**
   * DummyLoopObject:daemon:
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
   * DummyLoopObject:device:
   *
   * The #StoragedLinuxDevice for the object. Connect to the #GObject::notify
   * signal to get notified whenever this is updated.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DEVICE,
                                   g_param_spec_object ("device",
                                                        "Device",
                                                        "The device for the object",
                                                        STORAGED_TYPE_LINUX_DEVICE,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

}

static gboolean
dummy_loop_object_should_include_device (StoragedLinuxDevice *device)
{
  const gchar *device_name;

  device_name = g_udev_device_get_name (device->udev_device);
  return g_str_has_prefix (device_name, "loop");
}

/**
 * dummy_loop_object_new:
 * @daemon: A #StoragedDaemon.
 * @device: The #StoragedLinuxDevice for the sysfs block device.
 *
 * Create a new loop object.
 *
 * Returns: A #DummyLoopObject object or %NULL if @device does not represent a loop block device. Free with g_object_unref().
 */
DummyLoopObject *
dummy_loop_object_new (StoragedDaemon      *daemon,
                       StoragedLinuxDevice *device)
{
  GObject *object;

  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (STORAGED_IS_LINUX_DEVICE (device), NULL);

  if (! dummy_loop_object_should_include_device (device))
    return NULL;

  object = g_object_new (DUMMY_TYPE_LOOP_OBJECT,
                         "daemon", daemon,
                         "device", device,
                         NULL);

  if (object != NULL)
    return DUMMY_LOOP_OBJECT (object);
  else
    return NULL;
}

/**
 * dummy_loop_object_get_daemon:
 * @object: A #DummyLoopObject.
 *
 * Gets the daemon used by @object.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @object.
 */
StoragedDaemon *
dummy_loop_object_get_daemon (DummyLoopObject *object)
{
  g_return_val_if_fail (DUMMY_IS_LOOP_OBJECT (object), NULL);
  return object->daemon;
}

/**
 * dummy_loop_object_get_devices:
 * @object: A #DummyLoopObject.
 *
 * Gets the current #StoragedLinuxDevice objects associated with @object.
 *
 * Returns: A list of #StoragedLinuxDevice objects. Free each element with
 * g_object_unref(), then free the list with g_list_free().
 */
GList *
dummy_loop_object_get_devices (DummyLoopObject *object)
{
  GList *ret;
  g_return_val_if_fail (DUMMY_IS_LOOP_OBJECT (object), NULL);
  ret = g_list_copy (object->devices);
  g_list_foreach (ret, (GFunc) g_object_ref, NULL);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
update_iface (StoragedObject                     *object,
              const gchar                        *uevent_action,
              StoragedObjectHasInterfaceFunc      has_func,
              StoragedObjectConnectInterfaceFunc  connect_func,
              StoragedObjectUpdateInterfaceFunc   update_func,
              GType                               skeleton_type,
              gpointer                            _interface_pointer)
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
linux_loop_check (StoragedObject *object)
{
  return TRUE;
}

static void
linux_loop_connect (StoragedObject *object)
{
}

static gboolean
linux_loop_update (StoragedObject   *object,
                   const gchar      *uevent_action,
                   GDBusInterface   *_iface)
{
  DummyLoopObject *loop_object = DUMMY_LOOP_OBJECT (object);

  return dummy_linux_loop_update (DUMMY_LINUX_LOOP (loop_object->iface_loop), loop_object);
}

/* ---------------------------------------------------------------------------------------------------- */

static GList *
find_link_for_sysfs_path (DummyLoopObject *object,
                          const gchar     *sysfs_path)
{
  GList *l;
  GList *ret;
  ret = NULL;
  for (l = object->devices; l != NULL; l = l->next)
    {
      StoragedLinuxDevice *device = l->data;
      if (g_strcmp0 (g_udev_device_get_sysfs_path (device->udev_device), sysfs_path) == 0)
        {
          ret = l;
          goto out;
        }
    }
 out:
  return ret;
}

static gboolean
dummy_loop_object_process_uevent (StoragedModuleObject  *module_object,
                                  const gchar           *action,
                                  StoragedLinuxDevice   *device)
{
  DummyLoopObject *object;
  GList *link;

  g_return_val_if_fail (DUMMY_IS_LOOP_OBJECT (module_object), FALSE);
  g_return_val_if_fail (device == NULL || STORAGED_IS_LINUX_DEVICE (device), FALSE);

  if (! dummy_loop_object_should_include_device (device))
    return FALSE;

  object = DUMMY_LOOP_OBJECT (module_object);

  link = NULL;
  if (device != NULL)
    link = find_link_for_sysfs_path (object, g_udev_device_get_sysfs_path (device->udev_device));
  if (g_strcmp0 (action, "remove") == 0)
    {
      if (link != NULL)
        {
          g_object_unref (STORAGED_LINUX_DEVICE (link->data));
          object->devices = g_list_delete_link (object->devices, link);
        }
      else
        {
          storaged_warning ("Object doesn't have device with sysfs path %s on remove event",
                            g_udev_device_get_sysfs_path (device->udev_device));
        }
    }
  else
    {
      if (link != NULL)
        {
          g_object_unref (STORAGED_LINUX_DEVICE (link->data));
          link->data = g_object_ref (device);
        }
      else
        {
          if (device != NULL)
            {
              object->devices = g_list_append (object->devices, g_object_ref (device));
              g_object_notify (G_OBJECT (object), "device");
            }
        }
    }

  update_iface (STORAGED_OBJECT (object), action, linux_loop_check, linux_loop_connect, linux_loop_update,
                DUMMY_TYPE_LINUX_LOOP, &object->iface_loop);

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
dummy_loop_object_housekeeping (StoragedModuleObject  *object,
                                guint                  secs_since_last,
                                GCancellable          *cancellable,
                                GError               **error)
{
  GList *l;

  for (l = DUMMY_LOOP_OBJECT (object)->devices; l; l = l->next)
    {
      StoragedLinuxDevice *device = l->data;

      storaged_info ("Housekeeping on dummy loop object %s: processing device %s...",
                   g_dbus_object_get_object_path (G_DBUS_OBJECT (object)),
                   g_udev_device_get_name (device->udev_device));
      sleep (1);
    }

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
dummy_loop_object_iface_init (StoragedModuleObjectIface *iface)
{
  iface->process_uevent = dummy_loop_object_process_uevent;
  iface->housekeeping = dummy_loop_object_housekeeping;
}
