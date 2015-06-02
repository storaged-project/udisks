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
#include <src/storageddaemon.h>
#include <src/storagedlogging.h>
#include <src/storagedlinuxdevice.h>
#include <src/storagedmodulemanager.h>

#include <modules/storagedmoduleobject.h>

#include "storaged-iscsi-generated.h"
#include "storagediscsistate.h"
#include "storagedlinuxiscsisession.h"
#include "storagedlinuxiscsisessionobject.h"

/**
 * SECTION:storagedlinuxiscsisessionobject
 * @title: StoragedLinuxISCSISessionObject
 * @short_description: Object representing iSCSI sessions on Linuxu.
 *
 * Object corresponding to the iSCSI session on Linux.
 */

/**
 * StoragedLinuxISCSISessionObject:
 *
 * The #StoragedLinuxISCSISessionObject structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _StoragedLinuxISCSISessionObject {
  StoragedObjectSkeleton parent_instance;

  StoragedDaemon *daemon;
  gchar          *session_id;

  /* Interface(s) */
  StoragedLinuxISCSISession *iface_iscsi_session;
};

struct _StoragedLinuxISCSISessionObjectClass {
  StoragedObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_SESSION_ID,
  N_PROPERTIES
};

static const gchar *iscsi_session_object_path_prefix = "/org/storaged/Storaged/iscsi/";

