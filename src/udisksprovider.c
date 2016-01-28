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

#include <stdio.h>
#include <mntent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <gudev/gudev.h>

#include "udisksdaemon.h"
#include "udisksprovider.h"

/**
 * SECTION:udisksprovider
 * @title: UDisksProvider
 * @short_description: Abstract base class for all data providers
 *
 * Abstract base class for all data providers.
 */

struct _UDisksProviderPrivate
{
  UDisksDaemon *daemon;

  UDisksMountMonitor *mount_monitor;

  GFileMonitor *monitor;

  GUdevClient *gudev_client;

  GList *entries;
};

enum
{
  PROP_0,
  PROP_DAEMON
};

G_DEFINE_ABSTRACT_TYPE (UDisksProvider, udisks_provider, G_TYPE_OBJECT);

static void
udisks_provider_finalize (GObject *object)
{
  /* UDisksProvider *provider = UDISKS_PROVIDER (object); */

  /* note: we don't hold a ref to provider->priv->daemon */

  if (G_OBJECT_CLASS (udisks_provider_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (udisks_provider_parent_class)->finalize (object);
}

static void
udisks_provider_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  UDisksProvider *provider = UDISKS_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_value_set_object (value, udisks_provider_get_daemon (provider));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_provider_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  UDisksProvider *provider = UDISKS_PROVIDER (object);

  switch (prop_id)
    {
    case PROP_DAEMON:
      g_assert (provider->priv->daemon == NULL);
      /* we don't take a reference to the daemon */
      provider->priv->daemon = g_value_get_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
udisks_provider_init (UDisksProvider *provider)
{
  provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider, UDISKS_TYPE_PROVIDER, UDisksProviderPrivate);
}

static void
udisks_provider_start_default (UDisksProvider *provider)
{
  /* do nothing */
}

static void
udisks_provider_class_init (UDisksProviderClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = udisks_provider_finalize;
  gobject_class->set_property = udisks_provider_set_property;
  gobject_class->get_property = udisks_provider_get_property;

  klass->start = udisks_provider_start_default;

  /**
   * UDisksProvider:daemon:
   *
   * The #UDisksDaemon the provider is for.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DAEMON,
                                   g_param_spec_object ("daemon",
                                                        "Daemon",
                                                        "The daemon the provider is for",
                                                        UDISKS_TYPE_DAEMON,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (klass, sizeof (UDisksProviderPrivate));
}

/**
 * udisks_provider_get_daemon:
 * @provider: A #UDisksProvider.
 *
 * Gets the daemon used by @provider.
 *
 * Returns: A #UDisksDaemon. Do not free, the object is owned by @provider.
 */
UDisksDaemon *
udisks_provider_get_daemon (UDisksProvider *provider)
{
  g_return_val_if_fail (UDISKS_IS_PROVIDER (provider), NULL);
  return provider->priv->daemon;
}

/**
 * udisks_provider_start:
 * @provider: A #UDisksProvider.
 *
 * Starts the provider.
 */
void
udisks_provider_start  (UDisksProvider *provider)
{
  g_return_if_fail (UDISKS_IS_PROVIDER (provider));
  UDISKS_PROVIDER_GET_CLASS (provider)->start (provider);
}


/* ---------------------------------------------------------------------------------------------------- */
