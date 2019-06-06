/*
 * Esrille Unbrick I2C Bridge Kernel Driver
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define DRV_NAME		"eub_i2c"
#define LEN_BUFFER		32
#define I2C_MSG_HDR_SIZE	6
#define MINOR_NUM		1

struct eub_i2c_dev {
	struct device *dev;
	struct i2c_adapter adapter;
	struct completion completion;
	struct i2c_msg *curr_msgs;
	int num_msgs;
	int msg_err;

	struct cdev proxy_cdev;
	unsigned int proxy_major;
	struct class *proxy_class;

	char buffer[LEN_BUFFER];
	size_t len;
	size_t offset;

	struct mutex mutex;
	wait_queue_head_t inq;
	wait_queue_head_t outq;
};

/*
 * I2C Proxy inode
 */

static int proxy_open(struct inode *inode, struct file *file)
{
	struct eub_i2c_dev *i2c_dev;
	i2c_dev = container_of(inode->i_cdev, struct eub_i2c_dev, proxy_cdev);
	if (i2c_dev  == NULL) {
		pr_err("%s: container_of\n", __func__);
		return -EFAULT;
	}
	file->private_data = i2c_dev;
	return 0;
}

static int proxy_close(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t proxy_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct eub_i2c_dev *i2c_dev = filp->private_data;
	ssize_t ret;

	if (mutex_lock_interruptible(&i2c_dev->mutex))
		return -ERESTARTSYS;

	while (i2c_dev->len == i2c_dev->offset) {
		mutex_unlock(&i2c_dev->mutex);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(i2c_dev->outq, (i2c_dev->len != i2c_dev->offset)))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&i2c_dev->mutex))
			return -ERESTARTSYS;
	}
	ssize_t len = i2c_dev->len - i2c_dev->offset;
	if (len < count)
		count = len;
	if (copy_to_user(buf, i2c_dev->buffer + i2c_dev->offset, count) != 0) {
		i2c_dev->msg_err = -EIO;
		ret = -EFAULT;
	} else {
		i2c_dev->offset += count;
		ret = count;
	}

	mutex_unlock(&i2c_dev->mutex);
	if (ret < 0)
		complete(&i2c_dev->completion);

	return ret;
}

static ssize_t proxy_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct eub_i2c_dev *i2c_dev = filp->private_data;
	int i;
	ssize_t ret;

	if (mutex_lock_interruptible(&i2c_dev->mutex))
		return -ERESTARTSYS;

	if (i2c_dev->len != count) {
		i2c_dev->msg_err = ret = -EIO;
	} else if (copy_from_user(i2c_dev->buffer, buf, count) != 0) {
		i2c_dev->msg_err = -EIO;
		ret = -EFAULT;
	} else {
		ret = count;
		char* p = i2c_dev->buffer;
		for (i = 0; i < i2c_dev->num_msgs; ++i) {
			struct i2c_msg *to = &i2c_dev->curr_msgs[i];
			if (0 < to->len && (to->flags & I2C_M_RD)) {
				memcpy(to->buf,
				       p + I2C_MSG_HDR_SIZE, to->len);
			}
			p += I2C_MSG_HDR_SIZE + to->len;
		}
	}
	mutex_unlock(&i2c_dev->mutex);
	complete(&i2c_dev->completion);

	return ret;
}

struct file_operations proxy_fops = {
	.open    = proxy_open,
	.release = proxy_close,
	.read    = proxy_read,
	.write   = proxy_write,
};

static int proxy_init(struct eub_i2c_dev *i2c_dev)
{
	int nr = i2c_dev->adapter.nr;
	int ret;
	dev_t dev;

	ret = alloc_chrdev_region(&dev, nr, MINOR_NUM, DRV_NAME);
	if (ret) {
		pr_err("failed to allocate device number\n");
		return ret;
	}
	i2c_dev->proxy_major = MAJOR(dev);

	cdev_init(&i2c_dev->proxy_cdev, &proxy_fops);
	i2c_dev->proxy_cdev.owner = THIS_MODULE;
	ret = cdev_add(&i2c_dev->proxy_cdev, dev, MINOR_NUM);
	if (ret) {
		pr_err("failed to register device\n");
		unregister_chrdev_region(dev, MINOR_NUM);
		return ret;
	}

	/*
	 * Create sysfs entries at /sys/class/eub_i2c/
	 */
	i2c_dev->proxy_class = class_create(THIS_MODULE, DRV_NAME);
	if (IS_ERR(i2c_dev->proxy_class)) {
		ret = PTR_ERR(i2c_dev->proxy_class);
		pr_err("failed to create class\n");
		cdev_del(&i2c_dev->proxy_cdev);
		unregister_chrdev_region(dev, MINOR_NUM);
		return ret;
	}

	struct device *sysfs_dev =
		device_create(i2c_dev->proxy_class, NULL, dev, NULL,
			      "i2c-proxy%d", nr);
	if (IS_ERR(sysfs_dev)) {
		ret = PTR_ERR(sysfs_dev);
		pr_err("failed to create device\n");
		class_destroy(i2c_dev->proxy_class);
		cdev_del(&i2c_dev->proxy_cdev);
		unregister_chrdev_region(dev, MINOR_NUM);
		return ret;
	}
	return 0;
}

static void proxy_exit(struct eub_i2c_dev *i2c_dev)
{
	int nr = i2c_dev->adapter.nr;

	dev_t dev = MKDEV(i2c_dev->proxy_major, nr);
	device_destroy(i2c_dev->proxy_class, dev);
	class_destroy(i2c_dev->proxy_class);
	cdev_del(&i2c_dev->proxy_cdev);
	unregister_chrdev_region(dev, MINOR_NUM);
}

