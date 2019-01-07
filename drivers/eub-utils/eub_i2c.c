/*
 * Esrille Unbrick I2C Bridge
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
#include <string.h>
#include <termios.h>
#include <signal.h>
#include <linux/i2c.h>

#include "eub_i2c.h"

#define UART_DELAY	100	// microseconds
#define UART_TIMEOUT	(500000/UART_DELAY)

static void print_msg(struct i2c_packed_msg *msg)
{
	printf("%s addr=0x%02x, len=%u flags=%s%s%s%s%s%s%s\n",
		msg->flags & I2C_M_RD ? "read" : "write", msg->addr, msg->len,
		msg->flags & I2C_M_TEN ? "TEN" : "",
		msg->flags & I2C_M_RECV_LEN ? "RECV_LEN" : "",
		msg->flags & I2C_M_NO_RD_ACK ? "NO_RD_ACK" : "",
		msg->flags & I2C_M_IGNORE_NAK ? "IGNORE_NAK" : "",
		msg->flags & I2C_M_REV_DIR_ADDR ? "REV_DIR_ADDR" : "",
		msg->flags & I2C_M_NOSTART ? "NOSTART" : "",
		msg->flags & I2C_M_STOP ? "STOP" : "");
}

static uint8_t garbage[LEN_BUFFER];

int i2c_xfer(int fd, uint8_t *buffer, uint16_t len)
{
	// append trailer
	uint8_t* end = buffer + 1 + len;
	*end = 0;

	// stuff data (COBS)
	uint8_t* p;
	uint8_t* code = buffer;
	uint8_t c = 1;
	for (p = code + 1; p <= end; ++p) {
		if (*p) {
			++c;
		} else {
			*code = c;
			code = p;
			c = 1;
		}
	}

	size_t count;
	size_t offset;

	// send the request
	count = len + 2;
	offset = 0;
	do {
		ssize_t ret = write(fd, buffer + offset, count);
		if (ret <= 0) {
			usleep(UART_DELAY);
			continue;
		}
		offset += ret;
		count -= ret;
	} while (0 < count);

	// receive the reply
	size_t timeout = 0;
	count = len + 2;
	offset = 0;
	uint8_t* tail;
	do {
		ssize_t ret = read(fd, buffer + offset, count);
		if (ret <= 0) {
			if (UART_TIMEOUT < ++timeout)
				break;
			usleep(UART_DELAY);
			continue;
		}
		tail = memchr(buffer + offset, 0, ret);
		offset += ret;
		count -= ret;
		// Check zero in buffer, which indicates an error.
		if (tail)
			break;
	} while (0 < count);

	if (tail == end) {
		// unstuff data
		for (p = buffer; p < end; p += c) {
			c = *p;
			if (c == 0)
				break;
			*p = 0;
		}
		if (p == end) {
			return 0;
		}
	}

	printf("%s: out of sync\n", __func__);
	i2c_sync(fd);
	return -1;
}

void i2c_sync(int fd)
{
	usleep(UART_DELAY * LEN_BUFFER);
	while (0 < read(fd, garbage, LEN_BUFFER)) {
		usleep(UART_DELAY);
	}
}
