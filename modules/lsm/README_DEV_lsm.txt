## How to setup a develop environment for lsm module using ONTAP simulator.
 * You can use a HP SmartArray server as an develop environment which is
   much easier to setup, but it's not that flexible like ONTAP simulator
   for testing LUN create/remove and etc. Check manpage hpsa_lsmplugin(1)
   from libstoragemgmt-hpsa-plugin if you intend to set HP SmartArray for
   libstoragemgmt.
 * Setup an NetApp ONTAP simulator.
 * Enable http administrator way ONTAP:
      options httpd.enable on
      options httpd.admin.enable on
      options httpd.admin.ssl.enable on
 * Assuming ONTAP simulator has dns name as 'na-sim'. Admin account is 'root'.
 * Follow some guide to create a LUN and map to a iscsi initiator, or these
   commands:
        aggr create test_ag -t raid_dp 6       # 6 disk RAID 6.
        vol create /vol/test_ag test_ag 1G
        iscsi start
        igroup create -i -t linux gris-test iqn.1994-05.com.redhat:gris-st-lsm-test
        lun create -t linux -s 4m /vol/test_vg/lun1
        lun map /vol/test_vg/lun1 gris-test
 * On your developer host or VM. Setup the iscsi initiator.
        sudo yum install iscsi-initiator-utils -y
        echo 'InitiatorName=iqn.1994-05.com.redhat:gris-st-lsm-test' | \
            sudo tee /etc/iscsi/initiatorname.iscsi
        iscsiadm -m discovery -t st -p na-sim
        iscsiadm -m node -l
 * Install libstoragemgmt-devel 1.2+ and libstoragemgmt-netapp-plugin.
 * Try these command to test whether libstoragemgmt works:
    systemctl start libstoragemgmt
    lsmcli ls -l -u ontap://root@na-sim -P      # Input ONTAP password.
 * It should print some information about this ONTAP array.
 * Compile and install Udisks project with lsm module:
    ./autogen.sh  --libdir=/usr/lib64 --prefix=/usr  --sysconfdir /etc \
        --enable-lsm
    make -j5
    sudo make install
 * Update /etc/udisks2/modules.conf.d/udisks2_lsm.conf to hold these lines:
    extra_uris = ["ontap://root@na-sim"]
    extra_passwords = ["your_ontap_password"]
 * Run udisksd with lsm module loading:
    sudo valgrind --show-leak-kinds=all --leak-check=full \
        --trace-children=yes --show-reachable=no
        --log-file=/tmp/mem_check.log  /usr/libexec/udisks2/udisksd
        --force-load-modules
 * Use qdbus(or other tool) to query dbus interface:
    sudo qdbus --system org.freedesktop.UDisks2
 * Use "dbus-monitor --system" command to monitor dbus signals.

## Files
* lsm_module_iface.c

    Implemented the public module public method defined by
    modules/udisksmoduleifacetypes.h

* lsm_data.c lsm_data.h

    Use LibStorageMgmt API to query RAID info. Lock is used to protect
    static data.

* lsm_linux_drive.c

    Handling interface data fill in and update then using glib main loop
    event.

* lsm_linux_manager.c

    Empty interface for org.freedesktop.UDisks2.Manager.LSM.

* lsm_types.h

    Methods used by lsm_module_iface.c but implemented in
    lsm_linux_drive.c and lsm_linux_manager.c.

## Workflow

* udisks_module_manager_load_modules () of
  src/udisksmodulemanager.c

    Check whether libudisks2_lsm.so has required g_module_symbol () in
    udisks_module_manager_load_modules ().

* udisks_linux_provider_start () of src/udiskslinuxprovider.c

    If module is loaded __after__ udisksd daemon, only 1 codeplugin
    will be kicked off by ensure_modules ();

    If module is enabled via command option --force-load-modules
    __3__ times coldplug (generate add udev event on all devices) will be
    performed:

      * ensure_modules () invoke codeplug.
      * udisks_linux_provider_start () do twice codeplugs.

    The codeplug just generate add udev event on all devices which
    will then handled by udisks_linux_drive_object_uevent ()
    of src/udiskslinuxdriveobject.c in our case.

* The add udev event of disk drive was handled by
  udisks_linux_drive_object_uevent () of src/udiskslinuxdriveobject.c.

* Then update_iface () of src/udiskslinuxdriveobject.c invoke module
  functions:
    * _drive_check () of lsm_module_iface.c:
        If return TRUE, the dbus org.freedesktop.UDisks2.Drive.LSM interface
        will be created.
        In order to refresh the lsm cache when new disk attached,
        in this method, we do extra refresh if not found in cache.
        Do extra check like about skipping CD ROM, skipping first empty
        codeplug, and validating vpd83.
    * _drive_connect () of lsm_module_iface.c:
        No idea what this for. We just return void.
    * _drive_update () of lsm_module_iface.c:
        For add udev event, invoke udisks_linux_drive_lsm_update () of
        lsm_linux_drive.c which create loop event to refresh cache at
        certain interval (configurable).
        For remove udev event, just g_object_unref ().
    * _on_refresh_data () of lsm_linux_drive.c:
        Triggered by glib loop event, refresh raid info via
        std_lsm_vol_data_get () of lsm_data.c.

