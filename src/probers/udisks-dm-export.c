/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <libdevmapper.h>

static void
usage (void)
{
  g_printerr ("incorrect usage\n");
}

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
  struct dm_info info;
  GString *target_types_str;
  GString *start_str;
  GString *length_str;
  GString *params_str;
  gchar buf[4096];

  ret = FALSE;
  dmt = NULL;

  dmt = dm_task_create (DM_DEVICE_TABLE);
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

  if (!info.exists)
    {
      goto out;
    }

  if (info.target_count != -1)
    g_print ("UDISKS_DM_TARGETS_COUNT=%d\n", info.target_count);

  target_types_str = g_string_new (NULL);
  start_str = g_string_new (NULL);
  length_str = g_string_new (NULL);
  params_str = g_string_new (NULL);

  /* export all tables */
  next = NULL;
  do
    {
      next = dm_get_next_target (dmt, next, &start, &length, &target_type, &params);
      if (target_type != NULL)
        {
          g_string_append (target_types_str, target_type);
          g_string_append_printf (start_str, "%" G_GUINT64_FORMAT, start);
          g_string_append_printf (length_str, "%" G_GUINT64_FORMAT, length);
          /* Set target_params for known-safe and known-needed target types only. In particular,
           * we must not export it for "crypto", since that would expose
           * information about the key. */
          if ((g_strcmp0 (target_type, "linear") == 0 ||
               g_strcmp0 (target_type, "multipath") == 0)
              && params != NULL && strlen (params) > 0)
            {
              _udev_util_encode_string (params, buf, sizeof (buf));
              g_string_append (params_str, buf);
            }
        }

      if (next != NULL)
        {
          g_string_append_c (target_types_str, ' ');
          g_string_append_c (start_str, ' ');
          g_string_append_c (length_str, ' ');
          g_string_append_c (params_str, ' ');
        }
    }
  while (next != NULL);

  if (target_types_str->len > 0)
      g_print ("UDISKS_DM_TARGETS_TYPE=%s\n", target_types_str->str);
  if (start_str->len > 0)
      g_print ("UDISKS_DM_TARGETS_START=%s\n", start_str->str);
  if (length_str->len > 0)
      g_print ("UDISKS_DM_TARGETS_LENGTH=%s\n", length_str->str);
  if (params_str->len > 0)
      g_print ("UDISKS_DM_TARGETS_PARAMS=%s\n", params_str->str);

  g_string_free (target_types_str, TRUE);
  g_string_free (start_str, TRUE);
  g_string_free (length_str, TRUE);
  g_string_free (params_str, TRUE);

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
