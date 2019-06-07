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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mfd/core.h>

#include <linux/mfd/eub_mobo.h>

static const struct mfd_cell eub_mobo_devs[] = {
	{
		.name = "eub_backlight",
		.of_compatible = "esrille,eub_backlight",
	},
	{
		.name = "eub_touch",
		.of_compatible = "esrille,eub_touch",
	},
};

static int i2c_client_read_device(struct i2c_client *i2c, char reg,
				  int bytes, void *dest)
{
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

static int eub_mobo_i2c_read_device(struct eub_mobo_dev *eub_mobo, char reg,
				    int bytes, void *dest)
{
	return i2c_client_read_device(eub_mobo->i2c_client, reg, bytes, dest);
}

static int eub_mobo_i2c_write_device(struct eub_mobo_dev *eub_mobo, char reg,
				   int bytes, void *src)
{
	struct i2c_client *i2c = eub_mobo->i2c_client;
	/* we add 1 byte for device register */
	u8 msg[EUB_MOBO_REG_MAX + 1];
	int ret;

	if (EUB_MOBO_REG_MAX < bytes)
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

static int eub_mobo_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct eub_mobo_dev *eub_mobo;
	u8 val;
	int ret;

	/* sync up with eub-i2c service */
	ret = i2c_client_read_device(i2c, 0, 1, &val);
	if (ret < 0) {
		dev_info(&i2c->dev, "%s: will retry\n", __func__);
		return -EPROBE_DEFER;
	}

	eub_mobo = devm_kzalloc(&i2c->dev, sizeof(struct eub_mobo_dev),
				GFP_KERNEL);
	if (eub_mobo == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, eub_mobo);
	eub_mobo->dev = &i2c->dev;
	eub_mobo->i2c_client = i2c;
	eub_mobo->read_dev = eub_mobo_i2c_read_device;
	eub_mobo->write_dev = eub_mobo_i2c_write_device;

	ret = devm_mfd_add_devices(eub_mobo->dev, -1, eub_mobo_devs,
				   ARRAY_SIZE(eub_mobo_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(eub_mobo->dev, "mfd_add_devices failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id eub_mobo_i2c_id[] = {
	{ "eub_mobo" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, eub_mobo_i2c_id);

static const struct of_device_id eub_mobo_of_match[] = {
	{ .compatible = "esrille,eub_mobo", },
	{},
};
MODULE_DEVICE_TABLE(of, eub_mobo_of_match);

static struct i2c_driver eub_mobo_i2c_driver = {
	.driver = {
		   .name = "eub_mobo",
		   .of_match_table = eub_mobo_of_match,
	},
	.probe = eub_mobo_i2c_probe,
	.id_table = eub_mobo_i2c_id,
};
module_i2c_driver(eub_mobo_i2c_driver);

MODULE_DESCRIPTION("Esrille Unbrick Motherboard Multi-Function Driver");
MODULE_AUTHOR("Esrille Inc. <info@esrille.com>");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: eub_i2c");
