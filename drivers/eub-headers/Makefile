VERSION = 0.1.0

MFD_HEADERS = eub_mobo.h eub_power.h

srcdir ?= /usr/src/eub-headers-$(VERSION)
linuxdir = $(srcdir)/linux
mfddir = $(linuxdir)/mfd

all:

install:
	install -d $(mfddir)
	install $(addprefix linux/mfd/,$(MFD_HEADERS)) $(mfddir)

uninstall:
	rm $(addprefix $(mfddir)/,$(MFD_HEADERS))
	rmdir --ignore-fail-on-non-empty $(mfddir)
	rmdir --ignore-fail-on-non-empty $(linuxdir)
	rmdir --ignore-fail-on-non-empty $(srcdir)

.PHONY: install uninstall
