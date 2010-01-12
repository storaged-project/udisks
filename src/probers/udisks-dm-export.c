/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <libdevmapper.h>

#include <lvm2app.h>

static void
usage (void)
{
  g_printerr ("incorrect usage\n");
}

/* ---------------------------------------------------------------------------------------------------- */

/* based on the export patch in https://bugzilla.redhat.com/show_bug.cgi?id=438604 */

static int
dm_export (int major, int minor)
{
  gboolean ret;
  struct dm_task *dmt;
  void *next;
  uint64_t start, length;
  char *target_type;
  char *params;
  const char *name;
  const char *uuid;
  struct dm_info info;

  ret = FALSE;
  dmt = NULL;

  dmt = dm_task_create (DM_DEVICE_STATUS);
  if (dmt == NULL)
    {
      perror ("dm_task_create");
      goto out;
    }

  if (dm_task_set_major (dmt, major) == 0)
    {
      perror ("dm_task_set_major");
      goto out;
    }

  if (dm_task_set_minor (dmt, minor) == 0)
    {
      perror ("dm_task_set_minor");
      goto out;
    }

  if (dm_task_run (dmt) == 0)
    {
      perror ("dm_task_run");
      goto out;
    }

  if (dm_task_get_info (dmt, &info) == 0 || !info.exists)
    {
      perror ("dm_task_get_info");
      goto out;
    }

  name = dm_task_get_name (dmt);
  if (name == NULL)
    {
      perror ("dm_task_get_name");
      goto out;
    }
  g_print ("UDISKS_DM_NAME=%s\n", name);

  uuid = dm_task_get_uuid (dmt);
  if (uuid != NULL)
    {
      g_print ("UDISKS_DM_UUID=%s\n", uuid);
    }

  if (!info.exists)
    {
      g_print ("UDISKS_DM_STATE=NOTPRESENT\n");
      goto out;
    }

  g_print ("UDISKS_DM_STATE=%s\n",
           info.suspended ? "SUSPENDED" :
           (info.read_only ? " READONLY" : "ACTIVE"));

  if (!info.live_table && !info.inactive_table)
    {
      g_print ("UDISKS_DM_TABLE_STATE=NONE\n");
    }
  else
    {
      g_print ("UDISKS_DM_TABLE_STATE=%s%s%s\n",
               info.live_table ? "LIVE" : "",
               info.live_table && info.inactive_table ? "/" : "",
               info.inactive_table ? "INACTIVE" : "");
    }

  if (info.open_count != -1)
    {
      g_print ("UDISKS_DM_OPENCOUNT=%d\n", info.open_count);
    }

  g_print ("UDISKS_DM_LAST_EVENT_NR=%" G_GUINT32_FORMAT "\n", (guint32) info.event_nr);

  g_print ("UDISKS_DM_MAJOR=%d\n", info.major);
  g_print ("UDISKS_DM_MINOR=%d\n", info.minor);

  if (info.target_count != -1)
    g_print ("UDISKS_DM_TARGET_COUNT=%d\n", info.target_count);

  /* export all table types */
  next = NULL;
  next = dm_get_next_target (dmt, next, &start, &length, &target_type, &params);
  if (target_type != NULL)
    {
      g_print ("UDISKS_DM_TARGET_TYPES=%s", target_type);
      while (next != NULL)
        {
          next = dm_get_next_target (dmt, next, &start, &length, &target_type, &params);
          if (target_type)
            g_print (",%s", target_type);
        }
      g_print ("\n");
    }

  ret = TRUE;

 out:
  if (dmt != NULL)
    dm_task_destroy(dmt);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int argc,
      char *argv[])
{
  int ret;
  int major;
  int minor;
  char *endp;

  ret = 1;

  if (argc != 3)
    {
      usage ();
      goto out;
    }

  major = strtol (argv[1], &endp, 10);
  if (endp == NULL || *endp != '\0')
    {
      usage ();
      goto out;
    }

  minor = strtol (argv[2], &endp, 10);
  if (endp == NULL || *endp != '\0')
    {
      usage ();
      goto out;
    }

  /* First export generic information about the mapped device */
  if (!dm_export (major, minor))
    goto out;

  ret = 0;

 out:
  return ret;
}
