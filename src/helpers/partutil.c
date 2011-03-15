/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/***************************************************************************
 *
 * part.c : library for reading and writing partition tables - uses
 *          libparted for the heavy lifting
 *
 * Copyright (C) 2006 David Zeuthen, <david@fubar.dk>
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
 **************************************************************************/

#define _LARGEFILE64_SOURCE

#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <stdint.h>
#include <linux/fs.h>

#include <linux/hdreg.h>

#define BLKGETSIZE64 _IOR(0x12,114,size_t)

#include <glib/gstdio.h>
#include "partutil.h"

static void
DEBUG (const gchar *format,
       ...)
{
#if 1
  va_list args;
  va_start (args, format);
  g_vfprintf (stderr, format, args);
  va_end (args);
  g_fprintf (stderr, "\n");
  fflush ( stderr);
#endif
}

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define USE_PARTED
#ifdef USE_PARTED
#include <parted/parted.h>
#endif

const char *
part_get_scheme_name (PartitionScheme scheme)
{
  const char *s;

  switch (scheme)
    {
    case PART_TYPE_GPT:
      s = "gpt";
      break;
    case PART_TYPE_MSDOS:
      s = "mbr";
      break;
    case PART_TYPE_MSDOS_EXTENDED:
      s = "embr";
      break;
    case PART_TYPE_APPLE:
      s = "apm";
      break;
    default:
      s = NULL;
      break;
    }

  return s;
}

struct PartitionEntry_s;
typedef struct PartitionEntry_s PartitionEntry;

struct PartitionEntry_s
{
  gboolean is_part_table;

  /* NULL iff is_part_table==FALSE */
  PartitionTable *part_table;

  /* these are always set */
  guint8 *data;
  int length;

  /* offset _on disk_ where the entry starts */
  guint64 offset;
};

struct PartitionTable_s
{
  /* partitioning scheme used */
  PartitionScheme scheme;

  /* offset of table on disk */
  guint64 offset;
  guint64 size;

  /* block size */
  guint64 block_size;

  /* entries in partition table */
  GSList *entries;
};

void
part_table_find (PartitionTable *p,
                 guint64 offset,
                 PartitionTable **out_part_table,
                 int *out_entry)
{
  int n;
  int num_entries;

  *out_part_table = p;
  *out_entry = -1;

  num_entries = part_table_get_num_entries (p);
  for (n = 0; n < num_entries; n++)
    {
      guint64 pe_offset;
      guint64 pe_size;

      pe_offset = part_table_entry_get_offset (p, n);
      pe_size = part_table_entry_get_size (p, n);

      if ((offset >= pe_offset) && (offset < pe_offset + pe_size))
        {
          PartitionTable *part_table_nested;

          part_table_nested = part_table_entry_get_nested (p, n);
          /* return the extended partition only if the offset points to it - otherwise
           * look for a logical partition
           */
          if (part_table_nested != NULL && offset > pe_offset)
            {
              part_table_find (part_table_nested, offset, out_part_table, out_entry);
            }
          else
            {
              *out_entry = n;
            }

          /* and we're done... */
          break;
        }
    }
}

static guint16
get_le16 (const void *buf)
{
  return GUINT16_FROM_LE (*((guint16 *) buf));
}

static guint32
get_le32 (const void *buf)
{
  return GUINT32_FROM_LE (*((guint32 *) buf));
}

static guint64
get_le64 (const void *buf)
{
  return GUINT64_FROM_LE (*((guint64 *) buf));
}

static guint32
get_be32 (const void *buf)
{
  return GUINT32_FROM_BE (*((guint32 *) buf));
}

/* see http://en.wikipedia.org/wiki/Globally_Unique_Identifier - excerpt
 *
 * Guids are most commonly written in text as a sequence of hexadecimal digits as such:
 *
 *   3F2504E0-4F89-11D3-9A0C-0305E82C3301
 *
 * This text notation follows from the data structure defined above. The sequence is
 *
 *  1. Data1 (8 characters)
 *  2. Hyphen
 *  3. Data2 (4 characters)
 *  4. Hyphen
 *  5. Data3 (4 characters)
 *  6. Hyphen
 *  7. Initial two items from Data4 (4 characters)
 *  8. Hyphen
 *  9. Remaining six items from Data4 (12 characters)
 *
 * Often braces are added to enclose the above format, as such:
 *
 *   {3F2504E0-4F89-11D3-9A0C-0305E82C3301}
 *
 * When printing fewer characters is desired guids are sometimes encoded
 * into a base64 string of 22 to 24 characters (depending on
 * padding). For instance:
 *
 *   7QDBkvCA1+B9K/U0vrQx1A
 *   7QDBkvCA1+B9K/U0vrQx1A==
 */

typedef struct efi_guid_s
{
  guint32 data1;
  guint16 data2;
  guint16 data3;
  guint8 data4[8];
}__attribute__ ((packed)) efi_guid;

static char *
get_le_guid (const void *buf)
{
  const efi_guid *guid = (const efi_guid *) buf;

  return g_strdup_printf ("%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                          get_le32 (&(guid->data1)),
                          get_le16 (&(guid->data2)),
                          get_le16 (&(guid->data3)),
                          guid->data4[0],
                          guid->data4[1],
                          guid->data4[2],
                          guid->data4[3],
                          guid->data4[4],
                          guid->data4[5],
                          guid->data4[6],
                          guid->data4[7]);
}

#ifdef USE_PARTED
static gboolean
set_le_guid (guint8 *buf,
             const char *source)
{
  efi_guid *guid = (efi_guid *) buf;
  guint32 __attribute__((__unused__)) data1;
  guint16 __attribute__((__unused__)) data2;
  guint16 __attribute__((__unused__)) data3;
  guint8 __attribute__((__unused__)) data4[8];
  gboolean ret;
  int n;

  ret = FALSE;

  n = sscanf (source,
              "%x-%hx-%hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
              &guid->data1,
              &guid->data2,
              &guid->data3,
              &(guid->data4[0]),
              &(guid->data4[1]),
              &(guid->data4[2]),
              &(guid->data4[3]),
              &(guid->data4[4]),
              &(guid->data4[5]),
              &(guid->data4[6]),
              &(guid->data4[7]));

  if (n != 11)
    {
      DEBUG ("guid '%s' is not valid", source);
      goto out;
    }

#if 0
  DEBUG ("source = %s", source);
  DEBUG ("data1 = %08x", guid->data1);
  DEBUG ("data2 = %04x", guid->data2);
  DEBUG ("data3 = %04x", guid->data3);
  DEBUG ("data4[0] = %02x", guid->data4[0]);
  DEBUG ("data4[1] = %02x", guid->data4[1]);
  DEBUG ("data4[2] = %02x", guid->data4[2]);
  DEBUG ("data4[3] = %02x", guid->data4[3]);
  DEBUG ("data4[4] = %02x", guid->data4[4]);
  DEBUG ("data4[5] = %02x", guid->data4[5]);
  DEBUG ("data4[6] = %02x", guid->data4[6]);
  DEBUG ("data4[7] = %02x", guid->data4[7]);
#endif

  guid->data1 = GUINT32_TO_LE (guid->data1);
  guid->data2 = GUINT16_TO_LE (guid->data2);
  guid->data3 = GUINT16_TO_LE (guid->data3);

  ret = TRUE;

out:
  return ret;
}
#endif

