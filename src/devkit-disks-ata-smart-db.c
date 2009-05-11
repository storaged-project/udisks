/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 David Zeuthen <david@fubar.dk>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <sqlite3.h>
#include <zlib.h>

#include "devkit-disks-daemon.h"
#include "devkit-disks-device.h"
#include "devkit-disks-device-private.h"
#include "devkit-disks-ata-smart-db.h"

struct DevkitDisksAtaSmartDbPrivate
{
        sqlite3 *db;
};

G_DEFINE_TYPE (DevkitDisksAtaSmartDb, devkit_disks_ata_smart_db, G_TYPE_OBJECT)

#define DEVKIT_DISKS_ATA_SMART_DB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DEVKIT_DISKS_TYPE_DEVICE, DevkitDisksDevicePrivate))


static void
devkit_disks_ata_smart_db_finalize (GObject *object)
{
        DevkitDisksAtaSmartDb *db = DEVKIT_DISKS_ATA_SMART_DB (object);

        if (db->priv->db != NULL)
                sqlite3_close (db->priv->db);

        if (G_OBJECT_CLASS (devkit_disks_ata_smart_db_parent_class)->finalize != NULL)
                G_OBJECT_CLASS (devkit_disks_ata_smart_db_parent_class)->finalize (object);
}

static void
devkit_disks_ata_smart_db_class_init (DevkitDisksAtaSmartDbClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;

        g_type_class_add_private (klass, sizeof (DevkitDisksAtaSmartDbPrivate));

        object_class->finalize = devkit_disks_ata_smart_db_finalize;
}

static void
devkit_disks_ata_smart_db_init (DevkitDisksAtaSmartDb *db)
{
        gint ret;
        gchar *err_msg;

        db->priv = G_TYPE_INSTANCE_GET_PRIVATE (db, DEVKIT_DISKS_TYPE_ATA_SMART_DB, DevkitDisksAtaSmartDbPrivate);

        ret = sqlite3_open_v2 (PACKAGE_LOCALSTATE_DIR "/lib/DeviceKit-disks/ata-smart-db.sqlite3",
                               &db->priv->db,
                               SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                               NULL);
        if (ret != SQLITE_OK) {
                g_debug ("error opening sqlite3 database at "
                         PACKAGE_LOCALSTATE_DIR "/lib/DeviceKit-disks/ata-smart-db.sqlite3"
                         ": %s", sqlite3_errmsg (db->priv->db));
                sqlite3_close (db->priv->db);
                g_object_unref (db);
                goto out;
        }

        /* create tables */
        ret = sqlite3_exec (db->priv->db,
                            "CREATE TABLE AtaSmartEntry ("
                            "  disk_id TEXT, "
                            "  time_collected INTEGER, "
                            "  is_failing INTEGER,"
                            "  is_failing_valid INTEGER,"
                            "  has_bad_sectors INTEGER,"
                            "  has_bad_attributes INTEGER,"
                            "  temperature_kelvin REAL,"
                            "  power_on_secs INTEGER,"
                            "  compressed_data BLOB "
                            ");",
                            NULL,
                            NULL,
                            &err_msg);
        if (ret != SQLITE_OK) {
                g_debug ("SQL error creating tables: %s", err_msg);
                sqlite3_free (err_msg);
        }

 out:
        ;
}

DevkitDisksAtaSmartDb *
devkit_disks_ata_smart_db_new (void)
{
        return DEVKIT_DISKS_ATA_SMART_DB (g_object_new (DEVKIT_DISKS_TYPE_ATA_SMART_DB, NULL));
}

static gchar *
get_disk_id (DevkitDisksDevice *device)
{
        gchar *s;
        gchar *result;

        result = NULL;

        if (device->priv->drive_vendor == NULL || strlen (device->priv->drive_vendor) == 0)
                goto out;
        if (device->priv->drive_model == NULL || strlen (device->priv->drive_model) == 0)
                goto out;
        if (device->priv->drive_revision == NULL || strlen (device->priv->drive_revision) == 0)
                goto out;
        if (device->priv->drive_serial == NULL || strlen (device->priv->drive_serial) == 0)
                goto out;

        s = g_strdup_printf ("%s_%s_%s_%s",
                             device->priv->drive_vendor,
                             device->priv->drive_model,
                             device->priv->drive_revision,
                             device->priv->drive_serial);
        result = g_uri_escape_string (s, NULL, FALSE);
        g_free (s);

out:
        return result;
}

