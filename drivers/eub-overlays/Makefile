dtbo-y += eub_mobo.dtbo eub_power.dtbo eub_i2c.dtbo eub_rtc.dtbo eub_dac.dtbo

targets += $(dtbo-y)

always		:= $(dtbo-y)
clean-files	:= *.dtbo

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd)

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install:
	install $(dtbo-y) /boot/overlays/

uninstall:
	rm $(addprefix /boot/overlays/,$(dtbo-y))
