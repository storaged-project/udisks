import os
import stat
import re
import six
import tempfile
import time
import sys
import shutil
import json
import uuid
import dbus
import unittest
import udiskstestcase



def find_nvme_ctrl_devs_for_subnqn(subnqn):
    """
    Find NVMe controller devices for the specified subsystem nqn

    :param str subnqn: subsystem nqn
    """

    def _check_subsys(subsys, dev_paths):
        if subsys['SubsystemNQN'] == subnqn:
            for ctrl in subsys['Controllers']:
                path = os.path.join('/dev/', ctrl['Controller'])
                try:
                    st = os.lstat(path)
                    # nvme controller node is a character device
                    if stat.S_ISCHR(st.st_mode):
                        dev_paths += [path]
                except:
                    pass

    ret, out = udiskstestcase.run_command("nvme list --output-format=json --verbose")
    if ret != 0:
        raise RuntimeError("Error getting NVMe list: %s" % out)

    decoder = json.JSONDecoder()
    decoded = decoder.decode(out)
    if not decoded or 'Devices' not in decoded:
        return []

    dev_paths = []
    for dev in decoded['Devices']:
        # nvme-cli 2.x
        if 'Subsystems' in dev:
            for subsys in dev['Subsystems']:
                _check_subsys(subsys, dev_paths)
        # nvme-cli 1.x
        if 'SubsystemNQN' in dev:
            _check_subsys(dev, dev_paths)

    return dev_paths


def find_nvme_ns_devs_for_subnqn(subnqn):
    """
    Find NVMe namespace block devices for the specified subsystem nqn

    :param str subnqn: subsystem nqn
    """

    def _check_namespaces(node, ns_dev_paths):
        for ns in node['Namespaces']:
            path = os.path.join('/dev/', ns['NameSpace'])
            try:
                st = os.lstat(path)
                if stat.S_ISBLK(st.st_mode):
                    ns_dev_paths += [path]
            except:
                pass

    def _check_subsys(subsys, ns_dev_paths):
        if subsys['SubsystemNQN'] == subnqn:
            if 'Namespaces' in subsys:
                _check_namespaces(subsys, ns_dev_paths)
            # kernel 4.18
            if 'Controllers' in subsys:
                for ctrl in subsys['Controllers']:
                    if 'Namespaces' in ctrl:
                        _check_namespaces(ctrl, ns_dev_paths)

    ret, out = udiskstestcase.run_command("nvme list --output-format=json --verbose")
    if ret != 0:
        raise RuntimeError("Error getting NVMe list: %s" % out)

    decoder = json.JSONDecoder()
    decoded = decoder.decode(out)
    if not decoded or 'Devices' not in decoded:
        return []

    ns_dev_paths = []
    for dev in decoded['Devices']:
        # nvme-cli 2.x
        if 'Subsystems' in dev:
            for subsys in dev['Subsystems']:
                _check_subsys(subsys, ns_dev_paths)
        # nvme-cli 1.x
        if 'SubsystemNQN' in dev:
            _check_subsys(dev, ns_dev_paths)

    return ns_dev_paths


def get_nvme_hostnqn():
    """
    Retrieves NVMe host NQN string from /etc/nvme/hostnqn or uses nvme-cli to generate
    new one (stable, typically generated from machine DMI data) when not available.
    """

    hostnqn = None
    try:
        hostnqn = read_file('/etc/nvme/hostnqn')
    except:
        pass

    if hostnqn is None or len(hostnqn.strip()) < 1:
        ret, hostnqn = udiskstestcase.run_command('nvme gen-hostnqn')
        if ret != 0:
            raise RuntimeError("Cannot get host NQN: '%s %s'" % (hostnqn, err))

    return hostnqn.strip()