static PartitionEntry *
part_entry_new (PartitionTable *e_part_table,
                const guint8 *data,
                int length,
                guint64 offset)
{
  PartitionEntry *pe;

  pe = g_new0 (PartitionEntry, 1);
  pe->is_part_table = (e_part_table != NULL);
  pe->part_table = e_part_table;
  pe->offset = offset;
  pe->length = length;
  pe->data = g_new0 (guint8, length);
  memcpy (pe->data, data, length);

  return pe;
}

static void
part_entry_free (PartitionEntry *pe)
{
  if (pe->part_table != NULL)
    {
      part_table_free (pe->part_table);
    }
  g_free (pe->data);
  g_free (pe);
}

static PartitionTable *
part_table_new_empty (PartitionScheme scheme,
                      int block_size)
{
  PartitionTable *p;

  p = g_new0 (PartitionTable, 1);
  p->scheme = scheme;
  p->offset = 0;
  p->entries = NULL;
  p->block_size = block_size;

  return p;
}

void
part_table_free (PartitionTable *p)
{
  GSList *i;

  if (p == NULL)
    return;

  for (i = p->entries; i != NULL; i = i->next)
    {
      PartitionEntry *pe = i->data;
      part_entry_free (pe);
    }
  g_slist_free (p->entries);
  g_free (p);
}

#if 0
static PartitionTable *
part_table_parse_bsd (int fd, guint64 offset, guint64 size, int block_size)
{
  PartitionTable *p;

  p = NULL;

  /* TODO */

  return p;
}
#endif

#define MSDOS_MAGIC			"\x55\xaa"
#define MSDOS_PARTTABLE_OFFSET		0x1be
#define MSDOS_SIG_OFF			0x1fe

#if 0
static void
hexdump (const guint8 *mem, int size)
{
  int i;
  int j;
  int n;
  const guint8 *buf = (const guint8 *) mem;

  n = 0;
  printf ("Dumping %d=0x%x bytes\n", size, size);
  while (n < size)
    {

      fprintf (stderr, "0x%04x: ", n);

      j = n;
      for (i = 0; i < 16; i++)
        {
          if (j >= size)
            break;
          fprintf (stderr, "%02x ", buf[j]);
          j++;
        }

      for (; i < 16; i++)
        {
          fprintf (stderr, "   ");
        }

      fprintf (stderr, "   ");

      j = n;
      for (i = 0; i < 16; i++)
        {
          if (j >= size)
            break;
          fprintf (stderr, "%c", isprint(buf[j]) ? buf[j] : '.');
          j++;
        }

      fprintf (stderr, "\n");

      n += 16;
    }
}
#endif

static PartitionTable *
part_table_parse_msdos_extended (int fd,
                                 guint64 offset,
                                 guint64 size,
                                 int block_size)
{
  int n;
  PartitionTable *p;
  guint64 next;

  DEBUG ("Entering MS-DOS extended parser (offset=%lld, size=%lld)", offset, size);

  p = NULL;

  next = offset;

  while (next != 0)
    {
      guint64 readfrom;
      const guint8 embr[512];

      readfrom = next;
      next = 0;

      DEBUG ("readfrom = %lld", readfrom);

      if ((guint64) lseek64 (fd, readfrom, SEEK_SET) != readfrom)
        {
          DEBUG ("lseek failed (%s)", strerror (errno));
          goto out;
        }
      if (read (fd, &embr, sizeof(embr)) != sizeof(embr))
        {
          DEBUG ("read failed (%s)", strerror (errno));
          goto out;
        }

      //hexdump (embr, 512);

      if (memcmp (&embr[MSDOS_SIG_OFF], MSDOS_MAGIC, 2) != 0)
        {
          DEBUG ("No MSDOS_MAGIC found");
          goto out;
        }

      DEBUG ("MSDOS_MAGIC found");

      if (p == NULL)
        {
          p = part_table_new_empty (PART_TYPE_MSDOS_EXTENDED, block_size);
          p->offset = offset;
          p->size = size;
        }

      for (n = 0; n < 2; n++)
        {
          PartitionEntry *pe;
          guint64 pstart;
          guint64 psize;

          pstart = block_size * ((guint64) get_le32 (&(embr[MSDOS_PARTTABLE_OFFSET + n * 16 + 8])));
          psize = block_size * ((guint64) get_le32 (&(embr[MSDOS_PARTTABLE_OFFSET + n * 16 + 12])));

          if (psize == 0)
            continue;

          pe = NULL;

          if (n == 0)
            {
              //DEBUG ("part %d (offset %lld, size %lld, type 0x%02x)",
              //     n, readfrom + pstart, psize, ptype));

              //DEBUG ("pstart = %lld", pstart));

              //hexdump (&(embr[MSDOS_PARTTABLE_OFFSET + n * 16]), 16);

              pe = part_entry_new (NULL, &(embr[MSDOS_PARTTABLE_OFFSET + n * 16]), 16, readfrom
                                   + MSDOS_PARTTABLE_OFFSET + n * 16);
            }
          else
            {
              if (pstart != 0)
                {
                  //DEBUG ("found chain at offset %lld", offset + pstart);
                  next = offset + pstart;
                }
            }

          //DEBUG ("pe = %p", pe));

          if (pe != NULL)
            {
              p->entries = g_slist_append (p->entries, pe);
            }
        }

    }

 out:
  DEBUG ("Exiting MS-DOS extended parser");
  return p;
}