void
devkit_disks_ata_smart_db_add_entry (DevkitDisksAtaSmartDb *db,
                                     DevkitDisksDevice     *device,
                                     time_t                 time_collected,
                                     gboolean               is_failing,
                                     gboolean               is_failing_valid,
                                     gboolean               has_bad_sectors,
                                     gboolean               has_bad_attributes,
                                     gdouble                temperature_kelvin,
                                     guint64                power_on_seconds,
                                     const void            *blob,
                                     gsize                  blob_size)
{
        gint ret;
        char *s;
        gchar *disk_id;
        sqlite3_stmt *stmt;
        guchar *compressed_blob;
        uLongf compressed_blob_size;

        s = NULL;
        stmt = NULL;
        disk_id = NULL;
        compressed_blob = NULL;

        if (db->priv->db == NULL) {
                g_warning ("No database");
                goto out;
        }

        disk_id = get_disk_id (device);
        if (disk_id == NULL) {
                g_warning ("Error getting stable ID for device");
                goto out;
        }

        /* compress the data */
        compressed_blob = g_new0 (guchar, blob_size * 3 / 2 + 32);
        ret = compress2 (compressed_blob, &compressed_blob_size,
                         blob, blob_size,
                         1);
        if (ret != Z_OK) {
                g_warning ("Error compressing blob: %d", ret);
                goto out;
        }

        //g_debug ("Compressed %ld bytes into %ld bytes", blob_size, compressed_blob_size);

        /* insert it into the database */
        s = sqlite3_mprintf ("INSERT INTO AtaSmartEntry "
                             "(disk_id, "
                             "time_collected, "
                             "is_failing, "
                             "is_failing_valid, "
                             "has_bad_sectors, "
                             "has_bad_attributes, "
                             "temperature_kelvin, "
                             "power_on_secs, "
                             "compressed_data) "
                             "VALUES ('%q', "
                             "%" G_GUINT64_FORMAT ", "
                             "%d, "
                             "%d, "
                             "%d, "
                             "%d, "
                             "%g, "
                             "%" G_GUINT64_FORMAT ", "
                             "?)",
                             disk_id,
                             (guint64) time_collected,
                             is_failing,
                             is_failing_valid,
                             has_bad_sectors,
                             has_bad_attributes,
                             temperature_kelvin,
                             power_on_seconds);
        ret = sqlite3_prepare_v2 (db->priv->db,
                                  s,
                                  -1,
                                  &stmt,
                                  NULL);
        if (ret != SQLITE_OK) {
                g_warning ("SQL error preparing statement: %d", ret);
                goto out;
        }
        ret = sqlite3_bind_blob (stmt,
                                 1,
                                 compressed_blob,
                                 compressed_blob_size,
                                 SQLITE_STATIC);
        if (ret != SQLITE_OK) {
                g_warning ("SQL error binding BLOB: %d", ret);
                goto out;
        }
        ret = sqlite3_step (stmt);
        if (ret != SQLITE_DONE) {
                g_warning ("SQL error executing statement: %d", ret);
                goto out;
        }

 out:
        g_free (compressed_blob);
        g_free (disk_id);
        if (s != NULL)
                sqlite3_free (s);
        if (stmt != NULL)
                sqlite3_finalize (stmt);
}