def setup_nvme_target(dev_paths, subnqn):
    """
    Sets up a new NVMe target loop device (using nvmetcli) on top of the
    :param:`dev_paths` backing block devices.

    :param set dev_paths: set of backing block device paths
    :param str subnqn: Subsystem NQN
    """

    # modprobe required nvme target modules
    for module in ['nvmet', 'nvme-loop']:
        ret, out = udiskstestcase.run_command("modprobe %s" % module)
        if ret != 0:
            raise RuntimeError("Cannot load required NVMe target modules: %s" % out)

    # create a JSON file for nvmetcli
    with tempfile.NamedTemporaryFile(mode='wt', delete=False) as tmp:
        tcli_json_file = tmp.name
        namespaces = ",".join(["""
        {{
          "device": {{
            "nguid": "{nguid}",
            "path": "{path}"
          }},
          "enable": 1,
          "nsid": {nsid}
        }}
        """.format(nguid=uuid.uuid4(), path=dev_path, nsid=i) for i, dev_path in enumerate(dev_paths, start=1)])

        json = """
{
  "ports": [
    {
      "addr": {
        "adrfam": "",
        "traddr": "",
        "treq": "not specified",
        "trsvcid": "",
        "trtype": "loop"
      },
      "portid": 1,
      "referrals": [],
      "subsystems": [
        "%s"
      ]
    }
  ],
  "subsystems": [
    {
      "attr": {
        "allow_any_host": "1"
      },
      "namespaces": [
%s
      ],
      "nqn": "%s"
    }
  ]
}
"""
        tmp.write(json % (subnqn, namespaces, subnqn))

    ret, out = udiskstestcase.run_command("nvmetcli restore %s" % tcli_json_file)
    os.unlink(tcli_json_file)
    if ret != 0:
        raise RuntimeError("Error setting up the NVMe target: %s" % out)


