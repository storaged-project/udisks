/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2010 David Zeuthen <zeuthen@gmail.com>
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

#ifndef __UDISKS_CRYPTTAB_MONITOR_H__
#define __UDISKS_CRYPTTAB_MONITOR_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

#define UDISKS_TYPE_CRYPTTAB_MONITOR  (udisks_crypttab_monitor_get_type ())
#define UDISKS_CRYPTTAB_MONITOR(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), UDISKS_TYPE_CRYPTTAB_MONITOR, UDisksCrypttabMonitor))
#define UDISKS_IS_CRYPTTAB_MONITOR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UDISKS_TYPE_CRYPTTAB_MONITOR))

GType                   udisks_crypttab_monitor_get_type    (void) G_GNUC_CONST;
UDisksCrypttabMonitor  *udisks_crypttab_monitor_new         (void);
GList                  *udisks_crypttab_monitor_get_entries (UDisksCrypttabMonitor  *monitor);

G_END_DECLS

#endif /* __UDISKS_CRYPTTAB_MONITOR_H__ */
