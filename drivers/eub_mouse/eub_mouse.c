/*
 * Esrille Unbrick Mouse Driver
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

#include <linux/mfd/eub_power.h>

#define DRIVER_NAME		"eub_mouse"

/* GPIO Pins */
#define GPIO_PIN_BTN_LEFT	40	// SW1
#define GPIO_PIN_BTN_RIGHT	41	// SW2
#define GPIO_PIN_BTN_MIDDLE	42	// SW3
#define GPIO_PIN_BTN_SIDE	43	// SW4

/* Control Polling Rate */
static int scan_rate = 60;
module_param(scan_rate, int, 0644);
MODULE_PARM_DESC(scan_rate, "Polling rate in times/sec. Default = 60");

static u8 stick_play = 8;

/* The main device structure */
struct eub_mouse {
	struct eub_power_dev	*mfd;
	struct device		*dev;
	struct input_dev	*input;
	struct delayed_work	dwork;
	spinlock_t		lock;
	int			scan_rate_param;
	int			scan_ms;
	int			x_orig;
	int			y_orig;
};

static inline void set_scan_rate(struct eub_mouse *joystick, int scan_rate)
{
	joystick->scan_ms = MSEC_PER_SEC / scan_rate;
	joystick->scan_rate_param = scan_rate;
}

static int eub_mouse_reg_read(struct eub_mouse *joystick, u8 reg,
			      int size, void *dest)
{
	return joystick->mfd->read_dev(joystick->mfd, reg, size, dest);
}

static int __maybe_unused eub_mouse_reg_get(struct eub_mouse *joystick, u8 reg)
{
	u8 val;
	int err;

	err = eub_mouse_reg_read(joystick, reg, 1, &val);
	if (err)
		return err;
	return val;
}

static int __maybe_unused eub_mouse_reg_set(struct eub_mouse *joystick, u8 reg,
					    u8 val)
{
	return joystick->mfd->write_dev(joystick->mfd, reg, 1, &val);
}

static bool eub_mouse_get_input(struct eub_mouse *joystick)
{
	struct input_dev *input = joystick->input;
	s8 x_delta, y_delta, left, right, middle, back;
	u8 val[3];
	int err;

	err = eub_mouse_reg_read(joystick, EUB_POWER_REG_SWITCH, 3, val);
	if (err)
		return 0;

	x_delta = val[EUB_POWER_REG_X - EUB_POWER_REG_SWITCH] -
			joystick->x_orig;
	if (0 <= x_delta)
		x_delta = (x_delta <= stick_play) ? 0 : (x_delta - stick_play);
	else
		x_delta = (-x_delta <= stick_play) ? 0 : (x_delta + stick_play);

	y_delta = joystick->y_orig -
			val[EUB_POWER_REG_Y - EUB_POWER_REG_SWITCH];
	if (0 <= y_delta)
		y_delta = (y_delta <= stick_play) ? 0 : (y_delta - stick_play);
	else
		y_delta = (-y_delta <= stick_play) ? 0 : (y_delta + stick_play);

	/* Report the button event */
	left = gpio_get_value(GPIO_PIN_BTN_LEFT);
	input_report_key(input, BTN_LEFT, left);
	right = gpio_get_value(GPIO_PIN_BTN_RIGHT);
	input_report_key(input, BTN_RIGHT, right);
	middle = gpio_get_value(GPIO_PIN_BTN_MIDDLE);
	input_report_key(input, BTN_MIDDLE, middle);
	back = gpio_get_value(GPIO_PIN_BTN_SIDE);
	input_report_key(input, BTN_SIDE, back);

	/* Report the deltas */
	input_report_rel(input, REL_X, x_delta);
	input_report_rel(input, REL_Y, y_delta);

	/* Report GUI button */
	input_report_key(input, KEY_LEFTMETA, val[0]);

	input_sync(input);

	return x_delta || y_delta || left || right;
}

static void eub_mouse_reschedule_work(struct eub_mouse *joystick,
				      unsigned long delay)
{
	unsigned long flags;

	spin_lock_irqsave(&joystick->lock, flags);

	mod_delayed_work(system_wq, &joystick->dwork, delay);

	spin_unlock_irqrestore(&joystick->lock, flags);
}

static void eub_mouse_check_params(struct eub_mouse *joystick)
{
	if (scan_rate != joystick->scan_rate_param)
		set_scan_rate(joystick, scan_rate);
}

/* Control the Device polling rate / Work Handler sleep time */
static unsigned long eub_mouse_adjust_delay(struct eub_mouse *joystick,
					    bool have_data)
{
	unsigned long delay;

        delay = joystick->scan_ms;
        return msecs_to_jiffies(delay);
}

