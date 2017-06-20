import os
import subprocess
import glob
import syslog

from distutils.version import LooseVersion


class ForceClean(object):
    """Class for force cleaning after tests have finished. The reason for this
       is to ensure the same conditions for the next test run.
       If everything works, this does nothing. Otherwise it informs about
       actions performed and also writes them in the syslog."""

    def __init__(self):
        self.org_state_snapshot = None

    @staticmethod
    def _run_command(command):
        res = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        out, _err = res.communicate()
        return out.decode().strip()

    def _get_snapshot(self):
        """Return current machine state - used for before
           and after state comparison"""
        result = self._run_command("lsblk -nlo NAME")
        return result

    def record_state(self):
        """Get the first machine snapshot"""
        self.org_state_snapshot = self._get_snapshot()

    @staticmethod
    def _get_diff(old_state, new_state):
        old_list = old_state.split("\n")
        new_list = new_state.split("\n")
        diff = set(new_list) - set(old_list)
        return diff

    def restore_state(self):
        """Get the second machine snapshot, compare it to the first one and perform
           the cleaning. Return list of devices that could not be removed."""
        new_state_snapshot = self._get_snapshot()
        diff = self._get_diff(self.org_state_snapshot, new_state_snapshot)

        if diff:
            print("These devices were not properly cleaned by tests:\n" + "\n".join(diff))
            print("Removing by force...")
            # Put information into the syslog to be able to track possible issues
            syslog.syslog("Following devices were not removed after UDisks2 D-Bus" +
                          " tests and will be removed by force: %s" % str(diff)[1:-1])

        import blivet

        # we need at least blivet 2.0 to do this cleanup
        if LooseVersion(blivet.__version__) >= LooseVersion("2.0.0"):
            blvt = blivet.Blivet()
            blvt.reset()
            for device in diff:
                # kill all processes that are using the device
                # get list of mountpoints from blivet mountpoint dictionary
                mountpoints = [mpoint for mpoint, dev in
                               blivet.mounts.mounts_cache.mountpoints.items()
                               if dev == device]

                for mountpoint in mountpoints:
                    self._run_command("fuser -km %s" % mountpoint)

            # just try to remove everything
            blvt.config.exclusive_disks = diff
            blvt.reset()
            blvt.devicetree.teardown_all()

        self._run_command("modprobe -r scsi_debug")
        self._run_command("targetcli clearconfig confirm=True")
        for disk_file in glob.glob("/var/tmp/udisks_test_disk*"):
            os.unlink(disk_file)

        cleaned_state_snapshot = self._get_snapshot()
        not_cleaned = self._get_diff(self.org_state_snapshot, cleaned_state_snapshot)

        if not_cleaned:
            print("Failed to remove following devices:\n" + "\n".join(not_cleaned))
        else:
            print("Devices successfully removed.")

        return not_cleaned