static PartitionTable *
part_table_parse_msdos (int fd,
                        guint64 offset,
                        guint64 size,
                        int block_size,
                        gboolean *found_gpt)
{
  int n;
  const guint8 mbr[512] __attribute__ ((aligned));
  PartitionTable *p;

  DEBUG ("Entering MS-DOS parser (offset=%lld, size=%lld)", offset, size);

  *found_gpt = FALSE;

  p = NULL;

  if (lseek64 (fd, offset, SEEK_SET) < 0)
    {
      DEBUG ("lseek failed (%s)", strerror (errno));
      goto out;
    }
  if (read (fd, &mbr, sizeof(mbr)) != sizeof(mbr))
    {
      DEBUG ("read failed (%s)", strerror (errno));
      goto out;
    }

  //hexdump (mbr, 512);

  if (memcmp (&mbr[MSDOS_SIG_OFF], MSDOS_MAGIC, 2) != 0)
    {
      DEBUG ("No MSDOS_MAGIC found");
      goto out;
    }

  DEBUG ("MSDOS_MAGIC found");

  /* sanity checks */
  for (n = 0; n < 4; n++)
    {
      if (mbr[MSDOS_PARTTABLE_OFFSET + n * 16 + 0] != 0 && mbr[MSDOS_PARTTABLE_OFFSET + n * 16 + 0] != 0x80)
        {
          DEBUG ("partitioning flag for part %d is not 0x00 or 0x80", n);
          goto out;
        }
      /* protective MBR for GPT => GPT, not MS-DOS */
      if (mbr[MSDOS_PARTTABLE_OFFSET + n * 16 + 4] == 0xee)
        {
          DEBUG ("found partition type 0xee => protective MBR for GPT");
          *found_gpt = TRUE;
          goto out;
        }
    }

  p = part_table_new_empty (PART_TYPE_MSDOS, block_size);
  p->offset = offset;
  p->size = size;

  /* we _always_ want to create four partitions */
  for (n = 0; n < 4; n++)
    {
      PartitionEntry *pe;
      guint64 pstart;
      guint64 psize;
      guint8 ptype;
      PartitionTable *e_part_table;

      pstart = block_size * ((guint64) get_le32 (&(mbr[MSDOS_PARTTABLE_OFFSET + n * 16 + 8])));
      psize = block_size * ((guint64) get_le32 (&(mbr[MSDOS_PARTTABLE_OFFSET + n * 16 + 12])));
      ptype = mbr[MSDOS_PARTTABLE_OFFSET + n * 16 + 4];

      DEBUG ("looking at part %d (offset %lld, size %lld, type 0x%02x)", n, pstart, psize, ptype);

      pe = NULL;
      e_part_table = NULL;

      /* look for embedded partition tables */
      switch (ptype)
        {

          /* extended partitions */
        case 0x05: /* MS-DOS */
        case 0x0f: /* Win95 */
        case 0x85: /* Linux */
          e_part_table = part_table_parse_msdos_extended (fd, pstart, psize, block_size);
          if (e_part_table != NULL)
            {
              pe = part_entry_new (e_part_table, &(mbr[MSDOS_PARTTABLE_OFFSET + n * 16]), 16, offset
                                   + MSDOS_PARTTABLE_OFFSET + n * 16);
            }
          break;

        case 0xa5: /* FreeBSD */
        case 0xa6: /* OpenBSD */
        case 0xa9: /* NetBSD */
          //e_part_table = part_table_parse_bsd (fd, pstart, psize, block_size);
          //break;

        default:
          DEBUG ("new part entry");
          pe = part_entry_new (NULL, &(mbr[MSDOS_PARTTABLE_OFFSET + n * 16]), 16, offset + MSDOS_PARTTABLE_OFFSET + n
                               * 16);
          break;
        }

      //DEBUG ("pe = %p", pe));

      if (pe != NULL)
        {
          p->entries = g_slist_append (p->entries, pe);
        }
    }

 out:
  DEBUG ("Exiting MS-DOS parser");
  return p;
}

#define GPT_MAGIC "EFI PART"

#define GPT_PART_TYPE_GUID_EMPTY "00000000-0000-0000-0000-000000000000"

static PartitionTable *
part_table_parse_gpt (int fd,
                      guint64 offset,
                      guint64 size,
                      int block_size)
{
  int n;
  PartitionTable *p;
  guint8 buf[16];
  guint64 partition_entry_lba;
  int num_entries;
  int size_of_entry;

  DEBUG ("Entering EFI GPT parser");

  /* by way of getting here, we've already checked for a protective MBR */

  p = NULL;

  /* Check GPT signature */
  if (lseek64 (fd, offset + 512 + 0, SEEK_SET) < 0)
    {
      DEBUG ("lseek failed (%s)", strerror (errno));
      goto out;
    }
  if (read (fd, buf, 8) != 8)
    {
      DEBUG ("read failed (%s)", strerror (errno));
      goto out;
    }
  if (memcmp (buf, GPT_MAGIC, 8) != 0)
    {
      DEBUG ("No GPT_MAGIC found");
      goto out;
    }

  DEBUG ("GPT magic found");

  /* Disk UUID */
  if (lseek64 (fd, offset + 512 + 56, SEEK_SET) < 0)
    {
      DEBUG ("lseek failed (%s)", strerror (errno));
      goto out;
    }
  if (read (fd, buf, 16) != 16)
    {
      DEBUG ("read failed (%s)", strerror (errno));
      goto out;
    }
  //hexdump ((guint8*) buf, 16);

  if (lseek64 (fd, offset + 512 + 72, SEEK_SET) < 0)
    {
      DEBUG ("lseek failed (%s)", strerror (errno));
      goto out;
    }
  if (read (fd, buf, 8) != 8)
    {
      DEBUG ("read failed (%s)", strerror (errno));
      goto out;
    }
  partition_entry_lba = get_le64 (buf);

  if (lseek64 (fd, offset + 512 + 80, SEEK_SET) < 0)
    {
      DEBUG ("lseek failed (%s)", strerror (errno));
      goto out;
    }
  if (read (fd, buf, 4) != 4)
    {
      DEBUG ("read failed (%s)", strerror (errno));
      goto out;
    }
  num_entries = get_le32 (buf);

  if (lseek64 (fd, offset + 512 + 84, SEEK_SET) < 0)
    {
      DEBUG ("lseek failed (%s)", strerror (errno));
      goto out;
    }
  if (read (fd, buf, 4) != 4)
    {
      DEBUG ("read failed (%s)", strerror (errno));
      goto out;
    }
  size_of_entry = get_le32 (buf);

  p = part_table_new_empty (PART_TYPE_GPT, block_size);
  p->offset = offset;
  p->size = size;

  DEBUG ("partition_entry_lba=%lld", partition_entry_lba);
  DEBUG ("num_entries=%d", num_entries);
  DEBUG ("size_of_entry=%d", size_of_entry);

  for (n = 0; n < num_entries; n++)
    {
      PartitionEntry *pe;
      struct
      {
        guint8 partition_type_guid[16];
        guint8 partition_guid[16];
        guint8 starting_lba[8];
        guint8 ending_lba[8];
        guint8 attributes[8];
        guint8 partition_name[72];
      } gpt_part_entry;
      char *partition_type_guid;

      if (lseek64 (fd, offset + partition_entry_lba * 512 + n * size_of_entry, SEEK_SET) < 0)
        {
          DEBUG ("lseek failed (%s)", strerror (errno));
          goto out;
        }
      if (read (fd, &gpt_part_entry, 128) != 128)
        {
          DEBUG ("read failed (%s)", strerror (errno));
          goto out;
        }

      partition_type_guid = get_le_guid (gpt_part_entry.partition_type_guid);

      if (strcmp (partition_type_guid, GPT_PART_TYPE_GUID_EMPTY) == 0)
        continue;

      pe
        = part_entry_new (NULL, (guint8*) &gpt_part_entry, 128, offset + partition_entry_lba * 512 + n
                          * size_of_entry);
      p->entries = g_slist_append (p->entries, pe);

      g_free (partition_type_guid);

      //hexdump ((guint8 *) &gpt_part_entry, 128);

    }

 out:
  DEBUG ("Leaving EFI GPT parser");
  return p;
}

