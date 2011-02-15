
policydir = $(datadir)/polkit-1/actions
dist_policy_DATA = org.freedesktop.udisks2.policy

# TODO: merge translations from somewhere
#
org.freedesktop.udisks2.policy : org.freedesktop.udisks2.policy.in Makefile
	cp $< $@

clean-local :
	rm -f *~ *.policy

DISTCLEANFILES = $(dist_policy_DATA)

EXTRA_DIST = $(dist_policy_DATA:.policy=.policy.in)
