import os
import re
import time
import unittest

import storagedtestcase


MODPROBECONF = '/usr/lib/modprobe.d/zram.conf'
MODLOADCONF = '/usr/lib/modules-load.d/zram.conf'
ZRAMCONFDIR = '/usr/local/lib/zram.conf.d'


class StoragedZRAMTest(storagedtestcase.StoragedTestCase):
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
        storagedtestcase.StoragedTestCase.setUpClass()
        if not cls.check_module_loaded('ZRAM'):
            raise unittest.SkipTest('Storaged module for zram tests not loaded, skipping.')

        cls._save_conf_files()

    @classmethod
    def tearDownClass(cls):
        storagedtestcase.StoragedTestCase.tearDownClass()

        cls._restore_conf_files()

        cls.run_command('modprobe -r zram')

    def _get_zrams(self):
        time.sleep(0.5)
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')

        return [path for path in objects.keys() if re.match(r'.*/block_devices/zram[0-9]+$', path)]

    def _swapoff(self, swap):
        self.run_command('swapoff %s' % swap)

    def test_create_destroy(self):
        manager = self.get_object('/Manager')
        manager.CreateDevices([10 * 1024**2, 10 * 1024**2], [1, 2], self.no_options,
                              dbus_interface=self.iface_prefix + '.Manager.ZRAM')
        time.sleep(3)

        zrams = self._get_zrams()
        self.assertEqual(len(zrams), 2)

        # zram devices properties
        for path in zrams:
            zram = self.bus.get_object(self.iface_prefix, path)
            self.assertIsNotNone(zram)

            zram_name = path.split('/')[-1]
            self.assertTrue(os.path.exists('/dev/%s' % zram_name))

            sys_size = self.read_file('/sys/block/%s/disksize' % zram_name).strip()
            dbus_size = self.get_property(zram, '.Block.ZRAM', 'disksize')
            dbus_size.assertEqual(int(sys_size))

            sys_alg = self.read_file('/sys/block/%s/comp_algorithm' % zram_name).strip()
            dbus_alg = self.get_property(zram, '.Block.ZRAM', 'comp_algorithm')
            dbus_alg.assertEqual(sys_alg)

            sys_streams = self.read_file('/sys/block/%s/max_comp_streams' % zram_name).strip()
            dbus_streams = self.get_property(zram, '.Block.ZRAM', 'max_comp_streams')
            dbus_streams.assertEqual(int(sys_streams))

        # destroy zrams
        manager.DestroyDevices(self.no_options,
                               dbus_interface=self.iface_prefix + '.Manager.ZRAM')
        zrams = self._get_zrams()
        self.assertEqual(len(zrams), 0)

    def test_activate_deactivate(self):
        manager = self.get_object('/Manager')
        manager.CreateDevices([10 * 1024**2], [1], self.no_options,
                              dbus_interface=self.iface_prefix + '.Manager.ZRAM')
        time.sleep(3)

        zrams = self._get_zrams()
        self.assertEqual(len(zrams), 1)

        zram = self.bus.get_object(self.iface_prefix, zrams[0])
        self.assertIsNotNone(zram)

        zram_name = zrams[0].split('/')[-1]

        # activate the ZRAM device
        zram.Activate(1, self.no_options, dbus_interface=self.iface_prefix + '.Block.ZRAM')
        self.addCleanup(self._swapoff, '/dev/%s' % zram_name)
        time.sleep(1)
        zram.Refresh(dbus_interface=self.iface_prefix + '.Block.ZRAM')

        # check if is active
        active = self.get_property(zram, '.Block.ZRAM', 'active')
        active.assertTrue()

        # and if is in /proc/swaps
        _ret, out = self.run_command('swapon --show=NAME --noheadings')
        swaps = out.split()
        self.assertIn('/dev/%s' % zram_name, swaps)

        # test some properties
        sys_reads = self.read_file('/sys/block/%s/num_reads' % zram_name).strip()
        dbus_reads = self.get_property(zram, '.Block.ZRAM', 'num_reads')
        dbus_reads.assertEqual(int(sys_reads))

        sys_writes = self.read_file('/sys/block/%s/num_writes' % zram_name).strip()
        dbus_writes = self.get_property(zram, '.Block.ZRAM', 'num_writes')
        dbus_writes.assertEqual(int(sys_writes))

        sys_compr = self.read_file('/sys/block/%s/compr_data_size' % zram_name).strip()
        dbus_compr = self.get_property(zram, '.Block.ZRAM', 'compr_data_size')
        dbus_compr.assertEqual(int(sys_compr))

        sys_orig = self.read_file('/sys/block/%s/orig_data_size' % zram_name).strip()
        dbus_orig = self.get_property(zram, '.Block.ZRAM', 'orig_data_size')
        dbus_orig.assertEqual(int(sys_orig))

        # deactivate the ZRAM device
        zram.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.Block.ZRAM')
        time.sleep(1)
        zram.Refresh(dbus_interface=self.iface_prefix + '.Block.ZRAM')

        # check if is not active
        active = self.get_property(zram, '.Block.ZRAM', 'active')
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
