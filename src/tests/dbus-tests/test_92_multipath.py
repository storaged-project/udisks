import os
import dbus
import udiskstestcase
import subprocess
import time

MP_IFACE = '.Multipath'
PATH_IFACE = '.Multipath.Path'

# TODO(Gris Ge): Add test cases to simulator disk offline/resume,
#                mpath removal and etc.

class UdisksMultipathTestCase(udiskstestcase.UdisksTestCase):
    '''
    Test suit for multipath plugin
    '''
    def _assert_prop_is_not_empty_str(self, obj, interface, prop_name):
        string = self.get_property(obj, interface, prop_name).value

        if string is None or len(string) == 0:
            self.fail('Empty property: \'%s\'' % prop_name)

    def _assert_prop_is_not_zero(self, obj, interface, prop_name):
        value = self.get_property(obj, interface, prop_name).value

        if value is None or value == 0:
            self.fail('Property is zero: \'%s\'' % prop_name)

    @classmethod
    def tearDown(cls):
        subprocess.call(['systemctl', 'stop', 'multipathd.service'])
        subprocess.call(['multipath', '-F'])
        subprocess.call(['modprobe', '-r', 'scsi_debug'])

    @classmethod
    def setUpClass(cls):
        assert subprocess.call(['modprobe', 'scsi_debug', 'dev_size_mb=10',
                                'vpd_use_hostno=0', 'add_host=8', 'max_luns=2']
                               ) == 0
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('Multipath'):
            raise unittest.SkipTest(
                'Udisks module for multipath tests not loaded, skipping.')
        with open('/etc/multipath.conf', 'w') as mp_conf_fd:
            mp_conf_fd.write('''
defaults {
    user_friendly_names     yes
    find_multipaths         yes
}''')
        assert subprocess.call(['systemctl', 'restart',
                                'multipathd.service']) == 0
        time.sleep(10)   # Wait multipath daemon to start up

    def _get_dbus_drv_obj(self, dbus_blk_obj):
        obj_path = self.get_property_raw(dbus_blk_obj, '.Block', 'Drive')
        return self.bus.get_object(self.iface_prefix, obj_path)

    def _test_blk_mp(self, mp_obj):
        mp_blk_obj_path = self.get_property(
            mp_obj, '.Multipath', 'Block').value
        mp_blk_obj = self.get_object(mp_blk_obj_path)
        tmp_mp_obj_path = self.get_property(
            mp_blk_obj, '.Block.Multipath', 'Multipath').value
        if tmp_mp_obj_path != mp_obj.object_path:
            self.fail("BUG: The block object %s is hold the incorrect "
                      "Multipath property: %s" %
                      (mp_obj.object_path, tmp_mp_obj_path))

    def _test_drv_mp(self, mp_obj):
        mp_drv_obj_path = self.get_property(
            mp_obj, '.Multipath', 'Drive').value
        mp_drv_obj = self.get_object(mp_drv_obj_path)
        tmp_mp_obj_path = self.get_property(
            mp_drv_obj, '.Drive.Multipath', 'Multipath').value
        if tmp_mp_obj_path != mp_obj.object_path:
            self.fail("BUG: The drive object %s is hold the incorrect "
                      "Multipath property: %s" %
                      (mp_obj.object_path, tmp_mp_obj_path))

    def _test_mp_path(self, mp_obj, mp_path_obj_path):
        mp_path_obj = self.get_object(mp_path_obj_path)
        self._assert_prop_is_not_empty_str(mp_path_obj, PATH_IFACE, 'Status')
        self._assert_prop_is_not_empty_str(mp_path_obj, PATH_IFACE, 'Name')
        mp_obj_path = self.get_property(
            mp_path_obj, PATH_IFACE, 'Multipath').value
        if mp_obj_path != mp_obj.object_path:
            self.fail("BUG: The path object %s is holding the "
                      "incorrect 'Multipath' property: %s" %
                      (mp_obj.object_path, mp_obj_path))
        blk_obj_path = self.get_property(
            mp_path_obj, PATH_IFACE, 'Block').value
        blk_obj = self.get_object(blk_obj_path)
        mp_obj_path = self.get_property(
            blk_obj, ".Block.Multipath", "Multipath").value
        if mp_obj_path != mp_obj.object_path:
            self.fail("BUG: The path object %s is holding the "
                      "incorrect 'Multipath' property: %s" %
                      (blk_obj.object_path, mp_obj_path))

    def test_mp_init_query(self):
        '''
        Tests the properties query from scsi_debug mpaths.
        '''
        mp_obj_path = None
        dbus_blk_obj = None

        manager_obj = self.get_object('/Manager')

        if manager_obj is None:
            self.fail("BUG: Failed to get manager")

        mp_obj_paths = manager_obj.GetAllMultipaths(
            dbus_interface=self.iface_prefix + '.Manager.Multipath')

        if len(mp_obj_paths) == 0:
            self.fail('No multipath found')

        for mp_obj_path in mp_obj_paths:
            mp_obj = self.get_object(mp_obj_path)
            self._assert_prop_is_not_empty_str(mp_obj, MP_IFACE, 'Block')
            self._assert_prop_is_not_empty_str(mp_obj, MP_IFACE, 'WWID')
            self._assert_prop_is_not_empty_str(mp_obj, MP_IFACE, 'Name')
            self._test_blk_mp(mp_obj)
            self._test_drv_mp(mp_obj)
            mp_path_obj_paths = self.get_property(
                mp_obj, '.Multipath', 'Paths').value
            for mp_path_obj_path in mp_path_obj_paths:
                self._test_mp_path(mp_obj, mp_path_obj_path)
