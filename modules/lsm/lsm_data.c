/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2015 Red Hat, Inc.
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
 * Author: Gris Ge <fge@redhat.com>
 *
 */

#include "config.h"

#include "lsm_data.h"

#include <src/udisksdaemon.h>
#include <src/udisksdaemontypes.h>
#include <src/udiskslogging.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libconfig.h>
#include <string.h>
#include <stdint.h>

#define _STD_LSM_SIM_URI "sim://"
#define _STD_LSM_HPSA_URI "hpsa://"

#define _STD_LSM_CONF_PATH_PREFIX PACKAGE_SYSCONF_DIR
#define _STD_LSM_CONF_PATH "udisks/modules.conf.d/"
#define _STD_LSM_CONF_FILE "udisks2_lsm.conf"
#define _STD_LSM_CONF_REFRESH_KEYNAME "refresh_interval"
#define _STD_LSM_CONF_SIM_KEYNAME "enable_sim"
#define _STD_LSM_CONF_HPSA_KEYNAME "enable_hpsa"
#define _STD_LSM_CONF_EXT_URIS_KEYNAME "extra_uris"
#define _STD_LSM_CONF_EXT_PASS_KEYNAME "extra_passwords"
#define _STD_LSM_CONNECTION_DEFAULT_TMO 30000

/*
 * _LsmUriSet is holding the URI and password string pointers.
 */
struct _LsmUriSet
{
  const char *uri;
  const char *password;
};

/*
 * _LsmConnData is holding lsm connection information for each
 * volume.
 */
struct _LsmConnData
{
  lsm_connect *lsm_conn;
  lsm_volume *lsm_vol;
  const char *pl_id;
};

/*
 * _LsmPlData is holding the pool information.
 * It's shared by all the volumes under the same pool.
 */
struct _LsmPlData
{
  gint64 last_refresh_time;
  gboolean is_ok;
  gboolean is_raid_degraded;
  gboolean is_raid_error;
  gboolean is_raid_verifying;
  gboolean is_raid_reconstructing;
  const char *status_info;
};

/*
 * _LsmVriData is holding the volume RAID information.
 */
struct _LsmVriData
{
  gint64 last_refresh_time;
  const char *raid_type_str;
  uint32_t min_io_size;
  uint32_t opt_io_size;
  uint32_t raid_disk_count;
};

static GPtrArray *_conf_lsm_uri_sets = NULL;
static uint32_t _conf_refresh_interval = 30;
static const gboolean _sys_id_supported = TRUE;
static GPtrArray *_all_lsm_conn_array = NULL;
static GHashTable *_supported_sys_id_hash = NULL;
static GHashTable *_vpd83_2_lsm_conn_data_hash = NULL;
static GHashTable *_pl_id_2_lsm_pl_data_hash = NULL;
static GHashTable *_vpd83_2_lsm_vri_data_hash = NULL;
static char *_std_lsm_conf_file_abs_path = NULL;

static struct _LsmUriSet *_lsm_uri_set_new (const char *uri, const char *pass);
static void _handle_lsm_error (const char *msg, lsm_connect *lsm_conn);
static const char *_lsm_raid_type_to_str (lsm_volume_raid_type raid_type);
static void _load_module_conf (UDisksDaemon *daemon);
static lsm_connect *_create_lsm_connect (struct _LsmUriSet *lsm_uri_set);
static GPtrArray *_get_supported_lsm_volumes (lsm_connect *lsm_conn);
static GPtrArray *_get_supported_lsm_pls (lsm_connect *lsm_conn);
static gboolean _fill_supported_system_id_hash (lsm_connect *lsm_conn);
static void _fill_pl_id_2_lsm_pl_data_hash (GPtrArray *lsm_pl_array,
                                            gint64 last_refresh_time);
static void _fill_vpd83_2_lsm_conn_data_hash (lsm_connect *lsm_conn,
                                              GPtrArray *lsm_vol_array);
static void _fill_lsm_pl_data (struct _LsmPlData *lsm_pl_data,
                               lsm_pool *lsm_pl, gint64 last_refresh_time);
static struct _LsmVriData *
_refresh_lsm_vri_data (struct _LsmConnData *lsm_conn_data, const char *vpd83);
static struct _LsmPlData *_lsm_pl_data_lookup (const char *vpd83);
static struct _LsmVriData *_lsm_vri_data_lookup (const char *vpd83);

static const gchar *_lsm_get_conf_path (UDisksDaemon *daemon);

static void _free_lsm_connect (gpointer data);
static void _free_lsm_uri_set (gpointer data);
static void _free_lsm_conn_data (gpointer data);
static void _free_lsm_pl_data (gpointer data);
static void _free_lsm_vri_data (gpointer data);

static struct _LsmUriSet *
_lsm_uri_set_new (const char *uri, const char *pass)
{
  struct _LsmUriSet *lsm_uri_set;

  lsm_uri_set = (struct _LsmUriSet *) g_malloc (sizeof (struct _LsmUriSet));
  lsm_uri_set->uri = g_strdup (uri);
  lsm_uri_set->password = g_strdup (pass);
  return lsm_uri_set;
}

/*
 * Print lsm error via udisks_warning ()
 *
 */
