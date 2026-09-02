// SPDX-License-Identifier: GPL-2.0-only
/*
 * FocalTech FTEXX00 fingerprint sensor transport over SPI
 *
 * This module exposes the /dev/focal_moh_spi ABI used by FocalTech's
 * userspace libfprint backend. It does not process or log fingerprint data.
 *
 * Copyright (c) 2024 FocalTech Systems (ShenZhen) Co., Ltd.
 * Copyright (c) 2026 FTEXX00 Linux driver contributors
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/unaligned.h>

#define FOCAL_DRIVER_NAME	"focaltech-ftexx00"
#define FOCAL_DEVICE_NAME	"focal_moh_spi"
#define FOCAL_IRQ_NAME		"focaltech-fingerprint"
#define FOCAL_VERSION		"0.1"

#define FOCAL_MAX_BUFFER_SIZE	(32 * 1024)
#define FOCAL_DEFAULT_SPI_HZ	4000000

/* Packet types used by the FocalTech userspace transport. */
#define FOCAL_SPI_READ_ONLY	0x5a
#define FOCAL_SPI_READ_WRITE	0xa5
#define FOCAL_SPI_BACK_DATA	0xb9

/* These raw values are part of FocalTech's userspace ABI. */
enum focal_ioctl_cmd {
	FF_IOC_RESET_DEV = 0x8086,
	FF_IOC_POWER_OFF,
	FF_IOC_POWER_ON,
	FF_IOC_IRQ_EN,
	FF_IOC_LOG_EN,
	FF_IOC_RELEASE_POLL,
	FF_IOC_CSN,
};

enum focal_wake_event {
	FOCAL_WAKE_EVENT_NONE = 0,
	FOCAL_WAKE_EVENT_ENABLE,
	FOCAL_WAKE_EVENT_INT,
	FOCAL_WAKE_EVENT_RESUME,
	FOCAL_WAKE_EVENT_SUSPEND,
	FOCAL_WAKE_EVENT_DISABLE,
};

/* The vendor ABI uses a packed, little-endian five-byte header. */
struct focal_spi_packet {
	u8 type;
	u8 tx_len[2];
	u8 rx_len[2];
	u8 payload[];
} __packed;

#define FOCAL_PACKET_HEADER_SIZE offsetof(struct focal_spi_packet, payload)

struct focal_fp_data {
	struct spi_device *spi;
	struct gpio_desc *reset_gpio;
	struct miscdevice miscdev;
	struct mutex io_lock;
	wait_queue_head_t poll_wait;
	enum focal_wake_event wake_event;
	bool initialized;
	bool irq_disabled;
	bool claimed;
	bool debug_enabled;
	u8 *write_buffer;
	u8 *read_buffer;
	u8 *sensor_init_data;
	struct acpi_gpio_params reset_gpio_params;
	struct acpi_gpio_mapping reset_gpio_mapping[2];
};

/* Set this to zero to retain the maximum speed supplied by firmware. */
static uint spi_clock_hz = FOCAL_DEFAULT_SPI_HZ;
module_param(spi_clock_hz, uint, 0444);
MODULE_PARM_DESC(spi_clock_hz,
		 "SPI clock override in Hz; 0 keeps the firmware value");

/* -1 preserves firmware polarity, 0 forces active-low, 1 active-high. */
static int cs_active_high = -1;
module_param(cs_active_high, int, 0444);
MODULE_PARM_DESC(cs_active_high,
		 "Chip-select polarity: -1 firmware, 0 active-low, 1 active-high");

/* Settings for firmware that exposes reset only as an unnamed GpioIo. */
static uint reset_gpio_index;
module_param(reset_gpio_index, uint, 0444);
MODULE_PARM_DESC(reset_gpio_index,
		 "Unnamed ACPI GpioIo reset resource index (default 0)");

static bool reset_active_low = true;
module_param(reset_active_low, bool, 0444);
MODULE_PARM_DESC(reset_active_low,
		 "Polarity of the unnamed ACPI reset GPIO fallback");