void
devkit_disks_ata_smart_db_delete_entries (DevkitDisksAtaSmartDb *db,
                                          time_t                 cut_off_point)
{
        char *s;
        gint ret;
        sqlite3_stmt *stmt;

        s = NULL;
        stmt = NULL;

        s = sqlite3_mprintf ("DELETE FROM AtaSmartEntry WHERE time_collected < %"G_GUINT64_FORMAT ";",
                             (guint64) cut_off_point);
        ret = sqlite3_prepare_v2 (db->priv->db,
                                  s,
                                  -1,
                                  &stmt,
                                  NULL);
        if (ret != SQLITE_OK) {
                g_warning ("SQL error preparing statement: %d", ret);
                goto out;
        }
        ret = sqlite3_step (stmt);
        if (ret != SQLITE_DONE) {
                g_warning ("SQL error executing statement: %d", ret);
                goto out;
        }

 out:
        if (s != NULL)
                sqlite3_free (s);
        if (stmt != NULL)
                sqlite3_finalize (stmt);
}

gboolean
devkit_disks_ata_smart_db_get_entries (DevkitDisksAtaSmartDb              *db,
                                       DevkitDisksDevice                  *device,
                                       time_t                              since,
                                       time_t                              until,
                                       guint64                             spacing,
                                       DevkitDisksAtaSmartDbGetEntriesFunc callback,
                                       gpointer                            user_data)
{
        gboolean ret;
        char *s;
        sqlite3_stmt *stmt;
        gchar *disk_id;
        guint64 last_time_collected;

        ret = FALSE;
        stmt = NULL;

        if (db->priv->db == NULL) {
                g_warning ("No database");
                goto out;
        }

        disk_id = get_disk_id (device);
        if (disk_id == NULL) {
                g_warning ("Error getting stable ID for device");
                goto out;
        }

        s = sqlite3_mprintf ("SELECT"
                             " AtaSmartEntry.time_collected,"
                             " AtaSmartEntry.compressed_data, "
                             " AtaSmartEntry.is_failing, "
                             " AtaSmartEntry.is_failing_valid, "
                             " AtaSmartEntry.has_bad_sectors, "
                             " AtaSmartEntry.has_bad_attributes, "
                             " AtaSmartEntry.temperature_kelvin, "
                             " AtaSmartEntry.power_on_secs "
                             "FROM AtaSmartEntry "
                             "WHERE"
                             " AtaSmartEntry.disk_id='%q' AND"
                             " AtaSmartEntry.time_collected >= %" G_GUINT64_FORMAT " AND"
                             " AtaSmartEntry.time_collected <= %" G_GUINT64_FORMAT " "
                             "ORDER BY AtaSmartEntry.time_collected;",
                             disk_id,
                             since,
                             until);
        ret = sqlite3_prepare_v2 (db->priv->db,
                                  s,
                                  -1,
                                  &stmt,
                                  NULL);
        if (ret != SQLITE_OK) {
                g_warning ("SQL error preparing statement: %d", ret);
                goto out;
        }

        last_time_collected = 0;
        do {
                guint64 time_collected;
                const void *compressed_blob;
                gsize compressed_blob_size;
                static guchar blob[2048]; /* assume 2k is enough */
                uLongf blob_size;
                gint rc;
                gboolean    is_failing;
                gboolean    is_failing_valid;
                gboolean    has_bad_sectors;
                gboolean    has_bad_attributes;
                gdouble     temperature_kelvin;
                guint64     power_on_seconds;

                ret = sqlite3_step (stmt);

                if (ret == SQLITE_DONE)
                        break;

                if (ret != SQLITE_ROW) {
                        g_warning ("SQL error stepping: %d", ret);
                        goto out;
                }

                time_collected = sqlite3_column_int64 (stmt, 0);

                if (time_collected < (guint64) since)
                        continue;
                if (time_collected > (guint64) until)
                        continue;
                if (time_collected - last_time_collected < spacing)
                        continue;

                last_time_collected = time_collected;


                compressed_blob = sqlite3_column_blob (stmt, 1);
                compressed_blob_size = sqlite3_column_bytes (stmt, 1);

                is_failing = sqlite3_column_int (stmt, 2);
                is_failing_valid = sqlite3_column_int (stmt, 3);
                has_bad_sectors = sqlite3_column_int (stmt, 4);
                has_bad_attributes = sqlite3_column_int (stmt, 5);
                temperature_kelvin = sqlite3_column_double (stmt, 6);
                power_on_seconds = sqlite3_column_int64 (stmt, 7);

                blob_size = sizeof blob;
                rc = uncompress (blob, &blob_size, compressed_blob, compressed_blob_size);
                if (rc != Z_OK) {
                        g_warning ("Decompression of compressed blob of size %d from time %" G_GUINT64_FORMAT
                                   " for device %s FAILED with return code %d. Ignoring.",
                                   (gint) compressed_blob_size,
                                   time_collected,
                                   device->priv->device_file,
                                   rc);
                        continue;
                }

                //g_debug ("haz row %ld %ld %ld", time_collected, compressed_blob_size, blob_size);

                callback (time_collected,
                          is_failing,
                          is_failing_valid,
                          has_bad_sectors,
                          has_bad_attributes,
                          temperature_kelvin,
                          power_on_seconds,
                          blob,
                          blob_size,
                          user_data);

        } while (ret != SQLITE_DONE);


 out:
        g_free (disk_id);
        if (s != NULL)
                sqlite3_free (s);
        if (stmt != NULL)
                sqlite3_finalize (stmt);
        return ret;
}