static void
_handle_lsm_error (const char *msg, lsm_connect *lsm_conn)
{
  lsm_error *lsm_err = NULL;

  lsm_err = lsm_error_last_get (lsm_conn);
  if (lsm_err)
    {
      udisks_warning ("%s. Error code: %d, error message: %s",
                      msg, lsm_error_number_get (lsm_err),
                      lsm_error_message_get (lsm_err));
      lsm_error_free (lsm_err);
    }
  else
    {
      udisks_warning
        ("LSM: %s. But failed to retrieve error code and message", msg);
    }
  return;
}

/*
 * Convert lsm_volume_raid_type to 'RAID 1' like string.
 * The memory of string does not require manually free.
 */

static const char *
_lsm_raid_type_to_str (lsm_volume_raid_type raid_type)
{
  switch (raid_type)
    {
    case LSM_VOLUME_RAID_TYPE_JBOD:
      return "JBOD";
    case LSM_VOLUME_RAID_TYPE_RAID0:
      return "RAID 0";
    case LSM_VOLUME_RAID_TYPE_RAID1:
      return "RAID 1";
    case LSM_VOLUME_RAID_TYPE_RAID5:
      return "RAID 5";
    case LSM_VOLUME_RAID_TYPE_RAID6:
      return "RAID 6";
    case LSM_VOLUME_RAID_TYPE_RAID10:
      return "RAID 10";
    case LSM_VOLUME_RAID_TYPE_RAID50:
      return "RAID 50";
    case LSM_VOLUME_RAID_TYPE_RAID60:
      return "RAID 60";
    default:
      return "";
    }
}

/*
 * Use libconfig.h to read config file.
 * Set these static variables
 *    GPtrArray _conf_lsm_uri_sets
 *    gint64 _conf_refresh_interval
 * Memory should be freed by
 */
static void
_load_module_conf (UDisksDaemon *daemon)
{
  struct _LsmUriSet *lsm_uri_set = NULL;
  config_t *cfg = NULL;
  int cfg_value_int = 0;
  config_setting_t *ext_uris = NULL;
  config_setting_t *ext_pass = NULL;
  const char *uri = NULL;
  const char *password = NULL;
  const char *conf_path = NULL;
  int i;

  udisks_debug ("LSM: loading configure");

  /* Get the abs config file path. */
  conf_path = _lsm_get_conf_path (daemon);

  cfg = (config_t *) g_malloc (sizeof (config_t));
  config_init (cfg);
  if (CONFIG_TRUE != config_read_file (cfg, conf_path))
    {
      udisks_warning
        ("LSM: Failed to load config: %s, error: %s at line %d",
         conf_path, config_error_text (cfg), config_error_line (cfg));
      _conf_lsm_uri_sets = NULL;
      goto out;
    }

  config_lookup_int (cfg, _STD_LSM_CONF_REFRESH_KEYNAME, &cfg_value_int);
  if (cfg_value_int > 0)
    {
      _conf_refresh_interval = cfg_value_int & UINT32_MAX;
    }

  _conf_lsm_uri_sets =
    g_ptr_array_new_full (0, (GDestroyNotify) _free_lsm_uri_set );

  cfg_value_int = 0;            // Disable simualtor by default
  config_lookup_bool (cfg, _STD_LSM_CONF_SIM_KEYNAME, &cfg_value_int);

  if (cfg_value_int)
    {
      lsm_uri_set = _lsm_uri_set_new (_STD_LSM_SIM_URI, NULL);
      g_ptr_array_add (_conf_lsm_uri_sets, lsm_uri_set);
    }

  cfg_value_int = 1;            // Enable HPSA by default.
  config_lookup_bool (cfg, _STD_LSM_CONF_HPSA_KEYNAME, &cfg_value_int);
  if (cfg_value_int)
    {
      lsm_uri_set = _lsm_uri_set_new (_STD_LSM_HPSA_URI, NULL);
      g_ptr_array_add (_conf_lsm_uri_sets, lsm_uri_set);
    }

  // Check extra_uris and extra_passwords
  ext_uris = config_lookup (cfg, _STD_LSM_CONF_EXT_URIS_KEYNAME);
  if (ext_uris && !config_setting_is_array (ext_uris))
    {
      udisks_warning ("LSM: Invalid setting of '%s' in %s",
                      _STD_LSM_CONF_EXT_URIS_KEYNAME, conf_path);
      goto out;
    }

  ext_pass = config_lookup (cfg, _STD_LSM_CONF_EXT_PASS_KEYNAME);
  if (ext_pass && !config_setting_is_array (ext_pass))
    {
      udisks_warning ("LSM: Invalid configure setting of '%s' in %s",
                      _STD_LSM_CONF_EXT_PASS_KEYNAME, conf_path);
      goto out;
    }

  if (ext_uris == NULL && ext_pass == NULL)
    {
      goto out;
    }

  if ((ext_uris && !ext_pass) || (!ext_uris && ext_pass))
    {
      udisks_warning ("LSM: Invalid configure setting: '%s' and '%s' "
                      "should be used in pair",
                      _STD_LSM_CONF_EXT_URIS_KEYNAME,
                      _STD_LSM_CONF_EXT_PASS_KEYNAME);
      goto out;
    }

  if (config_setting_length (ext_uris) != config_setting_length (ext_pass))
    {
      udisks_warning ("LSM: Invalid configure setting: the element "
                      "count of '%s' and '%s' does not match.",
                      _STD_LSM_CONF_EXT_URIS_KEYNAME,
                      _STD_LSM_CONF_EXT_PASS_KEYNAME);
      goto out;
    }

  for (i = 0; i < config_setting_length (ext_uris); ++i)
    {
      uri = config_setting_get_string_elem (ext_uris, i);
      password = config_setting_get_string_elem (ext_pass, i);

      if (strlen (uri) <= 0)
        continue;
      udisks_debug ("LSM: Fount extra URI: %s", uri);

      lsm_uri_set = _lsm_uri_set_new (uri, password);
      g_ptr_array_add (_conf_lsm_uri_sets, lsm_uri_set);
    }

out:
  if (_conf_lsm_uri_sets && _conf_lsm_uri_sets->len == 0) {
      g_ptr_array_unref (_conf_lsm_uri_sets);
      _conf_lsm_uri_sets = NULL;
  }
  config_destroy (cfg);
  g_free ((gpointer) cfg);
}