static struct focal_fp_data *focal_file_data(struct file *file)
{
	return file->private_data;
}

static void focal_debug(struct focal_fp_data *data, const char *message)
{
	if (data->debug_enabled)
		dev_info(&data->spi->dev, "%s\n", message);
}

static void focal_wake_userspace(struct focal_fp_data *data,
				 enum focal_wake_event event)
{
	WRITE_ONCE(data->wake_event, event);
	wake_up_interruptible(&data->poll_wait);
}

static void focal_assert_reset(struct focal_fp_data *data)
{
	/* GPIO descriptor values are logical: one means asserted. */
	gpiod_set_value_cansleep(data->reset_gpio, 1);
}

static void focal_deassert_reset(struct focal_fp_data *data)
{
	gpiod_set_value_cansleep(data->reset_gpio, 0);
}

static void focal_hw_reset(struct focal_fp_data *data)
{
	focal_assert_reset(data);
	msleep(10);
	focal_deassert_reset(data);
	msleep(50);
}

static int focal_spi_transfer(struct focal_fp_data *data, u16 tx_len,
			      u16 rx_len)
{
	if (!READ_ONCE(data->initialized))
		return -ENODEV;

	if (tx_len)
		return spi_write_then_read(data->spi, data->write_buffer, tx_len,
					   data->read_buffer, rx_len);

	return spi_read(data->spi, data->read_buffer, rx_len);
}

static int focal_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct focal_fp_data *data;
	int ret = 0;

	data = container_of(misc, struct focal_fp_data, miscdev);
	mutex_lock(&data->io_lock);
	if (!data->initialized)
		ret = -ENODEV;
	else if (data->claimed)
		ret = -EBUSY;
	else {
		data->claimed = true;
		data->wake_event = FOCAL_WAKE_EVENT_NONE;
		file->private_data = data;
	}
	mutex_unlock(&data->io_lock);

	return ret;
}

static int focal_release(struct inode *inode, struct file *file)
{
	struct focal_fp_data *data = focal_file_data(file);

	mutex_lock(&data->io_lock);
	if (!data->irq_disabled) {
		disable_irq(data->spi->irq);
		data->irq_disabled = true;
	}
	data->wake_event = FOCAL_WAKE_EVENT_NONE;
	data->claimed = false;
	mutex_unlock(&data->io_lock);

	return 0;
}

static irqreturn_t focal_spi_irq_handler(int irq, void *dev_id)
{
	struct focal_fp_data *data = dev_id;

	if (READ_ONCE(data->wake_event) <= FOCAL_WAKE_EVENT_ENABLE)
		focal_wake_userspace(data, FOCAL_WAKE_EVENT_INT);

	return IRQ_HANDLED;
}

