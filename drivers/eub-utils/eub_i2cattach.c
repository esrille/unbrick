/*
 * Esrille Unbrick I2C Bridge Program
 *
 * Copyright (C) 2018, 2019 Esrille Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include "eub_i2c.h"

static volatile sig_atomic_t quit_flag = 0;

static void quit_handler(int signum)
{
	quit_flag = 1;
}

int main()
{
	uint8_t buffer[LEN_BUFFER + 2];
	ssize_t len;
	struct termios ti;

	struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_handler = quit_handler;
	act.sa_flags = 0;
	if (sigaction(SIGTERM, &act, NULL) == -1) {
		perror("sigaction");
		return 1;
	}

	int uart = open("/dev/serial0", O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (uart < 0) {
		perror("open serial0");
		return 1;
	}

	tcflush(uart, TCIOFLUSH);
	if (tcgetattr(uart, &ti) < 0) {
		perror("Can't get port settings");
		return -1;
	}
	cfmakeraw(&ti);
	ti.c_cflag |= CLOCAL;
	ti.c_cflag &= ~CRTSCTS;
	cfsetispeed(&ti, BAUDRATE);
	cfsetospeed(&ti, BAUDRATE);
	if (tcsetattr(uart, TCSANOW, &ti) < 0) {
		perror("Can't set port settings");
		return -1;
	}
	tcflush(uart, TCIOFLUSH);

	i2c_sync(uart);

	int fd = open("/dev/i2c-proxy3", O_RDWR);
	if (fd < 0) {
		perror("open i2c-proxy3");
		return 1;
	}

	while (!quit_flag) {
		len = read(fd, buffer + 1, LEN_BUFFER);
		if (len < 0) {
			perror("read fd");
			break;
		}
		lockf(uart, F_LOCK, 0);
		int ret = i2c_xfer(uart, buffer, len);
		lockf(uart, F_ULOCK, 0);
		do {
			len = write(fd, buffer + 1, (ret < 0) ? 0 : len);
		} while (len == EINTR);
		if (len < 0) {
			perror("write fd");
			break;
		}
	}
	close(fd);
	close(uart);
	return 0;
}
