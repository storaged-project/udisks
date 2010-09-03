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

#include "udisksjobimpl.h"

typedef struct _UDisksJobImplClass   UDisksJobImplClass;

struct _UDisksJobImpl
{
  UDisksJobStub parent_instance;
};

struct _UDisksJobImplClass
{
  UDisksJobStubClass parent_class;
};

static void job_iface_init (UDisksJobIface *iface);

G_DEFINE_TYPE_WITH_CODE (UDisksJobImpl, udisks_job_impl, UDISKS_TYPE_JOB_STUB,
                         G_IMPLEMENT_INTERFACE (UDISKS_TYPE_JOB, job_iface_init));

static void
udisks_job_impl_init (UDisksJobImpl *job)
{
}

static void
udisks_job_impl_class_init (UDisksJobImplClass *klass)
{
}

UDisksJob *
udisks_job_impl_new (void)
{
  return UDISKS_JOB (g_object_new (UDISKS_TYPE_JOB_IMPL, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
handle_cancel (UDisksJob              *object,
               GDBusMethodInvocation  *invocation,
               const gchar* const     *options)
{
  //UDisksJobImpl *job = UDISKS_JOB_IMPL (object);
  g_dbus_method_invocation_return_dbus_error (invocation, "org.foo.error.job.cancel", "no, not yet implemented");
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
job_iface_init (UDisksJobIface *iface)
{
  iface->handle_cancel   = handle_cancel;
}

/* ---------------------------------------------------------------------------------------------------- */
