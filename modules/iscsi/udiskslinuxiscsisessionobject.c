/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Peter Hatina <phatina@redhat.com>
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
 */

#include "config.h"

#include <libiscsi.h>
#include <src/udisksdaemon.h>
#include <src/udiskslogging.h>
#include <src/udiskslinuxdevice.h>
#include <src/udisksmodulemanager.h>
#include <src/udisksmoduleobject.h>

#include "udiskslinuxiscsisession.h"
#include "udiskslinuxiscsisessionobject.h"

/**
 * SECTION:udiskslinuxiscsisessionobject
 * @title: UDisksLinuxISCSISessionObject
 * @short_description: Object representing iSCSI sessions on Linux.
 *
 * Object corresponding to the iSCSI session on Linux.
 */

/**
 * UDisksLinuxISCSISessionObject:
 *
 * The #UDisksLinuxISCSISessionObject structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _UDisksLinuxISCSISessionObject {
  UDisksObjectSkeleton parent_instance;

  UDisksLinuxModuleISCSI *module;
  gchar                  *session_id;

  GHashTable             *sysfs_paths;

  /* Interface(s) */
  UDisksLinuxISCSISession *iface_iscsi_session;
};

struct _UDisksLinuxISCSISessionObjectClass {
  UDisksObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_SESSION_ID,
  N_PROPERTIES
};

#define ISCSI_SESSION_OBJECT_PATH_PREFIX "/org/freedesktop/UDisks2/iscsi/"

static void udisks_linux_iscsi_session_object_update_iface (UDisksLinuxISCSISessionObject *session_object);
static void udisks_linux_iscsi_session_object_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxISCSISessionObject, udisks_linux_iscsi_session_object, UDISKS_TYPE_OBJECT_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT, udisks_linux_iscsi_session_object_iface_init));

static void
udisks_linux_iscsi_session_object_get_property (GObject *object, guint property_id,
                                                GValue *value, GParamSpec *pspec)
{
  UDisksLinuxISCSISessionObject *session_object = UDISKS_LINUX_ISCSI_SESSION_OBJECT (object);

  switch (property_id)
    {
      case PROP_MODULE:
        g_value_set_object (value, udisks_linux_iscsi_session_object_get_module (session_object));
        break;

      case PROP_SESSION_ID:
        g_value_set_string (value, udisks_linux_iscsi_session_object_get_session_id (session_object));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
udisks_linux_iscsi_session_object_set_property (GObject *object, guint property_id,
                                                const GValue *value, GParamSpec *pspec)
{
  UDisksLinuxISCSISessionObject *session_object = UDISKS_LINUX_ISCSI_SESSION_OBJECT (object);

  switch (property_id)
    {
      case PROP_MODULE:
        g_assert (session_object->module == NULL);
        session_object->module = g_value_dup_object (value);
        break;

      case PROP_SESSION_ID:
        g_assert (session_object->session_id == NULL);
        session_object->session_id = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
udisks_linux_iscsi_session_object_constructed (GObject *object)
{
  UDisksLinuxISCSISessionObject *session_object = UDISKS_LINUX_ISCSI_SESSION_OBJECT (object);
  gchar *object_path;

  /* Set the object path. */
  object_path = udisks_linux_iscsi_session_object_get_object_path (session_object);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (session_object), object_path);
  g_free (object_path);

  /* Create the D-Bus interface. */
  session_object->iface_iscsi_session = udisks_linux_iscsi_session_new ();
  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (session_object),
                                        G_DBUS_INTERFACE_SKELETON (session_object->iface_iscsi_session));

  /* Update the interface. */
  udisks_linux_iscsi_session_object_update_iface (session_object);

  if (G_OBJECT_CLASS (udisks_linux_iscsi_session_object_parent_class)->constructed)
    G_OBJECT_CLASS (udisks_linux_iscsi_session_object_parent_class)->constructed (object);
}

static void
udisks_linux_iscsi_session_object_finalize (GObject *object)
{
  UDisksLinuxISCSISessionObject *session_object = UDISKS_LINUX_ISCSI_SESSION_OBJECT (object);

  g_clear_object (&session_object->iface_iscsi_session);

  g_free (session_object->session_id);
  g_object_unref (session_object->module);
  g_hash_table_destroy (session_object->sysfs_paths);

  if (G_OBJECT_CLASS (udisks_linux_iscsi_session_object_parent_class)->finalize)
    G_OBJECT_CLASS (udisks_linux_iscsi_session_object_parent_class)->finalize (object);
}

static void
udisks_linux_iscsi_session_object_class_init (UDisksLinuxISCSISessionObjectClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = udisks_linux_iscsi_session_object_get_property;
  gobject_class->set_property = udisks_linux_iscsi_session_object_set_property;
  gobject_class->constructed = udisks_linux_iscsi_session_object_constructed;
  gobject_class->finalize = udisks_linux_iscsi_session_object_finalize;

  /**
   * UDisksLinuxISCSISessionObject:module:
   *
   * The #UDisksDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MODULE,
                                   g_param_spec_object ("module",
                                                        "Module",
                                                        "The module for the object",
                                                        UDISKS_TYPE_LINUX_MODULE_ISCSI,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * UDisksLinuxISCSISessionObject:session_id:
   *
   * The iSCSI session id.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_SESSION_ID,
                                   g_param_spec_string ("session-id",
                                                        "Session ID",
                                                        "The iSCSI session ID",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
udisks_linux_iscsi_session_object_init (UDisksLinuxISCSISessionObject *session_object)
{
  g_return_if_fail (UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT (session_object));

  session_object->module = NULL;
  session_object->sysfs_paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * udisks_linux_iscsi_session_object_new:
 * @module: A #UDisksLinuxModuleISCSI.
 * @session_id: A iSCSI session identifier.
 *
 * Create a new iSCSI session object.
 *
 * Returns: A #UDisksLinuxISCSISessionObject object. Free with g_object_unref().
 */
UDisksLinuxISCSISessionObject *
udisks_linux_iscsi_session_object_new (UDisksLinuxModuleISCSI *module,
                                       const gchar            *session_id)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_MODULE_ISCSI (module), NULL);
  g_return_val_if_fail (session_id, NULL);

  return g_object_new (UDISKS_TYPE_LINUX_ISCSI_SESSION_OBJECT,
                       "module", module,
                       "session-id", session_id,
                       NULL);
}

/**
 * udisks_linux_iscsi_session_object_get_module:
 * @session_object: A #UDisksLinuxISCSISessionObject.
 *
 * Returns: A #UDisksLinuxModuleISCSI. Do not free, the object is owned by @session_object.
 */
UDisksLinuxModuleISCSI *
udisks_linux_iscsi_session_object_get_module (UDisksLinuxISCSISessionObject *session_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT (session_object), NULL);
  return session_object->module;
}

