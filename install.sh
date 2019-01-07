#!/bin/sh -x
sudo apt install dkms raspberrypi-kernel-headers
cd drivers
cd eub-headers
sudo make install; cd ..
cd eub_backlight
sudo make install; cd ..
cd eub_battery
sudo make install; cd ..
cd eub_dac
sudo make install; cd ..
cd eub_i2c
sudo make install; cd ..
cd eub_mobo
sudo make install; cd ..
cd eub_mouse
sudo make install; cd ..
cd eub_power
sudo make install; cd ..
cd eub_touch
sudo make install; cd ..
cd eub-overlays
make; sudo make install; cd ..
cd eub-utils
make; sudo make install; sudo make enable; cd ..
