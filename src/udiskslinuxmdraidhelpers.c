/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2017 Red Hat, Inc.
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
 * Author: Vojtech Trefny <vtrefny@redhat.com>
 *
 */

#include <glib.h>
#include <gudev/gudev.h>
#include <stdlib.h>

#include "udiskslinuxmdraidhelpers.h"
#include "udiskslogging.h"

gboolean
mdraid_has_redundancy (const gchar *raid_level)
{
  return raid_level != NULL &&
         g_str_has_prefix (raid_level, "raid") &&
         g_strcmp0 (raid_level, "raid0") != 0;
}

gboolean
mdraid_has_stripes (const gchar *raid_level)
{
  return raid_level != NULL &&
         g_str_has_prefix (raid_level, "raid") &&
         g_strcmp0 (raid_level, "raid1") != 0;
}

gchar *
read_sysfs_attr (GUdevDevice *device,
                 const gchar *attr)
{
  gchar *ret = NULL;
  gchar *path = NULL;
  GError *error = NULL;

  g_return_val_if_fail (G_UDEV_IS_DEVICE (device), NULL);

  path = g_strdup_printf ("%s/%s", g_udev_device_get_sysfs_path (device), attr);
  if (!g_file_get_contents (path, &ret, NULL /* size */, &error))
    {
      udisks_warning ("Error reading sysfs attr `%s': %s (%s, %d)",
                      path, error->message, g_quark_to_string (error->domain), error->code);
      g_clear_error (&error);
      goto out;
    }

  /* remove newline from the attribute */
  if (ret != NULL)
    g_strstrip (ret);

 out:
  g_free (path);
  return ret;
}

gint
read_sysfs_attr_as_int (GUdevDevice *device,
                        const gchar *attr)
{
  gint ret = 0;
  gchar *str = NULL;

  str = read_sysfs_attr (device, attr);
  if (str == NULL)
    goto out;

  ret = atoi (str);
  g_free (str);

 out:
  return ret;
}

guint64
read_sysfs_attr_as_uint64 (GUdevDevice *device,
                           const gchar *attr)
{
  guint64 ret = 0;
  gchar *str = NULL;

  str = read_sysfs_attr (device, attr);
  if (str == NULL)
    goto out;

  ret = atoll (str);
  g_free (str);

 out:
  return ret;
}
