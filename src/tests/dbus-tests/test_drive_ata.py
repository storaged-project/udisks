import os
import dbus
import re
import unittest
import time

from udiskstestcase import UdisksTestCase

SMART_CMDLINE_FAIL      = 1 << 0
SMART_OPEN_READ_FAIL    = 1 << 1
SMART_ATA_CHECKSUM_FAIL = 1 << 2

# disks which (don't) support S.M.A.R.T.
smart_unsupported = set()
smart_supported = set()

sata_disks = (dev for dev in os.listdir("/dev") if re.match(r'sd[a-z]+$', dev))
for disk in sata_disks:
    ret, out = UdisksTestCase.run_command("smartctl -a /dev/%s" % disk)

    # Only the following bits in the exit status mean the device failed to
    # provide valid SMART data, others may be set for different reasons (see
    # man:smartctl(8) for details).
    #
    # NOTE: There seems to be a bug in smartctl because its exit status is 1 in
    # case of "/dev/sdb: Unknown USB bridge [0x46f4:0x0001 (0x000)]" which
    # doesn't really look like a "Command line did not parse."
    if (ret & (SMART_CMDLINE_FAIL | SMART_OPEN_READ_FAIL | SMART_ATA_CHECKSUM_FAIL)) == 0:
        smart_supported.add(disk)
    else:
        smart_unsupported.add(disk)

