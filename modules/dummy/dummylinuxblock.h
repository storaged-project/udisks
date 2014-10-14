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

#ifndef __DUMMY_LINUX_BLOCK_H__
#define __DUMMY_LINUX_BLOCK_H__

#include <src/udisksdaemontypes.h>
#include "dummytypes.h"
#include "dummy-generated.h"

G_BEGIN_DECLS

#define DUMMY_TYPE_LINUX_BLOCK  (dummy_linux_block_get_type ())
#define DUMMY_LINUX_BLOCK(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), DUMMY_TYPE_LINUX_BLOCK, DummyLinuxBlock))
#define DUMMY_IS_LINUX_BLOCK(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), DUMMY_TYPE_LINUX_BLOCK))

GType            dummy_linux_block_get_type           (void) G_GNUC_CONST;
DummyDummyBlock *dummy_linux_block_new                (void);
gboolean         dummy_linux_block_update             (DummyLinuxBlock         *drive,
                                                       UDisksLinuxBlockObject  *object);

G_END_DECLS

#endif /* __DUMMY_LINUX_BLOCK_H__ */