static lsm_connect *
_create_lsm_connect (struct _LsmUriSet *lsm_uri_set)
{
  lsm_connect *lsm_conn = NULL;
  lsm_error *lsm_err = NULL;
  int lsm_rc;
  const char *uri = NULL;
  const char *password = NULL;

  if (lsm_uri_set == NULL)
    {
      udisks_debug ("LSM: _create_lsm_connect (): Skip on NULL lsm_uri_set");
      return NULL;
    }
  uri = lsm_uri_set->uri;
  password = lsm_uri_set->password;

  udisks_debug ("LSM: Connecting to URI: %s", uri);
  lsm_rc = lsm_connect_password (uri, password, &lsm_conn,
                                 _STD_LSM_CONNECTION_DEFAULT_TMO,
                                 &lsm_err, LSM_CLIENT_FLAG_RSVD);
  if (lsm_rc == LSM_ERR_DAEMON_NOT_RUNNING)
    {
      udisks_warning ("LSM: The libStorageMgmt daemon is not running "
                      "(process name lsmd), try "
                      "'service libstoragemgmt start'");
      lsm_error_free (lsm_err);
      return NULL;
    }
  if (lsm_rc != LSM_ERR_OK)
    {
      udisks_warning ("LSM: Failed to connect plugin via URI '%s', "
                      "error code: %d, error message: %s",
                      uri, lsm_error_number_get (lsm_err),
                      lsm_error_message_get (lsm_err));
      lsm_error_free (lsm_err);
      return NULL;
    }
  udisks_debug ("LSM: Plugin for URI '%s' connected", uri);
  return lsm_conn;
}

/*
 * Update _supported_sys_id_hash GHashTable when system is having
 * LSM_CAP_VOLUMES and LSM_CAP_VOLUME_RAID_INFO capabilities:
 *  {
 *    system_id: TRUE;
 *  }
 * Return TRUE when provided connection has supported system, or FALSE.
 */
static gboolean
_fill_supported_system_id_hash (lsm_connect *lsm_conn)
{
  lsm_storage_capabilities *lsm_cap = NULL;
  lsm_system **lsm_syss = NULL;
  uint32_t lsm_sys_count = 0;
  int lsm_rc = 0;
  uint32_t i = 0;
  const char *lsm_sys_id = NULL;
  gboolean rc = FALSE;

  lsm_rc = lsm_system_list (lsm_conn, &lsm_syss, &lsm_sys_count,
                            LSM_CLIENT_FLAG_RSVD);

  if (lsm_rc != LSM_ERR_OK)
    {
      _handle_lsm_error ("LSM: Failed to list systems", lsm_conn);
      return rc;
    }

  if (lsm_sys_count == 0)
    {
      udisks_debug ("LSM: No system found in this lsm connection");
      return rc;
    }

  for (i = 0; i < lsm_sys_count; ++i)
    {
      lsm_sys_id = lsm_system_id_get (lsm_syss[i]);
      if ((lsm_sys_id == NULL) || (strlen (lsm_sys_id) == 0))
        {
          udisks_debug ("LSM: BUG: got NULL system ID");
          continue;
        }
      lsm_cap = NULL;
      lsm_rc = lsm_capabilities (lsm_conn, lsm_syss[i], &lsm_cap,
                                 LSM_CLIENT_FLAG_RSVD);
      if (lsm_rc != LSM_ERR_OK)
        {
          _handle_lsm_error ("LSM: error on lsm_capabilities () ",
                             lsm_conn);
          continue;
        }
      if (lsm_capability_supported (lsm_cap, LSM_CAP_VOLUMES) &&
          lsm_capability_supported (lsm_cap, LSM_CAP_VOLUME_RAID_INFO))
        {
          udisks_debug ("LSM: System '%s'(%s) is connected and supported.",
                        lsm_system_name_get (lsm_syss[i]), lsm_sys_id);
          g_hash_table_insert (_supported_sys_id_hash,
                               (gpointer) g_strdup (lsm_sys_id),
                               (gpointer) &_sys_id_supported);
          rc = TRUE;
        }
      else
        {
          udisks_debug ("LSM: System '%s'(%s) is not supporting "
                        "LSM_CAP_VOLUMES or LSM_CAP_VOLUME_RAID_INFO.",
                        lsm_system_name_get (lsm_syss[i]), lsm_sys_id);
        }
      lsm_capability_record_free (lsm_cap);
    }

  lsm_system_record_array_free (lsm_syss, lsm_sys_count);

  return rc;
}