/**
 * udisks_linux_iscsi_session_object_get_session_id:
 * @session_object: A #UDisksLinuxISCSISessionObject.
 *
 * Returns: The iSCSI session id.
 */
const gchar *
udisks_linux_iscsi_session_object_get_session_id (UDisksLinuxISCSISessionObject *session_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT (session_object), NULL);
  return session_object->session_id;
}

/**
 * udisks_linux_iscsi_session_object_make_object_path:
 * @session_id: A string with iSCSI session id.
 *
 * Returns: DBus object path for the given session identifier.
 */
gchar *
udisks_linux_iscsi_session_object_make_object_path (const gchar *session_id)
{
  GString *object_path;

  g_return_val_if_fail (session_id, NULL);

  object_path = g_string_new (ISCSI_SESSION_OBJECT_PATH_PREFIX);
  object_path = g_string_append (object_path, session_id);

  return g_string_free (object_path, FALSE);
}

/**
 * udisks_linux_iscsi_session_object_get_session_id_from_sysfs_path:
 * @sysfs_path: Path to sysfs.
 *
 * Returns: String with session identifier.
 */
gchar *
udisks_linux_iscsi_session_object_get_session_id_from_sysfs_path (const gchar *sysfs_path)
{
  GRegex *regex;
  GMatchInfo *match_info;
  GError *error = NULL;
  gchar *rval = NULL;

  /* Search for session ID */
  regex = g_regex_new ("session[0-9]+", 0, 0, &error);
  g_regex_match (regex, sysfs_path, 0, &match_info);
  if (g_match_info_matches (match_info))
    rval = g_match_info_fetch (match_info, 0);

  g_match_info_free (match_info);
  g_regex_unref (regex);

  return rval;
}

/**
 * udisks_linux_iscsi_session_object_get_object_path:
 * @session_object: A #UDisksLinuxISCSISessionObject.
 *
 * Returns: The object path of DBus object.
 */
