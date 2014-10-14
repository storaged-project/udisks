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

#include <sys/types.h>

#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <src/udiskslogging.h>
#include <src/udiskslinuxdriveobject.h>
#include <src/udiskslinuxblockobject.h>
#include <src/udisksdaemon.h>
#include <src/udisksdaemonutil.h>
#include <src/udisksbasejob.h>
#include <src/udiskssimplejob.h>
#include <src/udisksthreadedjob.h>
#include <src/udiskslinuxdevice.h>

#include "dummytypes.h"
#include "dummylinuxdrive.h"
#include "dummy-generated.h"

/**
 * SECTION:udiskslinuxdriveata
 * @title: DummyLinuxDrive
 * @short_description: Linux implementation of #DummyDriveDummy
 *
 * This type provides an implementation of the #DummyDriveDummy
 * interface on Linux.
 */

typedef struct _DummyLinuxDriveClass   DummyLinuxDriveClass;

/**
 * DummyLinuxDrive:
 *
 * The #DummyLinuxDrive structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _DummyLinuxDrive
{
  DummyDriveDummySkeleton parent_instance;

  UDisksThreadedJob *selftest_job;
};

struct _DummyLinuxDriveClass
{
  DummyDriveDummySkeletonClass parent_class;
};

static void dummy_linux_drive_iface_init (DummyDriveDummyIface *iface);

G_DEFINE_TYPE_WITH_CODE (DummyLinuxDrive, dummy_linux_drive, DUMMY_TYPE_DRIVE_DUMMY_SKELETON,
                         G_IMPLEMENT_INTERFACE (DUMMY_TYPE_DRIVE_DUMMY, dummy_linux_drive_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
dummy_linux_drive_finalize (GObject *object)
{
  DummyLinuxDrive *drive = DUMMY_LINUX_DRIVE (object);

  if (G_OBJECT_CLASS (dummy_linux_drive_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (dummy_linux_drive_parent_class)->finalize (object);
}


static void
dummy_linux_drive_init (DummyLinuxDrive *drive)
{
#if 0
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (drive),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
#endif

  dummy_drive_dummy_set_hello (DUMMY_DRIVE_DUMMY (drive), "Hello world!");
}

static void
dummy_linux_drive_class_init (DummyLinuxDriveClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = dummy_linux_drive_finalize;
}

/**
 * dummy_linux_drive_new:
 *
 * Creates a new #DummyLinuxDrive instance.
 *
 * Returns: A new #DummyLinuxDrive. Free with g_object_unref().
 */
DummyDriveDummy *
dummy_linux_drive_new (void)
{
  return DUMMY_DRIVE_DUMMY (g_object_new (DUMMY_TYPE_LINUX_DRIVE,
                                         NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * dummy_linux_drive_update:
 * @drive: A #DummyLinuxDrive.
 * @object: The enclosing #UDisksLinuxDriveObject instance.
 *
 * Updates the interface.
 *
 * Returns: %TRUE if configuration has changed, %FALSE otherwise.
 */
gboolean
dummy_linux_drive_update (DummyLinuxDrive        *drive,
                          UDisksLinuxDriveObject *object)
{
  UDisksLinuxDevice *device;

  device = udisks_linux_drive_object_get_device (object, TRUE /* get_hw */);
  if (device == NULL)
    goto out;

  /* do something */

 out:
  if (device != NULL)
    g_object_unref (device);

  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

#define HELLO_TIMEOUT  2  /* sec. */

static gboolean
say_hello_timeout (gpointer user_data)
{
  UDisksJob *job = UDISKS_JOB (user_data);
  DummyDriveDummy *_drive;
  GDBusMethodInvocation *invocation;

  _drive = g_object_get_data (G_OBJECT (job), "src-object");
  invocation = g_object_get_data (G_OBJECT (job), "src-invocation");

  udisks_simple_job_complete (UDISKS_SIMPLE_JOB (job), TRUE, "");
  dummy_drive_dummy_set_hello (_drive, "Already said \"Hello world\" to you!");
  dummy_drive_dummy_complete_say_hello (_drive, invocation, "Successfully said \"Hello world\" to you!");

  /*  Emit the signal  */
  /*  HINT: monitor e.g. with `gdbus monitor -y -d org.freedesktop.UDisks2 -o /org/freedesktop/UDisks2/drives/xxxx`  */
  dummy_drive_dummy_emit_hello_said (_drive, TRUE, "Signalling successful \"Hello world\" message.");

  return FALSE;
}

static gboolean
handle_say_hello (DummyDriveDummy *_drive,
                  GDBusMethodInvocation *invocation)
{
  UDisksDaemon *daemon;
  UDisksBaseJob *job = NULL;
  UDisksLinuxDriveObject *object = NULL;
  GError *error = NULL;
  uid_t caller_uid;

  dummy_drive_dummy_set_hello (_drive, "Slowly saying \"Hello world\" to you!");

  object = udisks_daemon_util_dup_object (_drive, &error);
  if (object == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  daemon = udisks_linux_drive_object_get_daemon (object);
  if (!udisks_daemon_util_get_caller_uid_sync (daemon,
                                               invocation,
                                               NULL /* GCancellable */,
                                               &caller_uid,
                                               NULL,
                                               NULL,
                                               &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
      goto out;
    }

  job = udisks_daemon_launch_simple_job (daemon,
                                         UDISKS_OBJECT (object),
                                         "telling-hello",
                                         caller_uid, NULL);
  g_object_set_data_full (G_OBJECT (job),
                          "src-object",
                          g_object_ref (_drive),
                          g_object_unref);
  g_object_set_data_full (G_OBJECT (job),
                          "src-invocation",
                          g_object_ref (invocation),
                          g_object_unref);
  udisks_job_set_cancelable (UDISKS_JOB (job), FALSE);
  udisks_job_set_expected_end_time (UDISKS_JOB (job), g_get_real_time () + HELLO_TIMEOUT * G_USEC_PER_SEC);
  udisks_job_set_progress_valid (UDISKS_JOB (job), FALSE);
  g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                              HELLO_TIMEOUT,
                              say_hello_timeout,
                              g_object_ref (job),
                              g_object_unref);

 out:
  g_clear_object (&object);

  return TRUE; /* returning TRUE means that we handled the method invocation */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
dummy_linux_drive_iface_init (DummyDriveDummyIface *iface)
{
  iface->handle_say_hello = handle_say_hello;
}