static void
_free_lsm_volume_record (gpointer data)
{
  lsm_volume_record_free ((lsm_volume *) data);
  return;
}

/*
 * Return an array of lsm_volume which system_id is in supported_sys_id_hash.
 */
static GPtrArray *
_get_supported_lsm_volumes (lsm_connect *lsm_conn)
{
  GPtrArray *lsm_vol_array = NULL;
  lsm_volume **lsm_vols = NULL;
  lsm_volume *lsm_vol_dup = NULL;
  const char *lsm_sys_id = NULL;
  uint32_t lsm_vol_count = 0;
  int rc = 0;
  uint32_t i = 0;
  const char *lsm_vpd83;

  rc = lsm_volume_list (lsm_conn, NULL, NULL, &lsm_vols, &lsm_vol_count,
                        LSM_CLIENT_FLAG_RSVD);
  if (rc != LSM_ERR_OK)
    {
      _handle_lsm_error ("LSM: Failed to list volumes", lsm_conn);
      return NULL;
    }

  lsm_vol_array =
    g_ptr_array_new_full (0, (GDestroyNotify) _free_lsm_volume_record);

  for (i = 0; i < lsm_vol_count; ++i)
    {

      lsm_vpd83 = lsm_volume_vpd83_get (lsm_vols[i]);
      if (strlen (lsm_vpd83) == 0)
        {
          udisks_debug ("LSM: Volume %s(%s) has no VPD 83.",
                        lsm_volume_id_get (lsm_vols[i]),
                        lsm_volume_name_get (lsm_vols[i]));
          continue;
        }

      lsm_sys_id = lsm_volume_system_id_get (lsm_vols[i]);
      if (g_hash_table_lookup (_supported_sys_id_hash, lsm_sys_id) == NULL)
        {
          udisks_debug
            ("LSM: Volume VPD %s been rule out as its system is not "
             "supported", lsm_vpd83);
          continue;
        }

      lsm_vol_dup = lsm_volume_record_copy (lsm_vols[i]);
      if (lsm_vol_dup == NULL)
          // No memory, quit as g_malloc () did.
          exit (1);
      g_ptr_array_add (lsm_vol_array, lsm_vol_dup);
    }

  lsm_volume_record_array_free (lsm_vols, lsm_vol_count);
  if (lsm_vol_array->len == 0)
    {
      g_ptr_array_unref (lsm_vol_array);
      return NULL;
    }
  return lsm_vol_array;
}

static void
_free_lsm_pool_record (gpointer data)
{
  lsm_pool_record_free ((lsm_pool *) data);
  return;
}

/*
 * Return an array of lsm_pool which system_id is in supported_sys_id_hash.
 */
static GPtrArray *
_get_supported_lsm_pls (lsm_connect *lsm_conn)
{
  GPtrArray *lsm_pl_array = NULL;
  lsm_pool **lsm_pls = NULL;
  lsm_pool *lsm_pl_dup = NULL;
  const char *lsm_sys_id = NULL;
  uint32_t lsm_pl_count = 0;
  uint32_t i = 0;
  int lsm_rc;

  lsm_rc = lsm_pool_list (lsm_conn, NULL, NULL, &lsm_pls,
                          &lsm_pl_count, LSM_CLIENT_FLAG_RSVD);

  if (lsm_rc != LSM_ERR_OK)
    {
      _handle_lsm_error ("LSM: Failed to list pools", lsm_conn);
      return NULL;
    }

  lsm_pl_array =
    g_ptr_array_new_full (0, (GDestroyNotify) _free_lsm_pool_record);

  for (i = 0; i < lsm_pl_count; ++i)
    {

      lsm_sys_id = lsm_pool_system_id_get (lsm_pls[i]);
      if (g_hash_table_lookup (_supported_sys_id_hash, lsm_sys_id) == NULL)
        {
          udisks_debug
            ("LSM: Pool %s(%s) been rule out as its system is not supported",
             lsm_pool_name_get (lsm_pls[i]),
             lsm_pool_id_get (lsm_pls[i]));
          continue;
        }

      lsm_pl_dup = lsm_pool_record_copy (lsm_pls[i]);
      if (lsm_pl_dup == NULL)
        {
          // No memory, quit as g_malloc () did.
          exit (1);
        }
      g_ptr_array_add (lsm_pl_array, lsm_pl_dup);
    }
  lsm_pool_record_array_free (lsm_pls, lsm_pl_count);
  if (lsm_pl_array->len == 0)
    {
      g_ptr_array_unref (lsm_pl_array);
      return NULL;
    }
  return lsm_pl_array;
}

static void
_fill_pl_id_2_lsm_pl_data_hash (GPtrArray *lsm_pl_array,
                                gint64 last_refresh_time)
{
  struct _LsmPlData *lsm_pl_data = NULL;
  lsm_pool *lsm_pl = NULL;
  const char *pl_id = NULL;
  const char *orig_pl_id = NULL;
  struct _LsmPlData *orig_lsm_pl_data = NULL;
  guint i;

  for (i = 0; i < lsm_pl_array->len; ++i)
    {
      lsm_pl = g_ptr_array_index (lsm_pl_array, i);
      pl_id = lsm_pool_id_get (lsm_pl);
      if ((pl_id == NULL) || (strlen (pl_id) == 0))
        continue;

      /* Overide old data  */
      g_hash_table_lookup_extended (_pl_id_2_lsm_pl_data_hash, pl_id,
                                    (gpointer *) &orig_pl_id,
                                    (gpointer *) &orig_lsm_pl_data);
      if (orig_pl_id != NULL)
        g_hash_table_remove (_pl_id_2_lsm_pl_data_hash,
                             (gconstpointer) orig_pl_id);

      lsm_pl_data = (struct _LsmPlData *)
        g_malloc (sizeof (struct _LsmPlData));

      _fill_lsm_pl_data (lsm_pl_data, lsm_pl, last_refresh_time);
      g_hash_table_insert (_pl_id_2_lsm_pl_data_hash,
                           (gpointer) g_strdup (pl_id),
                           (gpointer) lsm_pl_data);
    }
}

