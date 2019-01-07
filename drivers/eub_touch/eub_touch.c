/*
 * Esrille Unbrick Touch Screen Driver
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
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <linux/mfd/eub_mobo.h>

#define DRIVER_NAME		"eub_touch"

#define MIN_X	26
#define MAX_X	998
#define MIN_Y	26
#define MAX_Y	998

/* Control Polling Rate */
static int scan_rate = 60;
module_param(scan_rate, int, 0644);
MODULE_PARM_DESC(scan_rate, "Polling rate in times/sec. Default = 60");

/* The main device structure */
struct eub_touch {
	struct eub_mobo_dev	*mfd;
	struct device		*dev;
	struct input_dev	*input;
	struct delayed_work	dwork;
	spinlock_t		lock;
	int			scan_rate_param;
	int			scan_ms;

	int			x;
	int			y;
	int			touch;	// previous touch state
};

static inline void set_scan_rate(struct eub_touch *touch, int scan_rate)
{
	touch->scan_ms = MSEC_PER_SEC / scan_rate;
	touch->scan_rate_param = scan_rate;
}

static int eub_touch_reg_get(struct eub_touch *touch, u8 reg)
{
	u8 val;
	int err;

	err = touch->mfd->read_dev(touch->mfd, reg, 1, &val);
	if (err)
		return err;
	return val;
}

static int __maybe_unused eub_touch_reg_set(struct eub_touch *touch, u8 reg,
					    u8 val)
{
	return touch->mfd->write_dev(touch->mfd, reg, 1, &val);
}

static bool eub_touch_get_input(struct eub_touch *touch)
{
	struct input_dev *input = touch->input;
	u8 val[6];
	s32 x, y, z;
	int ret;

	ret = touch->mfd->read_dev(touch->mfd, EUB_MOBO_REG_X_LOW, 6, val);
	if (ret < 0)
		return false;

	x = val[0] | (val[1] << 8);
	y = val[2] | (val[3] << 8);
	z = val[4] | (val[5] << 8);
	if (x < MIN_X || MAX_X < x || y < MIN_Y || MAX_Y < y)
		z = 0x3ff;

	/* Report the button event */
	if (z < 0x3fc) {
		/* Report the positions */
		if (touch->touch) {
			// low pass filter
			if (touch->x == -1 || touch->y == -1) {
				touch->x = x;
				touch->y = y;
			} else {
				touch->x += (x >> 2) - (touch->x >> 2);
				touch->y += (y >> 2) - (touch->y >> 2);
			}
			input_report_key(input, BTN_TOUCH, 1);
			input_report_abs(input, ABS_X, touch->x);
			input_report_abs(input, ABS_Y, touch->y);
		} else {
			/* Wait for the values to settle. */
			input_report_key(input, BTN_TOUCH, 0);
		}
		touch->touch = 1;
	} else {
		/* If z is 3.3V, the X-plate and Y-plate are not touching. */
		touch->touch = 0;
		touch->x = touch->y = -1;
		input_report_key(input, BTN_TOUCH, 0);
	}
	input_sync(input);
	return true;
}

static void eub_touch_reschedule_work(struct eub_touch *touch,
				      unsigned long delay)
{
	unsigned long flags;

	spin_lock_irqsave(&touch->lock, flags);

	mod_delayed_work(system_wq, &touch->dwork, delay);

	spin_unlock_irqrestore(&touch->lock, flags);
}

static void eub_touch_check_params(struct eub_touch *touch)
{
	if (scan_rate != touch->scan_rate_param)
		set_scan_rate(touch, scan_rate);
}

/* Control the Device polling rate / Work Handler sleep time */
static unsigned long eub_touch_adjust_delay(struct eub_touch *touch,
					    bool have_data)
{
	unsigned long delay;

        delay = touch->scan_ms;
        return msecs_to_jiffies(delay);
}

/* Work Handler */
static void eub_touch_work_handler(struct work_struct *work)
{
	bool have_data;
	struct eub_touch *touch =
			container_of(work, struct eub_touch, dwork.work);
	unsigned long delay;

	eub_touch_check_params(touch);

	have_data = eub_touch_get_input(touch);
	delay = eub_touch_adjust_delay(touch, have_data);

	eub_touch_reschedule_work(touch, delay);
}