## LSM Connection internal data layout -- lsm_data.c

This module methods will not run in multiple threads, we don't have
mutex lock to protect static data.

Data layout:


_conf_lsm_uri_sets =  [struct _LsmConnData * ];

    create:     _load_module_conf ()
                        |
                        v
                    _lsm_uri_set_new ()
    free:       std_lsm_data_teardown ()
                        |
                        v
                    g_ptr_array_unref ()
                        |
                        v
                    _free_lsm_uri_set ()
    used by:    _create_lsm_connect ()
    notes:      Might allow user to define timeout and etc in the future.

_all_lsm_conn_array = [lsm_connect *];

    create:     std_lsm_data_init ()
                        |
                        v
                    _create_lsm_connect ()
    free:       std_lsm_data_teardown ()
                        |
                        v
                    g_ptr_array_unref ()
                        |
                        v
                    _free_lsm_connect ()
    used by:    std_lsm_vpd83_list_refresh ()
    notes:      As we are save lsm_connect pointer in many spaces,
                we need this array for clean up and VPD83 cache refresh.

_supported_sys_id_hash = {
    const char *sys_id => static const gboolean _sys_id_supported;
}

    create:     std_lsm_data_init ()
                        |
                        v
                    _fill_supported_system_id_hash ()
    free:       std_lsm_data_teardown ()
                        |
                        v
                    g_hash_table_unref ()
                        |
                        v
                    g_free ()
    used by:    _get_supported_lsm_volumes ()
                _get_supported_lsm_pls ()
    notes:      Save the supported system.id to filter out unsupported
                volumes and pools.

_vpd83_2_lsm_conn_data_hash = {
    const char *vpd83 => struct _LsmConnData *;
};

    create:     std_lsm_data_init ()
                        |
                        v
                    _fill_vpd83_2_lsm_conn_data_hash ()
    free:       std_lsm_data_teardown ()
                        |
                        v
                    g_hash_table_unref ()
                        |
                        v
                    _free_lsm_conn_data ()
    used by:    _lsm_pl_data_lookup ()
                _lsm_vri_data_lookup ()
                _refresh_lsm_vri_data ()
    notes:      Cache the required data for lsm_volume_raid_info (),
                lsm_pools () and methods.

_pl_id_2_lsm_pl_data_hash = {
    const char *pl_id => struct _LsmPlData *;
}

    create:     std_lsm_data_init ()
                        |
                        v
                    _fill_pl_id_2_lsm_pl_data_hash ()
    free:       std_lsm_data_teardown ()
                        |
                        v
                    g_hash_table_unref ()
                        |
                        v
                    _free_lsm_pl_data ()
    used by:    _lsm_pl_data_lookup ()
    notes:      Cache the pool status to avoid duplicate wasted call.

_vpd83_2_lsm_vri_data_hash = {
    const char *vpd83 => struct _LsmVriData *;
}

    create:     _lsm_vri_data_lookup ()
                        |
                        v
                    _refresh_lsm_vri_data ()
    free:       std_lsm_data_teardown ()
                        |
                        v
                    g_hash_table_unref ()
                        |
                        v
                    _free_lsm_vri_data ()
    used by:    _lsm_pl_data_lookup ()
    notes:      Cache the lsm_volume_raid_info () returned data.


* std_lsm_data_init ():
    * Read configuration -- _read_module_conf ().
    * Create LSM connection for each URI. -- _create_lsm_connect ().
        * Invoke lsm_system_list () and lsm_capabilities() to check
          LSM_CAP_VOLUMES and LSM_CAP_VOLUME_RAID_INFO capabilities.

            _get_supported_lsm_systems ()

        * Invoke lsm_volumes_list () and lsm_pool_list().

            _get_supported_lsm_volumes ()
            _get_supported_lsm_pools ()

        * Create these static hash tables:
            * _lsm_conn_data_array
            * _vpd83_2_lsm_conn_data_hash
            * _pl_id_2_lsm_pl_data_hash
            * _vpd83_2_lsm_vri_data_hash
              # ^ this will be fill in when requested via
              # std_lsm_vol_data_get ().

* std_lsm_vol_data_get ():
    Combine data retrieved from _lsm_pl_data_lookup () and
    _lsm_vri_data_lookup () to generate struct StdLsmVolData *.

* std_lsm_data_teardown ():
    * Free memorys and close connections.

## TODO
* Add extra sections to the documentation, currently we only have dbus
  interface documentation.