/*
 * Use lsm_conn_data to fill in these hash tables to speed up the future
 * search:
 *    _vpd83_2_lsm_conn_data_hash
 *    _pl_id_2_lsm_pl_data_hash
 */
static void
_fill_vpd83_2_lsm_conn_data_hash (lsm_connect *lsm_conn,
                                  GPtrArray *lsm_vol_array)
{
  struct _LsmConnData *lsm_conn_data = NULL;
  lsm_volume *lsm_vol = NULL;
  const char *pl_id = NULL;
  const char *vpd83 = NULL;
  guint i;

  for (i = 0; i < lsm_vol_array->len; ++i)
    {
      lsm_vol = g_ptr_array_index (lsm_vol_array, i);
      if (lsm_vol == NULL)
        continue;

      vpd83 = lsm_volume_vpd83_get (lsm_vol);
      if ((vpd83 == NULL) || (strlen (vpd83) == 0))
        continue;

      pl_id = lsm_volume_pool_id_get (lsm_vol);

      if ((pl_id == NULL) || (strlen (pl_id) == 0))
        continue;

      lsm_conn_data = (struct _LsmConnData *)
        g_malloc (sizeof (struct _LsmConnData));

      lsm_conn_data->lsm_conn = lsm_conn;
      lsm_conn_data->lsm_vol = lsm_volume_record_copy (lsm_vol);
      if (lsm_conn_data->lsm_vol == NULL)
        exit (1);   // No memory
      lsm_conn_data->pl_id = g_strdup (pl_id);

      g_hash_table_insert (_vpd83_2_lsm_conn_data_hash, g_strdup (vpd83),
                           lsm_conn_data);
    }
}

static void
_fill_lsm_pl_data (struct _LsmPlData *lsm_pl_data, lsm_pool *lsm_pl,
                   gint64 last_refresh_time)
{
  const char *lsm_pl_status_info = NULL;
  uint64_t lsm_pl_status = 0;

  lsm_pl_status = lsm_pool_status_get (lsm_pl);
  lsm_pl_status_info = lsm_pool_status_info_get (lsm_pl);

  lsm_pl_data->last_refresh_time = last_refresh_time;
  lsm_pl_data->status_info = g_strdup (lsm_pl_status_info);

  if (lsm_pl_status & LSM_POOL_STATUS_OK)
    {
      lsm_pl_data->is_ok = TRUE;
    }
  else
    {
      lsm_pl_data->is_ok = FALSE;
    }

  if (lsm_pl_status & LSM_POOL_STATUS_DEGRADED)
    {
      lsm_pl_data->is_raid_degraded = TRUE;
      lsm_pl_data->is_ok = FALSE;
    }
  else
    {
      lsm_pl_data->is_raid_degraded = FALSE;
    }

  if (lsm_pl_status & LSM_POOL_STATUS_ERROR)
    {
      lsm_pl_data->is_raid_error = TRUE;
      lsm_pl_data->is_ok = FALSE;
    }
  else
    {
      lsm_pl_data->is_raid_error = FALSE;
    }

  if (lsm_pl_status & LSM_POOL_STATUS_VERIFYING)
    {
      lsm_pl_data->is_raid_verifying = TRUE;
      lsm_pl_data->is_ok = FALSE;
    }
  else
    {
      lsm_pl_data->is_raid_verifying = FALSE;
    }

  if (lsm_pl_status & LSM_POOL_STATUS_RECONSTRUCTING)
    {
      lsm_pl_data->is_raid_reconstructing = TRUE;
      lsm_pl_data->is_ok = FALSE;
    }
  else
    {
      lsm_pl_data->is_raid_reconstructing = FALSE;
    }
  return;
}


/*
 * Refresh _LsmVriData in _vpd83_2_lsm_vri_data_hash for certain VPD83.
 * If volume has been delete, update _vpd83_2_lsm_conn_data_hash also to
 * reflect that.
 */
