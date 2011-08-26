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

#ifndef __UDISKS_PROVIDER_H__
#define __UDISKS_PROVIDER_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_PROVIDER         (udisks_provider_get_type ())
#define UDISKS_PROVIDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_PROVIDER, UDisksProvider))
#define UDISKS_PROVIDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), UDISKS_TYPE_PROVIDER, UDisksProviderClass))
#define UDISKS_PROVIDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), UDISKS_TYPE_PROVIDER, UDisksProviderClass))
#define UDISKS_IS_PROVIDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_PROVIDER))
#define UDISKS_IS_PROVIDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), UDISKS_TYPE_PROVIDER))

typedef struct _UDisksProviderClass   UDisksProviderClass;
typedef struct _UDisksProviderPrivate UDisksProviderPrivate;

/**
 * UDisksProvider:
 *
 * The #UDisksProvider structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _UDisksProvider
{
  /*< private >*/
  GObject parent_instance;
  UDisksProviderPrivate *priv;
};

/**
 * UDisksProviderClass:
 * @parent_class: The parent class.
 * @start: Virtual function for udisks_provider_start(). The default implementation does nothing.
 *
 * Class structure for #UDisksProvider.
 */
struct _UDisksProviderClass
{
  GObjectClass parent_class;

  void (*start) (UDisksProvider *provider);

  /*< private >*/
  gpointer padding[8];
};


GType           udisks_provider_get_type   (void) G_GNUC_CONST;
UDisksDaemon   *udisks_provider_get_daemon (UDisksProvider *provider);
void            udisks_provider_start      (UDisksProvider *provider);

G_END_DECLS

#endif /* __UDISKS_PROVIDER_H__ */
