/*
 * Esrille Unbrick Power Off Program via I2C Bridge
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
#include "eub_i2c.h"

int main()
{
	uint8_t buffer[LEN_BUFFER + 2];

	int uart = open("/dev/serial0", O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (uart < 0) {
		perror("open serial0");
		return 1;
	}

	struct i2c_packed_msg *msg = (struct i2c_packed_msg *) &buffer[1];
	msg->addr = 9;
	msg->flags = 0;
	msg->len = 2;
	msg->buf[0] = 1;
	msg->buf[1] = 0;
	lockf(uart, F_LOCK, 0);
	i2c_xfer(uart, buffer, 7);
	lockf(uart, F_ULOCK, 0);

	close(uart);
	return 0;
}
