# Generate config.h.py with translated defines for easy use within Python

CONFIG_H_PY = config_h.py
MODULES =

if HAVE_BTRFS
MODULES += btrfs
endif

if HAVE_ISCSI
MODULES += iscsi
endif

if HAVE_LVM2
MODULES += lvm2
endif

if HAVE_ZRAM
MODULES += zram
endif

if HAVE_LSM
MODULES += lsm
endif

if HAVE_BCACHE
MODULES += bcache
endif

$(CONFIG_H_PY):
	echo -n 'UDISKS_MODULES_ENABLED = { ' > $(CONFIG_H_PY)
	for i in $(MODULES); do \
		echo -n "'$$i', " >> $(CONFIG_H_PY); \
	done
	echo '}' >> $(CONFIG_H_PY)

all: $(CONFIG_H_PY)

EXTRA_DIST =

clean-local:
	rm -f *~ $(CONFIG_H_PY)