#define MAC_MAGIC "ER"
#define MAC_PART_MAGIC "PM"

static PartitionTable *
part_table_parse_apple (int fd,
                        guint64 offset,
                        guint64 size,
                        int device_block_size)
{
  int n;
  PartitionTable *p;
  struct
  {
    guint16 signature;
    guint16 block_size;
    guint32 block_count;
    /* more stuff */
  }__attribute__ ((packed)) mac_header;
  struct
  {
    guint16 signature;
    guint16 res1;
    guint32 map_count;
    guint32 start_block;
    guint32 block_count;
    char name[32];
    char type[32];
    guint32 data_start;
    guint32 data_count;
    guint32 status;
    guint32 boot_start;
    guint32 boot_size;
    guint32 boot_load;
    guint32 boot_load2;
    guint32 boot_entry;
    guint32 boot_entry2;
    guint32 boot_cksum;
    char processor[16]; /* identifies ISA of boot */
    /* more stuff */
  }__attribute__ ((packed)) mac_part;
  int block_size;
  int map_count;

  DEBUG ("Entering Apple parser");

  p = NULL;

  /* Check Mac start of disk signature */
  if (lseek64 (fd, offset + 0, SEEK_SET) < 0)
    {
      DEBUG ("lseek failed (%s)", strerror (errno));
      goto out;
    }
  if (read (fd, &mac_header, sizeof(mac_header)) != sizeof(mac_header))
    {
      DEBUG ("read failed (%s)", strerror (errno));
      goto out;
    }
  if (memcmp (&(mac_header.signature), MAC_MAGIC, 2) != 0)
    {
      DEBUG ("No MAC_MAGIC found");
      goto out;
    }

  block_size = GUINT16_FROM_BE (mac_header.block_size);

  DEBUG ("Mac MAGIC found, block_size=%d", block_size);

  p = part_table_new_empty (PART_TYPE_APPLE, block_size);
  p->offset = offset;
  p->size = size;

  /* get number of entries from first entry   */
  if (lseek64 (fd, offset + block_size, SEEK_SET) < 0)
    {
      DEBUG ("lseek failed (%s)", strerror (errno));
      goto out;
    }
  if (read (fd, &mac_part, sizeof(mac_part)) != sizeof(mac_part))
    {
      DEBUG ("read failed (%s)", strerror (errno));
      goto out;
    }
  map_count = GUINT32_FROM_BE (mac_part.map_count); /* num blocks in part map */

  DEBUG ("map_count = %d", map_count);

  for (n = 0; n < map_count; n++)
    {
      PartitionEntry *pe;

      if (memcmp (&(mac_part.signature), MAC_PART_MAGIC, 2) != 0)
        {
          DEBUG ("No MAC_PART_MAGIC found");
          break;
        }

      if (lseek64 (fd, offset + (n + 1) * block_size, SEEK_SET) < 0)
        {
          DEBUG ("lseek failed (%s)", strerror (errno));
          goto out;
        }
      if (read (fd, &mac_part, sizeof(mac_part)) != sizeof(mac_part))
        {
          DEBUG ("read failed (%s)", strerror (errno));
          goto out;
        }

      pe = part_entry_new (NULL, (guint8*) &mac_part, sizeof(mac_part), offset + (n + 1) * block_size);
      p->entries = g_slist_append (p->entries, pe);

    }

 out:
  DEBUG ("Leaving Apple parser");
  return p;
}

static PartitionTable *
part_table_load_from_disk_from_file (char *device_file)
{
  int fd;
  PartitionTable *ret;

  ret = NULL;

  fd = open (device_file, O_RDONLY);
  if (fd < 0)
    {
      DEBUG ("Cannot open '%s': %m", device_file);
      goto out;
    }

  ret = part_table_load_from_disk (fd);
  close (fd);
 out:
  return ret;
}

PartitionTable *
part_table_load_from_disk (int fd)
{
  int block_size;
  guint64 size;
  PartitionTable *p;
  gboolean found_gpt;

  p = NULL;

  if (ioctl (fd, BLKGETSIZE64, &size) != 0)
    {
      DEBUG ("Cannot determine size of device");
      goto out;
    }

  if (ioctl (fd, BLKSSZGET, &block_size) != 0)
    {
      DEBUG ("Cannot determine block size");
      goto out;
    }

#ifdef POSIX_FADV_RANDOM
  /* No read-ahead, please */
  posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM);
#endif

  p = part_table_parse_msdos (fd, 0, size, block_size, &found_gpt);
  if (p != NULL)
    {
      DEBUG ("MSDOS partition table detected");
      goto out;
    }

  if (found_gpt)
    {
      p = part_table_parse_gpt (fd, 0, size, block_size);
      if (p != NULL)
        {
          DEBUG ("EFI GPT partition table detected");
          goto out;
        }
    }

  p = part_table_parse_apple (fd, 0, size, block_size);
  if (p != NULL)
    {
      DEBUG ("Apple partition table detected");
      goto out;
    }

  DEBUG ("No known partition table found");

 out:
  return p;
}

PartitionScheme
part_table_get_scheme (PartitionTable *p)
{
  return p->scheme;
}

int
part_table_get_num_entries (PartitionTable *p)
{
  return g_slist_length (p->entries);
}

guint64
part_table_get_offset (PartitionTable *p)
{
  return p->offset;
}

guint64
part_table_get_size (PartitionTable *p)
{
  return p->size;
}

PartitionTable *
part_table_entry_get_nested (PartitionTable *p,
                             int entry)
{
  PartitionEntry *pe;

  if (p == NULL)
    return NULL;

  if ((pe = g_slist_nth_data (p->entries, entry)) == NULL)
    return NULL;

  if (pe->is_part_table)
    return pe->part_table;
  else
    return NULL;
}

/**************************************************************************/

gboolean
part_table_entry_is_in_use (PartitionTable *p,
                            int entry)
{
  gboolean ret;
  PartitionEntry *pe;

  ret = FALSE;

  if (p == NULL)
    goto out;

  if ((pe = g_slist_nth_data (p->entries, entry)) == NULL)
    goto out;

  switch (p->scheme)
    {
    case PART_TYPE_GPT:
      ret = TRUE;
      break;
    case PART_TYPE_MSDOS:
    case PART_TYPE_MSDOS_EXTENDED:
      if (part_table_entry_get_offset (p, entry) == 0)
        ret = FALSE;
      else
        ret = TRUE;
      break;
    case PART_TYPE_APPLE:
      ret = TRUE;
      break;
    default:
      break;
    }
 out:
  return ret;
}