static struct _LsmVriData *
_refresh_lsm_vri_data (struct _LsmConnData *lsm_conn_data,
                       const char *vpd83)
{
  struct _LsmVriData *lsm_vri_data = NULL;
  const char *orig_vpd83 = NULL;
  struct _LsmVriData *orig_lsm_vri_data = NULL;
  struct _LsmConnData *orig_lsm_conn_data = NULL;
  lsm_volume_raid_type raid_type;
  uint32_t strip_size, disk_count, min_io_size, opt_io_size;
  int lsm_rc;

  // Remove _vpd83_2_lsm_vri_data_hash old entry
  g_hash_table_lookup_extended (_vpd83_2_lsm_vri_data_hash, vpd83,
                                (gpointer *) &orig_vpd83,
                                (gpointer *) &orig_lsm_vri_data);
  if (orig_vpd83 != NULL)
    g_hash_table_remove (_vpd83_2_lsm_vri_data_hash,
                         (gconstpointer) orig_vpd83);

  lsm_rc = lsm_volume_raid_info (lsm_conn_data->lsm_conn,
                                 lsm_conn_data->lsm_vol, &raid_type,
                                 &strip_size, &disk_count, &min_io_size,
                                 &opt_io_size, LSM_CLIENT_FLAG_RSVD);

  if (lsm_rc != LSM_ERR_OK)
    {
      if (lsm_rc == LSM_ERR_NOT_FOUND_VOLUME)
        udisks_debug ("LSM: Volume %s deleted", vpd83);
      else
        _handle_lsm_error ("LSM: Failed to retrieve RAID information "
                           "of volume", lsm_conn_data->lsm_conn);

      // Remove _vpd83_2_lsm_conn_data_hash entry
      g_hash_table_lookup_extended (_vpd83_2_lsm_conn_data_hash, vpd83,
                                    (gpointer *) &orig_vpd83,
                                    (gpointer *) &orig_lsm_conn_data);

      if (orig_vpd83 != NULL)
        g_hash_table_remove (_vpd83_2_lsm_conn_data_hash,
                             (gconstpointer) orig_vpd83);

      return NULL;
    }

  lsm_vri_data = (struct _LsmVriData *) g_malloc (sizeof (struct _LsmVriData));
  lsm_vri_data->raid_type_str =
    g_strdup (_lsm_raid_type_to_str (raid_type));
  lsm_vri_data->min_io_size = min_io_size;
  lsm_vri_data->opt_io_size = opt_io_size;
  lsm_vri_data->raid_disk_count = disk_count;
  lsm_vri_data->last_refresh_time = g_get_monotonic_time ();

  g_hash_table_insert (_vpd83_2_lsm_vri_data_hash, g_strdup (vpd83),
                       lsm_vri_data);

  return lsm_vri_data;
}

/*
 * Check _pl_id_2_lsm_pl_data_hash and _vpd83_2_lsm_conn_data_hash hash table
 * to find out the struct _LsmPlData.
 * If data is outdated, try to update it.
 */
static struct _LsmPlData *
_lsm_pl_data_lookup (const char *vpd83)
{
  struct _LsmConnData *lsm_conn_data = NULL;
  struct _LsmPlData *lsm_pl_data = NULL;
  GPtrArray *new_lsm_pl_array = NULL;
  gint64 current_time = 0;
  gint64 refresh_interval = 0;
  const char *orig_pl_id;
  struct _LsmPlData *orig_lsm_pl_data;

  refresh_interval = std_lsm_refresh_time_get ();

  if ((_vpd83_2_lsm_conn_data_hash == NULL) ||
      (_pl_id_2_lsm_pl_data_hash == NULL))
    return NULL;

  lsm_conn_data = g_hash_table_lookup (_vpd83_2_lsm_conn_data_hash, vpd83);

  if ((lsm_conn_data == NULL) || (lsm_conn_data->pl_id == NULL))
    return NULL;

  lsm_pl_data = g_hash_table_lookup (_pl_id_2_lsm_pl_data_hash,
                                     lsm_conn_data->pl_id);

  if (lsm_pl_data == NULL)
    return NULL;

  current_time = g_get_monotonic_time ();

  if ((current_time - lsm_pl_data->last_refresh_time) / 1000000
      < refresh_interval)
    return lsm_pl_data;

  // Refresh data is required.
  udisks_debug ("LSM: Refreshing Pool(id %s) data", lsm_conn_data->pl_id);
  new_lsm_pl_array = _get_supported_lsm_pls (lsm_conn_data->lsm_conn);
  _fill_pl_id_2_lsm_pl_data_hash (new_lsm_pl_array, current_time);
  g_ptr_array_unref (new_lsm_pl_array);

  // Search again
  lsm_pl_data = g_hash_table_lookup (_pl_id_2_lsm_pl_data_hash,
                                     lsm_conn_data->pl_id);

  if (_pl_id_2_lsm_pl_data_hash == NULL)
    // Normally old data should not be delete yet.
    return NULL;

  if (lsm_pl_data->last_refresh_time != current_time)
    {
      udisks_debug ("LSM: _lsm_pl_data_lookup: pool deleted");

      // Pool got deleted, we should delete the old data
      g_hash_table_lookup_extended (_pl_id_2_lsm_pl_data_hash,
                                    lsm_conn_data->pl_id,
                                    (gpointer *) &orig_pl_id,
                                    (gpointer *) &orig_lsm_pl_data);
      if (orig_pl_id != NULL)
        g_hash_table_remove (_pl_id_2_lsm_pl_data_hash,
                             (gconstpointer) orig_pl_id);
      return NULL;
    }
  return lsm_pl_data;
}

/*
 * Search _vpd83_2_lsm_vri_data_hash, if not found or outdated, update it.
 */
