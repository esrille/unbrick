#!/bin/sh -x
cd drivers
cd eub_backlight
sudo make uninstall; cd ..
cd eub_battery
sudo make uninstall; cd ..
cd eub_dac
sudo make uninstall; cd ..
cd eub_i2c
sudo make uninstall; cd ..
cd eub_mobo
sudo make uninstall; cd ..
cd eub_mouse
sudo make uninstall; cd ..
cd eub-overlays
sudo make uninstall; cd ..
cd eub_power
sudo make uninstall; cd ..
cd eub_touch
sudo make uninstall; cd ..
cd eub-utils
sudo make disable; sudo make uninstall; cd ..
cd eub-headers
sudo make uninstall; cd ..
