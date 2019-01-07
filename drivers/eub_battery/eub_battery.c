/*
 * Esrille Unbrick Battery Driver
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
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <linux/mfd/eub_power.h>

#define DRIVER_NAME		"eub_battery"

#define BATTERY_LEVELS_SIZE	100	/* from 2.00 (200) to 2.99 (299) */
#define SCAN_DELAY		HZ

/* The main device structure */
struct eub_battery {
	struct eub_power_dev	*mfd;
	struct device		*dev;

	int			voltage_raw;		/* measured value of µV */
	int			voltage_uV;		/* unit of µV */
	int			rated_capacity;		/* units of µAh */
	int			rem_capacity;		/* of ‱ (1/10,000) */

	struct power_supply	*battery;
	struct power_supply	*ac;

	struct delayed_work	dwork;
};

/* Percentage from 2.0V to 2.99V */
static const uint8_t battery_levels[BATTERY_LEVELS_SIZE] = {
/*  .00  .01  .02  .03  .04  .05  .06  .07  .08  .09 */
      0,   0,   0,   0,   0,   0,   1,   1,   1,   1,	/* .0 */
      1,   2,   2,   2,   3,   3,   3,   4,   4,   5,	/* .1 */
      5,   6,   7,   7,   8,   9,  10,  11,  12,  14,	/* .2 */
     16,  18,  20,  22,  24,  26,  29,  33,  37,  42,	/* .3 */
     47,  51,  55,  59,  63,  67,  71,  75,  78,  80,	/* .4 */
     82,  83,  85,  86,  87,  88,  89,  90,  91,  92,	/* .5 */
     93,  93,  94,  94,  95,  96,  96,  96,  97,  97,	/* .6 */
     98,  98,  99,  99,  99, 100, 100, 100, 100, 100,	/* .7 */
    100, 100, 100, 100, 100, 100, 100, 100, 100, 100,	/* .8 */
    100, 100, 100, 100, 100, 100, 100, 100, 100, 100,	/* .9 */
};

static unsigned int rated_capacity = 1900 * 6;
module_param(rated_capacity, uint, 0644);
MODULE_PARM_DESC(rated_capacity, "rated battery capacity, mAh");

static int eub_battery_reg_get(struct eub_battery *eub_battery, u8 reg)
{
	u8 val;
	int err;

	err = eub_battery->mfd->read_dev(eub_battery->mfd, reg, 1, &val);
	if (err)
		return err;
	return val;
}

static int __maybe_unused eub_battery_reg_set(struct eub_battery *eub_battery,
					      u8 reg, u8 val)
{
	return eub_battery->mfd->write_dev(eub_battery->mfd, reg, 1, &val);
}

static void eub_battery_init_status(struct eub_battery *eub_battery)
{
	int val = eub_battery_reg_get(eub_battery, EUB_POWER_REG_VREF);
	int capacity;

	if (val < 0)
		return;

	eub_battery->voltage_raw = 3300000 * val / 255; /* µV */
	eub_battery->voltage_uV = 3 * eub_battery->voltage_raw;

	if (eub_battery->voltage_raw <= 2000000) {	// less than 2 V?
		capacity = 0;
	} else if (3000000 <= eub_battery->voltage_raw) {
		capacity = 100;
	} else {
		int level = (eub_battery->voltage_raw - 2000000) / 10000;
		capacity = battery_levels[level];
	}
	eub_battery->rem_capacity = capacity * 100;
}

static void eub_battery_update_status(struct eub_battery *eub_battery)
{
	int val = eub_battery_reg_get(eub_battery, EUB_POWER_REG_VREF);
	int capacity;

	if (val < 0)
		return;

	val = 3300000 * val / 255; /* µV */
	/*
	 * Apply low pass filter:
	 * 0.75 * prev + 0.25 * current
	 */
	eub_battery->voltage_raw -= (eub_battery->voltage_raw >> 2);
	eub_battery->voltage_raw += (val >> 2);
	eub_battery->voltage_uV = 3 * eub_battery->voltage_raw;

	if (eub_battery->voltage_raw <= 2000000) {	// less than 2 V?
		capacity = 0;
	} else if (3000000 <= eub_battery->voltage_raw) {
		capacity = 100;
	} else {
		int level = (eub_battery->voltage_raw - 2000000) / 10000;
		capacity = battery_levels[level];
	}
	capacity *= 100;
	/*
	 * Apply low pass filter if rem_capacity is close to capacity:
	 * 0.9375 * prev + 0.0625 * current
	 */
	int diff = eub_battery->rem_capacity - capacity;
	if (diff < 0)
		diff = -diff;
	if (20 < diff / 100) {
		eub_battery->rem_capacity = capacity;
	} else {
		eub_battery->rem_capacity -= (eub_battery->rem_capacity >> 4);
		eub_battery->rem_capacity += (capacity >> 4);
		if (eub_battery->rem_capacity < 0) {
			eub_battery->rem_capacity = 0;
		}
	}
}