char *
part_table_entry_get_type (PartitionTable *p,
                           int entry)
{
  char *s = NULL;
  PartitionEntry *pe;

  if (p == NULL)
    goto out;

  if ((pe = g_slist_nth_data (p->entries, entry)) == NULL)
    goto out;

  switch (p->scheme)
    {
    case PART_TYPE_GPT:
      s = get_le_guid (&(pe->data[0]));
      break;
    case PART_TYPE_MSDOS:
    case PART_TYPE_MSDOS_EXTENDED:
      s = g_strdup_printf ("0x%02x", pe->data[4]);
      break;
    case PART_TYPE_APPLE:
      s = g_strdup ((char *) pe->data + 2 * 2 + 3 * 4 + 32);
      g_strchomp (s);
      break;
    default:
      break;
    }
 out:
  if (s != NULL)
    {
      g_strchomp (s);
    }
  return s;
}

char *
part_table_entry_get_uuid (PartitionTable *p,
                           int entry)
{
  char *s = NULL;
  PartitionEntry *pe;

  if (p == NULL)
    goto out;

  if ((pe = g_slist_nth_data (p->entries, entry)) == NULL)
    goto out;

  switch (p->scheme)
    {
    case PART_TYPE_GPT:
      s = get_le_guid (&(pe->data[16]));
      break;
    default:
      break;
    }
 out:
  if (s != NULL)
    {
      g_strchomp (s);
    }
  return s;
}

char *
part_table_entry_get_label (PartitionTable *p,
                            int entry)
{
  char *s = NULL;
  PartitionEntry *pe;

  if (p == NULL)
    goto out;

  if ((pe = g_slist_nth_data (p->entries, entry)) == NULL)
    goto out;

  switch (p->scheme)
    {
    case PART_TYPE_GPT:
      s = g_utf16_to_utf8 ((const gunichar2 *) &(pe->data[56]), 36, NULL, NULL, NULL);
      break;
    case PART_TYPE_APPLE:
      s = g_strdup ((char *) pe->data + 2 * 2 + 3 * 4);
      g_strchomp (s);
      break;
    default:
      break;
    }
 out:
  if (s != NULL)
    {
      g_strchomp (s);
    }
  return s;
}

char **
part_table_entry_get_flags (PartitionTable *p,
                            int entry)
{
  int n;
  char **ss = NULL;
  guint32 apm_status;
  guint64 gpt_attributes;
  PartitionEntry *pe;

  if (p == NULL)
    goto out;

  if ((pe = g_slist_nth_data (p->entries, entry)) == NULL)
    goto out;

  ss = g_new0 (char*, 6 + 1); /* hard coded to max items we'll return */
  ss[0] = NULL;
  n = 0;

  switch (p->scheme)
    {
    case PART_TYPE_GPT:
      gpt_attributes = get_le64 (&(pe->data[48]));

      /* From Table 16 of EFI 2.0 spec, bit zero means:
       *
       * "Required for the platform to function. The system
       * cannot function normally if this partition is
       * removed. This partition should be considered as
       * part of the hardware of the system, and if it is
       * removed the system may not boot. It may contain
       * diagnostics, recovery tools, or other code or data
       * that is critical to the functioning of a system
       * independent of any OS."
       *
       */
      if (gpt_attributes & (1 << 0))
        {
          ss[n++] = g_strdup ("required");
        }

      /* TODO: handle partition type specific attributes
       *
       * Found on the Internet: "For basic data partitions, the following attribute is
       * defined:0x8000000000000000 prevents the partition from having a drive letter automatically
       * assigned. By default, each partition is assigned a new drive letter. Setting this
       * attribute ensures that when a disk is moved to a new computer, a new drive letter
       * will not be automatically generated. Instead, the user can manually assign drive
       * letters. Note: Other attributes can be added at any time."
       */
      break;

    case PART_TYPE_MSDOS:
    case PART_TYPE_MSDOS_EXTENDED:
      if (pe->data[0] == 0x80)
        {
          ss[n++] = g_strdup ("boot");
        }
      break;

    case PART_TYPE_APPLE:
      apm_status = get_be32 (&(pe->data[2 * 2 + 3 * 4 + 2 * 32 + 2 * 4]));
      if (apm_status & (1 << 1))
        ss[n++] = g_strdup ("allocated");
      if (apm_status & (1 << 2))
        ss[n++] = g_strdup ("in_use");
      if (apm_status & (1 << 3))
        ss[n++] = g_strdup ("boot");
      if (apm_status & (1 << 4))
        ss[n++] = g_strdup ("allow_read");
      if (apm_status & (1 << 5))
        ss[n++] = g_strdup ("allow_write");
      if (apm_status & (1 << 6))
        ss[n++] = g_strdup ("boot_code_is_pic");
      break;
    default:
      break;
    }
  ss[n] = NULL;

 out:
  return ss;
}

guint64
part_table_entry_get_offset (PartitionTable *p,
                             int entry)
{
  guint64 val;
  PartitionEntry *pe;

  val = G_MAXUINT64;
  if (p == NULL)
    goto out;

  if ((pe = g_slist_nth_data (p->entries, entry)) == NULL)
    goto out;

  switch (p->scheme)
    {
    case PART_TYPE_GPT:
      val = p->block_size * ((guint64) get_le64 (pe->data + 32));
      break;

    case PART_TYPE_MSDOS:
      val = p->block_size * ((guint64) get_le32 (pe->data + 8));
      break;
    case PART_TYPE_MSDOS_EXTENDED:
      /* tricky here.. the offset in the EMBR is from the start of the EMBR and they are
       * scattered around the ext partition... Hence, just use the entry's offset and subtract
       * it's offset from the EMBR..
       */
      val = p->block_size * ((guint64) get_le32 (pe->data + 8)) + pe->offset - MSDOS_PARTTABLE_OFFSET;
      break;
    case PART_TYPE_APPLE:
      val = p->block_size * ((guint64) get_be32 (pe->data + 2 * 2 + 1 * 4));
      break;
    default:
      break;
    }
 out:
  return val;
}

