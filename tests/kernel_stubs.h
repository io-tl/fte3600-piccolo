/* SPDX-License-Identifier: GPL-2.0-only */
/* Test doubles only: no device files, syscalls on hardware, or fingerprint data. */
#ifndef FOCAL_TEST_KERNEL_STUBS_H
#define FOCAL_TEST_KERNEL_STUBS_H

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>

#define __user
#define __packed __attribute__((packed))
#define BIT(n) (1UL << (n))
#define container_of(p, type, member) \
	((type *)((char *)(p) - offsetof(type, member)))
#define ERESTARTSYS 512
#define IRQ_HANDLED 1
typedef unsigned char u8;
typedef uint16_t u16;
typedef unsigned int __poll_t;
typedef int irqreturn_t;
typedef pthread_mutex_t spinlock_t;
typedef unsigned int wait_queue_head_t;
struct mutex { pthread_mutex_t lock; };
struct kref { unsigned int count; };
struct miscdevice { int unused; };
struct inode { int unused; };
struct file { void *private_data; };
struct device { void *data; };
struct spi_device { struct device dev; int irq; };
struct gpio_desc { int value; };
struct acpi_gpio_params { int unused; };
struct acpi_gpio_mapping { int unused; };
typedef struct { __poll_t events; } poll_table;

static struct {
	struct spi_device spi;
	struct gpio_desc gpio;
	int irq_depth;
	bool irq_freed;
	bool hardware_removed;
	unsigned int enables, disables, setups, directions, gpio_writes;
	unsigned int transfers, frees, error_logs, sleeps, compat_calls;
	unsigned int last_tx, last_rx;
	int setup_error, direction_error, transfer_error;
	bool fail_copy_to, fail_copy_from;
	void (*after_copy_to)(void);
	void (*after_enable)(void);
	void *compat_argument;
} fake;

static void mutex_init(struct mutex *lock)
{
	assert(!pthread_mutex_init(&lock->lock, NULL));
}
static void mutex_lock(struct mutex *lock)
{
	assert(!pthread_mutex_lock(&lock->lock));
}
static int mutex_lock_interruptible(struct mutex *lock)
{
	mutex_lock(lock);
	return 0;
}
static void mutex_unlock(struct mutex *lock)
{
	assert(!pthread_mutex_unlock(&lock->lock));
}
#define spin_lock_init(lock) assert(!pthread_mutex_init((lock), NULL))
#define spin_lock_irqsave(lock, flags) do { \
	(flags) = 0; assert(!pthread_mutex_lock(lock)); \
} while (0)
#define spin_unlock_irqrestore(lock, flags) do { \
	(void)(flags); assert(!pthread_mutex_unlock(lock)); \
} while (0)
#define wake_up_interruptible(queue) \
	((void)__atomic_fetch_add((queue), 1, __ATOMIC_RELAXED))
#define poll_wait(file, queue, table) ((void)(file), (void)(queue), (void)(table))
#define poll_requested_events(table) ((table) ? (table)->events : ~0U)
#define spi_get_drvdata(spi) ((spi)->dev.data)
#define to_spi_device(devp) container_of(devp, struct spi_device, dev)

static void kref_init(struct kref *ref) { ref->count = 1; }
static void kref_get(struct kref *ref) { assert(ref->count); ref->count++; }
static void kref_put(struct kref *ref, void (*release)(struct kref *))
{
	assert(ref->count);
	if (!--ref->count)
		release(ref);
}
static void kfree(void *p) { fake.frees++; free(p); }
static void misc_deregister(struct miscdevice *misc) { (void)misc; }

static void enable_irq(int irq)
{
	assert(!fake.hardware_removed && !fake.irq_freed && fake.irq_depth == 1);
	fake.irq_depth--;
	fake.enables++;
	if (fake.after_enable)
		fake.after_enable();
}
static void disable_irq(int irq)
{
	assert(!fake.hardware_removed && !fake.irq_freed && fake.irq_depth == 0);
	fake.irq_depth++;
	fake.disables++;
}
static void devm_free_irq(struct device *dev, int irq, void *data)
{
	assert(!fake.irq_freed && fake.irq_depth == 1);
	fake.irq_freed = true;
}
static void gpiod_set_value_cansleep(struct gpio_desc *gpio, int value)
{
	assert(!fake.hardware_removed);
	fake.gpio_writes++;
	gpio->value = value;
}
static int gpiod_direction_output(struct gpio_desc *gpio, int value)
{
	assert(!fake.hardware_removed);
	fake.directions++;
	if (fake.direction_error)
		return fake.direction_error;
	gpio->value = value;
	return 0;
}
static int spi_setup(struct spi_device *spi)
{
	assert(!fake.hardware_removed);
	fake.setups++;
	return fake.setup_error;
}
static int spi_write_then_read(struct spi_device *spi, const void *tx,
			       unsigned int tx_len, void *rx, unsigned int rx_len)
{
	assert(!fake.hardware_removed);
	fake.transfers++;
	fake.last_tx = tx_len;
	fake.last_rx = rx_len;
	memset(rx, 0xa6, rx_len);
	return fake.transfer_error;
}
static int spi_read(struct spi_device *spi, void *rx, unsigned int rx_len)
{
	return spi_write_then_read(spi, NULL, 0, rx, rx_len);
}
static int spi_write(struct spi_device *spi, const void *tx, unsigned int tx_len)
{
	assert(!fake.hardware_removed);
	fake.transfers++;
	fake.last_tx = tx_len;
	return fake.transfer_error;
}
static void msleep(unsigned int ms) { fake.sleeps++; }
static u16 get_unaligned_le16(const void *p)
{
	const u8 *bytes = p;
	return bytes[0] | (bytes[1] << 8);
}
static size_t copy_from_user(void *dst, const void *src, size_t length)
{
	if (!src || fake.fail_copy_from)
		return length;
	memcpy(dst, src, length);
	return 0;
}
static size_t copy_to_user(void *dst, const void *src, size_t length)
{
	if (fake.after_copy_to) {
		void (*hook)(void) = fake.after_copy_to;
		fake.after_copy_to = NULL;
		hook();
	}
	if (!dst || fake.fail_copy_to)
		return length;
	memcpy(dst, src, length);
	return 0;
}
static void *compat_ptr(unsigned long value)
{
	fake.compat_calls++;
	return fake.compat_argument;
}
static void test_log(struct device *dev, const char *format, ...)
	__attribute__((format(printf, 2, 3)));
static void test_log(struct device *dev, const char *format, ...) { }
#define dev_info test_log
#define dev_err(dev, ...) do { fake.error_logs++; test_log(dev, __VA_ARGS__); } while (0)
#define dev_err_ratelimited dev_err

#endif
