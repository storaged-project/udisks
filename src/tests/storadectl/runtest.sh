#!/bin/bash
# vim: dict+=/usr/share/beakerlib/dictionary.vim cpt=.,w,b,u,t,i,k
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
#   Author: Martin Hatina <mhatina@redhat.com>
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
#   Copyright (c) 2015 Red Hat, Inc.
#
#   This copyrighted material is made available to anyone wishing
#   to use, modify, copy, or redistribute it subject to the terms
#   and conditions of the GNU General Public License version 2.
#
#   This program is distributed in the hope that it will be
#   useful, but WITHOUT ANY WARRANTY; without even the implied
#   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
#   PURPOSE. See the GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public
#   License along with this program; if not, write to the Free
#   Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
#   Boston, MA 02110-1301, USA.
#
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

# Include Beaker environment
. /usr/bin/rhts-environment.sh || exit 1
. /usr/share/beakerlib/beakerlib.sh || exit 1

rlJournalStart
    rlPhaseStartSetup

    rlPhaseEnd

    rlPhaseStartTest "help"
        rlRun "storagectl help > help.txt"
        if [ ! -s help.txt ]; then
            rlFail "No help displayed!"
        fi
    rlPhaseEnd

    rlPhaseStartTest "info"
        rlRun "storagectl info -b /dev/vda2 > vda2.txt"
        # SIZE
        rlRun "cat vda2.txt | grep `lsblk -b | grep vda2 | awk {'print $4'}`"
        # IdType
        rlRun "cat vda2.txt | grep `lsblk -f | grep vda2 | awk {'print $3'}`"
        # IdUUID
        rlRun "cat vda2.txt | grep `lsblk -f | grep vda2 | awk {'print $2'}`"
        # Symlinks
        rlRun "SYMLINKS=`cat vda2.txt | grep Symlinks | awk {'print $2'}"
        rlRun "diff `cat vda2.txt` `storagectl info -b $SYMLINKS"
        # UUID
        rlAssertGrep `lsblk --output-all | grep vda2 | awk {'print $6'}` vda2.txt
    rlPhaseEnd

    rlPhaseStartTest "dump"
        rlRun "storagectl dump > dump.txt"
        for dev in `ll /dev/block/ | grep -v total | awk {'print $11'} | sed 's/..\///'`
        do
            rlAssertGrep " Device: /dev/$dev" dump.txt
        done
    rlPhaseEnd

    rlPhaseStartTest "status"
        rlRun "storagectl status > status.txt"
        rlRun "lsblk > lsblk.txt"

        for dev in `cat status.txt`
        do
            rlAssertGrep $dev lsblk.txt
        done
    rlPhaseEnd

    rlPhaseStartTest "monitor"
        rlRun "storagectl monitor &"
        rlRun "ps -ax | grep storagectl"
    rlPhaseEnd

    rlPhaseStartTest "mount"
        # not finished
        rlRun "storagectl mount -b /dev/vda3"
    rlPhaseEnd

    rlPhaseStartCleanup
        rlRun "rm -rf *.txt"
        rlRun "pkill -9 storagectl"
    rlPhaseEnd

rlJournalPrintText
rlJournalEnd
