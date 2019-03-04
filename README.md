# Software and Driver for Esrille Unbrick

Unbrick is a portable personal computer designed at Esrille Inc., powered by Raspberry Pi Compute Module 3 Lite.

- [Product Page](http://www.esrille.com/unbrick.html)

Software and Driver for Esrille Unbrick provides device drivers and other utilities to use analog stick, touch screen, and other devices included in Esrille Unbrick.

## Installing Software and Driver for Esrille Unbrick

### 1. Download the Raspbian Stretch with desktop image

Visit https://www.raspberrypi.org/downloads/raspbian/ and download Raspbian Stretch with desktop. Note this software has been tested with,

- 2018-11-13-raspbian-stretch.zip

### 2. Flash the Raspbian image to your SD card

Download [Etcher](https://www.balena.io/etcher/) if you need an easy flashing tool.

### 3. Copy cmdline.txt and config.txt to your SD card

Copy unbrick/boot/cmdline.txt and unbrick/boot/config.txt into the /boot partition of your SD card.

- unbrick/boot/cmdline.txt disables the serial console since Unbrick uses the serial port for internal peripherals.
- unbrick/boot/config.txt enables the LCD screen and other devices included in Esrille Unbrick.

### 4. Boot your Unbrick

Insert your SD card to your Unbrick, and connect keyboard, mouse and network adapter. Then boot your Unbrick. Configure your network adapter at this point.

### 5. Copy unbrick directory to your home directory in your Unbrick

Open Terminal. Copy unbrick directory to /home/pi/unbrick.

```
$ cd
$ git clone https://github.com/esrille/unbrick.git
```

Alternatively, if you're using Linux, you may copy unbrick directory to /rootfs/home/pi/unbrick in your SD card in step 3.

### 6. Install device drivers and other utilities

Execute,

```
$ cd unbrick
$ ./install.sh
```

to install device drivers and other utilities.

After install.sh completes, reboot your Unbrick. Now you'll be able to use analog stick, touch screen, and other devices included in Unbrick.
