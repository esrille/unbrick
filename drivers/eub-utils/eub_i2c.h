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

#include <stdint.h>

#define BAUDRATE	B115200

#define LEN_BUFFER	32

struct i2c_packed_msg {
	uint16_t addr;		/* slave address			*/
	uint16_t flags;
	uint16_t len;		/* msg length				*/
	uint8_t buf[2];		/* variable length msg data
*/
};

int i2c_xfer(int fd, uint8_t *buffer, uint16_t len);
void i2c_sync(int fd);