#if 0
void
devkit_disks_ata_smart_db_record_smart_values (DevkitDisksAtaSmartDb *ata_smart_db,
                                         DevkitDisksDevice *device)
{
        int n;
        int ret;
        char *err_msg;
        char *s;
        char *disk_id;
        sqlite3_int64 row_id;
        GString *str;

        g_return_if_fail (device != NULL);
        g_return_if_fail (ata_smart_db != NULL);
        g_return_if_fail (ata_smart_db->priv->db != NULL);

        disk_id = NULL;

        disk_id = drive_get_safe_uuid (device);
        if (disk_id == NULL) {
                g_warning ("no drive uuid for %s", device->priv->native_path);
                goto out;
        }

        s = sqlite3_mprintf (
                "BEGIN TRANSACTION;"
                "INSERT INTO SmartEntry "
                "(disk_id, time_collected, temperature, time_powered_on, last_self_test_result, is_failing) "
                "VALUES ('%q', %" G_GUINT64_FORMAT ", %d, %" G_GUINT64_FORMAT ", '%q', %d)",
                disk_id,
                device->priv->drive_smart_time_collected,
                (int) device->priv->drive_smart_temperature,
                device->priv->drive_smart_time_powered_on,
                device->priv->drive_smart_last_self_test_result,
                device->priv->drive_smart_is_failing ? 1 : 0);
        ret = sqlite3_exec (ata_smart_db->priv->db, s, NULL, NULL, &err_msg);
        sqlite3_free (s);
        if (ret != SQLITE_OK) {
                g_warning ("SQL error: %s", err_msg);
                sqlite3_free (err_msg);
                goto out;
        }

        row_id = sqlite3_last_insert_rowid (ata_smart_db->priv->db);

        str = g_string_new (NULL);
        for (n = 0; n < (int) device->priv->drive_smart_attributes->len; n++) {
                GValue elem = {0};
                int id;
                char *name;
                int flags;
                int value;
                int worst;
                int threshold;
                char *raw_string;

                g_value_init (&elem, SMART_DATA_STRUCT_TYPE);
                g_value_set_static_boxed (&elem, device->priv->drive_smart_attributes->pdata[n]);
                dbus_g_type_struct_get (&elem,
                                        0, &id,
                                        1, &name,
                                        2, &flags,
                                        3, &value,
                                        4, &worst,
                                        5, &threshold,
                                        6, &raw_string,
                                        G_MAXUINT);

                s = sqlite3_mprintf (
                        "INSERT INTO SmartAttr "
                        "VALUES (%" G_GINT64_FORMAT ", '%q', %" G_GUINT64_FORMAT ", %d, '%q', %d, %d, %d, %d, '%q');\n",
                        row_id,
                        disk_id,
                        device->priv->drive_smart_time_collected,
                        id,
                        name,
                        flags,
                        value,
                        worst,
                        threshold,
                        raw_string);
                g_string_append (str, s);
                sqlite3_free (s);
        }

        g_string_append_printf (str, "COMMIT;");

        s = g_string_free (str, FALSE);
        ret = sqlite3_exec (ata_smart_db->priv->db, s, NULL, NULL, &err_msg);
        g_free (s);
        if (ret != SQLITE_OK) {
                g_warning ("SQL error: %s", err_msg);
                sqlite3_free (err_msg);
        }
out:
        g_free (disk_id);
}

