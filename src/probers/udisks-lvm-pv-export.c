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

const gchar *pv_uuid = NULL;

/* ---------------------------------------------------------------------------------------------------- */

static vg_t
find_vg_for_pv_uuid (lvm_t        lvm_ctx,
                     const gchar *pv_uuid,
                     pv_t        *out_pv)
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
          struct dm_list *pvs;

          pvs = lvm_vg_list_pvs (vg);
          if (pvs != NULL)
            {
              struct lvm_pv_list *pv_list;
              dm_list_iterate_items (pv_list, pvs)
                {
                  const char *uuid;
                  pv_t pv = pv_list->pv;

                  uuid = lvm_pv_get_uuid (pv);
                  if (uuid != NULL)
                    {
                      if (g_strcmp0 (uuid, pv_uuid) == 0)
                        {
                          if (out_pv != NULL)
                            *out_pv = pv;
                          ret = vg;
                          goto out;
                        }
                    }
                }
            }

          lvm_vg_close (vg);
        }
    }

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
print_vg (vg_t vg)
{
  const char *s;
  struct dm_list *pvs;
  struct dm_list *lvs;

  s = lvm_vg_get_uuid (vg); g_print ("UDISKS_LVM2_PV_VG_UUID=%s\n", s);
  s = lvm_vg_get_name (vg); g_print ("UDISKS_LVM2_PV_VG_NAME=%s\n", s);
  g_print ("UDISKS_LVM2_PV_VG_SIZE=%" G_GUINT64_FORMAT "\n", lvm_vg_get_size (vg));
  g_print ("UDISKS_LVM2_PV_VG_FREE_SIZE=%" G_GUINT64_FORMAT "\n", lvm_vg_get_free_size (vg));
  g_print ("UDISKS_LVM2_PV_VG_EXTENT_SIZE=%" G_GUINT64_FORMAT "\n", lvm_vg_get_extent_size (vg));
  g_print ("UDISKS_LVM2_PV_VG_EXTENT_COUNT=%" G_GUINT64_FORMAT "\n", lvm_vg_get_extent_count (vg));
  g_print ("UDISKS_LVM2_PV_VG_SEQNUM=%" G_GUINT64_FORMAT "\n", lvm_vg_get_seqno (vg));

  /* First we print the PVs that is part of the VG. We need this information
   * because not all PVs may be available.
   *
   * The format used is a space-separated list of list of key value
   * pairs separated by a semicolon. Since no value can contain
   * the semicolon character we don't need to worry about escaping
   * anything
   *
   * The following keys are recognized:
   *
   *  uuid:               the UUID of the PV
   *  size:               the size of the PV (TODO: pending liblvm addition)
   *  allocated_size:     the allocated size of the PV (TODO: pending liblvm addition)
   *
   * Here's an example of a string with this information
   *
   */
  pvs = lvm_vg_list_pvs (vg);
  if (pvs != NULL)
    {
      GString *str;
      struct lvm_pv_list *pv_list;

      str = g_string_new (NULL);
      dm_list_iterate_items (pv_list, pvs)
        {
          const char *uuid;
          guint64 size;
          guint64 free_size;
          pv_t pv = pv_list->pv;

          uuid = lvm_pv_get_uuid (pv);
          if (uuid != NULL)
            g_string_append_printf (str, "uuid=%s", uuid);
          size = lvm_pv_get_size (pv);
          g_string_append_printf (str, ";size=%" G_GUINT64_FORMAT, size);
          free_size = lvm_pv_get_free (pv);
          g_string_append_printf (str, ";allocated_size=%" G_GUINT64_FORMAT, size - free_size);

          g_string_append_c (str, ' ');
        }
      g_print ("UDISKS_LVM2_PV_VG_PV_LIST=%s\n", str->str);
      g_string_free (str, TRUE);
    }

  /* Then print the LVs that is part of the VG - we need this because
   * of the fact that LVs can be activated/deactivated independent of
   * each other.
   *
   * The format used is a space-separated list of list of key value
   * pairs separated by a semicolon. Since no value can contain
   * the semicolon character we don't need to worry about escaping
   * anything
   *
   * The following keys are recognized:
   *
   *  uuid:     the UUID of the LV
   *  size:     the size of the LV
   *  name:     the name of the LV
   *  active:   1 if the LV is active (e.g. a mapped device exists) or 0 if the LV is inactive (no mapped device exists)
   *
   * Here's an example of a string with this information
   *
   *  name=vg_test_lv1;uuid=rOHShU-4Qd4-Nvtl-gxdc-zpVr-cv5K-3H1Kzo;size=209715200;active=0 \
   *  name=vg_test_lv2;uuid=YxV3GP-zOyg-TjeX-dGhp-E2Ws-OmwM-oJhGC6;size=314572800;active=0 \
   *  name=vg_test_lv3;uuid=n43A2u-rKx5-sCPc-d5y5-XQmc-1ikS-K1srI4;size=314572800;active=0 \
   *  name=lv4;uuid=Teb0lH-KFwr-R0pF-IbYX-WGog-E2Hs-ej20dP;size=1501560832;active=1
   *
   * At the same time, find the LV for this device
   */
  lvs = lvm_vg_list_lvs (vg);
  if (lvs != NULL)
    {
      GString *str;
      struct lvm_lv_list *lv_list;

      str = g_string_new (NULL);
      dm_list_iterate_items (lv_list, lvs)
        {
          const char *uuid;
          const char *name;
          gboolean is_active;
          guint64 size;
          lv_t lv = lv_list->lv;

          uuid = lvm_lv_get_uuid (lv);
          name = lvm_lv_get_name (lv);
          size = lvm_lv_get_size (lv);
          is_active = lvm_lv_is_active (lv) != 0 ? 1 : 0;

          if (uuid != NULL && name != NULL)
            {
              g_string_append_printf (str, "name=%s", name);
              g_string_append_c (str, ';');
              g_string_append_printf (str, "uuid=%s", uuid);
              g_string_append_c (str, ';');
              g_string_append_printf (str, "size=%" G_GUINT64_FORMAT ";", size);
              g_string_append_c (str, ';');
              g_string_append_printf (str, "active=%d", is_active);
              g_string_append_c (str, ' ');
            }
        }
      g_print ("UDISKS_LVM2_PV_VG_LV_LIST=%s\n", str->str);
      g_string_free (str, TRUE);
    }

}

