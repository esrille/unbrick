MODULE_VERSION ?= 0.1.0
DKMS_DIR := /usr/src/eub_mouse-$(MODULE_VERSION)
DKMS_KEY := -m eub_mouse -v $(MODULE_VERSION)

obj-m := eub_mouse.o

ccflags-y += -std=gnu99 -Wall -Wno-declaration-after-statement -I /usr/src/eub-headers-$(MODULE_VERSION)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install:
	mkdir -p $(DKMS_DIR)
	cp Makefile dkms.conf eub_mouse.c $(DKMS_DIR)
	dkms add $(DKMS_KEY)
	dkms build $(DKMS_KEY)
	dkms install $(DKMS_KEY) --force

uninstall:
	dkms remove --all $(DKMS_KEY)
	rm -rf $(DKMS_DIR)