static void storaged_linux_iscsi_session_object_update_iface (StoragedLinuxISCSISessionObject *session_object);
static void storaged_linux_iscsi_session_object_iface_init (StoragedModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (StoragedLinuxISCSISessionObject, storaged_linux_iscsi_session_object,
                         STORAGED_TYPE_OBJECT_SKELETON,
                         G_IMPLEMENT_INTERFACE (STORAGED_TYPE_MODULE_OBJECT,
                                                storaged_linux_iscsi_session_object_iface_init));

static void
storaged_linux_iscsi_session_object_get_property (GObject *object, guint property_id,
                                                  GValue *value, GParamSpec *pspec)
{
  StoragedLinuxISCSISessionObject *session_object = STORAGED_LINUX_ISCSI_SESSION_OBJECT (object);

  switch (property_id)
    {
      case PROP_DAEMON:
        g_value_set_object (value, storaged_linux_iscsi_session_object_get_daemon (session_object));
        break;

      case PROP_SESSION_ID:
        g_value_set_string (value, storaged_linux_iscsi_session_object_get_session_id (session_object));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
storaged_linux_iscsi_session_object_set_property (GObject *object, guint property_id,
                                                  const GValue *value, GParamSpec *pspec)
{
  StoragedLinuxISCSISessionObject *session_object = STORAGED_LINUX_ISCSI_SESSION_OBJECT (object);

  switch (property_id)
    {
      case PROP_DAEMON:
        g_assert (session_object->daemon == NULL);
        /* We don't take a reference to daemon. */
        session_object->daemon = g_value_get_object (value);
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
storaged_linux_iscsi_session_object_constructed (GObject *object)
{
  StoragedLinuxISCSISessionObject *session_object = STORAGED_LINUX_ISCSI_SESSION_OBJECT (object);

  /* Set the object path. */
  gchar *object_path = storaged_linux_iscsi_session_object_get_object_path (session_object);
  g_dbus_object_skeleton_set_object_path (G_DBUS_OBJECT_SKELETON (session_object), object_path);
  g_free (object_path);

  /* Create the D-Bus interface. */
  session_object->iface_iscsi_session = storaged_linux_iscsi_session_new ();
  g_dbus_object_skeleton_add_interface (G_DBUS_OBJECT_SKELETON (session_object),
                                        G_DBUS_INTERFACE_SKELETON (session_object->iface_iscsi_session));

  /* Update the interface. */
  storaged_linux_iscsi_session_object_update_iface (session_object);

  if (G_OBJECT_CLASS (storaged_linux_iscsi_session_object_parent_class)->constructed)
    G_OBJECT_CLASS (storaged_linux_iscsi_session_object_parent_class)->constructed (object);
}

static void
storaged_linux_iscsi_session_object_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (storaged_linux_iscsi_session_object_parent_class)->dispose)
    G_OBJECT_CLASS (storaged_linux_iscsi_session_object_parent_class)->dispose (object);
}

static void
storaged_linux_iscsi_session_object_finalize (GObject *object)
{
  StoragedLinuxISCSISessionObject *session_object = STORAGED_LINUX_ISCSI_SESSION_OBJECT (object);

  g_free (session_object->session_id);

  if (G_OBJECT_CLASS (storaged_linux_iscsi_session_object_parent_class)->finalize)
    G_OBJECT_CLASS (storaged_linux_iscsi_session_object_parent_class)->finalize (object);
}

static void
storaged_linux_iscsi_session_object_class_init (StoragedLinuxISCSISessionObjectClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = storaged_linux_iscsi_session_object_get_property;
  gobject_class->set_property = storaged_linux_iscsi_session_object_set_property;
  gobject_class->constructed = storaged_linux_iscsi_session_object_constructed;
  gobject_class->dispose = storaged_linux_iscsi_session_object_dispose;
  gobject_class->finalize = storaged_linux_iscsi_session_object_finalize;

  /**
   * StoragedLinuxISCSISessionObject:daemon:
   *
   * The #StoragedDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        STORAGED_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * StoragedLinuxISCSISessionObject:session_id:
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
storaged_linux_iscsi_session_object_init (StoragedLinuxISCSISessionObject *session_object)
{
  g_return_if_fail (STORAGED_IS_LINUX_ISCSI_SESSION_OBJECT (session_object));

  session_object->daemon = NULL;
}

/**
 * storaged_linux_iscsi_session_object_new:
 * @daemon: A #StoragedDaemon.
 * @session_id: A iSCSI session identifier.
 *
 * Create a new iSCSI session object.
 *
 * Returns: A #StoragedLinuxISCSISessionObject object. Free with g_object_unref().
 */
StoragedLinuxISCSISessionObject *
storaged_linux_iscsi_session_object_new (StoragedDaemon *daemon,
                                         const gchar    *session_id)
{
  g_return_val_if_fail (STORAGED_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (session_id, NULL);

  return g_object_new (STORAGED_TYPE_LINUX_ISCSI_SESSION_OBJECT,
                       "daemon", daemon,
                       "session-id", session_id,
                       NULL);
}

/**
 * storaged_linux_iscsi_session_object_get_daemon:
 * @session_object: A #StoragedLinuxISCSISessionObject.
 *
 * Returns: A #StoragedDaemon. Do not free, the object is owned by @session_object.
 */
StoragedDaemon *
storaged_linux_iscsi_session_object_get_daemon (StoragedLinuxISCSISessionObject *session_object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_ISCSI_SESSION_OBJECT (session_object), NULL);
  return session_object->daemon;
}

/**
 * storaged_linux_iscsi_session_object_get_session_id:
 * @session_object: A #StoragedLinuxISCSISessionObject.
 *
 * Returns: The iSCSI session id.
 */
const gchar *
storaged_linux_iscsi_session_object_get_session_id (StoragedLinuxISCSISessionObject *session_object)
{
  g_return_val_if_fail (STORAGED_IS_LINUX_ISCSI_SESSION_OBJECT (session_object), NULL);
  return session_object->session_id;
}

/**
 * storaged_linux_iscsi_session_object_make_object_path:
 * @session_id: A string with iSCSI session id.
 *
 * Returns: DBus object path for the given session identifier.
 */
gchar *
storaged_linux_iscsi_session_object_make_object_path (const gchar *session_id)
{
  GString *object_path;

  g_return_val_if_fail (session_id, NULL);

  object_path = g_string_new (iscsi_session_object_path_prefix);
  object_path = g_string_append (object_path, session_id);

  return g_string_free (object_path, FALSE);
}

/**
 * storaged_linux_iscsi_session_object_get_session_id_from_sysfs_path:
 * @sysfs_path: Path to sysfs.
 *
 * Returns: String with session identifier.
 */
gchar *
storaged_linux_iscsi_session_object_get_session_id_from_sysfs_path (const gchar *sysfs_path)
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
 * storaged_linux_iscsi_session_object_get_object_path:
 * @session_object: A #StoragedLinuxISCSISessionObject.
 *
 * Returns: The object path of DBus object.
 */
gchar *
storaged_linux_iscsi_session_object_get_object_path (StoragedLinuxISCSISessionObject *session_object)
{
  const gchar *session_id;

  g_return_val_if_fail (STORAGED_IS_LINUX_ISCSI_SESSION_OBJECT (session_object), NULL);

  session_id = storaged_linux_iscsi_session_object_get_session_id (session_object);

  return storaged_linux_iscsi_session_object_make_object_path (session_id);
}

static void
storaged_linux_iscsi_session_object_update_iface (StoragedLinuxISCSISessionObject *session_object)
{
  StoragedModuleManager *module_manager;
  StoragedISCSISession *iface;
  struct libiscsi_context *ctx;
  struct libiscsi_session_info session_info;

  g_return_if_fail (STORAGED_IS_LINUX_ISCSI_SESSION_OBJECT (session_object));

  module_manager = storaged_daemon_get_module_manager (session_object->daemon);
  ctx = storaged_module_manager_get_module_state_pointer (module_manager, ISCSI_MODULE_NAME);

  /* Get session info */
  if (libiscsi_get_session_info_by_id (ctx, &session_info, session_object->session_id) != 0)
    {
      storaged_error ("Can not retrieve session information for %s", session_object->session_id);
      return;
    }

  /* Set properties */
  iface = STORAGED_ISCSI_SESSION (session_object->iface_iscsi_session);
  storaged_iscsi_session_set_target_name (iface, session_info.targetname);
  storaged_iscsi_session_set_tpgt (iface, session_info.tpgt);
  storaged_iscsi_session_set_address (iface, session_info.address);
  storaged_iscsi_session_set_port (iface, session_info.port);
  storaged_iscsi_session_set_persistent_address (iface, session_info.persistent_address);
  storaged_iscsi_session_set_persistent_port (iface, session_info.persistent_port);
  storaged_iscsi_session_set_abort_timeout (iface, session_info.tmo.abort_tmo);
  storaged_iscsi_session_set_lu_reset_timeout (iface, session_info.tmo.lu_reset_tmo);
  storaged_iscsi_session_set_recovery_timeout (iface, session_info.tmo.recovery_tmo);
  storaged_iscsi_session_set_tgt_reset_timeout (iface, session_info.tmo.tgt_reset_tmo);
}

static gboolean
storaged_linux_iscsi_session_object_process_uevent (StoragedModuleObject  *module_object,
                                                    const gchar           *action,
                                                    StoragedLinuxDevice   *device)
{
  StoragedLinuxISCSISessionObject *session_object;
  gchar *session_id;
  const gchar *sysfs_path;

  g_return_val_if_fail (STORAGED_IS_LINUX_ISCSI_SESSION_OBJECT (module_object), FALSE);
  g_return_val_if_fail (device == NULL || STORAGED_IS_LINUX_DEVICE (device), FALSE);

  session_object = STORAGED_LINUX_ISCSI_SESSION_OBJECT (module_object);
  sysfs_path = g_udev_device_get_sysfs_path (device->udev_device);
  session_id = storaged_linux_iscsi_session_object_get_session_id_from_sysfs_path (sysfs_path);

  /* Did we get uevent for this session? */
  if (session_id && g_strcmp0 (session_id, session_object->session_id) == 0)
    {
      if (g_strcmp0 (action, "remove") == 0)
        {
          /* Returning FALSE means that the device is removed. */
          return FALSE;
        }
      else
        {
          /* Returning TRUE means that the device is being kept claimed. */
          return TRUE;
        }
    }

  g_free (session_id);

  return FALSE;
}

static gboolean
storaged_linux_iscsi_session_object_housekeeping (StoragedModuleObject  *object,
                                                  guint                  secs_since_last,
                                                  GCancellable          *cancellable,
                                                  GError               **error)
{
  /* No housekeeping needed so far.  Return TRUE to indicate, we finished
   * successfully. */
  return TRUE;
}

/* -------------------------------------------------------------------------- */

void storaged_linux_iscsi_session_object_iface_init (StoragedModuleObjectIface *iface)
{
  iface->process_uevent = storaged_linux_iscsi_session_object_process_uevent;
  iface->housekeeping = storaged_linux_iscsi_session_object_housekeeping;
}
