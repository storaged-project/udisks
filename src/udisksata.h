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

#ifndef __UDISKS_ATA_H__
#define __UDISKS_ATA_H__

#include "udisksdaemontypes.h"

G_BEGIN_DECLS

/**
 * UDisksAtaCommandInput:
 * @command: Command
 * @feature: Feature
 * @count: Count
 * @device: Device
 * @lba: LBA
 * @buffer_size: Size of the @buffer member or 0
 * @buffer: Data to send to device or %NULL
 *
 * Struct used for input data when sending ATA commands.
 */
struct _UDisksAtaCommandInput
{
  /*< public >*/
  guint8  command;
  guint8  feature;
  guint8  count;
  guint8  device;
  guint32 lba;
  gsize   buffer_size;
  guchar *buffer;
};

/**
 * UDisksAtaCommandOutput:
 * @error: Error
 * @count: Count
 * @device: Device
 * @status: Status
 * @lba: LBA
 * @buffer_size: Size of the @buffer member or 0
 * @buffer: Pointer to where to recieve data from the device or %NULL
 *
 * Struct used for output data when sending ATA commands.
 */
struct _UDisksAtaCommandOutput
{
  /*< public >*/
  guint8  error;
  guint8  count;
  guint8  device;
  guint8  status;
  guint32 lba;
  gsize   buffer_size;
  guchar *buffer;
};

gboolean udisks_ata_send_command_sync (gint                       fd,
                                       gint                       timeout_msec,
                                       UDisksAtaCommandProtocol   protocol,
                                       UDisksAtaCommandInput     *input,
                                       UDisksAtaCommandOutput    *output,
                                       GError                   **error);


G_END_DECLS

#endif /* __UDISKS_ATA_H__ */
