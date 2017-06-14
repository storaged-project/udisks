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

#include <modules/udisksmoduleobject.h>

#include "udisks-iscsi-generated.h"
#include "udisksiscsistate.h"
#include "udiskslinuxiscsisession.h"
#include "udiskslinuxiscsisessionobject.h"

/**
 * SECTION:udiskslinuxiscsisessionobject
 * @title: UDisksLinuxISCSISessionObject
 * @short_description: Object representing iSCSI sessions on Linuxu.
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

  UDisksDaemon     *daemon;
  UDisksISCSIState *state;
  gchar            *session_id;

  /* Interface(s) */
  UDisksLinuxISCSISession *iface_iscsi_session;
};

struct _UDisksLinuxISCSISessionObjectClass {
  UDisksObjectSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_DAEMON,
  PROP_SESSION_ID,
  N_PROPERTIES
};

static const gchar *iscsi_session_object_path_prefix = "/org/freedesktop/UDisks2/iscsi/";

static void udisks_linux_iscsi_session_object_update_iface (UDisksLinuxISCSISessionObject *session_object);
static void udisks_linux_iscsi_session_object_iface_init (UDisksModuleObjectIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksLinuxISCSISessionObject, udisks_linux_iscsi_session_object,
                         UDISKS_TYPE_OBJECT_SKELETON,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_MODULE_OBJECT,
                                                udisks_linux_iscsi_session_object_iface_init));

static void
udisks_linux_iscsi_session_object_get_property (GObject *object, guint property_id,
                                                GValue *value, GParamSpec *pspec)
{
  UDisksLinuxISCSISessionObject *session_object = UDISKS_LINUX_ISCSI_SESSION_OBJECT (object);

  switch (property_id)
    {
      case PROP_DAEMON:
        g_value_set_object (value, udisks_linux_iscsi_session_object_get_daemon (session_object));
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
udisks_linux_iscsi_session_object_constructed (GObject *object)
{
  UDisksLinuxISCSISessionObject *session_object = UDISKS_LINUX_ISCSI_SESSION_OBJECT (object);
  UDisksModuleManager *module_manager;
  gchar *object_path;

  /* Store state pointer for later usage; libiscsi_context. */
  module_manager = udisks_daemon_get_module_manager (session_object->daemon);
  session_object->state = udisks_module_manager_get_module_state_pointer (module_manager,
                                                                          ISCSI_MODULE_NAME);

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
udisks_linux_iscsi_session_object_dispose (GObject *object)
{
  if (G_OBJECT_CLASS (udisks_linux_iscsi_session_object_parent_class)->dispose)
    G_OBJECT_CLASS (udisks_linux_iscsi_session_object_parent_class)->dispose (object);
}

static void
udisks_linux_iscsi_session_object_finalize (GObject *object)
{
  UDisksLinuxISCSISessionObject *session_object = UDISKS_LINUX_ISCSI_SESSION_OBJECT (object);

  g_free (session_object->session_id);

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
  gobject_class->dispose = udisks_linux_iscsi_session_object_dispose;
  gobject_class->finalize = udisks_linux_iscsi_session_object_finalize;

  /**
   * UDisksLinuxISCSISessionObject:daemon:
   *
   * The #UDisksDaemon for the object.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon for the object",
                                                        UDISKS_TYPE_DAEMON,
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

  session_object->daemon = NULL;
}

/**
 * udisks_linux_iscsi_session_object_new:
 * @daemon: A #UDisksDaemon.
 * @session_id: A iSCSI session identifier.
 *
 * Create a new iSCSI session object.
 *
 * Returns: A #UDisksLinuxISCSISessionObject object. Free with g_object_unref().
 */
UDisksLinuxISCSISessionObject *
udisks_linux_iscsi_session_object_new (UDisksDaemon *daemon,
                                       const gchar  *session_id)
{
  g_return_val_if_fail (UDISKS_IS_DAEMON (daemon), NULL);
  g_return_val_if_fail (session_id, NULL);

  return g_object_new (UDISKS_TYPE_LINUX_ISCSI_SESSION_OBJECT,
                       "daemon", daemon,
                       "session-id", session_id,
                       NULL);
}

/**
 * udisks_linux_iscsi_session_object_get_daemon:
 * @session_object: A #UDisksLinuxISCSISessionObject.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @session_object.
 */
UDisksDaemon *
udisks_linux_iscsi_session_object_get_daemon (UDisksLinuxISCSISessionObject *session_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT (session_object), NULL);
  return session_object->daemon;
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
 * udisks_linux_iscsi_session_object_get_state:
 * @session_object: A #UDisksLinuxISCSISessionObject.
 *
 * Returns: A #UDisksISCSIState. Do not free, the structure is owned by
 * #UDisksModuleManager.
 */
UDisksISCSIState *
udisks_linux_iscsi_session_object_get_state (UDisksLinuxISCSISessionObject *session_object)
{
  g_return_val_if_fail (UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT (session_object), NULL);
  return session_object->state;
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

  object_path = g_string_new (iscsi_session_object_path_prefix);
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
  UDisksISCSIState *state;
  struct libiscsi_context *ctx;
  struct libiscsi_session_info session_info;

  g_return_if_fail (UDISKS_IS_LINUX_ISCSI_SESSION_OBJECT (session_object));

  state = udisks_linux_iscsi_session_object_get_state (session_object);
  ctx = udisks_iscsi_state_get_libiscsi_context (state);

  /* Enter a critical section. */
  udisks_iscsi_state_lock_libiscsi_context (state);

  /* Get session info */
  if (libiscsi_get_session_info_by_id (ctx, &session_info, session_object->session_id) != 0)
    {
      udisks_critical ("Can not retrieve session information for %s", session_object->session_id);
      return;
    }

  /* Leave the critical section. */
  udisks_iscsi_state_unlock_libiscsi_context (state);

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
}

static gboolean
udisks_linux_iscsi_session_object_process_uevent (UDisksModuleObject *module_object,
                                                  const gchar        *action,
                                                  UDisksLinuxDevice  *device)
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
udisks_linux_iscsi_session_object_housekeeping (UDisksModuleObject  *object,
                                                  guint              secs_since_last,
                                                  GCancellable      *cancellable,
                                                  GError           **error)
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
