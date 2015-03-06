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

#ifndef __STORAGED_PROVIDER_H__
#define __STORAGED_PROVIDER_H__

#include "storageddaemontypes.h"

G_BEGIN_DECLS

#define STORAGED_TYPE_PROVIDER         (storaged_provider_get_type ())
#define STORAGED_PROVIDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), STORAGED_TYPE_PROVIDER, StoragedProvider))
#define STORAGED_PROVIDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), STORAGED_TYPE_PROVIDER, StoragedProviderClass))
#define STORAGED_PROVIDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), STORAGED_TYPE_PROVIDER, StoragedProviderClass))
#define STORAGED_IS_PROVIDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), STORAGED_TYPE_PROVIDER))
#define STORAGED_IS_PROVIDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), STORAGED_TYPE_PROVIDER))

typedef struct _StoragedProviderClass   StoragedProviderClass;
typedef struct _StoragedProviderPrivate StoragedProviderPrivate;

/**
 * StoragedProvider:
 *
 * The #StoragedProvider structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _StoragedProvider
{
  /*< private >*/
  GObject parent_instance;
  StoragedProviderPrivate *priv;
};

/**
 * StoragedProviderClass:
 * @parent_class: The parent class.
 * @start: Virtual function for storaged_provider_start(). The default implementation does nothing.
 *
 * Class structure for #StoragedProvider.
 */
struct _StoragedProviderClass
{
  GObjectClass parent_class;

  void (*start) (StoragedProvider *provider);

  /*< private >*/
  gpointer padding[8];
};


GType             storaged_provider_get_type   (void) G_GNUC_CONST;
StoragedDaemon   *storaged_provider_get_daemon (StoragedProvider *provider);
void              storaged_provider_start      (StoragedProvider *provider);

G_END_DECLS

#endif /* __STORAGED_PROVIDER_H__ */
