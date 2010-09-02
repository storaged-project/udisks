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

#include "linuxblock.h"

#include <gudev/gudev.h>

typedef struct
{
  GUdevClient *gudev_client;
} LinuxBlockProvider;

static LinuxBlockProvider *_g_provider = NULL;

/* ---------------------------------------------------------------------------------------------------- */

static void
on_uevent (GUdevClient  *client,
           const gchar  *action,
           GUdevDevice  *device,
           gpointer      user_data)
{
  //LinuxBlockProvider *provider = user_data;

  g_print ("%s:%s: entering\n", G_STRLOC, G_STRFUNC);
}

/* ---------------------------------------------------------------------------------------------------- */

/* called when the system bus connection has been acquired but before the well-known
 * org.freedesktop.UDisks name is claimed
 */
void
linux_block_init (GDBusObjectManager *manager)
{
  const gchar *subsystems[] = {"block", NULL};
  LinuxBlockProvider *provider;

  g_print ("%s:%s: entering\n", G_STRLOC, G_STRFUNC);

  _g_provider = provider = g_new0 (LinuxBlockProvider, 1);

  /* get ourselves an udev client */
  provider->gudev_client = g_udev_client_new (subsystems);
  g_signal_connect (provider->gudev_client,
                    "uevent",
                    G_CALLBACK (on_uevent),
                    provider);
}

/* called on shutdown */
void
linux_block_shutdown (void)
{
  LinuxBlockProvider *provider = _g_provider;

  g_print ("%s:%s: entering\n", G_STRLOC, G_STRFUNC);

  g_object_unref (provider->gudev_client);
  g_free (provider);
}

