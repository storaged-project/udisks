/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008 David Zeuthen <zeuthen@gmail.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/bsg.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <scsi/scsi_ioctl.h>
#include <linux/cdrom.h>

#include <stdint.h>

#include <glib.h>
#include <glib-object.h>

#include "udisksata.h"
#include "udisksprivate.h"
#include "udiskslogging.h"
#include "udisksdaemonutil.h"

#define UDISKS_ATA_DEFAULT_COMMAND_TIMEOUT_MSEC (5 * 1000)

/**
 * SECTION:udisksata
 * @title: ATA commands
 * @short_description: Helper routines for ATA commands
 *
 * Helper routines for sending ATA commands to a device.
 */

/**
 * sata_protocol:
 * @cdb: The CDB byte array to be updated (bytes 1 & 2)
 * @protocol: The specified protocol
 *
 * Set the CDB bytes 1 & 2 for correct SATA protocol.
 */
static void
sata_protocol (guint8 cdb[], UDisksAtaCommandProtocol protocol)
{
  switch (protocol)
    {
    case UDISKS_ATA_COMMAND_PROTOCOL_NONE:
      cdb[1] = 3 << 1;                  /* PROTOCOL: Non-data */
      cdb[2] = 0x20;                    /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=0, T_LENGTH=0 */
      break;
    case UDISKS_ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST:
      cdb[1] = 4 << 1;                  /* PROTOCOL: PIO Data-In */
      cdb[2] = 0x2e;                    /* OFF_LINE=0, CK_COND=1, T_DIR=1, BYT_BLOK=1, T_LENGTH=2 */
      break;
    case UDISKS_ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE:
      cdb[1] = 5 << 1;                  /* PROTOCOL: PIO Data-Out */
      cdb[2] = 0x26;                    /* OFF_LINE=0, CK_COND=1, T_DIR=0, BYT_BLOK=1, T_LENGTH=2 */
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}

/**
 * udisks_ata_send_command_sync:
 * @fd: A file descriptor for a ATA device.
 * @timeout_msec: Timeout in milli-seconds for the command. Use -1 for the default (5 seconds) timeout and %G_MAXINT for no timeout.
 * @protocol: The direction of the command.
 * @input: The input for the command.
 * @output: The output for the command.
 * @error: Return location for error or %NULL.
 *
 * Sends a command to an ATA device. Blocks the calling thread while the command is pending.
 *
 * Returns: %TRUE if the command succeeded, %FALSE if @error is set.
 */
gboolean
udisks_ata_send_command_sync (gint                       fd,
                              gint                       timeout_msec,
                              UDisksAtaCommandProtocol   protocol,
                              UDisksAtaCommandInput     *input,
                              UDisksAtaCommandOutput    *output,
                              GError                   **error)
{
  struct sg_io_v4 io_v4;
  uint8_t cdb[16];
  gint cdb_len = 16;
  uint8_t sense[32];
  uint8_t *desc = sense+8;
  gboolean use_ata12 = FALSE;
  gboolean ret = FALSE;
  gint rc;

  g_return_val_if_fail (fd != -1, FALSE);
  g_return_val_if_fail (timeout_msec == -1 || timeout_msec > 0, FALSE);
  g_return_val_if_fail (/* protocol >= 0 && */ protocol <= 2, FALSE);
  g_return_val_if_fail (input != NULL, FALSE);
  g_return_val_if_fail (input->buffer_size == 0 || input->buffer != NULL, FALSE);
  g_return_val_if_fail (output != NULL, FALSE);
  g_return_val_if_fail (output->buffer_size == 0 || output->buffer != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (timeout_msec == -1)
    timeout_msec = UDISKS_ATA_DEFAULT_COMMAND_TIMEOUT_MSEC;

  /* zero outputs, even if returning an error */
  output->error = 0;
  output->count = 0;
  output->device = 0;
  output->status = 0;
  output->lba = 0;
  if (output->buffer != NULL)
    memset (output->buffer, 0, output->buffer_size);

  memset (cdb, 0, sizeof (cdb));

  /* Prefer ATA PASS-THROUGH (16) to ATA PASS-THROUGH (12) since the op-code
   * for the latter clashes with the MMC blank command.
   *
   * TODO: this is hard-coded to FALSE for now - should retry with the 12-byte
   *       version only if the 16-byte version fails. But we don't do that
   *       right now
   */
  use_ata12 = FALSE;

  if (use_ata12)
    {
      /* Do not confuse optical drive firmware with ATA commands
       * some drives are reported to blank CD-RWs because the op-code
       * for ATA PASS-THROUGH (12) clashes with the MMC blank command.
       *
       * http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=556635
       */
      if (ioctl (fd, CDROM_GET_CAPABILITY, NULL) >= 0)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Refusing to send ATA PASS-THROUGH (12) to optical drive");
          goto out;
        }

      /*
       * ATA Pass-Through 12 byte command, as described in
       *
       *  T10 04-262r8 ATA Command Pass-Through
       *
       * from http://www.t10.org/ftp/t10/document.04/04-262r8.pdf
       */
      cdb[0] = 0xa1;                        /* OPERATION CODE: 12 byte pass through */
      sata_protocol (cdb, protocol);        /* Set the protocol bytes */
      cdb[3] = input->feature;              /* FEATURES */
      cdb[4] = input->count;                /* SECTORS */
      cdb[5] = (input->lba >>  0) & 0xff;   /* LBA LOW */
      cdb[6] = (input->lba >>  8) & 0xff;   /* LBA MID */
      cdb[7] = (input->lba >> 16) & 0xff;   /* LBA HIGH */
      cdb[8] = input->device;               /* SELECT */
      cdb[9] = input->command;              /* ATA COMMAND */
      cdb_len = 12;
    }
  else
    {
      /*
       * ATA Pass-Through 16 byte command, as described in
       *
       *  T10 04-262r8 ATA Command Pass-Through
       *
       * from http://www.t10.org/ftp/t10/document.04/04-262r8.pdf
       */
      cdb[0] = 0x85;                        /* OPERATION CODE: 16 byte pass through */
      sata_protocol (cdb, protocol);        /* Set the protocol bytes */
      cdb[ 3] = (input->feature >>  8) & 0xff;   /* FEATURES */
      cdb[ 4] = (input->feature >>  0) & 0xff;   /* FEATURES */
      cdb[ 5] = (input->count >>  8) & 0xff;     /* SECTORS */
      cdb[ 6] = (input->count >>  0) & 0xff;     /* SECTORS */
      cdb[ 8] = (input->lba >> 16) & 0xff;       /* LBA HIGH */
      cdb[10] = (input->lba >>  8) & 0xff;       /* LBA MID */
      cdb[12] = (input->lba >>  0) & 0xff;       /* LBA LOW */
      cdb[13] = input->device;                   /* SELECT */
      cdb[14] = input->command;                  /* ATA COMMAND */
      cdb_len = 16;
    }

  /* See http://sg.danny.cz/sg/sg_io.html and http://www.tldp.org/HOWTO/SCSI-Generic-HOWTO/index.html
   * for detailed information about how the SG_IO ioctl work
   */

  memset (sense, 0, sizeof (sense));
  memset (&io_v4, 0, sizeof (io_v4));
  io_v4.guard = 'Q';
  io_v4.protocol = BSG_PROTOCOL_SCSI;
  io_v4.subprotocol = BSG_SUB_PROTOCOL_SCSI_CMD;
  io_v4.request_len = cdb_len;
  io_v4.request = (uintptr_t) cdb;
  io_v4.max_response_len = sizeof (sense);
  io_v4.response = (uintptr_t) sense;
  io_v4.din_xfer_len = output->buffer_size;
  io_v4.din_xferp = (uintptr_t) output->buffer;
  io_v4.dout_xfer_len = input->buffer_size;
  io_v4.dout_xferp = (uintptr_t) input->buffer;
  if (timeout_msec == G_MAXINT)
    io_v4.timeout = G_MAXUINT;
  else
    io_v4.timeout = timeout_msec;

  rc = ioctl (fd, SG_IO, &io_v4);
  if (rc != 0)
    {
      /* could be that the driver doesn't do version 4, try version 3 */
      if (errno == EINVAL)
        {
          struct sg_io_hdr io_hdr;

          memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
          io_hdr.interface_id = 'S';
          io_hdr.cmdp = (unsigned char*) cdb;
          io_hdr.cmd_len = cdb_len;
          switch (protocol)
            {
            case UDISKS_ATA_COMMAND_PROTOCOL_NONE:
              io_hdr.dxfer_direction = SG_DXFER_NONE;
              break;

            case UDISKS_ATA_COMMAND_PROTOCOL_DRIVE_TO_HOST:
              io_hdr.dxferp = output->buffer;
              io_hdr.dxfer_len = output->buffer_size;
              io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
              break;

            case UDISKS_ATA_COMMAND_PROTOCOL_HOST_TO_DRIVE:
              io_hdr.dxferp = input->buffer;
              io_hdr.dxfer_len = input->buffer_size;
              io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
              break;
            }
          io_hdr.sbp = sense;
          io_hdr.mx_sb_len = sizeof (sense);
          if (timeout_msec == G_MAXINT)
            io_hdr.timeout = G_MAXUINT;
          else
            io_hdr.timeout = timeout_msec;

          rc = ioctl(fd, SG_IO, &io_hdr);
          if (rc != 0)
            {
              g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           "SGIO v3 ioctl failed (v4 not supported): %m");
              goto out;
            }
        }
      else
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                       "SGIO v4 ioctl failed: %m");
          goto out;
        }
    }

  if (!(sense[0] == 0x72 && desc[0] == 0x9 && desc[1] == 0x0c))
    {
      gchar *s = udisks_daemon_util_hexdump (sense, 32);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unexpected sense data returned:\n%s", s);
      g_free (s);
      goto out;
    }

  output->error = desc[3];
  output->count = desc[5];
  output->device = desc[12];
  output->status = desc[13];
  output->lba = (desc[11] << 16) | (desc[9] << 8) | desc[7];

  /* TODO: be more exact with the error code perhaps? */
  if (output->error != 0 || output->status & 0x01)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ATA command failed: error=0x%02x count=0x%02x status=0x%02x",
                   (guint) output->error, (guint) output->count, (guint) output->status);
      goto out;
    }

  ret = TRUE;

 out:
  return ret;
}