static ssize_t focal_read(struct file *file, char __user *user_buffer,
			  size_t count, loff_t *position)
{
	struct focal_fp_data *data = focal_file_data(file);
	struct focal_spi_packet *packet;
	u16 tx_len, rx_len;
	ssize_t ret;

	if (count < FOCAL_PACKET_HEADER_SIZE || count > FOCAL_MAX_BUFFER_SIZE)
		return -EINVAL;

	if (mutex_lock_interruptible(&data->io_lock))
		return -ERESTARTSYS;

	if (!data->initialized) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (copy_from_user(data->write_buffer, user_buffer, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	packet = (struct focal_spi_packet *)data->write_buffer;
	if (packet->type == FOCAL_SPI_BACK_DATA) {
		if (copy_to_user(user_buffer, data->sensor_init_data, count))
			ret = -EFAULT;
		else
			ret = count;
		goto out_unlock;
	}

	if (packet->type != FOCAL_SPI_READ_WRITE &&
	    packet->type != FOCAL_SPI_READ_ONLY) {
		ret = -EINVAL;
		goto out_unlock;
	}

	tx_len = packet->type == FOCAL_SPI_READ_WRITE ?
		 get_unaligned_le16(packet->tx_len) : 0;
	rx_len = get_unaligned_le16(packet->rx_len);

	if (!rx_len || tx_len > FOCAL_MAX_BUFFER_SIZE ||
	    rx_len > FOCAL_MAX_BUFFER_SIZE ||
	    tx_len + rx_len > FOCAL_MAX_BUFFER_SIZE ||
	    FOCAL_PACKET_HEADER_SIZE + tx_len > count) {
		ret = -EMSGSIZE;
		goto out_unlock;
	}

	/*
	 * This ABI is unlike a conventional read(2): count covers the five-byte
	 * header plus its transmit payload, while rx_len is the amount written
	 * back. For example, the backend calls read(..., 11) for a six-byte
	 * command whose response is up to 1016 bytes. Do not require rx_len <=
	 * count; the userspace backend supplies a larger allocation.
	 */
	if (tx_len)
		memmove(data->write_buffer, packet->payload, tx_len);

	ret = focal_spi_transfer(data, tx_len, rx_len);
	if (ret)
		goto out_unlock;

	if (copy_to_user(user_buffer, data->read_buffer, rx_len)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	/* Preserve the established ABI: success returns the requested count. */
	ret = count;

out_unlock:
	mutex_unlock(&data->io_lock);
	return ret;
}

static ssize_t focal_write(struct file *file, const char __user *user_buffer,
			   size_t count, loff_t *position)
{
	struct focal_fp_data *data = focal_file_data(file);
	struct focal_spi_packet *packet;
	size_t payload_len;
	ssize_t ret;

	if (count < FOCAL_PACKET_HEADER_SIZE || count > FOCAL_MAX_BUFFER_SIZE)
		return -EINVAL;

	if (mutex_lock_interruptible(&data->io_lock))
		return -ERESTARTSYS;

	if (!data->initialized) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (copy_from_user(data->write_buffer, user_buffer, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	packet = (struct focal_spi_packet *)data->write_buffer;
	if (packet->type == FOCAL_SPI_BACK_DATA) {
		memcpy(data->sensor_init_data, data->write_buffer, count);
		ret = count;
		goto out_unlock;
	}

	payload_len = count - FOCAL_PACKET_HEADER_SIZE;
	ret = spi_write(data->spi, packet->payload, payload_len);
	if (!ret)
		ret = count;

out_unlock:
	mutex_unlock(&data->io_lock);
	return ret;
}

static long focal_ioctl(struct file *file, unsigned int command,
			unsigned long argument)
{
	struct focal_fp_data *data = focal_file_data(file);
	long ret = 0;

	if (mutex_lock_interruptible(&data->io_lock))
		return -ERESTARTSYS;

	if (!data->initialized) {
		ret = -ENODEV;
		goto out_unlock;
	}

	switch (command) {
	case FF_IOC_RESET_DEV:
		focal_hw_reset(data);
		break;
	case FF_IOC_POWER_OFF:
		focal_assert_reset(data);
		break;
	case FF_IOC_POWER_ON:
		focal_deassert_reset(data);
		break;
	case FF_IOC_IRQ_EN:
		if (argument && data->irq_disabled) {
			enable_irq(data->spi->irq);
			data->irq_disabled = false;
		} else if (!argument && !data->irq_disabled) {
			disable_irq(data->spi->irq);
			data->irq_disabled = true;
		}
		break;
	case FF_IOC_LOG_EN:
		data->debug_enabled = !!argument;
		focal_debug(data, "userspace debug flag enabled");
		break;
	case FF_IOC_RELEASE_POLL:
		if (argument > FOCAL_WAKE_EVENT_DISABLE) {
			ret = -EINVAL;
			break;
		}
		focal_wake_userspace(data, argument);
		break;
	case FF_IOC_CSN:
		/* Chip select is managed by the SPI controller. */
		break;
	default:
		ret = -ENOTTY;
	}

out_unlock:
	mutex_unlock(&data->io_lock);
	return ret;
}

#ifdef CONFIG_COMPAT
static long focal_compat_ioctl(struct file *file, unsigned int command,
			       unsigned long argument)
{
	return focal_ioctl(file, command, argument);
}
#endif

static __poll_t focal_poll(struct file *file, poll_table *wait)
{
	struct focal_fp_data *data = focal_file_data(file);
	enum focal_wake_event event;

	poll_wait(file, &data->poll_wait, wait);
	event = xchg(&data->wake_event, FOCAL_WAKE_EVENT_NONE);

	/* Event values double as the mask expected by the userspace backend. */
	return (__poll_t)event;
}

static const struct file_operations focal_fops = {
	.owner = THIS_MODULE,
	.open = focal_open,
	.release = focal_release,
	.read = focal_read,
	.write = focal_write,
	.unlocked_ioctl = focal_ioctl,
	.poll = focal_poll,
#ifdef CONFIG_COMPAT
	.compat_ioctl = focal_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static int focal_get_reset_gpio(struct focal_fp_data *data)
{
	struct device *dev = &data->spi->dev;
	struct gpio_desc *gpio;
	int ret;

	/* Prefer a portable reset-gpios property supplied by ACPI _DSD. */
	gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return dev_err_probe(dev, PTR_ERR(gpio),
				     "failed to get named reset GPIO\n");
	if (gpio) {
		data->reset_gpio = gpio;
		return 0;
	}

	if (!has_acpi_companion(dev))
		return dev_err_probe(dev, -ENOENT,
				     "firmware has no reset-gpios property\n");

	/* Fall back to an unnamed GpioIo resource used by vendor ACPI tables. */
	data->reset_gpio_params.crs_entry_index = reset_gpio_index;
	data->reset_gpio_params.line_index = 0;
	data->reset_gpio_params.active_low = reset_active_low;
	data->reset_gpio_mapping[0].name = "reset-gpios";
	data->reset_gpio_mapping[0].data = &data->reset_gpio_params;
	data->reset_gpio_mapping[0].size = 1;
	data->reset_gpio_mapping[0].quirks = ACPI_GPIO_QUIRK_ONLY_GPIOIO;

	ret = devm_acpi_dev_add_driver_gpios(dev, data->reset_gpio_mapping);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to map unnamed ACPI reset GPIO\n");

	data->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(data->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(data->reset_gpio),
				     "failed to get unnamed ACPI reset GPIO %u\n",
				     reset_gpio_index);

	dev_info(dev, "using unnamed ACPI GpioIo %u as active-%s reset\n",
		 reset_gpio_index, reset_active_low ? "low" : "high");
	return 0;
}

static int focal_configure_hardware(struct focal_fp_data *data)
{
	struct spi_device *spi = data->spi;
	struct device *dev = &spi->dev;
	int ret;

	ret = focal_get_reset_gpio(data);
	if (ret)
		return ret;

	if (cs_active_high < -1 || cs_active_high > 1)
		return dev_err_probe(dev, -EINVAL,
				     "cs_active_high must be -1, 0, or 1\n");

	if (spi_clock_hz)
		spi->max_speed_hz = spi_clock_hz;
	spi->bits_per_word = 8;

	if (cs_active_high == 1)
		spi->mode |= SPI_CS_HIGH;
	else if (cs_active_high == 0)
		spi->mode &= ~SPI_CS_HIGH;

	ret = spi_setup(spi);
	if (ret)
		return dev_err_probe(dev, ret, "failed to configure SPI\n");

	dev_info(dev, "SPI mode %#x, %u bits, %u Hz, IRQ %d\n",
		 spi->mode, spi->bits_per_word, spi->max_speed_hz, spi->irq);
	return 0;
}

static int focal_allocate_buffers(struct focal_fp_data *data)
{
	struct device *dev = &data->spi->dev;

	/* SPI controller buffers must be backed by physically contiguous memory. */
	data->write_buffer = devm_kmalloc(dev, FOCAL_MAX_BUFFER_SIZE,
					 GFP_KERNEL);
	data->read_buffer = devm_kmalloc(dev, FOCAL_MAX_BUFFER_SIZE,
					GFP_KERNEL);
	data->sensor_init_data = devm_kzalloc(dev, FOCAL_MAX_BUFFER_SIZE,
					     GFP_KERNEL);

	if (!data->write_buffer || !data->read_buffer ||
	    !data->sensor_init_data)
		return -ENOMEM;

	return 0;
}

static int focal_spi_probe(struct spi_device *spi)
{
	struct focal_fp_data *data;
	int ret;

	data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->spi = spi;
	mutex_init(&data->io_lock);
	init_waitqueue_head(&data->poll_wait);
	spi_set_drvdata(spi, data);

	ret = focal_allocate_buffers(data);
	if (ret)
		return dev_err_probe(&spi->dev, ret,
				     "failed to allocate transfer buffers\n");

	ret = focal_configure_hardware(data);
	if (ret)
		return ret;

	focal_hw_reset(data);

	if (spi->irq <= 0)
		return dev_err_probe(&spi->dev, -EINVAL,
				     "firmware did not provide an IRQ\n");

	ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL,
					focal_spi_irq_handler, IRQF_ONESHOT,
					FOCAL_IRQ_NAME, data);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "failed to request IRQ\n");

	disable_irq(spi->irq);
	data->irq_disabled = true;
	data->initialized = true;

	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = FOCAL_DEVICE_NAME;
	data->miscdev.fops = &focal_fops;
	data->miscdev.parent = &spi->dev;
	data->miscdev.mode = 0600;

	ret = misc_register(&data->miscdev);
	if (ret) {
		data->initialized = false;
		enable_irq(spi->irq);
		data->irq_disabled = false;
		return dev_err_probe(&spi->dev, ret,
				     "failed to register userspace transport\n");
	}

	dev_info(&spi->dev, "FTEXX00 transport %s ready as /dev/%s\n",
		 FOCAL_VERSION, FOCAL_DEVICE_NAME);
	return 0;
}

static void focal_spi_remove(struct spi_device *spi)
{
	struct focal_fp_data *data = spi_get_drvdata(spi);

	misc_deregister(&data->miscdev);
	WRITE_ONCE(data->initialized, false);
	focal_wake_userspace(data, FOCAL_WAKE_EVENT_DISABLE);

	/* Balance disable_irq() before the managed IRQ is released. */
	if (data->irq_disabled) {
		enable_irq(spi->irq);
		data->irq_disabled = false;
	}
}

static int focal_spi_suspend(struct device *dev)
{
	struct focal_fp_data *data = spi_get_drvdata(to_spi_device(dev));

	focal_wake_userspace(data, FOCAL_WAKE_EVENT_SUSPEND);
	return 0;
}

static int focal_spi_resume(struct device *dev)
{
	struct focal_fp_data *data = spi_get_drvdata(to_spi_device(dev));

	focal_wake_userspace(data, FOCAL_WAKE_EVENT_RESUME);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(focal_spi_pm_ops, focal_spi_suspend,
				focal_spi_resume);

static const struct acpi_device_id focal_spi_acpi_match[] = {
	{ "FTE3600", 0 },
	{ "FTE4800", 0 },
	{ "FTE6600", 0 },
	{ "FTE6900", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, focal_spi_acpi_match);

static struct spi_driver focal_spi_driver = {
	.driver = {
		.name = FOCAL_DRIVER_NAME,
		.acpi_match_table = focal_spi_acpi_match,
		.pm = pm_sleep_ptr(&focal_spi_pm_ops),
	},
	.probe = focal_spi_probe,
	.remove = focal_spi_remove,
};
module_spi_driver(focal_spi_driver);

MODULE_AUTHOR("FocalTech Systems; FTEXX00 Linux driver contributors");
MODULE_DESCRIPTION("FocalTech FTE3600/FTE4800/FTE6600/FTE6900 SPI transport");
MODULE_LICENSE("GPL");
MODULE_VERSION(FOCAL_VERSION);