class UdisksDriveAtaTest(UdisksTestCase):
    '''Noninvasive tests for the Drive.Ata interface'''

    def get_smart_setting(self, disk, attr, out_prefix):
        _, out = self.run_command("smartctl -g%s /dev/%s" % (attr, disk))
        for line in out.splitlines():
            if line.startswith(out_prefix):
                prefix, sep, val = line.partition(":")
                return val.strip()

    def get_smart_info(self, disk, out_prefix, idx=1):
        _, out = self.run_command("smartctl -i /dev/%s" % disk)
        for line in out.splitlines():
            if line.startswith(out_prefix):
                if idx > 1:
                    idx -= 1
                else:
                    prefix, sep, val = line.partition(":")
                    return val.strip()

    def get_health_status(self, disk):
        ret, _ = self.run_command("smartctl -H /dev/%s" % disk)
        return ret == 8         # see smartctl(8) - "EXIT STATUS"

    def get_attrs(self, disk):
        attrs = dict()
        _, out = self.run_command("smartctl -A /dev/%s" % disk)
        lines = iter(out.splitlines())
        line = next(lines)
        # skip the lines at the beginning we don't care about
        while not line.startswith("ID# ATTRIBUTE_NAME"):
            line = next(lines)
        for line in lines:
            fields = re.split(r'\s+', line.strip())
            if len(fields) >= 10:
                attrs[int(fields[0])] = tuple(fields[1:])

        return attrs


    @unittest.skipUnless(smart_supported, "No disks supporting S.M.A.R.T. available")
    def test_iface_present(self):
        for disk in smart_supported:
            drive_name = self.get_drive_name(self.get_device(disk))
            drive_obj = self.get_object("/drives/%s" % drive_name)
            drive_intro = dbus.Interface(drive_obj, "org.freedesktop.DBus.Introspectable")
            intro_data = drive_intro.Introspect()
            self.assertIn('interface name="org.freedesktop.UDisks2.Drive.Ata"', intro_data)

        for disk in smart_unsupported:
            drive_name = self.get_drive_name(self.get_device(disk))
            drive_obj = self.get_object("/drives/%s" % drive_name)
            drive_intro = dbus.Interface(drive_obj, "org.freedesktop.DBus.Introspectable")
            intro_data = drive_intro.Introspect()
            self.assertNotIn('interface name="org.freedesktop.UDisks2.Drive.Ata"', intro_data)

    @unittest.skipUnless(smart_supported, "No disks supporting S.M.A.R.T. available")
    def test_properties(self):
        for disk in smart_supported:
            props = {"aam": self.get_smart_setting(disk, "aam", "AAM feature is"),
                     "apm": self.get_smart_setting(disk, "apm", "APM feature is"),
                     "lookahead": self.get_smart_setting(disk, "lookahead", "Rd look-ahead is"),
                     # XXX: any idea how to get what is reported as PmSupported/PmEnabled????
                     # XXX: security-frozen seems to be unsupported by smartctl itself
                     "smart_supported": self.get_smart_info(disk, "SMART support is:", 1),
                     "smart_enabled": self.get_smart_info(disk, "SMART support is:", 2),
                     "wcache": self.get_smart_setting(disk, "wcache", "Write cache is"),
                     "healthy": self.get_health_status(disk) == 0,
                    }

            drive_name = self.get_drive_name(self.get_device(disk))
            drive_obj = self.get_object("/drives/%s" % drive_name)

            self.get_property(drive_obj, ".Drive.Ata", "AamSupported").assertEqual(props["aam"] != "Unavailable")
            self.get_property(drive_obj, ".Drive.Ata", "AamEnabled").assertEqual(props["aam"] == "Enabled")
            self.get_property(drive_obj, ".Drive.Ata", "ApmSupported").assertEqual(props["apm"] != "Unavailable")
            self.get_property(drive_obj, ".Drive.Ata", "ApmEnabled").assertEqual(props["apm"] == "Enabled")
            self.get_property(drive_obj, ".Drive.Ata", "ReadLookaheadSupported").assertEqual(props["lookahead"] != "Unavailable")
            self.get_property(drive_obj, ".Drive.Ata", "ReadLookaheadEnabled").assertEqual(props["lookahead"] == "Enabled")
            self.get_property(drive_obj, ".Drive.Ata", "SmartSupported").assertEqual(props["smart_supported"].startswith("Available"))
            self.get_property(drive_obj, ".Drive.Ata", "SmartEnabled").assertEqual(props["smart_enabled"] == "Enabled")
            self.get_property(drive_obj, ".Drive.Ata", "WriteCacheSupported").assertEqual(props["wcache"] != "Unavailable")
            self.get_property(drive_obj, ".Drive.Ata", "WriteCacheEnabled").assertEqual(props["wcache"] == "Enabled")
            self.get_property(drive_obj, ".Drive.Ata", "SmartFailing").assertEqual(not props["healthy"])

            attrs = self.get_attrs(disk)
            # temperature has ID 190
            temp_attr = attrs.get(190)
            if temp_attr:
                # reported in Kelvins (double) by API, but in Celsius degrees (int) by CLI
                temp_c = self.get_property(drive_obj, ".Drive.Ata", "SmartTemperature").value - 273
                # nineth field is the raw value
                self.assertEqual(int(temp_c), int(temp_attr[8]))

            # power-on-hours has ID 9
            pwon_attr = attrs.get(9)
            if pwon_attr:
                # reported in seconds by API, but in hours by CLI
                pwon_s = self.get_property(drive_obj, ".Drive.Ata", "SmartPowerOnSeconds")
                # nineth field is the raw value
                self.assertEqual(int(pwon_s.value / 3600), int(pwon_attr[8]))

    @unittest.skipUnless(smart_supported, "No disks supporting S.M.A.R.T. available")
    def test_smart_get_attributes(self):
        for disk in smart_supported:
            drive_name = self.get_drive_name(self.get_device(disk))
            drive_ata = self.get_interface("/drives/%s" % drive_name, ".Drive.Ata")

            ret = drive_ata.SmartGetAttributes(self.no_options)
            attrs = self.get_attrs(disk)
            for ret_tup in ret:
                # get what we got from 'smartctl -A' for the same id
                attr = attrs[ret_tup[0]]

                # there should be some name of the attribute, but a different
                # than the one we get from smartctl (different case, _ replaced with -, etc.)
                self.assertTrue(ret_tup[1])

                # flags (we get hexa)
                self.assertEqual(ret_tup[2], int(attr[1], 16))

                # value
                self.assertEqual(ret_tup[3], int(attr[2]))

                # worst
                self.assertEqual(ret_tup[4], int(attr[3]))

                # threshold
                self.assertEqual(ret_tup[5], int(attr[4]))

                # pretty value (different units from 'smartctl -A' for temperature, time,...)
                if ret_tup[0] == 190: # temperature (milikelvin -> celsius)
                    self.assertEqual(int((ret_tup[6] / 1000) - 273), int(attr[8]))

                if ret_tup[0] == 9: # miliseconds -> hours
                    self.assertEqual(int(ret_tup[6] / 3600000), int(attr[8]))

    @unittest.skipUnless(smart_supported, "No disks supporting S.M.A.R.T. available")
    def test_smart_update(self):
        for disk in smart_supported:
            drive_name = self.get_drive_name(self.get_device(disk))
            drive_obj = self.get_object("/drives/%s" % drive_name)
            drive_ata = self.get_interface(drive_obj, ".Drive.Ata")

            # has to have a valid timestamp
            updated = self.get_property(drive_obj, ".Drive.Ata", "SmartUpdated")
            updated.assertTrue()
            orig = int(updated.value)

            # wait at least a second so that the timestamp has a chance to change
            time.sleep(1)
            drive_ata.SmartUpdate(self.no_options)
            updated = self.get_property(drive_obj, ".Drive.Ata", "SmartUpdated")
            updated.assertTrue()
            self.assertGreater(int(updated.value), orig)

            orig = int(updated.value)
            time.sleep(1)
            drive_ata.SmartUpdate(self.no_options)
            updated = self.get_property(drive_obj, ".Drive.Ata", "SmartUpdated")
            updated.assertTrue()
            self.assertGreater(int(updated.value), orig)
