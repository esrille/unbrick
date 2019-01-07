/*
 * Esrille Unbrick Motherboard Multi-Function Driver
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

#ifndef __LINUX_MFD_EUB_MOBO_H
#define __LINUX_MFD_EUB_MOBO_H

/*
 * ----------------------------------------------------------------------------
 * Registers, all 8 bits
 * ----------------------------------------------------------------------------
 */

/* Register definitions */
#define EUB_MOBO_REG_VERSION		0x00
#define EUB_MOBO_REG_BRIGHTNESS		0x01
#define EUB_MOBO_REG_DISPLAY		0x02
#define EUB_MOBO_REG_POWER_SWITCH	0x03
#define EUB_MOBO_REG_DAC		0x04
#define EUB_MOBO_REG_X_LOW		0x05
#define EUB_MOBO_REG_X_HIGH		0x06
#define EUB_MOBO_REG_Y_LOW		0x07
#define EUB_MOBO_REG_Y_HIGH		0x08
#define EUB_MOBO_REG_Z_LOW		0x09
#define EUB_MOBO_REG_Z_HIGH		0x0A
#define EUB_MOBO_REG_MAX		0x0B

struct eub_mobo_dev {
	struct device *dev;
	struct i2c_client *i2c_client;
	int (*read_dev)(struct eub_mobo_dev *eub_mobo, char reg, int size,
			void *dest);
	int (*write_dev)(struct eub_mobo_dev *eub_mobo, char reg, int size,
			 void *src);
};

#endif /*  __LINUX_MFD_EUB_MOBO_H */
