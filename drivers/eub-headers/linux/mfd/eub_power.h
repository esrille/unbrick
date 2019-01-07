/*
 * Esrille Unbrick Power Board Multi-Function Driver
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

#ifndef __LINUX_MFD_EUB_POWER_H
#define __LINUX_MFD_EUB_POWER_H

/*
 * ----------------------------------------------------------------------------
 * Registers, all 8 bits
 * ----------------------------------------------------------------------------
 */

/* Register definitions */
#define EUB_POWER_REG_VERSION	0x00
#define EUB_POWER_REG_SWITCH	0x01
#define EUB_POWER_REG_X		0x02
#define EUB_POWER_REG_Y		0x03
#define EUB_POWER_REG_VBUS	0x04
#define EUB_POWER_REG_VREF	0x05
#define EUB_POWER_REG_MAX	0x06

struct eub_power_dev {
	struct device *dev;
	struct i2c_client *i2c_client;
	int (*read_dev)(struct eub_power_dev *eub_power, char reg, int size,
			void *dest);
	int (*write_dev)(struct eub_power_dev *eub_power, char reg, int size,
			 void *src);
};

#endif /*  __LINUX_MFD_EUB_POWER_H */
