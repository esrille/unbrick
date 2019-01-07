MODULE_VERSION ?= 0.1.0
DKMS_DIR := /usr/src/eub_dac-$(MODULE_VERSION)
DKMS_KEY := -m eub_dac -v $(MODULE_VERSION)

obj-m := snd-soc-eub_dac.o

snd-soc-eub_dac-objs := eub_dac.o

ccflags-y += -std=gnu99 -Wall -Wno-declaration-after-statement -I /usr/src/eub-headers-$(MODULE_VERSION)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install:
	mkdir -p $(DKMS_DIR)
	cp Makefile dkms.conf eub_dac.c pcm512x.h $(DKMS_DIR)
	dkms add $(DKMS_KEY)
	dkms build $(DKMS_KEY)
	dkms install $(DKMS_KEY) --force

uninstall:
	dkms remove --all $(DKMS_KEY)
	rm -rf $(DKMS_DIR)