/*
 * I2C bus
 */

static void __maybe_unused eub_i2c_debug_print_msg(
	struct eub_i2c_dev *i2c_dev,
	struct i2c_msg *msg, int i, int total, const char *fname)
{
	pr_info("%s: msg(%d/%d) %s addr=0x%02x, len=%u flags=%s%s%s%s%s%s%s [i2c%d]\n",
		fname, i, total,
		msg->flags & I2C_M_RD ? "read" : "write", msg->addr, msg->len,
		msg->flags & I2C_M_TEN ? "TEN" : "",
		msg->flags & I2C_M_RECV_LEN ? "RECV_LEN" : "",
		msg->flags & I2C_M_NO_RD_ACK ? "NO_RD_ACK" : "",
		msg->flags & I2C_M_IGNORE_NAK ? "IGNORE_NAK" : "",
		msg->flags & I2C_M_REV_DIR_ADDR ? "REV_DIR_ADDR" : "",
		msg->flags & I2C_M_NOSTART ? "NOSTART" : "",
		msg->flags & I2C_M_STOP ? "STOP" : "",
		i2c_dev->adapter.nr);
}

static int eub_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
			int num)
{
	struct eub_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	unsigned long time_left;
	int i;
	int ret = num;

	if (num <= 0)
		return -EBADMSG;

	mutex_lock(&i2c_dev->mutex);

	while (i2c_dev->curr_msgs) {	/* busy */
		mutex_unlock(&i2c_dev->mutex);
		if (wait_event_interruptible(i2c_dev->inq,
					     (i2c_dev->curr_msgs == 0)))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&i2c_dev->mutex))
			return -ERESTARTSYS;
	}

	i2c_dev->curr_msgs = msgs;
	i2c_dev->num_msgs = 0;
	i2c_dev->len = i2c_dev->offset = 0;
	i2c_dev->msg_err = 0;

	char* p = i2c_dev->buffer;
	for (i = 0; i < num; i++) {
		struct i2c_msg *msg = &msgs[i];

		int len = LEN_BUFFER - i2c_dev->len;
		if (len < I2C_MSG_HDR_SIZE + msg->len) {
			ret = i2c_dev->msg_err = -EIO;
			break;
		}
		memcpy(p, msg, I2C_MSG_HDR_SIZE);
		memcpy(p + I2C_MSG_HDR_SIZE, msg->buf, msg->len);
		p += I2C_MSG_HDR_SIZE + msg->len;
		i2c_dev->len += I2C_MSG_HDR_SIZE + msg->len;
		++i2c_dev->num_msgs;
	}

	if (!i2c_dev->msg_err) {
		reinit_completion(&i2c_dev->completion);
		mutex_unlock(&i2c_dev->mutex);

		/* awake any reader and wait for the completion */
		wake_up_interruptible(&i2c_dev->outq);
		time_left = wait_for_completion_timeout(&i2c_dev->completion,
							adap->timeout);

		mutex_lock(&i2c_dev->mutex);
		if (!time_left) {
			ret = -ETIMEDOUT;
			pr_info("%s: i2c transfer timed out (%d)\n", __func__,
				adap->timeout);
		} else if (i2c_dev->msg_err) {
			ret = i2c_dev->msg_err;
			pr_info("%s: i2c transfer error\n", __func__);
		}
	}

	i2c_dev->curr_msgs = 0;
	i2c_dev->len = i2c_dev->offset = 0;
	mutex_unlock(&i2c_dev->mutex);

	wake_up_interruptible(&i2c_dev->inq);
	return ret;
}

static u32 eub_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm eub_i2c_algorithm = {
	.master_xfer = eub_i2c_xfer,
	.functionality = eub_i2c_functionality,
};

static int eub_i2c_probe(struct platform_device *pdev)
{
	struct eub_i2c_dev *i2c_dev;
	int err = -ENOMEM;
	struct i2c_adapter *adap;

	i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, i2c_dev);
	i2c_dev->dev = &pdev->dev;
	init_completion(&i2c_dev->completion);

	adap = &i2c_dev->adapter;
	i2c_set_adapdata(adap, i2c_dev);
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_DEPRECATED;
	adap->algo = &eub_i2c_algorithm;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;
	strlcpy(adap->name, dev_name(&pdev->dev), sizeof(adap->name));

	err = i2c_add_adapter(adap);
	if (err < 0) {
		dev_err(&pdev->dev, "could not add I2C adapter: %d\n", err);
		return err;
	}

	if (proxy_init(i2c_dev) < 0)
		return -ENOMEM;

	i2c_dev->curr_msgs = 0;
	i2c_dev->len = i2c_dev->offset = 0;

	init_waitqueue_head(&i2c_dev->inq);
	init_waitqueue_head(&i2c_dev->outq);
	mutex_init(&i2c_dev->mutex);

	return 0;
}

static int eub_i2c_remove(struct platform_device *pdev)
{
	struct eub_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	proxy_exit(i2c_dev);
	platform_set_drvdata(pdev, NULL);
	i2c_del_adapter(&i2c_dev->adapter);
	return 0;
}

static const struct of_device_id eub_i2c_of_match[] = {
        { .compatible = "eub_i2c" },
        {},
};
MODULE_DEVICE_TABLE(of, eub_i2c_of_match);

static struct platform_driver eub_i2c_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = eub_i2c_of_match,
	},
	.probe		= eub_i2c_probe,
	.remove		= eub_i2c_remove,
};

module_platform_driver(eub_i2c_driver);

MODULE_DESCRIPTION("Esrille Unbrick I2C Bridge Kernel Driver");
MODULE_AUTHOR("Esrille Inc. <info@esrille.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