static int eub_touch_open(struct input_dev *input)
{
	struct eub_touch *touch = input_get_drvdata(input);

	eub_touch_reschedule_work(touch,
				  eub_touch_adjust_delay(touch, true));
	return 0;
}

static void eub_touch_close(struct input_dev *input)
{
	struct eub_touch *touch = input_get_drvdata(input);

	cancel_delayed_work_sync(&touch->dwork);
}

static void eub_touch_set_input_params(struct eub_touch *touch)
{
	struct input_dev *input = touch->input;

	input->name = "Esrille Unbrick Touch Screen";
	input->id.version = eub_touch_reg_get(touch,
					      EUB_MOBO_REG_VERSION);
	input->dev.parent = touch->dev;
	input->open = eub_touch_open;
	input->close = eub_touch_close;
	input_set_drvdata(input, touch);

	__set_bit(INPUT_PROP_DIRECT, input->propbit);
	__set_bit(INPUT_PROP_POINTER, input->propbit);
	input_set_abs_params(input, ABS_X, MIN_X, MAX_X, 0, 0);
	input_abs_set_res(input, ABS_X, 9);	/* [units/mm] */
	input_set_abs_params(input, ABS_Y, MIN_Y, MAX_Y, 1, 0);
	input_abs_set_res(input, ABS_Y, 15);	/* [units/mm] */
	input_set_capability(input, EV_KEY, BTN_TOUCH);
}

static void eub_touch_configure(struct eub_touch *touch)
{
        pr_info("eub_touch_configure: ver=%d.\n",
		touch->input->id.version);
	touch->x = -1;
	touch->y = -1;
	touch->touch = 0;
}

static struct eub_touch *eub_touch_create(void)
{
	struct eub_touch *touch;

	touch = kzalloc(sizeof(struct eub_touch), GFP_KERNEL);
	if (!touch)
		return NULL;

	touch->scan_rate_param = scan_rate;
	set_scan_rate(touch, scan_rate);
	INIT_DELAYED_WORK(&touch->dwork, eub_touch_work_handler);
	spin_lock_init(&touch->lock);

	return touch;
}

static int eub_touch_probe(struct platform_device *pdev)
{
	struct eub_mobo_dev *eub_mobo_dev = dev_get_drvdata(pdev->dev.parent);
	struct eub_touch *touch;
	int ret;

	touch = eub_touch_create();
	if (!touch)
		return -ENOMEM;

	touch->input = input_allocate_device();
	touch->mfd = eub_mobo_dev;
	touch->dev = &pdev->dev;
	if (!touch->input) {
		ret = -ENOMEM;
		goto err_mem_free;
	}

	eub_touch_set_input_params(touch);

	eub_touch_configure(touch);

        dev_dbg(&pdev->dev,
                "Using polling at rate: %d times/sec\n", scan_rate);

	/* Register the device in input subsystem */
	ret = input_register_device(touch->input);
	if (ret) {
		dev_err(&pdev->dev,
                        "Input device register failed: %d\n", ret);
		goto err_input_free;
	}
	platform_set_drvdata(pdev, touch);
	return 0;

err_input_free:
	input_free_device(touch->input);
err_mem_free:
	kfree(touch);
	return ret;
}

static int eub_touch_remove(struct platform_device *pdev)
{
	struct eub_touch *touch = platform_get_drvdata(pdev);

	input_unregister_device(touch->input);
	kfree(touch);
	return 0;
}

static const struct of_device_id eub_touch_of_match[] = {
	{ .compatible = "esrille,eub_touch", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, eub_touch_of_match);

static struct platform_driver eub_touch_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(eub_touch_of_match),
	},
	.probe = eub_touch_probe,
	.remove = eub_touch_remove,
};

module_platform_driver(eub_touch_driver);

MODULE_DESCRIPTION("Esrille Unbrick Touch Screen Driver");
MODULE_AUTHOR("Esrille Inc. <info@esrille.com>");
MODULE_LICENSE("GPL");
