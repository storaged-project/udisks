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

/* This code is from udev - will become public libudev API at some point */

/* count of characters used to encode one unicode char */
static int utf8_encoded_expected_len(const char *str)
{
        unsigned char c = (unsigned char)str[0];

        if (c < 0x80)
                return 1;
        if ((c & 0xe0) == 0xc0)
                return 2;
        if ((c & 0xf0) == 0xe0)
                return 3;
        if ((c & 0xf8) == 0xf0)
                return 4;
        if ((c & 0xfc) == 0xf8)
                return 5;
        if ((c & 0xfe) == 0xfc)
                return 6;
        return 0;
}

/* decode one unicode char */
static int utf8_encoded_to_unichar(const char *str)
{
        int unichar;
        int len;
        int i;

        len = utf8_encoded_expected_len(str);
        switch (len) {
        case 1:
                return (int)str[0];
        case 2:
                unichar = str[0] & 0x1f;
                break;
        case 3:
                unichar = (int)str[0] & 0x0f;
                break;
        case 4:
                unichar = (int)str[0] & 0x07;
                break;
        case 5:
                unichar = (int)str[0] & 0x03;
                break;
        case 6:
                unichar = (int)str[0] & 0x01;
                break;
        default:
                return -1;
        }

        for (i = 1; i < len; i++) {
                if (((int)str[i] & 0xc0) != 0x80)
                        return -1;
                unichar <<= 6;
                unichar |= (int)str[i] & 0x3f;
        }

        return unichar;
}

/* expected size used to encode one unicode char */
static int utf8_unichar_to_encoded_len(int unichar)
{
        if (unichar < 0x80)
                return 1;
        if (unichar < 0x800)
                return 2;
        if (unichar < 0x10000)
                return 3;
        if (unichar < 0x200000)
                return 4;
        if (unichar < 0x4000000)
                return 5;
        return 6;
}

/* check if unicode char has a valid numeric range */
static int utf8_unichar_valid_range(int unichar)
{
        if (unichar > 0x10ffff)
                return 0;
        if ((unichar & 0xfffff800) == 0xd800)
                return 0;
        if ((unichar > 0xfdcf) && (unichar < 0xfdf0))
                return 0;
        if ((unichar & 0xffff) == 0xffff)
                return 0;
        return 1;
}

/* validate one encoded unicode char and return its length */
static int utf8_encoded_valid_unichar(const char *str)
{
        int len;
        int unichar;
        int i;

        len = utf8_encoded_expected_len(str);
        if (len == 0)
                return -1;

        /* ascii is valid */
        if (len == 1)
                return 1;

        /* check if expected encoded chars are available */
        for (i = 0; i < len; i++)
                if ((str[i] & 0x80) != 0x80)
                        return -1;

        unichar = utf8_encoded_to_unichar(str);

        /* check if encoded length matches encoded value */
        if (utf8_unichar_to_encoded_len(unichar) != len)
                return -1;

        /* check if value has valid range */
        if (!utf8_unichar_valid_range(unichar))
                return -1;

        return len;
}

static int is_whitelisted(char c, const char *white)
{
        if ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            strchr("#+-.:=@_", c) != NULL ||
            (white != NULL && strchr(white, c) != NULL))
                return 1;
        return 0;
}

/**
 * _udev_util_encode_string:
 * @str: input string to be encoded
 * @str_enc: output string to store the encoded input string
 * @len: maximum size of the output string, which may be
 *       four times as long as the input string
 *
 * Encode all potentially unsafe characters of a string to the
 * corresponding hex value prefixed by '\x'.
 *
 * Returns: 0 if the entire string was copied, non-zero otherwise.
 */
