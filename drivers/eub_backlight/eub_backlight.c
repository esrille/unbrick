/*
 * Esrille Unbrick Backlight Driver
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

#include <linux/backlight.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <linux/mfd/eub_mobo.h>

struct eub_backlight {
	struct eub_mobo_dev	*mfd;
	struct device		*dev;
};

static inline int eub_backlight_read(struct eub_backlight *gbl, u8 reg)
{
	u8 val;
	int err;

	err = gbl->mfd->read_dev(gbl->mfd, reg, 1, &val);
	if (err)
		return err;
	return val;
}

static inline int eub_backlight_write(struct eub_backlight *gbl, u8 reg, u8 val)
{
	return gbl->mfd->write_dev(gbl->mfd, reg, 1, &val);
}

static int eub_backlight_update_status(struct backlight_device *bl)
{
	struct eub_backlight *gbl = bl_get_data(bl);
	int brightness = bl->props.brightness;
	int ret;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

        ret = eub_backlight_write(gbl, EUB_MOBO_REG_BRIGHTNESS, brightness);
	if (ret) {
		dev_err(gbl->dev, "failed to set brightness\n");
		return ret;
	}
        ret = eub_backlight_write(gbl, EUB_MOBO_REG_DISPLAY,
				  (0 < brightness) ? 1 : 0);
	if (ret) {
		dev_err(gbl->dev, "failed to set brightness\n");
		return ret;
	}
	return 0;
}

static const struct backlight_ops eub_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= eub_backlight_update_status,
};

static int eub_backlight_probe(struct platform_device *pdev)
{
	struct eub_mobo_dev *eub_mobo_dev = dev_get_drvdata(pdev->dev.parent);
	struct eub_backlight *gbl;
	struct backlight_properties props;
	struct backlight_device *bl;

	gbl = devm_kzalloc(&pdev->dev, sizeof(*gbl), GFP_KERNEL);
	if (!gbl)
		return -ENOMEM;

	gbl->mfd = eub_mobo_dev;
	gbl->dev = eub_mobo_dev->dev;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 255;
	bl = devm_backlight_device_register(&pdev->dev, dev_name(&pdev->dev),
					&pdev->dev, gbl, &eub_backlight_ops,
					&props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	bl->props.brightness = 40;
	backlight_update_status(bl);
	platform_set_drvdata(pdev, gbl);

	return 0;
}

static const struct of_device_id eub_backlight_of_match[] = {
	{ .compatible = "esrille,eub_backlight", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, eub_backlight_of_match);

static struct platform_driver eub_backlight_driver = {
	.driver = {
		.name = "eub_backlight",
		.of_match_table = of_match_ptr(eub_backlight_of_match),
	},
	.probe = eub_backlight_probe,
};

module_platform_driver(eub_backlight_driver);

MODULE_DESCRIPTION("Esrille Unbrick Backlight Driver");
MODULE_AUTHOR("Esrille Inc. <info@esrille.com>");
MODULE_LICENSE("GPL");
