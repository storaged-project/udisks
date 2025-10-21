import os
import stat
import tempfile
import time
import shutil
import glob
import json
import uuid
import dbus
import unittest
import udiskstestcase

from config_h import PACKAGE_SYSCONF_DIR


def _wait_for_nvme_controllers_ready(subnqn, timeout=10):
    """
    Wait for NVMe controllers with matching subsystem NQN to be in live state

    :param str subnqn: subsystem nqn to match controllers against
    :param int timeout: timeout in seconds (default: 3)
    """
    start_time = time.time()

    while time.time() - start_time < timeout:
        try:
            for ctrl_path in glob.glob("/sys/class/nvme-fabrics/ctl/nvme*/"):
                state_file = os.path.join(ctrl_path, "state")
                subsysnqn_file = os.path.join(ctrl_path, "subsysnqn")
                try:
                    state = udiskstestcase.UdisksTestCase.read_file(state_file).strip()
                    controller_subnqn = udiskstestcase.UdisksTestCase.read_file(subsysnqn_file).strip()
                    if state == "live" and controller_subnqn == subnqn:
                        # Found a matching live controller
                        os.system("udevadm settle")
                        return
                except:
                    continue

        except:
            pass

        time.sleep(1)

    os.system("udevadm settle")

def find_nvme_ctrl_devs_for_subnqn(subnqn, wait_for_ready=True):
    """
    Find NVMe controller devices for the specified subsystem nqn

    :param str subnqn: subsystem nqn
    :param bool wait_for_ready: whether to wait for controllers to be ready (default: True)
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

    # Wait for controllers to be ready if requested
    if wait_for_ready:
        _wait_for_nvme_controllers_ready(subnqn)
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


def find_nvme_ns_devs_for_subnqn(subnqn, wait_for_ready=True):
    """
    Find NVMe namespace block devices for the specified subsystem nqn

    :param str subnqn: subsystem nqn
    :param bool wait_for_ready: whether to wait for controllers to be ready (default: True)
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

    if wait_for_ready:
        _wait_for_nvme_controllers_ready(subnqn)
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


