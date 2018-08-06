import os
import re
import time
import unittest

from distutils.version import LooseVersion

import udiskstestcase
from udiskstestcase import unstable_test


MODPROBECONF = '/usr/lib/modprobe.d/zram.conf'
MODLOADCONF = '/usr/lib/modules-load.d/zram.conf'
ZRAMCONFDIR = '/usr/local/lib/zram.conf.d'


class UdisksZRAMTest(udiskstestcase.UdisksTestCase):
    '''This is a basic ZRAM test suite'''

    conf = {MODPROBECONF: None,
            MODLOADCONF: None,
            ZRAMCONFDIR: {}}

    @classmethod
    def _save_conf_files(cls):
        # /usr/lib/modprobe.d/zram.conf
        if os.path.exists(MODPROBECONF):
            cls.conf[MODPROBECONF] = cls.read_file(MODPROBECONF)

        # /usr/lib/modules-load.d/zram.conf
        if os.path.exists(MODLOADCONF):
            cls.conf[MODLOADCONF] = cls.read_file(MODLOADCONF)

        # /usr/local/lib/zram.conf.d
        if os.path.exists(ZRAMCONFDIR):
            for fname in os.listdir(ZRAMCONFDIR):
                cls.conf[ZRAMCONFDIR][fname] = cls.read_file(ZRAMCONFDIR + '/' + fname)

    @classmethod
    def _restore_conf_files(cls):
        # /usr/lib/modprobe.d/zram.conf
        if cls.conf[MODPROBECONF]:
            cls.write_file(MODPROBECONF, cls.conf[MODPROBECONF])

        # /usr/lib/modules-load.d/zram.conf
        if cls.conf[MODLOADCONF]:
            cls.write_file(MODLOADCONF, cls.conf[MODLOADCONF])

        # /usr/local/lib/zram.conf.d
        for fname in cls.conf[ZRAMCONFDIR].keys():
            cls.write_file(ZRAMCONFDIR + '/' + fname, cls.conf[ZRAMCONFDIR][fname])

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if cls.distro[1:] == ("fedora", "27"):
            raise unittest.SkipTest('zram module (un)loading causes kernel panics on rawhide')
        if not cls.check_module_loaded('ZRAM'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for zram tests not loaded, skipping.')

        cls._save_conf_files()

    @classmethod
    def tearDownClass(cls):
        udiskstestcase.UdisksTestCase.tearDownClass()

        cls._restore_conf_files()

        cls.run_command('modprobe -r zram')

    def _get_zrams(self):
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')

        return [path for path in objects.keys() if re.match(r'.*/block_devices/zram[0-9]+$', path)]

    def _get_algorithm(self, zram_name):
        # comp_algorithm contains all supported algorithms, the right one is in square brackets
        modes = self.read_file('/sys/block/%s/comp_algorithm' % zram_name)
        return next((x[1:-1] for x in modes.split() if re.match(r'^\[.*\]$', x)), None)

    def _swapoff(self, swap):
        self.run_command('swapoff %s' % swap)

    @unstable_test
    def test_create_destroy(self):
        manager = self.get_object('/Manager')
        zrams = manager.CreateDevices([10 * 1024**2, 10 * 1024**2], [1, 2], self.no_options,
                                      dbus_interface=self.iface_prefix + '.Manager.ZRAM')
        self.assertEqual(len(zrams), 2)

        # zram devices properties
        for path in zrams:
            zram = self.bus.get_object(self.iface_prefix, path)
            self.assertIsNotNone(zram)

            zram_name = path.split('/')[-1]
            self.assertTrue(os.path.exists('/dev/%s' % zram_name))

            sys_size = self.read_file('/sys/block/%s/disksize' % zram_name).strip()
            dbus_size = self.get_property(zram, '.Block.ZRAM', 'Disksize')
            dbus_size.assertEqual(int(sys_size))

            sys_alg = self._get_algorithm(zram_name)
            dbus_alg = self.get_property(zram, '.Block.ZRAM', 'CompAlgorithm')
            dbus_alg.assertEqual(sys_alg)

            sys_streams = self.read_file('/sys/block/%s/max_comp_streams' % zram_name).strip()
            dbus_streams = self.get_property(zram, '.Block.ZRAM', 'MaxCompStreams')
            dbus_streams.assertEqual(int(sys_streams))

        # destroy zrams
        manager.DestroyDevices(self.no_options,
                               dbus_interface=self.iface_prefix + '.Manager.ZRAM')
        zrams = self._get_zrams()
        self.assertEqual(len(zrams), 0)

    def _test_zram_properties_fedora(self, zram_obj, zram_name):
        # test some properties
        sys_stat = self.read_file('/sys/block/%s/stat' % zram_name).strip().split()
        self.assertEqual(len(sys_stat), 11)
        sys_reads = sys_stat[0]
        sys_writes = sys_stat[4]

        dbus_reads = self.get_property(zram_obj, '.Block.ZRAM', 'NumReads')
        dbus_reads.assertEqual(int(sys_reads))

        dbus_writes = self.get_property(zram_obj, '.Block.ZRAM', 'NumWrites')
        dbus_writes.assertEqual(int(sys_writes))

        sys_mmstat = self.read_file('/sys/block/%s/mm_stat' % zram_name).strip().split()
        self.assertGreaterEqual(len(sys_mmstat), 7)  # 7 stats on 4.11, 8 stats on 4.18
        sys_compr = sys_mmstat[1]
        sys_orig = sys_mmstat[0]

        dbus_compr = self.get_property(zram_obj, '.Block.ZRAM', 'ComprDataSize')
        dbus_compr.assertEqual(int(sys_compr))

        dbus_orig = self.get_property(zram_obj, '.Block.ZRAM', 'OrigDataSize')
        dbus_orig.assertEqual(int(sys_orig))

    def _test_zram_properties_centos(self, zram_obj, zram_name):
        # test some properties
        sys_reads = int(self.read_file("/sys/block/zram0/num_reads").strip())
        dbus_reads = self.get_property(zram_obj, '.Block.ZRAM', 'NumReads')
        dbus_reads.assertEqual(sys_reads)

        sys_writes = int(self.read_file("/sys/block/zram0/num_writes").strip())
        dbus_writes = self.get_property(zram_obj, '.Block.ZRAM', 'NumWrites')
        dbus_writes.assertEqual(int(sys_writes))

        sys_compr = int(self.read_file("/sys/block/zram0/compr_data_size").strip())
        dbus_compr = self.get_property(zram_obj, '.Block.ZRAM', 'ComprDataSize')
        dbus_compr.assertEqual(sys_compr)

        sys_orig = int(self.read_file("/sys/block/zram0/orig_data_size").strip())
        dbus_orig = self.get_property(zram_obj, '.Block.ZRAM', 'OrigDataSize')
        dbus_orig.assertEqual(sys_orig)

    @unstable_test
    def test_activate_deactivate(self):
        manager = self.get_object('/Manager')
        zrams = manager.CreateDevices([10 * 1024**2], [1], self.no_options,
                                      dbus_interface=self.iface_prefix + '.Manager.ZRAM')
        self.assertEqual(len(zrams), 1)

        zram = self.bus.get_object(self.iface_prefix, zrams[0])
        self.assertIsNotNone(zram)

        zram_name = zrams[0].split('/')[-1]

        # activate the ZRAM device
        zram.Activate(1, self.no_options, dbus_interface=self.iface_prefix + '.Block.ZRAM')
        self.addCleanup(self._swapoff, '/dev/%s' % zram_name)
        time.sleep(1)
        zram.Refresh(self.no_options, dbus_interface=self.iface_prefix + '.Block.ZRAM')

        # check if is active
        active = self.get_property(zram, '.Block.ZRAM', 'Active')
        active.assertTrue()

        # and if is in /proc/swaps
        _ret, out = self.run_command('swapon --show=NAME --noheadings')
        swaps = out.split()
        self.assertIn('/dev/%s' % zram_name, swaps)

        # test some properties
        # location of some sysfs files we use is different since linux 4.11
        kernel_version = os.uname()[2]
        if LooseVersion(kernel_version) >= LooseVersion("4.11"):
            self._test_zram_properties_fedora(zram, zram_name)
        else:
            self._test_zram_properties_centos(zram, zram_name)

        # deactivate the ZRAM device
        zram.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.Block.ZRAM')
        time.sleep(1)
        zram.Refresh(self.no_options, dbus_interface=self.iface_prefix + '.Block.ZRAM')

        # check if is not active
        active = self.get_property(zram, '.Block.ZRAM', 'Active')
        active.assertFalse()

        # and if is not in /proc/swaps
        _ret, out = self.run_command('swapon --show=NAME --noheadings')
        swaps = out.split()
        self.assertNotIn('/dev/%s' % zram_name, swaps)

        # activate with label
        zram.ActivateLabeled(1, 'zram', self.no_options, dbus_interface=self.iface_prefix + '.Block.ZRAM')

        # only way how to tell the label actually works is to try to run
        # swapoff with '-L label'; running 'swapon --show=LABEL' doesn't work
        ret, _out = self.run_command('swapoff -L zram')
        self.assertEqual(ret, 0)

        # destroy zrams
        manager.DestroyDevices(self.no_options,
                               dbus_interface=self.iface_prefix + '.Manager.ZRAM')
        zrams = self._get_zrams()
        self.assertEqual(len(zrams), 0)