static gboolean
throw_error (DBusGMethodInvocation *context, int error_code, const char *format, ...)
{
        GError *error;
        va_list args;
        char *message;

        if (context == NULL)
                return TRUE;

        va_start (args, format);
        message = g_strdup_vprintf (format, args);
        va_end (args);

        error = g_error_new (DEVKIT_DISKS_ERROR,
                             error_code,
                             "%s", message);
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        g_free (message);
        return TRUE;
}

typedef struct {
        GPtrArray *array;

        gint64 cur_rowid;
        gboolean needs_draining;

        guint64 time_collected;
        double temperature;
        guint64 time_powered_on;
        char last_self_test_result[256];
        gboolean is_failing;
        GPtrArray *attrs;
} HistoricalData;

static void
historical_data_drain (HistoricalData *data)
{
        GValue elem = {0};

        if (!data->needs_draining)
                return;

        g_value_init (&elem, HISTORICAL_SMART_DATA_STRUCT_TYPE);
        g_value_take_boxed (&elem, dbus_g_type_specialized_construct (HISTORICAL_SMART_DATA_STRUCT_TYPE));
        dbus_g_type_struct_set (&elem,
                                0, data->time_collected,
                                1, data->temperature,
                                2, data->time_powered_on,
                                3, data->last_self_test_result,
                                4, data->is_failing,
                                5, data->attrs,
                                G_MAXUINT);
        g_ptr_array_add (data->array, g_value_get_boxed (&elem));

        g_ptr_array_foreach (data->attrs, (GFunc) g_value_array_free, NULL);
        g_ptr_array_free (data->attrs, TRUE);
        data->attrs = NULL;
        data->needs_draining = FALSE;
}

static int
historical_data_cb (void *user_data, int argc, char **argv, char **col_name)
{
        HistoricalData *data = (HistoricalData *) user_data;
        gint64 rowid;
        int id;
        const char *name;
        int flags;
        int value;
        int worst;
        int threshold;
        const char *raw;

        if (argc != 13) {
                g_warning ("expected 13 columns, got %d instead", argc);
                goto out;
        }

        /* TODO: could add checks for the column types */

        rowid = atoll (argv[0]);
        if (rowid != data->cur_rowid) {
                if (data->needs_draining) {
                        historical_data_drain (data);
                }

                data->needs_draining = TRUE;
                data->cur_rowid = rowid;
                data->time_collected = atoll (argv[1]);
                data->temperature = atof (argv[2]);
                data->time_powered_on = atoll (argv[3]);
                strncpy (data->last_self_test_result, argv[4], 256);
                data->is_failing = (strcmp (argv[5], "0") != 0);
                data->attrs = g_ptr_array_new ();

                /*g_warning ("got time_collected=%lld temperature=%g time_powered_on=%lld lstr='%s' is_failing=%d",
                           data->time_collected,
                           data->temperature,
                           data->time_powered_on,
                           data->last_self_test_result,
                           data->is_failing);*/
        }

        id = atoi (argv[6]);
        name = argv[7];
        flags = atoi (argv[8]);
        value = atoi (argv[9]);
        worst = atoi (argv[10]);
        threshold = atoi (argv[11]);
        raw = argv[12];

        /*g_warning ("got id=%d name='%s' flags=0x%04x value=%d worst=%d threshold=%d raw='%s'",
          id, name, flags, value, worst, threshold, raw);*/

        GValue elem = {0};
        g_value_init (&elem, SMART_DATA_STRUCT_TYPE);
        g_value_take_boxed (&elem, dbus_g_type_specialized_construct (SMART_DATA_STRUCT_TYPE));
        dbus_g_type_struct_set (&elem,
                                0, id,
                                1, name,
                                2, flags,
                                3, value,
                                4, worst,
                                5, threshold,
                                6, raw,
                                G_MAXUINT);
        g_ptr_array_add (data->attrs, g_value_get_boxed (&elem));


        /*
        int n;
        for (n = 0; n < argc; n++) {
                printf ("%s = %s\n", col_name[n], argv[n] ? argv[n] : "NULL");
        }
        printf("\n");
        */

out:
        return 0;
}