static int
_udev_util_encode_string(const char *str, char *str_enc, size_t len)
{
        size_t i, j;

        if (str == NULL || str_enc == NULL)
                return -1;

        for (i = 0, j = 0; str[i] != '\0'; i++) {
                int seqlen;

                seqlen = utf8_encoded_valid_unichar(&str[i]);
                if (seqlen > 1) {
                        if (len-j < (size_t)seqlen)
                                goto err;
                        memcpy(&str_enc[j], &str[i], seqlen);
                        j += seqlen;
                        i += (seqlen-1);
                } else if (str[i] == '\\' || !is_whitelisted(str[i], NULL)) {
                        if (len-j < 4)
                                goto err;
                        sprintf(&str_enc[j], "\\x%02x", (unsigned char) str[i]);
                        j += 4;
                } else {
                        if (len-j < 1)
                                goto err;
                        str_enc[j] = str[i];
                        j++;
                }
        }
        if (len-j < 1)
                goto err;
        str_enc[j] = '\0';
        return 0;
err:
        return -1;
}

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
                  char *uuid;
                  pv_t pv = pv_list->pv;

                  uuid = lvm_pv_get_uuid (pv);
                  if (uuid != NULL)
                    {
                      if (g_strcmp0 (uuid, pv_uuid) == 0)
                        {
                          if (out_pv != NULL)
                            *out_pv = pv;
                          ret = vg;
                          dm_free (uuid);
                          goto out;
                        }
                      dm_free (uuid);
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
  char *s;
  struct dm_list *pvs;
  struct dm_list *lvs;

  s = lvm_vg_get_uuid (vg); g_print ("UDISKS_LVM2_PV_VG_UUID=%s\n", s); dm_free (s);
  s = lvm_vg_get_name (vg); g_print ("UDISKS_LVM2_PV_VG_NAME=%s\n", s); dm_free (s);
  g_print ("UDISKS_LVM2_PV_VG_SIZE=%" G_GUINT64_FORMAT "\n", lvm_vg_get_size (vg) * 512); /* TODO */
  g_print ("UDISKS_LVM2_PV_VG_FREE_SIZE=%" G_GUINT64_FORMAT "\n", lvm_vg_get_free_size (vg) * 512); /* TODO */
  g_print ("UDISKS_LVM2_PV_VG_EXTENT_SIZE=%" G_GUINT64_FORMAT "\n", lvm_vg_get_extent_size (vg) * 512); /* TODO */
  g_print ("UDISKS_LVM2_PV_VG_EXTENT_COUNT=%" G_GUINT64_FORMAT "\n", lvm_vg_get_extent_count (vg));
  g_print ("UDISKS_LVM2_PV_VG_SEQNUM=%" G_GUINT64_FORMAT "\n", lvm_vg_get_seqno (vg));

  /* then print the PV UUIDs that is part of the VG */
  pvs = lvm_vg_list_pvs (vg);
  if (pvs != NULL)
    {
      GString *str;
      struct lvm_pv_list *pv_list;

      str = g_string_new (NULL);
      dm_list_iterate_items (pv_list, pvs)
        {
          char *uuid;
          pv_t pv = pv_list->pv;

          uuid = lvm_pv_get_uuid (pv);
          if (uuid != NULL)
            {
              g_string_append_printf (str, "uuid=%s ", uuid);
              dm_free (uuid);
            }
        }
      g_print ("UDISKS_LVM2_PV_VG_PV_LIST=%s\n", str->str);
      g_string_free (str, TRUE);
    }

  /* Then print the LVs that is part of the VG - we need this because
   * of the fact that LVs can be activated/deactivated independent of
   * each other... the format used is a space-separated list of list
   * of key value pairs separated by a semicolon. Each value is
   * escaped using _udev_util_encode_string() [1].
   *
   *  [1] : udev_util_encode_string() and the corresponding decode
   *        routine will be part of libudev at some point
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
          char *uuid;
          char *name;
          gboolean is_active;
          guint64 size;
          lv_t lv = lv_list->lv;
          gchar buf[256];

          uuid = lvm_lv_get_uuid (lv);
          name = lvm_lv_get_name (lv);
          size = lvm_lv_get_size (lv) * 512;
          is_active = lvm_lv_is_active (lv) != 0 ? 1 : 0;

          if (uuid != NULL && name != NULL)
            {
              _udev_util_encode_string (name, buf, sizeof (buf));
              g_string_append_printf (str, "name=%s;", buf);
              _udev_util_encode_string (uuid, buf, sizeof (buf));
              g_string_append_printf (str, "uuid=%s;", buf);
              g_string_append_printf (str, "size=%" G_GUINT64_FORMAT ";", size);
              g_string_append_printf (str, "active=%d ", is_active);
            }

          if (uuid != NULL)
            dm_free (uuid);
          if (name != NULL)
            dm_free (name);
        }
      g_print ("UDISKS_LVM2_PV_VG_LV_LIST=%s\n", str->str);
      g_string_free (str, TRUE);
    }

}

/* ---------------------------------------------------------------------------------------------------- */

static void
print_pv (pv_t pv)
{
  char *s;

  s = lvm_pv_get_uuid (pv); g_print ("UDISKS_LVM2_PV_UUID=%s\n", s); dm_free (s);
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