guint64
part_table_entry_get_size (PartitionTable *p,
                           int entry)
{
  guint64 val;
  PartitionEntry *pe;

  val = G_MAXUINT64;
  if (p == NULL)
    goto out;

  if ((pe = g_slist_nth_data (p->entries, entry)) == NULL)
    goto out;

  switch (p->scheme)
    {
    case PART_TYPE_GPT:
      val = p->block_size * (((guint64) get_le64 (pe->data + 40)) - ((guint64) get_le64 (pe->data + 32)) + 1);
      break;
    case PART_TYPE_MSDOS:
    case PART_TYPE_MSDOS_EXTENDED:
      val = p->block_size * ((guint64) get_le32 (pe->data + 12));
      break;
    case PART_TYPE_APPLE:
      val = p->block_size * ((guint64) get_be32 (pe->data + 2 * 2 + 2 * 4));
      break;
    default:
      break;
    }
 out:
  return val;
}

/**************************************************************************/

#ifdef USE_PARTED

/* internal function to both add OR change a partition - if size==0,
 * then we're changing, otherwise we're adding
 */

static gboolean
part_add_change_partition (char *device_file,
                           guint64 start,
                           guint64 size,
                           guint64 new_start,
                           guint64 new_size,
                           guint64 *out_start,
                           guint64 *out_size,
                           guint *out_num,
                           char *type,
                           char *label,
                           char **flags,
                           int geometry_hps,
                           int geometry_spt,
                           gboolean poke_kernel)
{
  int n;
  gboolean is_change;
  gboolean res;
  PedDevice *device;
  PedDisk *disk;
  PedPartition *part;
  PedConstraint* constraint;
  PedPartitionType ped_type;
  guint64 start_sector;
  guint64 end_sector;
  guint64 new_start_sector;
  guint64 new_end_sector;
  PartitionScheme scheme;
  guint8 mbr_flags = 0;
  guint8 mbr_part_type = 0;
  char *endp;
  guint64 gpt_attributes = 0;
  guint32 apm_status = 0;

  res = FALSE;

  is_change = FALSE;
  if (size == 0)
    {
      is_change = TRUE;
    }

  if (is_change)
    {
      DEBUG ("In part_change_partition: device_file=%s, start=%lld, new_start=%lld, new_size=%lld, type=%s",
             device_file,
             start,
             new_start,
             new_size,
             type);
    }
  else
    {
      DEBUG ("In part_add_partition: device_file=%s, start=%lld, size=%lld, type=%s", device_file, start, size, type);
    }

  scheme = PART_TYPE_UNKNOWN;
  if (is_change)
    {
      PartitionTable *p;
      PartitionTable *container_p;
      int container_entry;

      /* if changing something, make sure the partition to change actually exists */

      p = part_table_load_from_disk_from_file (device_file);
      if (p == NULL)
        {
          DEBUG ("Cannot load partition table from %s", device_file);
          goto out;
        }
      part_table_find (p, start + 512, &container_p, &container_entry);
      scheme = part_table_get_scheme (container_p);
      DEBUG ("containing partition table scheme = %d", scheme);
      part_table_free (p);

      if (container_entry < 0)
        {
          DEBUG ("Couldn't find partition to change");
          goto out;
        }
    }
  else
    {
      PartitionTable *p;
      PartitionTable *container_p;
      int container_entry;

      /* if adding, try to find the scheme of the partition table.. don't error out if we can't find it */
      p = part_table_load_from_disk_from_file (device_file);
      if (p != NULL)
        {
          part_table_find (p, start + 512, &container_p, &container_entry);
          scheme = part_table_get_scheme (container_p);
          DEBUG ("containing partition table scheme = %d", scheme);
          part_table_free (p);
        }
    }

  /* now that we know the partitoning scheme, sanity check type and flags */
  switch (scheme)
    {
    case PART_TYPE_UNKNOWN:
      /* unknown partition table format; error out if any type, label or flags are given */
      if ((flags != NULL && flags[0] != NULL))
        {
          DEBUG ("unknown partition table format and flags is not empty");
          goto out;
        }

      if (type != NULL && strlen (type) > 0)
        {
          DEBUG ("unknown partition table format and type is not empty");
          goto out;
        }

      if (label != NULL && strlen (label) > 0)
        {
          DEBUG ("unknown partition table format and label is not empty");
          goto out;
        }
      break;

    case PART_TYPE_MSDOS:
    case PART_TYPE_MSDOS_EXTENDED:
      mbr_flags = 0;
      if (flags != NULL)
        {
          for (n = 0; flags[n] != NULL; n++)
            {
              if (strcmp (flags[n], "boot") == 0)
                {
                  mbr_flags |= 0x80;
                }
              else
                {
                  DEBUG ("unknown flag '%s'", flags[n]);
                  goto out;
                }
            }
        }

      if (type != NULL)
        {
          mbr_part_type = (guint8) (strtol (type, &endp, 0));
          if (*endp != '\0')
            {
              DEBUG ("invalid type '%s' given", type);
              goto out;
            }
        }

      if (label != NULL)
        {
          DEBUG ("labeled partitions not supported on MSDOS or MSDOS_EXTENDED");
          goto out;
        }

      break;

    case PART_TYPE_GPT:
      gpt_attributes = 0;
      if (flags != NULL)
        {
          for (n = 0; flags[n] != NULL; n++)
            {
              if (strcmp (flags[n], "required") == 0)
                {
                  gpt_attributes |= 1;
                }
              else
                {
                  DEBUG ("unknown flag '%s'", flags[n]);
                  goto out;
                }
            }
        }
      break;

    case PART_TYPE_APPLE:
      apm_status = 0;
      if (flags != NULL)
        {
          for (n = 0; flags[n] != NULL; n++)
            {
              if (strcmp (flags[n], "allocated") == 0)
                {
                  apm_status |= (1 << 1);
                }
              else if (strcmp (flags[n], "in_use") == 0)
                {
                  apm_status |= (1 << 2);
                }
              else if (strcmp (flags[n], "boot") == 0)
                {
                  apm_status |= (1 << 3);
                }
              else if (strcmp (flags[n], "allow_read") == 0)
                {
                  apm_status |= (1 << 4);
                }
              else if (strcmp (flags[n], "allow_write") == 0)
                {
                  apm_status |= (1 << 5);
                }
              else if (strcmp (flags[n], "boot_code_is_pic") == 0)
                {
                  apm_status |= (1 << 6);
                }
              else
                {
                  DEBUG ("unknown flag '%s'", flags[n]);
                  goto out;
                }
            }
        }
      break;

    default:
      DEBUG ("partitioning scheme %d not supported", scheme);
      goto out;
    }

  switch (scheme)
    {
    case PART_TYPE_MSDOS:
      if (mbr_part_type == 0x05 || mbr_part_type == 0x85 || mbr_part_type == 0x0f)
        {
          ped_type = PED_PARTITION_EXTENDED;
        }
      else
        {
          ped_type = PED_PARTITION_NORMAL;
        }
      break;

    case PART_TYPE_MSDOS_EXTENDED:
      ped_type = PED_PARTITION_LOGICAL;
      if (mbr_part_type == 0x05 || mbr_part_type == 0x85 || mbr_part_type == 0x0f)
        {
          DEBUG ("Cannot create an extended partition inside an extended partition");
          goto out;
        }
      break;

    default:
      ped_type = PED_PARTITION_NORMAL;
      break;
    }

  /* now, create the partition */

  start_sector = start / 512;
  end_sector = (start + size) / 512 - 1;
  new_start_sector = new_start / 512;
  new_end_sector = (new_start + new_size) / 512 - 1;

  device = ped_device_get (device_file);
  if (device == NULL)
    {
      DEBUG ("ped_device_get() failed");
      goto out;
    }
  DEBUG ("got it");

  /* set drive geometry on libparted object if the user requested it */
  if (geometry_hps > 0 && geometry_spt > 0)
    {
      /* not sure this is authorized use of libparted, but, eh, it seems to work */
      device->hw_geom.cylinders = device->bios_geom.cylinders = device->length / geometry_hps / geometry_spt;
      device->hw_geom.heads = device->bios_geom.heads = geometry_hps;
      device->hw_geom.sectors = device->bios_geom.sectors = geometry_spt;
    }

  disk = ped_disk_new (device);
  if (disk == NULL)
    {
      DEBUG ("ped_disk_new() failed");
      goto out_ped_device;
    }
  DEBUG ("got disk");

  if (!is_change)
    {
      part = ped_partition_new (disk, ped_type, NULL, start_sector, end_sector);
      if (part == NULL)
        {
          DEBUG ("ped_partition_new() failed");
          goto out_ped_disk;
        }
      DEBUG ("new partition");
    }
  else
    {
      part = ped_disk_get_partition_by_sector (disk, start_sector);
      if (part == NULL)
        {
          DEBUG ("ped_partition_get_by_sector() failed");
          goto out_ped_disk;
        }
      DEBUG ("got partition");
    }

  /* TODO HACK XXX FIXME UGLY BAD: This is super ugly abuse of
   * libparted - we poke at their internal data structures - but
   * there ain't nothing we can do about it until libparted
   * provides API for this...
   */
  if (scheme == PART_TYPE_GPT)
    {
      struct
      {
        efi_guid type;
        /* more stuff */
      }*gpt_data = (void *) part->disk_specific;

      if (type != NULL)
        {
          if (!set_le_guid ((guint8*) &gpt_data->type, type))
            {
              DEBUG ("type '%s' for GPT appear to be malformed", type);
              goto out_ped_partition;
            }
        }

      ped_partition_set_flag (part, PED_PARTITION_HIDDEN, (gpt_attributes & 1) != 0);

    }
  else if (scheme == PART_TYPE_MSDOS || scheme == PART_TYPE_MSDOS_EXTENDED)
    {
      struct
      {
        unsigned char system;
        /* more stuff */
      }*dos_data = (void *) part->disk_specific;

      if (type != NULL)
        {
          dos_data->system = mbr_part_type;
        }

      ped_partition_set_flag (part, PED_PARTITION_BOOT, (mbr_flags & 0x80) != 0);

    }
  else if (scheme == PART_TYPE_APPLE)
    {
      struct
      {
        char volume_name[33]; /* eg: "Games" */
        char system_name[33]; /* eg: "Apple_Unix_SVR2" */
        char processor_name[17];
        int is_boot;
        int is_driver;
        int has_driver;
        int is_root;
        int is_swap;
        int is_lvm;
        int is_raid;
        PedSector data_region_length;
        PedSector boot_region_length;
        guint32 boot_base_address;
        guint32 boot_entry_address;
        guint32 boot_checksum;
        guint32 status;
        /* more stuff */
      }*mac_data = (void *) part->disk_specific;

      if (type != NULL)
        {
          memset (mac_data->system_name, 0, 33);
          strncpy (mac_data->system_name, type, 32);
        }

      if (flags != NULL)
        {
          mac_data->status = apm_status;
        }
    }

  if (label != NULL)
    {
      ped_partition_set_name (part, label);
    }

  if (geometry_hps > 0 && geometry_spt > 0)
    {
      /* respect drive geometry */
      constraint = ped_constraint_any (device);
    }
  else if (geometry_hps == -1 && geometry_spt == -1)
    {

      /* undocumented (or is it?) libparted usage again.. it appears that
       * the probed geometry is stored in hw_geom
       */
      device->bios_geom.cylinders = device->hw_geom.cylinders;
      device->bios_geom.heads = device->hw_geom.heads;
      device->bios_geom.sectors = device->hw_geom.sectors;

      constraint = ped_constraint_any (device);
    }
  else
    {
      PedGeometry *geo_start;
      PedGeometry *geo_end;

      /* ignore drive geometry */
      if (is_change)
        {
          geo_start = ped_geometry_new (device, new_start_sector, 1);
          geo_end = ped_geometry_new (device, new_end_sector, 1);
        }
      else
        {
          geo_start = ped_geometry_new (device, start_sector, 1);
          geo_end = ped_geometry_new (device, end_sector, 1);
        }

      constraint = ped_constraint_new (ped_alignment_any, ped_alignment_any, geo_start, geo_end, 1, device->length);
    }

 try_change_again:
  if (is_change)
    {
      if (ped_disk_set_partition_geom (disk, part, constraint, new_start_sector, new_end_sector) == 0)
        {
          DEBUG ("ped_disk_set_partition_geom() failed");
          goto out_ped_constraint;
        }
    }
  else
    {
      if (ped_disk_add_partition (disk, part, constraint) == 0)
        {
          DEBUG ("ped_disk_add_partition() failed");
          goto out_ped_constraint;
        }
    }

  if (out_start != NULL)
    *out_start = part->geom.start * 512;
  if (out_size != NULL)
    *out_size = part->geom.length * 512;
  if (out_num != NULL)
    *out_num = part->num;

  if (is_change)
    {
      /* make sure the resulting size is never smaller than requested
       * (this is because one will resize the FS and *then* change the partition table)
       */
      if (*out_size < new_size)
        {
          DEBUG ("new_size=%lld but resulting size, %lld, smaller than requested", new_size, *out_size);
          new_end_sector++;
          goto try_change_again;
        }
      else
        {
          DEBUG ("changed partition to start=%lld size=%lld", *out_start, *out_size);
        }
    }
  else
    {
      DEBUG ("added partition start=%lld size=%lld", *out_start, *out_size);
    }

  /* hmm, if we don't do this libparted crashes.. I assume that
   * ped_disk_add_partition assumes ownership of the
   * PedPartition when adding it... sadly this is not documented
   * anywhere.. sigh..
   */
  part = NULL;

  if (poke_kernel)
    {
      if (ped_disk_commit (disk) == 0)
        {
          DEBUG ("ped_disk_commit() failed");
          goto out_ped_disk;
        }
    }
  else
    {
      if (ped_disk_commit_to_dev (disk) == 0)
        {
          DEBUG ("ped_disk_commit_to_dev() failed");
          goto out_ped_constraint;
        }
    }
  DEBUG ("committed to disk");

  res = TRUE;

  ped_constraint_destroy (constraint);
  ped_disk_destroy (disk);
  ped_device_destroy (device);
  goto out;

 out_ped_constraint:
  ped_constraint_destroy (constraint);

 out_ped_partition:
  if (part != NULL)
    {
      ped_partition_destroy (part);
    }

 out_ped_disk:
  ped_disk_destroy (disk);

 out_ped_device:
  ped_device_destroy (device);

 out:
  return res;
}

