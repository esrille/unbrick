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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mfd/core.h>

#include <linux/mfd/eub_power.h>

static const struct mfd_cell eub_power_devs[] = {
	{
		.name = "eub_battery",
		.of_compatible = "esrille,eub_battery",
	},
	{
		.name = "eub_mouse",
		.of_compatible = "esrille,eub_mouse",
	},
};

static int eub_power_i2c_read_device(struct eub_power_dev *eub_power, char reg,
				     int bytes, void *dest)
{
	struct i2c_client *i2c = eub_power->i2c_client;
	struct i2c_msg xfer[2];
	int ret;

	/* Write register */
	xfer[0].addr = i2c->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg;

	/* Read data */
	xfer[1].addr = i2c->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = bytes;
	xfer[1].buf = dest;

	ret = i2c_transfer(i2c->adapter, xfer, 2);
	if (ret == 2)
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;

	return ret;
}

static int eub_power_i2c_write_device(struct eub_power_dev *eub_power, char reg,
				      int bytes, void *src)
{
	struct i2c_client *i2c = eub_power->i2c_client;
	/* we add 1 byte for device register */
	u8 msg[EUB_POWER_REG_MAX + 1];
	int ret;

	if (EUB_POWER_REG_MAX < bytes)
		return -EINVAL;

	msg[0] = reg;
	memcpy(&msg[1], src, bytes);

	ret = i2c_master_send(i2c, msg, bytes + 1);
	if (ret < 0)
		return ret;
	if (ret != bytes + 1)
		return -EIO;
	return 0;
}

static int eub_power_i2c_probe(struct i2c_client *i2c,
			       const struct i2c_device_id *id)
{
	struct eub_power_dev *eub_power;
	int ret;

	eub_power = devm_kzalloc(&i2c->dev, sizeof(struct eub_power_dev),
				GFP_KERNEL);
	if (eub_power == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, eub_power);
	eub_power->dev = &i2c->dev;
	eub_power->i2c_client = i2c;
	eub_power->read_dev = eub_power_i2c_read_device;
	eub_power->write_dev = eub_power_i2c_write_device;

	ret = devm_mfd_add_devices(eub_power->dev, -1, eub_power_devs,
				   ARRAY_SIZE(eub_power_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(eub_power->dev, "mfd_add_devices failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id eub_power_i2c_id[] = {
	{ "eub_power", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, eub_power_i2c_id);

static const struct of_device_id eub_power_of_match[] = {
	{.compatible = "esrille,eub_power", },
	{},
};
MODULE_DEVICE_TABLE(of, eub_power_of_match);

static struct i2c_driver eub_power_i2c_driver = {
	.driver = {
		   .name = "eub_power",
		   .of_match_table = of_match_ptr(eub_power_of_match),
	},
	.probe = eub_power_i2c_probe,
	.id_table = eub_power_i2c_id,
};

static int __init eub_power_i2c_init(void)
{
	return i2c_add_driver(&eub_power_i2c_driver);
}

/* init early so consumer devices can complete system boot */
subsys_initcall(eub_power_i2c_init);

static void __exit eub_power_i2c_exit(void)
{
	i2c_del_driver(&eub_power_i2c_driver);
}
module_exit(eub_power_i2c_exit);

MODULE_DESCRIPTION("Esrille Unbrick Power Board Multi-Function Driver");
MODULE_AUTHOR("Esrille Inc. <info@esrille.com>");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: eub_mobo");
MODULE_SOFTDEP("post: eub_battery");
MODULE_SOFTDEP("post: eub_mouse");