/* ---------------------------------------------------------------------------------------------------- */

static void
print_pv (pv_t pv)
{
  const char *s;

  s = lvm_pv_get_uuid (pv); g_print ("UDISKS_LVM2_PV_UUID=%s\n", s);
  g_print ("UDISKS_LVM2_PV_NUM_MDA=%" G_GUINT64_FORMAT "\n", lvm_pv_get_mda_count (pv));

  /* TODO: ask for more API in liblvm - pvdisplay(8) suggests more information
   * is available, e.g.
   *
   * --- Physical volume ---
   * PV Name               /dev/sda2
   * VG Name               vg_test_alpha
   * PV Size               1.82 GiB / not usable 3.08 MiB
   * Allocatable           yes
   * PE Size               4.00 MiB
   * Total PE              464
   * Free PE               106
   * Allocated PE          358
   * PV UUID               eWZFDe-3MmH-sWEs-a0Ni-DTwr-XMWL-9Frtot
  */
}

/* ---------------------------------------------------------------------------------------------------- */

int
main (int argc,
      char *argv[])
{
  int ret;
  lvm_t lvm_ctx;
  vg_t vg;
  pv_t pv;

  ret = 1;
  lvm_ctx = NULL;
  vg = NULL;

  if (argc != 2)
    {
      usage ();
      goto out;
    }

  pv_uuid = (const gchar *) argv[1];

  lvm_ctx = lvm_init (NULL);
  if (lvm_ctx == NULL)
    {
      g_printerr ("Error calling lvm_init(): %m\n");
      goto out;
    }

  vg = find_vg_for_pv_uuid (lvm_ctx, pv_uuid, &pv);
  if (vg == NULL)
    {
      g_printerr ("Error finding VG for PV UUID %s\n", pv_uuid);
      goto out;
    }

  print_vg (vg);
  print_pv (pv);

  ret = 0;

 out:
  if (vg != NULL)
    lvm_vg_close (vg);
  if (lvm_ctx != NULL)
    lvm_quit (lvm_ctx);
  return ret;
}
