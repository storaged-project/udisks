/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <libdevmapper.h>

#include <lvm2app.h>

static gchar *vg_uuid = NULL;
static gchar *lv_uuid = NULL;

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
      if (g_str_has_prefix (uuid, "LVM-") && strlen (uuid) == 4 + 32 + 32)
        {
          vg_uuid = g_strndup (uuid + 4, 32);
          lv_uuid = g_strndup (uuid + 4 + 32, 32);
        }
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

static void
strip_hyphens (gchar *s)
{
  gchar *p;

  g_return_if_fail (s != NULL);

  while ((p = strstr (s, "-")) != NULL)
    {
      gsize len;

      len = strlen (p + 1);
      g_memmove (p, p + 1, len);
      p[len] = '\0';
    }
}

static vg_t
find_vg (lvm_t        lvm_ctx,
         const gchar *vg_uuid)
{
  vg_t ret;
  struct dm_list *vg_names;
  struct lvm_str_list *str_list;

  ret = NULL;

  vg_names = lvm_list_vg_names (lvm_ctx);
  dm_list_iterate_items (str_list, vg_names)
    {
      vg_t vg;

      vg = lvm_vg_open (lvm_ctx, str_list->str, "r", 0);
      if (vg != NULL)
        {
          char *uuid;

          uuid = lvm_vg_get_uuid (vg);
          if (uuid != NULL)
            {
              /* gah, remove hyphens so we can match things up... */
              strip_hyphens (uuid);
              if (g_strcmp0 (uuid, vg_uuid) == 0)
                {
                  ret = vg;
                  dm_free (uuid);
                  goto out;
                }
            }
          if (uuid != NULL)
            dm_free (uuid);
          lvm_vg_close (vg);
        }
    }

 out:
  return ret;
}

static gboolean
lvm_export (void)
{
  gboolean ret;
  lvm_t lvm_ctx;
  vg_t vg;
  lv_t our_lv;
  char *s;
  struct dm_list *lvs;
  struct lvm_lv_list *lv_list;

  ret = FALSE;
  lvm_ctx = NULL;
  vg = NULL;

  lvm_ctx = lvm_init (NULL);
  if (lvm_ctx == NULL)
    {
      perror ("lvm_init");
      goto out;
    }

  ret = TRUE;

  vg = find_vg (lvm_ctx, vg_uuid);
  if (vg == NULL)
    {
      g_printerr ("Cannot find VG for uuid %s\n", vg_uuid);
      goto out;
    }

  /* Print information about the VG that the LV is part of */
  s = lvm_vg_get_uuid (vg); g_print ("UDISKS_LVM2_LV_VG_UUID=%s\n", s); dm_free (s);
  s = lvm_vg_get_name (vg); g_print ("UDISKS_LVM2_LV_VG_NAME=%s\n", s); dm_free (s);

  /* Find the LV object */
  our_lv = NULL;
  lvs = lvm_vg_list_lvs (vg);
  if (lvs == NULL)
    {
      g_printerr ("Cannot find any LVs for VG with uuid %s\n", vg_uuid);
      goto out;
    }
  dm_list_iterate_items (lv_list, lvs)
    {
      lv_t lv = lv_list->lv;
      char *uuid;

      uuid = lvm_lv_get_uuid (lv);
      if (uuid != NULL)
        {
          strip_hyphens (uuid);
          if (g_strcmp0 (uuid, lv_uuid) == 0)
            {
              our_lv = lv;
            }
          dm_free (uuid);
        }
    }

  if (our_lv == NULL)
    {
      g_printerr ("Cannot find LV for uuid %s\n", lv_uuid);
      goto out;
    }

  /* Finally print information about the LV itself */
  s = lvm_lv_get_uuid (our_lv); g_print ("UDISKS_LVM2_LV_UUID=%s\n", s); dm_free (s);
  s = lvm_lv_get_name (our_lv); g_print ("UDISKS_LVM2_LV_NAME=%s\n", s); dm_free (s);

 out:
  if (vg != NULL)
    lvm_vg_close (vg);
  if (lvm_ctx != NULL)
    lvm_quit (lvm_ctx);
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

  /* If the mapped device is a LVM Logical Volume, export information about the LV and VG */
  if (vg_uuid != NULL && lv_uuid != NULL)
    {
      if (!lvm_export ())
        goto out;
    }

  ret = 0;

 out:
  g_free (vg_uuid);
  g_free (lv_uuid);
  return ret;
}
