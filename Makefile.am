## Process this file with automake to produce Makefile.in

SHELL = /bin/bash
.SHELLFLAGS = -o pipefail -c

PYTHON ?= python3

NULL =

SUBDIRS = data udisks src tools modules po doc

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

GCC_COLORS ?= 'auto'
export GCC_COLORS

EXTRA_DIST =                                                                   \
	HACKING                                                                \
	README.md                                                              \
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS =                                                    \
	--enable-gtk-doc                                                       \
	--enable-modules                                                       \
	--disable-introspection                                                \
	--with-udevdir=$$dc_install_base/$(udevdir)                            \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)  \
	$(NULL)

sign: dist
	gpg --armor --detach-sign --output udisks-$(VERSION).tar.bz2.sign udisks-$(VERSION).tar.bz2

shortlog:
	git shortlog -r --no-merges $$(git tag -l | tail -n 1)..HEAD

clean-local:
	rm -f *~

dbus-tests:
	$(MAKE) all &>/dev/null
	sudo $(PYTHON) src/tests/dbus-tests/run_tests.py -l dbus_tests.log |& tee dbus_tests_output.log \
       && echo "dbus-tests: SUCCESS" >> RESULTS || echo "dbus-tests: FAILED" >> RESULTS
	@tail -1 RESULTS | grep -q "dbus-tests: SUCCESS"

unittests:
	$(MAKE) all &>/dev/null
	cd src/tests && ./udisks-test | tee -a ../../unittests_output.log \
       && echo "unittests: SUCCESS" >> ../../RESULTS || echo "unittests: FAILED" >> ../../RESULTS
	@tail -1 RESULTS | grep -q "unittests: SUCCESS"

integration-tests:
	$(MAKE) all &>/dev/null
	cd src/tests && sudo $(PYTHON) ./integration-test | tee -a ../../integration_tests_output.log \
       && echo "integration-tests: SUCCESS" >> ../../RESULTS || echo "integration-tests: FAILED" >> ../../RESULTS
	@tail -1 RESULTS | grep -q "integration-tests: SUCCESS"

ci: all
	sudo rm -f *.log RESULTS STATUS
	-$(MAKE) check
	-$(MAKE) dbus-tests
	-$(MAKE) unittests
	-$(MAKE) integration-tests
	@grep -q FAILED RESULTS && echo -n failure > STATUS || echo -n success > STATUS
	@grep -q success STATUS

RPMDIR = `readlink -f $(top_builddir)/packaging`
SRCDIR = `readlink -f $(top_srcdir)`
rpm: dist-bzip2
	rpmbuild \
		--define "_sourcedir $(SRCDIR)" \
		--define "_specdir $(RPMDIR)" \
		--define "_builddir $(RPMDIR)" \
		--define "_srcrpmdir $(RPMDIR)" \
		--define "_rpmdir $(RPMDIR)" \
		-ba $(SRCDIR)/packaging/udisks2.spec
srpm: dist-bzip2
	rpmbuild \
		--define "_sourcedir $(SRCDIR)" \
		--define "_specdir $(RPMDIR)" \
		--define "_builddir $(RPMDIR)" \
		--define "_srcrpmdir $(RPMDIR)" \
		--define "_rpmdir $(RPMDIR)" \
		-bs $(SRCDIR)/packaging/udisks2.spec
