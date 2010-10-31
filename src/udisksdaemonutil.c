/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2007-2010 David Zeuthen <zeuthen@gmail.com>
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

#include "udisksdaemonutil.h"

/**
 * SECTION:udisksdaemonutil
 * @title: Utilities
 * @short_description: Various utility routines
 *
 * Various utility routines.
 */

/**
 * udisks_decode_udev_string:
 * @str: An udev-encoded string or %NULL.
 *
 * Unescapes sequences like \x20 to " " and ensures the returned string is valid UTF-8.
 *
 * If the string is not valid UTF-8, try as hard as possible to convert to UTF-8.
 *
 * If %NULL is passed, then %NULL is returned.
 *
 * See udev_util_encode_string() in libudev/libudev-util.c in the udev
 * tree for what kinds of strings can be used.
 *
 * Returns: A valid UTF-8 string that must be freed with g_free().
 */
gchar *
udisks_decode_udev_string (const gchar *str)
{
  GString *s;
  gchar *ret;
  const gchar *end_valid;
  guint n;

  if (str == NULL)
    {
      ret = NULL;
      goto out;
    }

  s = g_string_new (NULL);
  for (n = 0; str[n] != '\0'; n++)
    {
      if (str[n] == '\\')
        {
          gint val;

          if (str[n + 1] != 'x' || str[n + 2] == '\0' || str[n + 3] == '\0')
            {
              g_warning ("**** NOTE: malformed encoded string `%s'", str);
              break;
            }

          val = (g_ascii_xdigit_value (str[n + 2]) << 4) | g_ascii_xdigit_value (str[n + 3]);

          g_string_append_c (s, val);

          n += 3;
        }
      else
        {
          g_string_append_c (s, str[n]);
        }
    }

  if (!g_utf8_validate (s->str, -1, &end_valid))
    {
      g_warning ("The string `%s' is not valid UTF-8. Invalid characters begins at `%s'", s->str, end_valid);
      ret = g_strndup (s->str, end_valid - s->str);
      g_string_free (s, TRUE);
    }
  else
    {
      ret = g_string_free (s, FALSE);
    }

 out:
  return ret;
}
