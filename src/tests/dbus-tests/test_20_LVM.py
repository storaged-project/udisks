import unittest
import storagedtestcase
import dbus
import os
import subprocess
import time

class StoragedLVMTest(storagedtestcase.StoragedTestCase):
    '''This is a basic LVM test suite'''

    def _create_vg(self, vgname, devices):
        self.udev_settle() # Since the devices might not be ready yet
        manager = self.get_object('', '/Manager')
        vg_path = manager.VolumeGroupCreate(vgname, devices, self.no_options,
                dbus_interface=self.iface_prefix + '.Manager.LVM2')
        self.udev_settle()
        time.sleep(0.5)
        vg = self.bus.get_object(self.iface_prefix, vg_path)
        self.assertIsNotNone(vg)
        self.assertEqual(subprocess.call(['vgs', vgname]), 0)
        return vg


    def _remove_vg(self, vg):
        vgname = self.get_property(vg, '.VolumeGroup', 'Name')
        vg.Delete(True, self.no_options, dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.udev_settle()
        self.assertNotEqual(subprocess.call(['vgs', vgname]), 0)


    def test_10_linear(self):
        '''Test linear (plain) LV functionality'''

        vgname = 'storaged_test_vg'

        # Use all the virtual devices but the last one
        devs = dbus.Array()
        for d in self.vdevs[:-1]:
            dev_obj = self.get_object('', '/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)
        vg = self._create_vg(vgname, devs)

        # Create linear LV on the VG
        vgsize = int(self.get_property(vg, '.VolumeGroup', 'Size'))
        vg_freesize = int(self.get_property(vg, '.VolumeGroup', 'FreeSize'))
        self.assertEqual(vgsize, vg_freesize)
        lvname = 'storaged_test_lv'
        lv_path = vg.CreatePlainVolume(lvname, dbus.UInt64(vgsize), self.no_options,
                dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.udev_settle()
        self.assertIsNotNone(lv_path)
        self.assertEqual(subprocess.call(['lvs', os.path.join(vgname, lvname)]), 0)
        lv = self.bus.get_object(self.iface_prefix, lv_path)
        lv_block_path = lv.Activate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.assertIsNotNone(lv_block_path)
        lvsize = int(self.get_property(lv, '.LogicalVolume', 'Size'))
        self.assertEqual(lvsize, vgsize)

        # Shrink the LV
        lv.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.udev_settle()
        lv.Resize(dbus.UInt64(lvsize/2), self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.udev_settle()
        lv_block_path = lv.Activate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        lv_block = self.bus.get_object(self.iface_prefix, lv_block_path)
        self.assertIsNotNone(lv_block)
        new_lvsize = int(self.get_property(lv, '.LogicalVolume', 'Size'))
        self.assertGreater(lvsize, new_lvsize)

        # Add one more device to the VG
        new_dev_obj = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[-1]))
        self.assertIsNotNone(new_dev_obj)
        vg.AddDevice(new_dev_obj, self.no_options, dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.udev_settle()
        time.sleep(1)
        new_vgsize = int(self.get_property(vg, '.VolumeGroup', 'Size'))
        self.assertGreater(new_vgsize, vgsize)

        # Resize the LV to the whole VG
        lv.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.udev_settle()
        lv.Resize(dbus.UInt64(new_vgsize), self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.udev_settle()
        new_lvsize = int(self.get_property(lv, '.LogicalVolume', 'Size'))
        self.assertEqual(new_vgsize, new_lvsize)

        # lvremove
        lv.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        lv.Delete(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.udev_settle()
        self.assertNotEqual(subprocess.call(['lvs', os.path.join(vgname, lvname)]), 0)

        # vgremove
        self._remove_vg(vg)


    def test_20_thin(self):
        '''Test thin volumes functionality'''

        vgname = 'storaged_test_thin_vg'

        # Use all the virtual devices
        devs = dbus.Array()
        for d in self.vdevs:
            dev_obj = self.get_object('', '/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)
        vg = self._create_vg(vgname, devs)

        # Create thin pool on the VG
        vgsize = int(self.get_property(vg, '.VolumeGroup', 'FreeSize'))
        tpname = 'storaged_test_tp'
        tp_path = vg.CreateThinPoolVolume(tpname, dbus.UInt64(vgsize), self.no_options,
                dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.udev_settle()
        self.assertIsNotNone(tp_path)
        self.assertEqual(subprocess.call(['lvs', os.path.join(vgname, tpname)]), 0)
        tp = self.bus.get_object(self.iface_prefix, tp_path)
        tpsize = int(self.get_property(tp, '.LogicalVolume', 'Size'))

        # Create thin volume in the pool with virtual size twice the backing pool
        tvname = 'storaged_test_tv'
        tv_path = vg.CreateThinVolume(tvname, dbus.UInt64(tpsize * 2), tp, self.no_options,
                dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.udev_settle()
        tv = self.bus.get_object(self.iface_prefix, tv_path)
        self.assertIsNotNone(tv)
        self.assertEqual(subprocess.call(['lvs', os.path.join(vgname, tvname)]), 0)

        # Check the block device of the thin volume
        lv_block_path = tv.Activate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.udev_settle()
        lv_block = self.bus.get_object(self.iface_prefix, lv_block_path)
        self.assertIsNotNone(lv_block)
        blocksize = int(self.get_property(lv_block, '.Block', 'Size'))
        self.assertGreater(blocksize, vgsize)

        # vgremove
        self._remove_vg(vg)


    def test_30_snapshot(self):
        '''Test LVM snapshoting'''

        vgname = 'storaged_test_snap_vg'

        # Use all the virtual devices
        devs = dbus.Array()
        for d in self.vdevs:
            dev_obj = self.get_object('', '/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)
        vg = self._create_vg(vgname, devs)

        # Create the origin LV
        vgsize = int(self.get_property(vg, '.VolumeGroup', 'FreeSize'))
        lvname = 'storaged_test_origin_lv'
        lv_path = vg.CreatePlainVolume(lvname, dbus.UInt64(vgsize / 2), self.no_options,
                dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.udev_settle()
        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.assertIsNotNone(lv)
        self.assertEqual(subprocess.call(['lvs', os.path.join(vgname, lvname)]), 0)
        time.sleep(1)

        # Create the LV's snapshot
        snapname = 'storaged_test_snap_lv'
        vg_freesize = int(self.get_property(vg, '.VolumeGroup', 'FreeSize'))
        snap_path = lv.CreateSnapshot(snapname, vg_freesize, self.no_options,
                dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.udev_settle()
        snap = self.bus.get_object(self.iface_prefix, snap_path)
        self.assertIsNotNone(snap)

        # vgremove
        self._remove_vg(vg)


    def test_40_cache(self):
        '''Test LVM snapshoting'''

        vgname = 'storaged_test_cache_vg'

        # Use all the virtual devices
        devs = dbus.Array()
        for d in self.vdevs:
            dev_obj = self.get_object('', '/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)
        vg = self._create_vg(vgname, devs)

        # Create the origin LV
        vgsize = int(self.get_property(vg, '.VolumeGroup', 'FreeSize'))
        lvname = 'storaged_test_origin_lv'
        lv_path = vg.CreatePlainVolume(lvname, dbus.UInt64(vgsize / 2), self.no_options,
                dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.udev_settle()
        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.assertIsNotNone(lv)
        self.assertEqual(subprocess.call(['lvs', os.path.join(vgname, lvname)]), 0)
        time.sleep(1)

        # Create the caching LV
        lvname = 'storaged_test_cache_lv'
        vgsize = int(self.get_property(vg, '.VolumeGroup', 'FreeSize'))
        lv_cache_path = vg.CreatePlainVolume(lvname, dbus.UInt64(vgsize / 2), self.no_options,
                dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.udev_settle()
        cache_lv = self.bus.get_object(self.iface_prefix, lv_cache_path)
        self.assertIsNotNone(cache_lv)

        # Add the cache to the origin
        lv.CacheAttach('storaged_test_cache_lv', self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        # vgremove
        self._remove_vg(vg)