gboolean
part_add_partition (char *device_file,
                    guint64 start,
                    guint64 size,
                    guint64 *out_start,
                    guint64 *out_size,
                    guint *out_num,
                    char *type,
                    char *label,
                    char **flags,
                    int geometry_hps,
                    int geometry_spt,
                    gboolean poke_kernel)
{
  return part_add_change_partition (device_file,
                                    start,
                                    size,
                                    0,
                                    0,
                                    out_start,
                                    out_size,
                                    out_num,
                                    type,
                                    label,
                                    flags,
                                    geometry_hps,
                                    geometry_spt,
                                    poke_kernel);
}

gboolean
part_change_partition (char *device_file,
                       guint64 start,
                       guint64 new_start,
                       guint64 new_size,
                       guint64 *out_start,
                       guint64 *out_size,
                       char *type,
                       char *label,
                       char **flags,
                       int geometry_hps,
                       int geometry_spt)
{
  return part_add_change_partition (device_file,
                                    start,
                                    0,
                                    new_start,
                                    new_size,
                                    out_start,
                                    out_size,
                                    NULL,
                                    type,
                                    label,
                                    flags,
                                    geometry_hps,
                                    geometry_spt,
                                    FALSE);
}

gboolean
part_del_partition (char *device_file,
                    guint64 offset,
                    gboolean poke_kernel)
{
  gboolean ret;
  PedDevice *device;
  PedDisk *disk;
  PedPartition *part;
  PartitionTable *p;
  gboolean is_extended;
  int n;

  DEBUG ("In part_del_partition: device_file=%s, offset=%lld", device_file, offset);
  ret = FALSE;

  /* sigh.. one would think that if you passed the sector of where the
   * the beginning of the extended partition starts, then _by_sector
   * would return the same as _extended_partition.
   *
   * Sadly it's not so..
   *
   * So, check if the passed offset actually corresponds to a nested
   * partition table...
   */
  is_extended = FALSE;
  p = part_table_load_from_disk_from_file (device_file);
  if (p != NULL)
    {
      for (n = 0; n < part_table_get_num_entries (p); n++)
        {
          PartitionTable *nested;
          nested = part_table_entry_get_nested (p, n);
          if (nested != NULL)
            {
              if (part_table_get_offset (nested) == offset)
                {
                  DEBUG ("partition to delete is an extended partition");
                  is_extended = TRUE;
                }
            }
        }
      part_table_free (p);
    }

  device = ped_device_get (device_file);
  if (device == NULL)
    {
      DEBUG ("ped_device_get() failed");
      goto out;
    }
  DEBUG ("got it");

  disk = ped_disk_new (device);
  if (disk == NULL)
    {
      DEBUG ("ped_disk_new() failed");
      goto out_ped_device;
    }
  DEBUG ("got disk");

  if (is_extended)
    {
      part = ped_disk_extended_partition (disk);
    }
  else
    {
      part = ped_disk_get_partition_by_sector (disk, offset / 512);
    }

  if (part == NULL)
    {
      DEBUG ("ped_disk_get_partition_by_sector() failed");
      goto out_ped_disk;
    }

  DEBUG ("got partition - part->type=%d", part->type);

  if (part->type & PED_PARTITION_METADATA)
    {
      DEBUG ("refusing to delete a metadata partition");
      goto out_ped_disk;
    }

  if (part->type & PED_PARTITION_PROTECTED)
    {
      DEBUG ("refusing to delete a protected partition");
      goto out_ped_disk;
    }

  if (part->type & PED_PARTITION_FREESPACE)
    {
      DEBUG ("refusing to delete a protected partition");
      goto out_ped_disk;
    }

  if (ped_disk_delete_partition (disk, part) == 0)
    {
      DEBUG ("ped_disk_delete_partition() failed");
      goto out_ped_disk;
    }

  if (poke_kernel)
    {
      if (ped_disk_commit (disk) == 0)
        {
          DEBUG ("ped_disk_commit() failed");
          goto out_ped_disk;
        }
    }
  else
    {
      if (ped_disk_commit_to_dev (disk) == 0)
        {
          DEBUG ("ped_disk_commit_to_dev() failed");
          goto out_ped_disk;
        }
    }
  DEBUG ("committed to disk");

  ret = TRUE;

  ped_disk_destroy (disk);
  ped_device_destroy (device);
  goto out;

 out_ped_disk:
  ped_disk_destroy (disk);

 out_ped_device:
  ped_device_destroy (device);

 out:
  return ret;
}