class UdisksNVMeTest(udiskstestcase.UdisksTestCase):
    SUBNQN = 'udisks_test_subnqn'
    NUM_NS = 2
    NS_SIZE = 1024**3

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not shutil.which("nvme"):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest("nvme executable (nvme-cli package) not found in $PATH, skipping.")
        if not shutil.which("nvmetcli"):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest("nvmetcli executable not found in $PATH, skipping.")
        ret, out = udiskstestcase.run_command("modprobe nvme-fabrics")
        if ret != 0:
            raise unittest.SkipTest("nvme-fabrics kernel module unavailable, skipping.")

        manager = cls.get_interface("/Manager", ".Manager")
        cls.loop_devs = []

        for i in range(cls.NUM_NS):
            with tempfile.NamedTemporaryFile(prefix="udisks_test", delete=True, mode='w+b') as temp:
                temp.truncate(cls.NS_SIZE)
                loop_dev_obj_path = manager.LoopSetup(temp.fileno(), cls.no_options)
                path, loop_dev = loop_dev_obj_path.rsplit("/", 1)
                cls.loop_devs += ["/dev/%s" % loop_dev]

        cls.hostnqn = get_nvme_hostnqn()
        setup_nvme_target(cls.loop_devs, cls.SUBNQN)

    def _nvme_disconnect(self, subnqn, ignore_errors=False):
        ret, out = self.run_command("nvme disconnect --nqn=%s" % subnqn)
        if not ignore_errors and (ret != 0 or 'disconnected 0 ' in out):
            raise RuntimeError("Error disconnecting the '%s' subsystem NQN: '%s'" % (subnqn, out))

    def _nvme_connect(self):
        ret, out = self.run_command("nvme connect --transport=loop --hostnqn=%s --nqn=%s" % (self.hostnqn, self.SUBNQN))
        if ret != 0:
            raise RuntimeError("Error connecting to the NVMe target: %s" % out)
        nvme_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        if len(nvme_devs) != 1:
            raise RuntimeError("Error looking up block device for the '%s' nqn" % self.SUBNQN)
        # at this point the NVMe controller may still be in a 'connecting' state and
        # kernels < 5.18 needean explicit uevent to deliver device info in a 'live' state
        self.udev_settle()
        self.run_command('udevadm trigger --subsystem-match=nvme --subsystem-match=block')

    @classmethod
    def tearDownClass(cls):
        ret, out = udiskstestcase.run_command("nvmetcli clear")
        if ret != 0:
            raise RuntimeError("Error clearing the NVMe target: %s" % out)
        # detach loop devices
        for i in cls.loop_devs:
            cls.run_command("losetup --detach %s" % i)
        udiskstestcase.UdisksTestCase.tearDownClass()


    def test_controller_info(self):
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)
        time.sleep(1)

        ctrl_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrl_devs), 1)
        ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ns_devs), self.NUM_NS)

        # find drive object through one of the namespace block objects
        drive_obj_path = None
        for d in ns_devs:
            ns = self.get_object('/block_devices/' + os.path.basename(d))
            # this will wait up to 20*0.5 seconds for the interface to appear
            self.assertHasIface(ns, 'org.freedesktop.UDisks2.NVMe.Namespace')
            p = self.get_property_raw(ns, '.Block', 'Drive')
            if not drive_obj_path:
                drive_obj_path = p
            else:
                self.assertEqual(drive_obj_path, p)

        drive_obj = self.get_object(drive_obj_path)
        self.assertHasIface(drive_obj, 'org.freedesktop.UDisks2.NVMe.Controller')
        # this will wait up to 10 seconds for the state to switch
        state = self.get_property(drive_obj, '.NVMe.Controller', 'State')
        state.assertEqual('live', timeout=10)

        wwn = self.get_property_raw(drive_obj, '.Drive', 'WWN')
        self.assertEqual(len(wwn), 0)
        model = self.get_property_raw(drive_obj, '.Drive', 'Model')
        self.assertEqual(model, 'Linux')
        id = self.get_property_raw(drive_obj, '.Drive', 'Id')
        self.assertTrue(id.startswith('Linux-'))
        size = self.get_property_raw(drive_obj, '.Drive', 'Size')
        self.assertEqual(size, 0)

        ctrl_id = self.get_property_raw(drive_obj, '.NVMe.Controller', 'ControllerID')
        self.assertGreater(ctrl_id, 0)
        nqn = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SubsystemNQN')
        self.assertEqual(nqn, self.SUBNQN)
        fguid = self.get_property_raw(drive_obj, '.NVMe.Controller', 'FGUID')
        self.assertEqual(len(fguid), 0)
        rev = self.get_property_raw(drive_obj, '.NVMe.Controller', 'NVMeRevision')
        self.assertTrue(rev.startswith('1.'))
        transport = self.get_property_raw(drive_obj, '.NVMe.Controller', 'Transport')
        self.assertEqual(transport, 'loop')
        unalloc_cap = self.get_property_raw(drive_obj, '.NVMe.Controller', 'UnallocatedCapacity')
        self.assertEquals(unalloc_cap, 0)


    def test_namespace_info(self):
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)
        time.sleep(1)

        ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ns_devs), self.NUM_NS)

        for d in ns_devs:
            ns = self.get_object('/block_devices/' + os.path.basename(d))
            self.assertHasIface(ns, 'org.freedesktop.UDisks2.NVMe.Namespace')

            drive_obj_path = self.get_property_raw(ns, '.Block', 'Drive')
            drive_obj = self.get_object(drive_obj_path)
            self.assertHasIface(drive_obj, 'org.freedesktop.UDisks2.NVMe.Controller')
            state = self.get_property(drive_obj, '.NVMe.Controller', 'State')
            state.assertEqual('live', timeout=10)

            size = self.get_property_raw(ns, '.Block', 'Size')
            self.assertEqual(size, self.NS_SIZE)

            nsid = self.get_property_raw(ns, '.NVMe.Namespace', 'NSID')
            self.assertGreater(nsid, 0)
            nguid = self.get_property_raw(ns, '.NVMe.Namespace', 'NGUID')
            self.assertGreater(len(nguid), 10)
            eui64 = self.get_property_raw(ns, '.NVMe.Namespace', 'EUI64')
            self.assertGreater(len(eui64), 10)
            uuid = self.get_property_raw(ns, '.NVMe.Namespace', 'UUID')
            self.assertGreater(len(uuid), 10)
            wwn = self.get_property_raw(ns, '.NVMe.Namespace', 'WWN')
            self.assertTrue(wwn.startswith('uuid.'))
            lbaf = self.get_property_raw(ns, '.NVMe.Namespace', 'LBAFormats')
            self.assertEquals(len(lbaf), 1)
            self.assertEquals(lbaf[0], (512, 1))
            lbaf_curr = self.get_property_raw(ns, '.NVMe.Namespace', 'FormattedLBASize')
            self.assertEquals(lbaf_curr, (512, 1))
            nsize = self.get_property_raw(ns, '.NVMe.Namespace', 'NamespaceSize')
            self.assertEqual(nsize, self.NS_SIZE / lbaf_curr[0])
            ncap = self.get_property_raw(ns, '.NVMe.Namespace', 'NamespaceCapacity')
            self.assertEqual(ncap, self.NS_SIZE / lbaf_curr[0])
            nutl = self.get_property_raw(ns, '.NVMe.Namespace', 'NamespaceUtilization')
            self.assertEqual(nutl, self.NS_SIZE / lbaf_curr[0])
            format_progress = self.get_property_raw(ns, '.NVMe.Namespace', 'FormatPercentRemaining')
            self.assertEqual(format_progress, -1)


    def test_health_info(self):
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)
        time.sleep(1)

        ctrl_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrl_devs), 1)
        ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ns_devs), self.NUM_NS)

        # find drive object through one of the namespace block objects
        ns = self.get_object('/block_devices/' + os.path.basename(ns_devs[0]))
        self.assertHasIface(ns, 'org.freedesktop.UDisks2.NVMe.Namespace')
        drive_obj_path = self.get_property_raw(ns, '.Block', 'Drive')
        drive_obj = self.get_object(drive_obj_path)
        self.assertHasIface(drive_obj, 'org.freedesktop.UDisks2.NVMe.Controller')
        state = self.get_property(drive_obj, '.NVMe.Controller', 'State')
        state.assertEqual('live', timeout=10)

        smart_updated = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SmartUpdated')
        self.assertGreater(smart_updated, 0)
        smart_warnings = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SmartCriticalWarning')
        self.assertEqual(len(smart_warnings), 0)
        smart_poh = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SmartPowerOnHours')
        self.assertEqual(smart_poh, 0)
        smart_temp = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SmartTemperature')
        self.assertEqual(smart_temp, 0)
        smart_selftest_status = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SmartSelftestStatus')
        self.assertEqual(smart_selftest_status, '')
        smart_selftest_remaining = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SmartSelftestPercentRemaining')
        self.assertEqual(smart_selftest_remaining, -1)

        drive_obj.SmartUpdate(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        smart_updated2 = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SmartUpdated')
        self.assertGreaterEqual(smart_updated2, smart_updated)

        attrs = drive_obj.SmartGetAttributes(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        self.assertGreater(len(attrs), 10)
        self.assertEqual(attrs['avail_spare'], 0);
        self.assertEqual(attrs['spare_thresh'], 0);
        self.assertEqual(attrs['percent_used'], 0);
        self.assertEqual(attrs['ctrl_busy_time'], 0);
        self.assertEqual(attrs['power_cycles'], 0);
        self.assertEqual(attrs['unsafe_shutdowns'], 0);
        self.assertEqual(attrs['media_errors'], 0);
        self.assertIn('num_err_log_entries', attrs);
        self.assertEqual(attrs['temp_sensors'], [0, 0, 0, 0, 0, 0, 0, 0]);
        self.assertEqual(attrs['warning_temp_time'], 0);
        self.assertEqual(attrs['critical_temp_time'], 0);

        # Try trigerring a self-test operation
        msg = 'Unknown self-test type xxx'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            drive_obj.SmartSelftestStart('xxx', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        msg = 'NVMe Get Log Page - Device Self-test Log command error: Invalid Field in Command'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            drive_obj.SmartSelftestStart('short', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        msg = 'NVMe Device Self-test command error: Invalid Command Opcode'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            drive_obj.SmartSelftestAbort(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')


    def test_sanitize(self):
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)
        time.sleep(1)

        ctrl_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrl_devs), 1)
        ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ns_devs), self.NUM_NS)

        # find drive object through one of the namespace block objects
        ns = self.get_object('/block_devices/' + os.path.basename(ns_devs[0]))
        self.assertHasIface(ns, 'org.freedesktop.UDisks2.NVMe.Namespace')
        drive_obj_path = self.get_property_raw(ns, '.Block', 'Drive')
        drive_obj = self.get_object(drive_obj_path)
        self.assertHasIface(drive_obj, 'org.freedesktop.UDisks2.NVMe.Controller')
        state = self.get_property(drive_obj, '.NVMe.Controller', 'State')
        state.assertEqual('live', timeout=10)

        sanitize_status = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SanitizeStatus')
        self.assertEqual(sanitize_status, '')
        sanitize_percent_remaining = self.get_property_raw(drive_obj, '.NVMe.Controller', 'SanitizePercentRemaining')
        self.assertEqual(sanitize_percent_remaining, -1)

        # Try trigerring a sanitize operation
        msg = 'Unknown sanitize action xxx'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            drive_obj.SanitizeStart('xxx', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        msg = 'NVMe Get Log Page - Sanitize Status Log command error: Invalid Field in Command'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            drive_obj.SanitizeStart('block-erase', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            drive_obj.SanitizeStart('crypto-erase', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            drive_obj.SanitizeStart('overwrite', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')


    def test_format_ns(self):
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)
        time.sleep(1)

        ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ns_devs), self.NUM_NS)

        for d in ns_devs:
            ns = self.get_object('/block_devices/' + os.path.basename(d))
            self.assertHasIface(ns, 'org.freedesktop.UDisks2.NVMe.Namespace')

            drive_obj_path = self.get_property_raw(ns, '.Block', 'Drive')
            drive_obj = self.get_object(drive_obj_path)
            self.assertHasIface(drive_obj, 'org.freedesktop.UDisks2.NVMe.Controller')
            state = self.get_property(drive_obj, '.NVMe.Controller', 'State')
            state.assertEqual('live', timeout=10)

            msg = 'Format NVM command error: Invalid Command Opcode: A reserved coded value or an unsupported value in the command opcode field'
            with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
                ns.FormatNamespace(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Namespace')

            d = dbus.Dictionary(signature='sv')
            with six.assertRaisesRegex(self, dbus.exceptions.DBusException, 'Unknown secure erase type xxx'):
                d['secure_erase'] = 'xxx'
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')
            with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
                d['secure_erase'] = 'user_data'
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')
            with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
                d['secure_erase'] = 'crypto_erase'
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')

            d = dbus.Dictionary(signature='sv')
            with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
                d['lba_data_size'] = 0
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')
            lbaf_curr = self.get_property_raw(ns, '.NVMe.Namespace', 'FormattedLBASize')
            with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
                d['lba_data_size'] = lbaf_curr[0]
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')
            msg = "Couldn't match desired LBA data block size in a device supported LBA format data sizes"
            with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
                d['lba_data_size'] = dbus.UInt16(666)
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')

    def test_fabrics_connect(self):
        manager = self.get_interface("/Manager", ".Manager.NVMe")
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, 'Invalid value specified for the transport address argument'):
            manager.Connect(self.SUBNQN, "notransport", "", self.no_options)
        msg = r'Error connecting the controller: failed to write to nvme-fabrics device'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            manager.Connect(self.SUBNQN, "loop", "127.0.0.1", self.no_options)
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            manager.Connect("unknownsubnqn", "loop", "", self.no_options)

        d = dbus.Dictionary(signature='sv')
        d['host_nqn'] = self.str_to_ay('nqn.2014-08.org.nvmexpress:uuid:01234567-8900-abcd-efff-abcdabcdabcd')
        d['host_id'] = self.str_to_ay('cccccccc-abcd-abcd-1234-1234567890ab')
        ctrl_obj_path = manager.Connect(self.SUBNQN, "loop", "", d)
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

        ctrl = self.get_object(ctrl_obj_path)
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Controller')
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Fabrics')

        hostnqn = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostNQN')
        self.assertEqual(hostnqn, d['host_nqn'])
        hostid = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostID')
        self.assertEqual(hostid, d['host_id'])
        tr_addr = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'TransportAddress')
        self.assertEqual(len(tr_addr), 1)   # the zero trailing byte

        ctrl.Disconnect(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Fabrics')
        ctrl = self.get_object(ctrl_obj_path)
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, r'Object does not exist at path .*|No such interface'):
            self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostNQN')