gboolean
devkit_disks_device_drive_smart_get_historical_data (DevkitDisksDevice     *device,
                                                     guint64                from,
                                                     guint64                to,
                                                     DBusGMethodInvocation *context)
{
        char *s;
        char *disk_id;
        GTimeVal now;
        int ret;
        char *err_msg;
        DevkitDisksAtaSmartDb *ata_smart_db;
        HistoricalData *data;
        PolKitCaller *pk_caller;

        disk_id = NULL;
        pk_caller = NULL;

        if (context != NULL) {
                if ((pk_caller = devkit_disks_damon_local_get_caller_for_context (device->priv->daemon,
                                                                                  context)) == NULL)
                        goto out;
        }

        if (context != NULL) {
                if (!devkit_disks_damon_local_check_auth (
                            device->priv->daemon,
                            pk_caller,
                            "org.freedesktop.devicekit.disks.drive-smart-retrieve-historical-data",
                            context)) {
                        goto out;
                }
        }

        ata_smart_db = devkit_disks_daemon_local_get_ata_smart_db (device->priv->daemon);

        disk_id = drive_get_safe_uuid (device);
        if (disk_id == NULL) {
                g_warning ("no drive uuid for %s", device->priv->native_path);
                throw_error (context, DEVKIT_DISKS_ERROR_FAILED, "No unique disk id for device");
                goto out;
        }

        if (from > to) {
                throw_error (context, DEVKIT_DISKS_ERROR_FAILED, "Malformed time range (from > to)");
                goto out;
        }

        if (to == 0) {
                g_get_current_time (&now);
                to = (guint64) now.tv_sec;
        }

        data = g_new0 (HistoricalData, 1);
        data->array = g_ptr_array_new ();
        data->cur_rowid = -1;

        s = sqlite3_mprintf ("SELECT"
                             " SmartEntry.smart_entry_id,"
                             " SmartEntry.time_collected,"
                             " SmartEntry.temperature,"
                             " SmartEntry.time_powered_on,"
                             " SmartEntry.last_self_test_result,"
                             " SmartEntry.is_failing,"
                             " SmartAttr.id,"
                             " SmartAttr.name,"
                             " Smartattr.flags, "
                             " SmartAttr.value,"
                             " SmartAttr.worst,"
                             " SmartAttr.threshold,"
                             " SmartAttr.raw "
                             "FROM SmartEntry, SmartAttr "
                             "WHERE"
                             " SmartEntry.disk_id='%q' AND"
                             " SmartEntry.smart_entry_id=SmartAttr.smart_entry_id AND"
                             " SmartEntry.time_collected >= %" G_GUINT64_FORMAT " AND"
                             " SmartEntry.time_collected <= %" G_GUINT64_FORMAT " "
                             "ORDER BY SmartEntry.smart_entry_id, SmartAttr.id;",
                             disk_id, from, to);
        ret = sqlite3_exec (ata_smart_db->priv->db,
                            s,
                            historical_data_cb,
                            data,
                            &err_msg);
        if (ret != SQLITE_OK) {
                g_warning ("SQL error: %s", err_msg);
                sqlite3_free (err_msg);
        }
        sqlite3_free (s);

        historical_data_drain (data);
        dbus_g_method_return (context, data->array);
        g_ptr_array_foreach (data->array, (GFunc) g_value_array_free, NULL);
        g_ptr_array_free (data->array, TRUE);
        g_free (data);

out:
        g_free (disk_id);
        if (pk_caller != NULL)
                polkit_caller_unref (pk_caller);
        return TRUE;
}
#endif
