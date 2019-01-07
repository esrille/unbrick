CFLAGS ?= -std=gnu99 -Wall -Wno-declaration-after-statement -Wno-unused-function

PROGRAMS = eub_i2cattach eub_i2cpoweroff
SERVICES = eub-i2c.service eub-poweroff.service

prefix ?= /usr/local
bindir = $(prefix)/bin

all: compile service

compile: $(PROGRAMS)

service: $(SERVICES)

distclean: clean

clean:
	rm -f $(PROGRAMS) *.service *.o

eub_i2cattach : eub_i2cattach.o eub_i2c.o

eub_i2cpoweroff : eub_i2cpoweroff.o eub_i2c.o

%.service : %.service.in
	sed "s^@@PREFIX@@^$(prefix)^g" < $< > $@

install: all
	install -d $(bindir)
	install $(PROGRAMS) $(bindir)
	install -d /etc/systemd/system
	install -m 644 $(SERVICES) /etc/systemd/system
	install -d /etc/udev/rules.d/
	install -m 644 85-hwclock.rules /etc/udev/rules.d/
	install -m 644 95-eub_touch.rules /etc/udev/rules.d/
	install -d /usr/share/X11/xorg.conf.d/
	install -m 644 95-eub_touch.conf /usr/share/X11/xorg.conf.d/
	systemctl daemon-reload

uninstall:
	rm $(addprefix $(bindir)/,$(PROGRAMS))
	rm $(addprefix /etc/systemd/system/,$(SERVICES))
	rm /etc/udev/rules.d/95-eub_touch.rules
	rm /usr/share/X11/xorg.conf.d/95-eub_touch.conf

enable:
	systemctl enable $(SERVICES)

disable:
	systemctl disable $(SERVICES)

.PHONY: compile clean distclean install uninstall enable disable