static void eub_battery_work(struct work_struct *work)
{
	struct eub_battery *eub_battery =
			container_of(work, struct eub_battery, dwork.work);

	if (rated_capacity != eub_battery->rated_capacity)
		eub_battery->rated_capacity = rated_capacity;

	eub_battery_update_status(eub_battery);
	mod_delayed_work(system_wq, &eub_battery->dwork, SCAN_DELAY);
}

static int eub_ac_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct eub_battery *eub_battery = power_supply_get_drvdata(psy);
	int ret = 0;
	int vbus;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		vbus = eub_battery_reg_get(eub_battery, EUB_POWER_REG_VBUS);
		if (vbus < 0) {
			ret = -EINVAL;
		} else {
			val->intval = vbus ? 1 : 0;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int eub_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct eub_battery *eub_battery = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = eub_battery->voltage_raw ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = eub_battery->voltage_uV;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = eub_battery->rated_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = eub_battery->rated_capacity *
			(eub_battery->rem_capacity / 100) / 100;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = eub_battery->rem_capacity / 100;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property eub_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static enum power_supply_property eub_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc battery_desc = {
	.name		= "battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= eub_battery_props,
	.num_properties	= ARRAY_SIZE(eub_battery_props),
	.get_property	= eub_battery_get_property,
};

static const struct power_supply_desc ac_desc = {
	.name		= "ac",
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.properties	= eub_ac_props,
	.num_properties	= ARRAY_SIZE(eub_ac_props),
	.get_property	= eub_ac_get_property,
};

static int eub_battery_probe(struct platform_device *pdev)
{
	struct eub_power_dev *eub_power_dev = dev_get_drvdata(pdev->dev.parent);
	struct eub_battery *eub_battery;
	struct power_supply_config psy_cfg = {};

	eub_battery = devm_kzalloc(&pdev->dev, sizeof(*eub_battery),
				   GFP_KERNEL);
	if (!eub_battery)
		return -ENOMEM;

	eub_battery->mfd = eub_power_dev;
	eub_battery->dev = &pdev->dev;
	eub_battery->voltage_raw = 0;
	eub_battery->rem_capacity = 0;
	eub_battery->rated_capacity = rated_capacity;
	eub_battery_init_status(eub_battery);

	psy_cfg.drv_data = eub_battery;

	eub_battery->ac = power_supply_register(&pdev->dev, &ac_desc, &psy_cfg);
	if (IS_ERR(eub_battery->ac)) {
		dev_err(eub_battery->dev, "failed to register battery\n");
		return PTR_ERR(eub_battery->ac);
	}

	eub_battery->battery = power_supply_register(&pdev->dev, &battery_desc,
						&psy_cfg);
	if (IS_ERR(eub_battery->battery)) {
		power_supply_unregister(eub_battery->ac);
		return PTR_ERR(eub_battery->battery);
	}

	INIT_DELAYED_WORK(&eub_battery->dwork, eub_battery_work);
	mod_delayed_work(system_wq, &eub_battery->dwork, SCAN_DELAY);

	platform_set_drvdata(pdev, eub_battery);
	return 0;
}

static int eub_battery_remove(struct platform_device *pdev)
{
	struct eub_battery *eub_battery = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&eub_battery->dwork);
	power_supply_unregister(eub_battery->battery);
	power_supply_unregister(eub_battery->ac);
	return 0;
}

static const struct of_device_id eub_battery_of_match[] = {
	{ .compatible = "esrille,eub_battery", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, eub_battery_of_match);

static struct platform_driver eub_battery_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(eub_battery_of_match),
	},
	.probe = eub_battery_probe,
	.remove = eub_battery_remove,
};

module_platform_driver(eub_battery_driver);

MODULE_DESCRIPTION("Esrille Unbrick Battery Driver");
MODULE_AUTHOR("Esrille Inc. <info@esrille.com>");
MODULE_LICENSE("GPL");