static struct _LsmVriData *
_lsm_vri_data_lookup (const char *vpd83)
{
  struct _LsmConnData *lsm_conn_data = NULL;
  struct _LsmVriData *lsm_vri_data = NULL;
  gint64 current_time = 0;
  gint64 refresh_interval = 0;

  refresh_interval = std_lsm_refresh_time_get ();

  if (_vpd83_2_lsm_conn_data_hash == NULL)
    return NULL;

  lsm_conn_data = g_hash_table_lookup (_vpd83_2_lsm_conn_data_hash, vpd83);

  if (lsm_conn_data == NULL)
    return NULL;

  lsm_vri_data = g_hash_table_lookup (_vpd83_2_lsm_vri_data_hash, vpd83);

  current_time = g_get_monotonic_time ();

  if ((lsm_vri_data != NULL) &&
      ((current_time - lsm_vri_data->last_refresh_time) / 1000000
        < refresh_interval))
    return lsm_vri_data;

  //Refresh data is required.
  udisks_debug ("LSM: Refreshing VRI data for %s", vpd83);
  return _refresh_lsm_vri_data (lsm_conn_data, vpd83);
}

static const char *
_lsm_get_conf_path (UDisksDaemon *daemon)
{
  gboolean uninstalled = udisks_daemon_get_uninstalled (daemon);

  _std_lsm_conf_file_abs_path = g_build_path (G_DIR_SEPARATOR_S,
                                              uninstalled ? BUILD_DIR : _STD_LSM_CONF_PATH_PREFIX,
                                              _STD_LSM_CONF_PATH,
                                              _STD_LSM_CONF_FILE,
                                              NULL);

  return (const char *) _std_lsm_conf_file_abs_path;
}

static void
_free_lsm_connect (gpointer data)
{
  lsm_connect_close ((lsm_connect *) data, LSM_CLIENT_FLAG_RSVD);
}

static void
_free_lsm_uri_set (gpointer data)
{
  struct _LsmUriSet *lsm_uri_set = (struct _LsmUriSet *) data;
  if (lsm_uri_set != NULL)
    {
      g_free ((gpointer) lsm_uri_set->uri);
      g_free ((gpointer) lsm_uri_set->password);
    }
  g_free ((gpointer) lsm_uri_set);
}

static void
_free_lsm_conn_data (gpointer data)
{
  struct _LsmConnData *lsm_conn_data = (struct _LsmConnData *) data;

  if (lsm_conn_data != NULL)
    {
        lsm_volume_record_free (lsm_conn_data->lsm_vol);
        g_free ((gpointer) lsm_conn_data->pl_id);
        g_free ((gpointer) lsm_conn_data);
    }
}

static void
_free_lsm_pl_data (gpointer data)
{
  struct _LsmPlData *lsm_pl_data = (struct _LsmPlData *) data;

  if (lsm_pl_data != NULL)
    {
      g_free ((gpointer) lsm_pl_data->status_info);
      g_free ((gpointer) lsm_pl_data);
    }
}

static void
_free_lsm_vri_data (gpointer data)
{
  struct _LsmVriData *lsm_vri_data = (struct _LsmVriData *) data;

  if (lsm_vri_data != NULL)
    {
      g_free ((gpointer) lsm_vri_data->raid_type_str);
      g_free ((gpointer) lsm_vri_data);
    }
}

void
std_lsm_data_init (UDisksDaemon *daemon)
{
  struct _LsmUriSet *lsm_uri_set = NULL;
  lsm_connect *lsm_conn = NULL;
  GPtrArray *lsm_vol_array = NULL;
  GPtrArray *lsm_pl_array = NULL;
  guint i = 0;
  gboolean rc = FALSE;

  _load_module_conf (daemon);
  if (_conf_lsm_uri_sets == NULL)
    {
      udisks_warning ("LSM: No URI found in config file %s",
                      _lsm_get_conf_path (daemon));
      return;
    }

  _all_lsm_conn_array =
    g_ptr_array_new_full (0, (GDestroyNotify) _free_lsm_connect);

  _vpd83_2_lsm_conn_data_hash =
      g_hash_table_new_full (g_str_hash, g_str_equal,
                            (GDestroyNotify) g_free,
                            (GDestroyNotify) _free_lsm_conn_data);

  _pl_id_2_lsm_pl_data_hash =
      g_hash_table_new_full (g_str_hash, g_str_equal,
                            (GDestroyNotify) g_free,
                            (GDestroyNotify) _free_lsm_pl_data);

  _vpd83_2_lsm_vri_data_hash =
      g_hash_table_new_full (g_str_hash, g_str_equal,
                             (GDestroyNotify) g_free,
                             (GDestroyNotify) _free_lsm_vri_data);

  _supported_sys_id_hash =
      g_hash_table_new_full (g_str_hash, g_str_equal,
                             (GDestroyNotify) g_free,
                             NULL);

  for (i =0; i < _conf_lsm_uri_sets->len; ++i)
    {
      lsm_uri_set = g_ptr_array_index (_conf_lsm_uri_sets, i);
      lsm_conn = _create_lsm_connect (lsm_uri_set);
      if (lsm_conn == NULL)
        continue;
      rc = _fill_supported_system_id_hash (lsm_conn);
      if (rc != TRUE)
        {
          lsm_connect_close (lsm_conn, LSM_CLIENT_FLAG_RSVD);
          continue;
        }
      g_ptr_array_add (_all_lsm_conn_array, lsm_conn);

      lsm_vol_array = _get_supported_lsm_volumes (lsm_conn);
      if (lsm_vol_array == NULL)
        {
          continue;
        }
      lsm_pl_array = _get_supported_lsm_pls (lsm_conn);

      _fill_pl_id_2_lsm_pl_data_hash (lsm_pl_array, g_get_monotonic_time ());
      _fill_vpd83_2_lsm_conn_data_hash (lsm_conn, lsm_vol_array);
      g_ptr_array_unref (lsm_vol_array);
      g_ptr_array_unref (lsm_pl_array);
    }
}