/* Work Handler */
static void eub_mouse_work_handler(struct work_struct *work)
{
	bool have_data;
	struct eub_mouse *joystick =
			container_of(work, struct eub_mouse, dwork.work);
	unsigned long delay;

	eub_mouse_check_params(joystick);

	have_data = eub_mouse_get_input(joystick);
	delay = eub_mouse_adjust_delay(joystick, have_data);

	eub_mouse_reschedule_work(joystick, delay);
}

static int eub_mouse_open(struct input_dev *input)
{
	struct eub_mouse *joystick = input_get_drvdata(input);

        eub_mouse_reschedule_work(joystick,
                                   eub_mouse_adjust_delay(joystick, true));
	return 0;
}

static void eub_mouse_close(struct input_dev *input)
{
	struct eub_mouse *joystick = input_get_drvdata(input);

	cancel_delayed_work_sync(&joystick->dwork);
}

static void eub_mouse_set_input_params(struct eub_mouse *joystick)
{
	struct input_dev *input = joystick->input;

	input->name = "Esrille Unbrick Mouse";
	input->id.version = eub_mouse_reg_get(joystick,
					      EUB_POWER_REG_VERSION);
	input->dev.parent = joystick->dev;
	input->open = eub_mouse_open;
	input->close = eub_mouse_close;
	input_set_drvdata(input, joystick);

	/* Register the device as mouse */
	input_set_capability(input, EV_REL, REL_X);
	input_set_capability(input, EV_REL, REL_Y);

	/* Register device's buttons and keys */
	input_set_capability(input, EV_KEY, BTN_LEFT);
	input_set_capability(input, EV_KEY, BTN_MIDDLE);
	input_set_capability(input, EV_KEY, BTN_RIGHT);
	input_set_capability(input, EV_KEY, BTN_SIDE);
	input_set_capability(input, EV_KEY, KEY_LEFTMETA);
}

static void eub_mouse_configure(struct eub_mouse *joystick)
{
	u8 val[2];
	int err;

	joystick->x_orig = joystick->y_orig = 128;
	err = eub_mouse_reg_read(joystick, EUB_POWER_REG_X, 2, val);
	if (0 <= err) {
		joystick->x_orig = val[0];
		joystick->y_orig = val[1];
		pr_info("%s: ver=%d, x=%d, y=%d, stick_play=%u.\n",
			__func__,
			joystick->input->id.version,
			joystick->x_orig,
			joystick->y_orig,
			stick_play);
	}
}

static struct eub_mouse *eub_mouse_create(void)
{
	struct eub_mouse *joystick;

	joystick = kzalloc(sizeof(struct eub_mouse), GFP_KERNEL);
	if (!joystick)
		return NULL;

	joystick->scan_rate_param = scan_rate;
	set_scan_rate(joystick, scan_rate);
	INIT_DELAYED_WORK(&joystick->dwork, eub_mouse_work_handler);
	spin_lock_init(&joystick->lock);

	return joystick;
}

static int eub_mouse_probe(struct platform_device *pdev)
{
	struct eub_power_dev *eub_power_dev = dev_get_drvdata(pdev->dev.parent);
	struct eub_mouse *joystick;
	int ret;

	if (pdev->dev.of_node) {
		of_property_read_u8(pdev->dev.of_node, "esrille,stick_play",
				    &stick_play);
	}

	joystick = eub_mouse_create();
	if (!joystick)
		return -ENOMEM;
	joystick->mfd = eub_power_dev;
	joystick->dev = &pdev->dev;
	joystick->input = input_allocate_device();
	if (!joystick->input) {
		ret = -ENOMEM;
		goto err_mem_free;
	}

	eub_mouse_set_input_params(joystick);

	eub_mouse_configure(joystick);

        dev_dbg(&pdev->dev,
                "Using polling at rate: %d times/sec\n", scan_rate);

	/* Register the device in input subsystem */
	ret = input_register_device(joystick->input);
	if (ret) {
		dev_err(&pdev->dev,
                        "Input device register failed: %d\n", ret);
		goto err_input_free;
	}
	platform_set_drvdata(pdev, joystick);
	return 0;

err_input_free:
	input_free_device(joystick->input);
err_mem_free:
	kfree(joystick);
	return ret;
}

static int eub_mouse_remove(struct platform_device *pdev)
{
	struct eub_mouse *joystick = platform_get_drvdata(pdev);

	input_unregister_device(joystick->input);
	kfree(joystick);
	return 0;
}

static const struct of_device_id eub_mouse_of_match[] = {
	{ .compatible = "esrille,eub_mouse", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, eub_mouse_of_match);

static struct platform_driver eub_mouse_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(eub_mouse_of_match),
	},
	.probe = eub_mouse_probe,
	.remove = eub_mouse_remove,
};

module_platform_driver(eub_mouse_driver);

MODULE_DESCRIPTION("Esrille Unbrick Mouse Driver");
MODULE_AUTHOR("Esrille Inc. <info@esrille.com>");
MODULE_LICENSE("GPL");
