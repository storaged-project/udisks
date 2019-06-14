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

GIT_REPO="https://github.com/storaged-project/udisks.git"
REPO_DIR="udisks"

rlJournalStart
    rlPhaseStartSetup
        rlAssertRpm git

        rlRun "cd /home/udisks"
        rlRun "su -c 'git clone $GIT_REPO' udisks"
        rlRun "cd $REPO_DIR"
        rlRun "dnf builddep -y packaging/udisks2.spec"
    rlPhaseEnd

    rlPhaseStartTest
        rlRun "su -c './autogen.sh' udisks"
        rlRun "su -c './configure --enable-modules --localstatedir=/var' udisks"
        rlRun "su -c 'make' udisks"

        rlRun "make install"
        rlRun "cp data/org.freedesktop.UDisks2 /usr/share/dbus-1/system.d/"
        rlRun "cp data/org.freedesktop.UDisks2.policy /usr/share/polkit-1/"
        rlRun "cp modules/lvm2/data/org.freedesktop.UDisks2.lvm2.policy /usr/share/polkit-1/"
    rlPhaseEnd

    rlPhaseStartCleanup
    rlPhaseEnd

rlJournalPrintText
rlJournalEnd