uint32_t
std_lsm_refresh_time_get (void)
{
  return _conf_refresh_interval;
}

/*
 * Return struct StdLsmVolData for given VPD83.
 * The memory should be freeed by std_lsm_vol_data_free ().
 */
struct StdLsmVolData *
std_lsm_vol_data_get (const char *vpd83)
{
  struct StdLsmVolData *std_lsm_vol_data = NULL;
  struct _LsmPlData *lsm_pl_data = NULL;
  struct _LsmVriData *lsm_vri_data = NULL;

  lsm_pl_data = _lsm_pl_data_lookup (vpd83);
  if (lsm_pl_data == NULL)
    goto out;

  lsm_vri_data = _lsm_vri_data_lookup (vpd83);
  if (lsm_vri_data == NULL)
    goto out;

  std_lsm_vol_data = (struct StdLsmVolData *) g_malloc
    (sizeof (struct StdLsmVolData));

  strncpy (std_lsm_vol_data->raid_type, lsm_vri_data->raid_type_str,
           _MAX_RAID_TYPE_LEN);
  std_lsm_vol_data->raid_type[_MAX_RAID_TYPE_LEN - 1] = '\0';

  strncpy (std_lsm_vol_data->status_info, lsm_pl_data->status_info,
           _MAX_STATUS_INFO_LEN);

  std_lsm_vol_data->status_info[_MAX_STATUS_INFO_LEN - 1] = '\0';

  std_lsm_vol_data->is_raid_degraded = lsm_pl_data->is_raid_degraded;
  std_lsm_vol_data->is_raid_reconstructing =
    lsm_pl_data->is_raid_reconstructing;
  std_lsm_vol_data->is_raid_verifying = lsm_pl_data->is_raid_verifying;
  std_lsm_vol_data->is_raid_error = lsm_pl_data->is_raid_error;
  std_lsm_vol_data->is_ok = lsm_pl_data->is_ok;
  std_lsm_vol_data->min_io_size = lsm_vri_data->min_io_size;
  std_lsm_vol_data->opt_io_size = lsm_vri_data->opt_io_size;
  std_lsm_vol_data->raid_disk_count = lsm_vri_data->raid_disk_count;

out:
  return std_lsm_vol_data;

}

void
std_lsm_vol_data_free (struct StdLsmVolData *std_lsm_vol_data)
{
  g_free ((gpointer) std_lsm_vol_data);
}

void
std_lsm_data_teardown (void)
{
  g_ptr_array_unref (_conf_lsm_uri_sets);
  _conf_lsm_uri_sets = NULL;

  g_hash_table_unref (_supported_sys_id_hash);
  _supported_sys_id_hash = NULL;

  g_ptr_array_unref (_all_lsm_conn_array);
  _all_lsm_conn_array = NULL;

  g_hash_table_unref (_vpd83_2_lsm_conn_data_hash);
  _vpd83_2_lsm_conn_data_hash = NULL;

  g_hash_table_unref (_vpd83_2_lsm_vri_data_hash);
  _vpd83_2_lsm_vri_data_hash = NULL;

  g_hash_table_unref (_pl_id_2_lsm_pl_data_hash);
  _pl_id_2_lsm_pl_data_hash = NULL;

  g_free ((gpointer) _std_lsm_conf_file_abs_path);
  _std_lsm_conf_file_abs_path = NULL;
}

void
std_lsm_vpd83_list_refresh (void)
{
  lsm_connect *lsm_conn = NULL;
  GPtrArray *lsm_pl_array = NULL;
  GPtrArray *lsm_vol_array = NULL;
  guint i;

  udisks_debug ("LSM: std_lsm_vpd83_list_refresh ()");

  if (_all_lsm_conn_array == NULL )
    return;

  /* free old data:
   *  _vpd83_2_lsm_conn_data_hash
   *  _pl_id_2_lsm_pl_data_hash
   */
  g_hash_table_remove_all (_vpd83_2_lsm_conn_data_hash);
  g_hash_table_remove_all (_pl_id_2_lsm_pl_data_hash);

  for (i = 0; i < _all_lsm_conn_array->len; ++i)
    {
      lsm_conn = g_ptr_array_index (_all_lsm_conn_array, i);
      if (lsm_conn == NULL)
        continue;

      lsm_vol_array = _get_supported_lsm_volumes (lsm_conn);
      if (lsm_vol_array == NULL)
        continue;
      lsm_pl_array = _get_supported_lsm_pls (lsm_conn);

      _fill_pl_id_2_lsm_pl_data_hash (lsm_pl_array, g_get_monotonic_time ());
      _fill_vpd83_2_lsm_conn_data_hash (lsm_conn, lsm_vol_array);
      g_ptr_array_unref (lsm_vol_array);
      g_ptr_array_unref (lsm_pl_array);
    }
}

gboolean
std_lsm_vpd83_is_managed (const char *vpd83)
{
  if ((vpd83 != NULL) &&
      (_vpd83_2_lsm_conn_data_hash != NULL) &&
      g_hash_table_lookup (_vpd83_2_lsm_conn_data_hash, vpd83))
    return TRUE;
  return FALSE;
}
