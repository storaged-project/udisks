
devkit_policydir = $(datadir)/PolicyKit/policy

dist_devkit_policy_DATA = org.freedesktop.devicekit.disks.policy

@INTLTOOL_POLICY_RULE@

check:
	polkit-policy-file-validate $(dist_devkit_policy_DATA)

clean-local :
	rm -f *~

DISTCLEANFILES = org.freedesktop.devicekit.disks.policy

EXTRA_DIST = org.freedesktop.devicekit.disks.policy.in
