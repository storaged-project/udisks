/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 David Zeuthen <david@fubar.dk>
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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include <glib.h>
#include <atasmart.h>

static void
usage (void)
{
        fprintf (stderr, "incorrect usage\n");
}

static guint64 temperature_mkelvin;
static gboolean has_bad_sectors;
static gboolean has_bad_attributes;
static guint64 power_on_seconds;


static void
collect_attrs (SkDisk *d, const SkSmartAttributeParsedData *a, void *user_data)
{
        GList **list = user_data;
        GString *s;

        s = g_string_new (NULL);

        if (strcmp (a->name, "temperature-centi-celsius") == 0 ||
            strcmp (a->name, "temperature-celsius") == 0 ||
            strcmp (a->name, "temperature-celsius-2") == 0 ||
            strcmp (a->name, "airflow-temperature-celsius") == 0) {
                temperature_mkelvin = a->pretty_value;
        }

        if (strcmp (a->name, "power-on-minutes") == 0 ||
            strcmp (a->name, "power-on-seconds") == 0 ||
            strcmp (a->name, "power-on-half-minutes") == 0 ||
            strcmp (a->name, "power-on-hours") == 0) {
                power_on_seconds = a->pretty_value / 1000;
        }

        if (strcmp (a->name, "reallocated-sector-count") ==0 ||
            strcmp (a->name, "current-pending-sector") == 0 ||
            strcmp (a->name, "reallocated-event-count") == 0) {
                if (a->pretty_value > 0)
                        has_bad_sectors = TRUE;
        }

        if (!a->good)
                has_bad_attributes = TRUE;

        g_string_append_printf (s,
                                "%d "                             /* id */
                                "%s "                             /* name */
                                "%d "                             /* flags */
                                "%d %d "                          /* online, prefailure */
                                "%d %d "                          /* current_value, current_value_valid */
                                "%d %d "                          /* worst_value, worst_value_valid */
                                "%d %d "                          /* threshold, threshold_valid */
                                "%d %d "                          /* good, good_valid */
                                "%d %" G_GUINT64_FORMAT " "       /* pretty_unit, pretty_value */
                                "%02x %02x %02x %02x %02x %02x",  /* raw[6] */

                                a->id,
                                a->name,
                                a->flags,
                                a->online, a->prefailure,

                                a->current_value, a->current_value_valid,
                                a->worst_value, a->worst_value_valid,
                                a->threshold, a->threshold_valid,
                                a->good, a->good_valid,

                                a->pretty_unit, a->pretty_value,

                                a->raw[0], a->raw[1], a->raw[2], a->raw[3], a->raw[4], a->raw[5]);

        *list = g_list_prepend (*list, g_string_free (s, FALSE));
}

int
main (int argc, char *argv[])
{
        int ret;
        const char *device;
        SkDisk *d;
        SkBool smart_is_available;
        SkBool awake;
        SkBool good;
        const SkSmartParsedData *data;
        GString *s;
        GList *attrs;
        GList *l;
        gboolean nowakeup;

        d = NULL;
        attrs = NULL;
        ret = 1;

        s = g_string_new (NULL);

        if (argc != 3) {
                usage ();
                goto out;
        }

        device = argv[1];

        nowakeup = atoi (argv[2]);

        if (sk_disk_open (device, &d) != 0) {
                fprintf (stderr, "Failed to open disk %s: %s\n", device, strerror (errno));
                goto out;
        }

        if (sk_disk_check_sleep_mode (d, &awake) != 0) {
                fprintf (stderr, "Failed to check if disk %s is awake: %s\n", device, strerror (errno));
                goto out;
        }

        /* don't wake up disk unless specically asked to */
        if (nowakeup && !awake) {
                fprintf (stderr, "Disk %s is asleep and nowakeup option was passed\n", device);
                ret = 2;
                goto out;
        }

        if (sk_disk_smart_is_available (d, &smart_is_available) != 0) {
                fprintf (stderr, "Failed to determine if smart is available for %s: %s\n", device, strerror (errno));
                goto out;
        }

        /* time collected */
        g_string_append_printf (s, "%" G_GUINT64_FORMAT "|", (guint64) time (NULL));

        /* version of data */
        g_string_append_printf (s, "atasmartv0");

        /* main smart data */
        if (sk_disk_smart_read_data (d) != 0) {
                fprintf (stderr, "Failed to read smart data for %s: %s\n", device, strerror (errno));
                goto out;
        }
        if (sk_disk_smart_parse (d, &data) != 0) {
                fprintf (stderr, "Failed to parse smart data for %s: %s\n", device, strerror (errno));
                goto out;
        }

        temperature_mkelvin = 0;
        has_bad_sectors = FALSE;
        has_bad_attributes = FALSE;
        power_on_seconds = 0;

        if (sk_disk_smart_parse_attributes (d, collect_attrs, &attrs) != 0) {
                fprintf (stderr, "Failed to parse smart attributes for %s: %s\n", device, strerror (errno));
                goto out;
        }
        attrs = g_list_reverse (attrs);

        /* health status
         *
         * note that this is allowed to fail; some USB disks don't report status
         */
        if (sk_disk_smart_status (d, &good) != 0) {
                fprintf (stderr, "Failed to read smart status for %s: %s\n", device, strerror (errno));
                g_string_append (s, "|1 0");
        } else {
                g_string_append_printf (s, "|%d 1", good ? 0 : 1);
        }
        g_string_append_printf (s, " %d %d %lg %" G_GUINT64_FORMAT,
                                has_bad_sectors,
                                has_bad_attributes,
                                temperature_mkelvin / 1000.0,
                                power_on_seconds);

        g_string_append_printf (s,
                                "|%d %d %d %d "
                                "%d %d %d %d "
                                "%d %d %d",
                                data->offline_data_collection_status,
                                data->total_offline_data_collection_seconds,
                                data->self_test_execution_status,
                                data->self_test_execution_percent_remaining,
                                data->short_and_extended_test_available,
                                data->conveyance_test_available,
                                data->start_test_available,
                                data->abort_test_available,
                                data->short_test_polling_minutes,
                                data->extended_test_polling_minutes,
                                data->conveyance_test_polling_minutes);

        /* then each attribute */
        for (l = attrs; l != NULL; l = l->next) {
                g_string_append (s, "|");
                g_string_append (s, l->data);
        }

        printf ("%s\n", s->str);

        ret = 0;

 out:
        g_string_free (s, TRUE);
        g_list_foreach (attrs, (GFunc) g_free, FALSE);
        g_list_free (attrs);

        if (d != NULL)
                sk_disk_free (d);
        return ret;
}