gchar *
udisks_linux_iscsi_session_object_get_object_path (UDisksLinuxISCSISessionObject *session_object)
{
  const gchar *session_id;

  g_return_val_if_fail (UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT (session_object), NULL);

  session_id = udisks_linux_iscsi_session_object_get_session_id (session_object);

  return udisks_linux_iscsi_session_object_make_object_path (session_id);
}

static void
udisks_linux_iscsi_session_object_update_iface (UDisksLinuxISCSISessionObject *session_object)
{
  UDisksISCSISession *iface;
  struct libiscsi_context *ctx;
  struct libiscsi_session_info session_info = {0,};

  g_return_if_fail (UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT (session_object));

  ctx = udisks_linux_module_iscsi_get_libiscsi_context (session_object->module);

  /* Enter a critical section. */
  udisks_linux_module_iscsi_lock_libiscsi_context (session_object->module);

  /* Get session info */
  if (libiscsi_get_session_info_by_id (ctx, &session_info, session_object->session_id) != 0)
    {
      udisks_linux_module_iscsi_unlock_libiscsi_context (session_object->module);
      udisks_warning ("Cannot retrieve session information for %s", session_object->session_id);
      return;
    }

  /* Leave the critical section. */
  udisks_linux_module_iscsi_unlock_libiscsi_context (session_object->module);

  /* Set properties */
  iface = UDISKS_ISCSI_SESSION (session_object->iface_iscsi_session);
  udisks_iscsi_session_set_target_name (iface, session_info.targetname);
  udisks_iscsi_session_set_tpgt (iface, session_info.tpgt);
  udisks_iscsi_session_set_address (iface, session_info.address);
  udisks_iscsi_session_set_port (iface, session_info.port);
  udisks_iscsi_session_set_persistent_address (iface, session_info.persistent_address);
  udisks_iscsi_session_set_persistent_port (iface, session_info.persistent_port);
  udisks_iscsi_session_set_abort_timeout (iface, session_info.tmo.abort_tmo);
  udisks_iscsi_session_set_lu_reset_timeout (iface, session_info.tmo.lu_reset_tmo);
  udisks_iscsi_session_set_recovery_timeout (iface, session_info.tmo.recovery_tmo);
  udisks_iscsi_session_set_tgt_reset_timeout (iface, session_info.tmo.tgt_reset_tmo);

  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (iface));
}

gboolean
udisks_linux_iscsi_session_object_process_uevent (UDisksModuleObject *module_object,
                                                  UDisksUeventAction  action,
                                                  UDisksLinuxDevice  *device,
                                                  gboolean           *keep)
{
  UDisksLinuxISCSISessionObject *session_object;
  gchar *session_id;
  const gchar *sysfs_path;

  g_return_val_if_fail (UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT (module_object), FALSE);
  g_return_val_if_fail (device != NULL && UDISKS_IS_LINUX_DEVICE (device), FALSE);

  session_object = UDISKS_LINUX_ISCSI_SESSION_OBJECT (module_object);
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);
  session_id = udisks_linux_iscsi_session_object_get_session_id_from_sysfs_path (sysfs_path);

  /* Did we get uevent for this session? */
  if (session_id && g_strcmp0 (session_id, session_object->session_id) == 0)
    {
      g_free (session_id);
      if (action == UDISKS_UEVENT_ACTION_REMOVE)
        {
          g_warn_if_fail (g_hash_table_remove (session_object->sysfs_paths, sysfs_path));
          *keep = g_hash_table_size (session_object->sysfs_paths) > 0;
          return TRUE;
        }
      else
        {
          *keep = TRUE;
          g_hash_table_add (session_object->sysfs_paths, g_strdup (sysfs_path));
          return TRUE;
        }
    }

  g_free (session_id);

  return FALSE;
}

static gboolean
udisks_linux_iscsi_session_object_housekeeping (UDisksModuleObject  *object,
                                                guint                secs_since_last,
                                                GCancellable        *cancellable,
                                                GError             **error)
{
  /* No housekeeping needed so far.  Return TRUE to indicate, we finished
   * successfully. */
  return TRUE;
}

/* -------------------------------------------------------------------------- */

void udisks_linux_iscsi_session_object_iface_init (UDisksModuleObjectIface *iface)
{
  iface->process_uevent = udisks_linux_iscsi_session_object_process_uevent;
  iface->housekeeping = udisks_linux_iscsi_session_object_housekeeping;
}