gboolean
part_create_partition_table (char *device_file,
                             PartitionScheme scheme)
{
  PedDevice *device;
  PedDisk *disk;
  PedDiskType *disk_type;
  gboolean ret;

  ret = FALSE;

  DEBUG ("In part_create_partition_table: device_file=%s, scheme=%d", device_file, scheme);

  device = ped_device_get (device_file);
  if (device == NULL)
    {
      DEBUG ("ped_device_get() failed");
      goto out;
    }
  DEBUG ("got it");

  switch (scheme)
    {
    case PART_TYPE_MSDOS:
      disk_type = ped_disk_type_get ("msdos");
      break;
    case PART_TYPE_APPLE:
      disk_type = ped_disk_type_get ("mac");
      break;
    case PART_TYPE_GPT:
      disk_type = ped_disk_type_get ("gpt");
      break;
    default:
      disk_type = NULL;
      break;
    }

  if (disk_type == NULL)
    {
      DEBUG ("Unknown or unsupported partitioning scheme %d", scheme);
      goto out;
    }

  disk = ped_disk_new_fresh (device, disk_type);
  if (disk == NULL)
    {
      DEBUG ("ped_disk_new_fresh() failed");
      goto out_ped_device;
    }
  DEBUG ("got disk");

  if (ped_disk_commit_to_dev (disk) == 0)
    {
      DEBUG ("ped_disk_commit_to_dev() failed");
      goto out_ped_disk;
    }
  DEBUG ("committed to disk");

  ret = TRUE;

  ped_disk_destroy (disk);
  ped_device_destroy (device);
  goto out;

 out_ped_disk:
  ped_disk_destroy (disk);

 out_ped_device:
  ped_device_destroy (device);

 out:
  return ret;
}

#endif /* USE_PARTED */

/**************************************************************************/