def setup_nvme_target(dev_paths, subnqn, tr_loop=True, tr_tcp_ipv4=False, tr_tcp_ipv4_svcid=4420, tr_tcp_ipv6=False, tr_tcp_ipv6_svcid=4420):
    """
    Sets up a new NVMe target (using nvmetcli) with :param:`dev_paths`
    as backing block devices. Supports loop and tcp transports over
    ipv4 and ipv6.

    :param set dev_paths: set of backing block device paths
    :param str subnqn: Subsystem NQN
    :param bool tr_loop: use the loop transport (default)
    :param bool tr_tcp_ipv4: use the tcp transport on 127.0.0.1
    :param int tr_tcp_ipv4_svcid: tcp port for IPv4 transport (default: 4420)
    :param bool tr_tcp_ipv6: use the tcp transport on ::1
    :param int tr_tcp_ipv6_svcid: tcp port for IPv6 transport (default: 4420)
    """

    # modprobe required nvme target modules
    kmods = ['nvmet']
    if tr_loop:
        kmods += ['nvme_loop']
    if tr_tcp_ipv4 or tr_tcp_ipv6:
        kmods += ['nvme_tcp']
    if tr_tcp_ipv6:
        kmods += ['ipv6']
    for module in kmods:
        ret, out = udiskstestcase.run_command("modprobe %s" % module)
        if ret != 0:
            raise RuntimeError("Cannot load required kernel module: %s" % out)

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

        ports_list = []
        if tr_loop:
            ports_list.append("""
    {
      "addr": {
        "trtype": "loop"
      },
      "portid": 1,
      "subsystems": [
        "%s"
      ]
    }""" % (subnqn))

        if tr_tcp_ipv4:
            ports_list.append("""
    {
      "addr": {
        "adrfam": "ipv4",
        "traddr": "127.0.0.1",
        "trsvcid": "%d",
        "trtype": "tcp"
      },
      "portid": 2,
      "subsystems": [
        "%s"
      ]
    }""" % (tr_tcp_ipv4_svcid, subnqn))

        if tr_tcp_ipv6:
            ports_list.append("""
    {
      "addr": {
        "adrfam": "ipv6",
        "traddr": "::1",
        "trsvcid": "%d",
        "trtype": "tcp"
      },
      "portid": 3,
      "subsystems": [
        "%s"
      ]
    }""" % (tr_tcp_ipv6_svcid, subnqn))

        ports = ",".join(ports_list)

        json = """
{
  "ports": [
%s
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
        tmp.write(json % (ports, namespaces, subnqn))

    ret, out = udiskstestcase.run_command("nvmetcli restore %s" % tcli_json_file)
    os.unlink(tcli_json_file)
    if ret != 0:
        raise RuntimeError("Error setting up the NVMe target: %s" % out)
    time.sleep(2)


def disable_target_ns(subnqn, nsid, enable=False):
    """
    Disables or enables particular namespace on the target.

    :param str subnqn: Subsystem NQN
    :param int nsid: Namespace ID
    :param bool enable: Enable or disable the namespace
    """

    with open("/sys/kernel/config/nvmet/subsystems/%s/namespaces/%d/enable" % (subnqn, nsid), "w") as f:
        f.write("1" if enable else "0")


class UdisksNVMeTest(udiskstestcase.UdisksTestCase):
    SUBNQN = 'udisks_test_subnqn'
    DISCOVERY_NQN = 'nqn.2014-08.org.nvmexpress.discovery'
    NUM_NS = 2
    NS_SIZE = 1024**3
    _ipv6_available = udiskstestcase.UdisksTestCase.module_available('ipv6') and os.path.exists('/proc/net/if_inet6')

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not shutil.which("nvme"):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest("nvme executable (nvme-cli package) not found in $PATH, skipping.")
        if not shutil.which("nvmetcli"):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest("nvmetcli executable not found in $PATH, skipping.")
        ret, _out = udiskstestcase.run_command("modprobe nvme-fabrics")
        if ret != 0:
            raise unittest.SkipTest("nvme-fabrics kernel module unavailable, skipping.")

        cls.dev_files = []

        for _ in range(cls.NUM_NS):
            with tempfile.NamedTemporaryFile(prefix="udisks_test", delete=False, mode='w+b', dir='/var/tmp') as temp:
                temp.truncate(cls.NS_SIZE)
                cls.dev_files += [temp.name]

    def _nvme_disconnect(self, subnqn, ignore_errors=False):
        # force re-enable all exported namespaces
        for i in range(1, self.NUM_NS + 1):
            disable_target_ns(self.SUBNQN, i, enable=True)
        ret, out = self.run_command("nvme disconnect --nqn=%s" % subnqn)
        if not ignore_errors and (ret != 0 or 'disconnected 0 ' in out):
            raise RuntimeError("Error disconnecting the '%s' subsystem NQN: '%s'" % (subnqn, out))

    def _nvme_connect(self):
        ret, out = self.run_command("nvme connect --transport=loop --nqn=%s" % self.SUBNQN)
        if ret != 0:
            raise RuntimeError("Error connecting to the NVMe target: %s" % out)
        nvme_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        if len(nvme_devs) != 1:
            raise RuntimeError("Error looking up block device for the '%s' nqn" % self.SUBNQN)
        # at this point the NVMe controller may still be in a 'connecting' state and
        # kernels < 5.18 needean explicit uevent to deliver device info in a 'live' state
        self.udev_settle()
        self.run_command('udevadm trigger --subsystem-match=nvme --subsystem-match=block')

    def _find_block_objects_for_ctrl(self, ctrl_obj_path):
        namespaces = []
        obj_mgr = self.get_object('')
        objects = obj_mgr.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        for p in [p for p in list(objects.keys()) if "/block_devices/nvme" in p]:
            ns = self.get_device(p)
            drive_obj_path = self.get_property_raw(ns, '.Block', 'Drive')
            if drive_obj_path == str(ctrl_obj_path):
                namespaces += [p]
        return namespaces

    @classmethod
    def tearDownClass(cls):
        ret, out = udiskstestcase.run_command("nvmetcli clear")
        if ret != 0:
            raise RuntimeError("Error clearing the NVMe target: %s" % out)
        for d in cls.dev_files:
            try:
                os.unlink(d)
            except FileNotFoundError:
                pass
        udiskstestcase.UdisksTestCase.tearDownClass()

    def test_controller_info(self):
        setup_nvme_target(self.dev_files, self.SUBNQN)
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

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
        self.assertEqual(nqn, self.str_to_ay(self.SUBNQN))
        fguid = self.get_property_raw(drive_obj, '.NVMe.Controller', 'FGUID')
        self.assertEqual(len(fguid), 0)
        rev = self.get_property_raw(drive_obj, '.NVMe.Controller', 'NVMeRevision')
        self.assertGreater(len(rev), 0)
        unalloc_cap = self.get_property_raw(drive_obj, '.NVMe.Controller', 'UnallocatedCapacity')
        self.assertEqual(unalloc_cap, 0)

    def test_namespace_info(self):
        setup_nvme_target(self.dev_files, self.SUBNQN)
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

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
            self.assertEqual(len(eui64), 0)
            uuid = self.get_property_raw(ns, '.NVMe.Namespace', 'UUID')
            self.assertGreater(len(uuid), 10)
            wwn = self.get_property_raw(ns, '.NVMe.Namespace', 'WWN')
            self.assertTrue(wwn.startswith('uuid.'))
            lbaf = self.get_property_raw(ns, '.NVMe.Namespace', 'LBAFormats')
            self.assertEqual(len(lbaf), 1)
            self.assertEqual(lbaf[0], (4096, 0, 1))
            lbaf_curr = self.get_property_raw(ns, '.NVMe.Namespace', 'FormattedLBASize')
            self.assertEqual(lbaf_curr, (4096, 0, 1))
            nsize = self.get_property_raw(ns, '.NVMe.Namespace', 'NamespaceSize')
            self.assertEqual(nsize, self.NS_SIZE / lbaf_curr[0])
            ncap = self.get_property_raw(ns, '.NVMe.Namespace', 'NamespaceCapacity')
            self.assertEqual(ncap, self.NS_SIZE / lbaf_curr[0])
            nutl = self.get_property_raw(ns, '.NVMe.Namespace', 'NamespaceUtilization')
            self.assertEqual(nutl, self.NS_SIZE / lbaf_curr[0])
            format_progress = self.get_property_raw(ns, '.NVMe.Namespace', 'FormatPercentRemaining')
            self.assertEqual(format_progress, -1)

    def test_health_info(self):
        setup_nvme_target(self.dev_files, self.SUBNQN)
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

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
        msg = 'The NVMe controller has no support for self-test operations'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            drive_obj.SmartSelftestStart('xxx', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            drive_obj.SmartSelftestStart('short', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        msg = 'NVMe Device Self-test command error: Invalid Command Opcode'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            drive_obj.SmartSelftestAbort(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')

    def test_sanitize(self):
        setup_nvme_target(self.dev_files, self.SUBNQN)
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

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
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            drive_obj.SanitizeStart('xxx', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        msg = r'The NVMe controller has no support for the .* sanitize operation'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            drive_obj.SanitizeStart('block-erase', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            drive_obj.SanitizeStart('crypto-erase', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            drive_obj.SanitizeStart('overwrite', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')

    def test_format_ns(self):
        setup_nvme_target(self.dev_files, self.SUBNQN)
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

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
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                ns.FormatNamespace(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Namespace')

            d = dbus.Dictionary(signature='sv')
            with self.assertRaisesRegex(dbus.exceptions.DBusException, 'Unknown secure erase type xxx'):
                d['secure_erase'] = 'xxx'
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                d['secure_erase'] = 'user_data'
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                d['secure_erase'] = 'crypto_erase'
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')

            d = dbus.Dictionary(signature='sv')
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                d['lba_data_size'] = 0
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')
            lbaf_curr = self.get_property_raw(ns, '.NVMe.Namespace', 'FormattedLBASize')
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                d['lba_data_size'] = lbaf_curr[0]
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                d['lba_data_size'] = lbaf_curr[0]
                d['metadata_size'] = lbaf_curr[1]
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')

            msg = "Couldn't match desired LBA data block size in a device supported LBA format data sizes"
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                d['lba_data_size'] = dbus.UInt16(666)
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                d['lba_data_size'] = lbaf_curr[0]
                d['metadata_size'] = dbus.UInt16(5)
                ns.FormatNamespace(d, dbus_interface=self.iface_prefix + '.NVMe.Namespace')

    def test_ns_detach(self):
        setup_nvme_target(self.dev_files, self.SUBNQN)
        self._nvme_connect()
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

        ctrl_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrl_devs), 1)
        ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ns_devs), self.NUM_NS)

        # find drive object through the second namespace block object
        ns = self.get_device(ns_devs[1])
        self.assertHasIface(ns, 'org.freedesktop.UDisks2.NVMe.Namespace', timeout=60)
        drive_obj_path = self.get_property_raw(ns, '.Block', 'Drive')
        drive_obj = self.get_object(drive_obj_path)
        self.assertHasIface(drive_obj, 'org.freedesktop.UDisks2.NVMe.Controller', timeout=60)

        # this will wait up to 10 seconds for the state to switch
        state = self.get_property(drive_obj, '.NVMe.Controller', 'State')
        state.assertEqual('live', timeout=10)

        ctrl_size = self.get_property(drive_obj, '.Drive', 'Size')
        ctrl_size.assertEqual(0)

        # detach the second namespace
        nsid = self.get_property_raw(ns, '.NVMe.Namespace', 'NSID')
        disable_target_ns(self.SUBNQN, nsid)

        # wait for the namespace block object to disappear
        self.assertObjNotOnBus(str(ns.object_path))
        self.assertHasIface(drive_obj, 'org.freedesktop.UDisks2.NVMe.Controller', timeout=60)
        state = self.get_property(drive_obj, '.NVMe.Controller', 'State')
        state.assertEqual('live', timeout=10)

        ctrl_size = self.get_property(drive_obj, '.Drive', 'Size')
        ctrl_size.assertEqual(0)

        # attach that namespace back
        disable_target_ns(self.SUBNQN, nsid, enable=True)
        self.assertHasIface(ns, 'org.freedesktop.UDisks2.NVMe.Namespace', timeout=60)
        nsid_new = self.get_property(ns, '.NVMe.Namespace', 'NSID')
        nsid_new.assertEqual(nsid)

    def test_fabrics_connect(self):
        setup_nvme_target(self.dev_files, self.SUBNQN)
        manager = self.get_interface("/Manager", ".Manager.NVMe")
        with self.assertRaisesRegex(dbus.exceptions.DBusException, 'Invalid value specified for the transport address argument'):
            manager.Connect(self.str_to_ay(self.SUBNQN), "notransport", "", self.no_options)
        msg = r'Error connecting the controller: failed to write to nvme-fabrics device'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Connect(self.str_to_ay(self.SUBNQN), "loop", "127.0.0.1", self.no_options)
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Connect(self.str_to_ay("unknownsubnqn"), "loop", "", self.no_options)

        d = dbus.Dictionary(signature='sv')
        d['host_nqn'] = self.str_to_ay('nqn.2014-08.org.nvmexpress:uuid:01234567-8900-abcd-efff-abcdabcdabcd')
        d['host_id'] = self.str_to_ay('cccccccc-abcd-abcd-1234-1234567890ab')
        ctrl_obj_path = manager.Connect(self.str_to_ay(self.SUBNQN), "loop", "", d)
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

        ctrl = self.get_object(ctrl_obj_path)
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Controller')
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Fabrics')

        hostnqn = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostNQN')
        self.assertEqual(hostnqn, d['host_nqn'])
        hostid = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostID')
        self.assertEqual(hostid, d['host_id'])
        transport = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'Transport')
        self.assertEqual(transport, 'loop')
        tr_addr = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'TransportAddress')
        self.assertEqual(len(tr_addr), 1)   # the zero trailing byte

        ctrl.Disconnect(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Fabrics')
        ctrl = self.get_object(ctrl_obj_path)
        with self.assertRaisesRegex(dbus.exceptions.DBusException, r'Object does not exist at path .*|No such interface'):
            self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostNQN')

    def test_fabrics_connect_tcp(self):
        setup_nvme_target(self.dev_files, self.SUBNQN, tr_loop=False, tr_tcp_ipv4=True)
        manager = self.get_interface("/Manager", ".Manager.NVMe")
        msg = r'Error connecting the controller: failed to write to nvme-fabrics device'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Connect(self.str_to_ay(self.SUBNQN), "tcp", "192.168.255.255", self.no_options)
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Connect(self.str_to_ay("unknownsubnqn"), "tcp", "127.0.0.1", self.no_options)
        msg = r'Error connecting the controller: failed to get transport address'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Connect(self.str_to_ay(self.SUBNQN), "tcp", "192.368.1.1", self.no_options)

        d = dbus.Dictionary(signature='sv')
        d['host_nqn'] = self.str_to_ay('nqn.2014-08.org.nvmexpress:uuid:fedcba98-7654-3210-fedc-ba9876543210')
        d['host_id'] = self.str_to_ay('fedcba98-7654-3210-fedc-ba9876543210')
        ctrl_obj_path = manager.Connect(self.str_to_ay(self.SUBNQN), "tcp", "127.0.0.1", d)
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

        ctrl = self.get_object(ctrl_obj_path)
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Controller')
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Fabrics')

        hostnqn = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostNQN')
        self.assertEqual(hostnqn, d['host_nqn'])
        hostid = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostID')
        self.assertEqual(hostid, d['host_id'])
        transport = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'Transport')
        self.assertEqual(transport, 'tcp')
        tr_addr = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'TransportAddress')
        self.assertEqual(self.ay_to_str(tr_addr), 'traddr=127.0.0.1,trsvcid=4420,src_addr=127.0.0.1')

        ctrl.Disconnect(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Fabrics')
        ctrl = self.get_object(ctrl_obj_path)
        with self.assertRaisesRegex(dbus.exceptions.DBusException, r'Object does not exist at path .*|No such interface'):
            self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostNQN')

    def test_fabrics_connect_tcp_ipv6(self):
        if not self._ipv6_available:
            self.skipTest('ipv6 kernel module not available, skipping.')

        setup_nvme_target(self.dev_files, self.SUBNQN, tr_loop=False, tr_tcp_ipv6=True)
        manager = self.get_interface("/Manager", ".Manager.NVMe")
        msg = r'Error connecting the controller: failed to write to nvme-fabrics device'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Connect(self.str_to_ay(self.SUBNQN), "tcp", "::2", self.no_options)
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Connect(self.str_to_ay("unknownsubnqn"), "tcp", "::1", self.no_options)

        d = dbus.Dictionary(signature='sv')
        d['host_nqn'] = self.str_to_ay('nqn.2014-08.org.nvmexpress:uuid:abcdef01-2345-6789-abcd-ef0123456789')
        d['host_id'] = self.str_to_ay('abcdef01-2345-6789-abcd-ef0123456789')
        ctrl_obj_path = manager.Connect(self.str_to_ay(self.SUBNQN), "tcp", "::1", d)
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

        ctrl = self.get_object(ctrl_obj_path)
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Controller')
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Fabrics')

        hostnqn = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostNQN')
        self.assertEqual(hostnqn, d['host_nqn'])
        hostid = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostID')
        self.assertEqual(hostid, d['host_id'])
        transport = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'Transport')
        self.assertEqual(transport, 'tcp')
        tr_addr = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'TransportAddress')
        self.assertEqual(self.ay_to_str(tr_addr), 'traddr=::1,trsvcid=4420,src_addr=::1')

        ctrl.Disconnect(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Fabrics')
        ctrl = self.get_object(ctrl_obj_path)
        with self.assertRaisesRegex(dbus.exceptions.DBusException, r'Object does not exist at path .*|No such interface'):
            self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostNQN')

    def test_fabrics_ns_detach_all(self):
        setup_nvme_target(self.dev_files, self.SUBNQN)
        manager = self.get_interface("/Manager", ".Manager.NVMe")

        ctrl_obj_path = manager.Connect(self.str_to_ay(self.SUBNQN), "loop", "", self.no_options)
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

        ctrl_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrl_devs), 1)
        ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ns_devs), self.NUM_NS)

        ctrl = self.get_object(ctrl_obj_path)
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Controller')
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Fabrics')
        transport = self.get_property(ctrl, '.NVMe.Fabrics', 'Transport')
        transport.assertEqual('loop')
        subnqn = self.get_property(ctrl, '.NVMe.Controller', 'SubsystemNQN')
        subnqn.assertEqual(self.str_to_ay(self.SUBNQN))
        ctrl_size = self.get_property(ctrl, '.Drive', 'Size')
        ctrl_size.assertEqual(0)

        # count number of namespaces pointing to our controller
        namespaces = self._find_block_objects_for_ctrl(ctrl_obj_path)
        self.assertEqual(len(namespaces), self.NUM_NS)

        # detach all namespaces
        for i in range(1, self.NUM_NS + 1):
            disable_target_ns(self.SUBNQN, i)

        # verify the namespaces are gone
        for ns in namespaces:
            self.assertObjNotOnBus(ns)

        # count number of namespaces pointing to our controller
        namespaces = self._find_block_objects_for_ctrl(ctrl_obj_path)
        self.assertEqual(len(namespaces), 0)
        ctrl_size = self.get_property(ctrl, '.Drive', 'Size')
        ctrl_size.assertEqual(0)

        ctrl.Disconnect(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Fabrics')

    def test_persistent_dc(self):
        setup_nvme_target(self.dev_files, self.SUBNQN)
        manager = self.get_interface("/Manager", ".Manager.NVMe")
        with self.assertRaisesRegex(dbus.exceptions.DBusException, 'Invalid value specified for the transport address argument'):
            manager.Connect(self.str_to_ay(self.DISCOVERY_NQN), "notransport", "", self.no_options)
        msg = r'Error connecting the controller: failed to write to nvme-fabrics device'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Connect(self.str_to_ay(self.DISCOVERY_NQN), "loop", "127.0.0.1", self.no_options)

        ctrl_obj_path = manager.Connect(self.str_to_ay(self.DISCOVERY_NQN), "loop", "", self.no_options)
        self.addCleanup(self._nvme_disconnect, self.DISCOVERY_NQN, ignore_errors=True)

        ctrl = self.get_object(ctrl_obj_path)
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Controller')
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Fabrics')

        # health information should never be updated on a discovery controller
        smart_timestamp = self.get_property_raw(ctrl, '.NVMe.Controller', 'SmartUpdated')
        self.assertEqual(smart_timestamp, 0)
        nqn = self.get_property_raw(ctrl, '.NVMe.Controller', 'SubsystemNQN')
        self.assertEqual(nqn, self.str_to_ay(self.DISCOVERY_NQN))
        transport = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'Transport')
        self.assertEqual(transport, 'loop')
        tr_addr = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'TransportAddress')
        self.assertEqual(len(tr_addr), 1)   # the zero trailing byte

        # make sure that no block device object references this drive object
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        block_paths = [p for p in list(objects.keys()) if '/block_devices/' in p]
        for p in block_paths:
            self.assertNotEqual(str(objects[p]['org.freedesktop.UDisks2.Block']['Drive']), str(ctrl_obj_path))

        # the following methods should fail on discovery controllers
        msg = 'NVMe Health Information is only supported on I/O controllers'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            ctrl.SmartUpdate(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        msg = 'SMART data not collected'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            ctrl.SmartGetAttributes(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')

        msg = 'The NVMe controller has no support for self-test operations'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            ctrl.SmartSelftestStart('xxx', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            ctrl.SmartSelftestStart('short', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        msg = 'NVMe Device Self-test command error: Invalid Command Opcode'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            ctrl.SmartSelftestAbort(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')

        msg = r'The NVMe controller has no support for the .* sanitize operation'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            ctrl.SanitizeStart('block-erase', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            ctrl.SanitizeStart('crypto-erase', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            ctrl.SanitizeStart('overwrite', self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Controller')

        ctrl.Disconnect(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Fabrics')
        ctrl = self.get_object(ctrl_obj_path)
        with self.assertRaisesRegex(dbus.exceptions.DBusException, r'Object does not exist at path .*|No such interface'):
            self.get_property_raw(ctrl, '.NVMe.Fabrics', 'HostNQN')

    def test_fabrics_multipath(self):
        setup_nvme_target(self.dev_files, self.SUBNQN, tr_tcp_ipv4=True, tr_tcp_ipv4_svcid=44420, tr_tcp_ipv6=self._ipv6_available, tr_tcp_ipv6_svcid=44220)
        self.addCleanup(self._nvme_disconnect, self.SUBNQN, ignore_errors=True)

        # connect loop
        manager = self.get_interface("/Manager", ".Manager.NVMe")
        ctrl_obj_path = manager.Connect(self.str_to_ay(self.SUBNQN), "loop", "", self.no_options)

        ctrl_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrl_devs), 1)
        ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ns_devs), self.NUM_NS)

        ctrl = self.get_object(ctrl_obj_path)
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Controller')
        self.assertHasIface(ctrl, 'org.freedesktop.UDisks2.NVMe.Fabrics')

        # connect tcp over ipv4
        msg = r'Error connecting the controller: connection refused'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Connect(self.str_to_ay(self.SUBNQN), "tcp", "127.0.0.1", self.no_options)
        d = dbus.Dictionary(signature='sv')
        d['transport_svcid'] = "44420"
        ctrl2_obj_path = manager.Connect(self.str_to_ay(self.SUBNQN), "tcp", "127.0.0.1", d)

        ctrl_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ctrl_devs), 2)
        ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
        self.assertEqual(len(ns_devs), self.NUM_NS)

        ctrl2 = self.get_object(ctrl2_obj_path)
        self.assertHasIface(ctrl2, 'org.freedesktop.UDisks2.NVMe.Controller')
        self.assertHasIface(ctrl2, 'org.freedesktop.UDisks2.NVMe.Fabrics')

        # connect tcp over ipv6
        if self._ipv6_available:
            msg = r'Error connecting the controller: connection refused'
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                manager.Connect(self.str_to_ay(self.SUBNQN), "tcp", "::1", self.no_options)

            d = dbus.Dictionary(signature='sv')
            d['transport_svcid'] = "44220"
            ctrl3_obj_path = manager.Connect(self.str_to_ay(self.SUBNQN), "tcp", "::1", d)

            ctrl_devs = find_nvme_ctrl_devs_for_subnqn(self.SUBNQN)
            self.assertEqual(len(ctrl_devs), 3)
            ns_devs = find_nvme_ns_devs_for_subnqn(self.SUBNQN)
            self.assertEqual(len(ns_devs), self.NUM_NS)

            ctrl3 = self.get_object(ctrl3_obj_path)
            self.assertHasIface(ctrl3, 'org.freedesktop.UDisks2.NVMe.Controller')
            self.assertHasIface(ctrl3, 'org.freedesktop.UDisks2.NVMe.Fabrics')

        # verify objects
        transport = self.get_property(ctrl, '.NVMe.Fabrics', 'Transport')
        transport.assertEqual('loop')
        tr_addr = self.get_property_raw(ctrl, '.NVMe.Fabrics', 'TransportAddress')
        self.assertEqual(len(tr_addr), 1)   # the zero trailing byte
        subnqn = self.get_property(ctrl, '.NVMe.Controller', 'SubsystemNQN')
        subnqn.assertEqual(self.str_to_ay(self.SUBNQN))
        ctrl_size = self.get_property(ctrl, '.Drive', 'Size')
        ctrl_size.assertEqual(0)

        transport = self.get_property(ctrl2, '.NVMe.Fabrics', 'Transport')
        transport.assertEqual('tcp')
        tr_addr = self.get_property_raw(ctrl2, '.NVMe.Fabrics', 'TransportAddress')
        self.assertEqual(self.ay_to_str(tr_addr), 'traddr=127.0.0.1,trsvcid=44420,src_addr=127.0.0.1')
        subnqn = self.get_property(ctrl2, '.NVMe.Controller', 'SubsystemNQN')
        subnqn.assertEqual(self.str_to_ay(self.SUBNQN))
        ctrl_size = self.get_property(ctrl2, '.Drive', 'Size')
        ctrl_size.assertEqual(0)

        if self._ipv6_available:
            transport = self.get_property(ctrl3, '.NVMe.Fabrics', 'Transport')
            transport.assertEqual('tcp')
            tr_addr = self.get_property_raw(ctrl3, '.NVMe.Fabrics', 'TransportAddress')
            self.assertEqual(self.ay_to_str(tr_addr), 'traddr=::1,trsvcid=44220,src_addr=::1')
            subnqn = self.get_property(ctrl3, '.NVMe.Controller', 'SubsystemNQN')
            subnqn.assertEqual(self.str_to_ay(self.SUBNQN))
            ctrl_size = self.get_property(ctrl3, '.Drive', 'Size')
            ctrl_size.assertEqual(0)

        # count number of namespaces pointing to our controller
        ns_ctrl3 = ()
        ns_ctrl1 = self._find_block_objects_for_ctrl(ctrl_obj_path)
        ns_ctrl2 = self._find_block_objects_for_ctrl(ctrl2_obj_path)
        if self._ipv6_available:
            ns_ctrl3 = self._find_block_objects_for_ctrl(ctrl3_obj_path)
        # NOTE: due to org.freedesktop.UDisks2.Block.Drive property single-path
        #       limitation, it may randomly point to any of the active controllers.
        #       That's still perfectly valid in a multipath scenario.
        self.assertEqual(len(ns_ctrl1) + len(ns_ctrl2) + len(ns_ctrl3), self.NUM_NS)

        # disconnect the first controller and watch the drive object references change
        ctrl.Disconnect(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Fabrics')
        ns_ctrl1 = self._find_block_objects_for_ctrl(ctrl_obj_path)
        self.assertEqual(len(ns_ctrl1), 0)
        ns_ctrl2 = self._find_block_objects_for_ctrl(ctrl2_obj_path)
        if self._ipv6_available:
            ns_ctrl3 = self._find_block_objects_for_ctrl(ctrl3_obj_path)
        self.assertEqual(len(ns_ctrl2) + len(ns_ctrl3), self.NUM_NS)

        ctrl2.Disconnect(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Fabrics')
        if self._ipv6_available:
            ns_ctrl1 = self._find_block_objects_for_ctrl(ctrl_obj_path)
            self.assertEqual(len(ns_ctrl1), 0)
            ns_ctrl2 = self._find_block_objects_for_ctrl(ctrl2_obj_path)
            self.assertEqual(len(ns_ctrl2), 0)
            ns_ctrl3 = self._find_block_objects_for_ctrl(ctrl3_obj_path)
            self.assertEqual(len(ns_ctrl3), self.NUM_NS)
            ctrl3.Disconnect(self.no_options, dbus_interface=self.iface_prefix + '.NVMe.Fabrics')

    def test_hostnqn(self):
        HOSTNQN_PATH = '/etc/nvme/hostnqn'
        HOSTID_PATH = '/etc/nvme/hostid'
        FAKE_HOSTNQN = 'nqn.2014-08.org.nvmexpress:uuid:beefbeef-beef-beef-beef-beefdeadbeef'
        FAKE_HOSTID = 'beeeeeef-beef-beef-beef-beefdeadbeef'

        if PACKAGE_SYSCONF_DIR != '/etc':
            self.skipTest("UDisks has been configured in non-system prefix, skipping...")

        # save hostnqn and hostid files
        try:
            saved_hostnqn = self.read_file(HOSTNQN_PATH)
            self.addCleanup(self.write_file, HOSTNQN_PATH, saved_hostnqn)
        except:
            self.addCleanup(self.remove_file, HOSTNQN_PATH, ignore_nonexistent=True)
        try:
            saved_hostid = self.read_file(HOSTID_PATH)
            self.addCleanup(self.write_file, HOSTID_PATH, saved_hostid)
        except:
            self.addCleanup(self.remove_file, HOSTID_PATH, ignore_nonexistent=True)
        self.remove_file(HOSTNQN_PATH, ignore_nonexistent=True)
        self.remove_file(HOSTID_PATH, ignore_nonexistent=True)

        # an external event, inotify watch on the daemon side
        time.sleep(2)

        manager = self.get_interface('/Manager', '.Manager.NVMe')
        hostnqn = self.get_property_raw(manager, '.Manager.NVMe', 'HostNQN')
        self.assertTrue(self.ay_to_str(hostnqn).startswith('nqn.2014-08.org.nvmexpress:uuid:'))
        hostid = self.get_property_raw(manager, '.Manager.NVMe', 'HostID')
        self.assertEqual(len(hostid), 1)  # the zero trailing byte

        manager.SetHostNQN(self.str_to_ay(FAKE_HOSTNQN), self.no_options, dbus_interface=self.iface_prefix + '.Manager.NVMe')
        manager.SetHostID(self.str_to_ay(FAKE_HOSTID), self.no_options, dbus_interface=self.iface_prefix + '.Manager.NVMe')
        hostnqn = self.get_property_raw(manager, '.Manager.NVMe', 'HostNQN')
        self.assertEqual(hostnqn, self.str_to_ay(FAKE_HOSTNQN))
        hostid = self.get_property_raw(manager, '.Manager.NVMe', 'HostID')
        self.assertEqual(hostid, self.str_to_ay(FAKE_HOSTID))

        self.remove_file(HOSTNQN_PATH, ignore_nonexistent=True)
        self.remove_file(HOSTID_PATH, ignore_nonexistent=True)
