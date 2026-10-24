// SPDX-License-Identifier: GPL-2.0-only

#include "kernel_stubs.h"
#include "driver_under_test.inc"

static struct focal_fp_data *data;
static struct file file;
static poll_table all_events = { .events = EPOLLIN | EPOLLPRI | EPOLLOUT };
static unsigned int tests;

_Static_assert(FF_IOC_RESET_DEV == 0x8086 && FF_IOC_CSN == 0x808c,
	       "vendor ioctl numbers must not change");
_Static_assert(FOCAL_WAKE_EVENT_INT == 2 && FOCAL_WAKE_EVENT_DISABLE == 5,
	       "vendor event codes must not change");
_Static_assert(_IOC_SIZE(FOCAL_IOC_GET_EVENT) == 4 &&
	       _IOC_SIZE(FOCAL_IOC_SET_EVENT_MODE) == 4,
	       "new ioctl payloads must be identical on 32 and 64 bit");

static void fixture(void)
{
	memset(&fake, 0, sizeof(fake));
	fake.irq_depth = 1;
	data = calloc(1, sizeof(*data));
	assert(data);
	data->spi = &fake.spi;
	data->reset_gpio = &fake.gpio;
	data->initialized = true;
	data->irq_disabled = true;
	data->write_buffer = calloc(1, FOCAL_MAX_BUFFER_SIZE);
	data->read_buffer = calloc(1, FOCAL_MAX_BUFFER_SIZE);
	data->sensor_init_data = calloc(1, FOCAL_MAX_BUFFER_SIZE);
	assert(data->write_buffer && data->read_buffer && data->sensor_init_data);
	mutex_init(&data->io_lock);
	spin_lock_init(&data->event_lock);
	kref_init(&data->refcount);
	fake.spi.dev.data = data;
	file.private_data = &data->miscdev;
	assert(focal_open(NULL, &file) == 0);
	assert(data->refcount.count == 2);
}

static void managed_cleanup(void)
{
	free(data->write_buffer);
	free(data->read_buffer);
	free(data->sensor_init_data);
	fake.hardware_removed = true;
	focal_put(data);
}

static void finish(void)
{
	if (data->claimed)
		assert(focal_release(NULL, &file) == 0);
	focal_spi_remove(&fake.spi);
	managed_cleanup();
	assert(fake.frees == 1);
	tests++;
}

static long command(unsigned int cmd, unsigned long arg)
{
	return focal_ioctl(&file, cmd, arg);
}

static void clear_events(void)
{
	assert(command(FF_IOC_RELEASE_POLL, FOCAL_WAKE_EVENT_NONE) == 0);
}

static void touch(void)
{
	assert(focal_spi_irq_handler(0, data) == IRQ_HANDLED);
}

static void explicit_mode(void)
{
	__u32 mode = FOCAL_EVENT_MODE_EXPLICIT;
	assert(command(FOCAL_IOC_SET_EVENT_MODE, (unsigned long)&mode) == 0);
}

static unsigned int get_event(void)
{
	__u32 event = 99;
	assert(command(FOCAL_IOC_GET_EVENT, (unsigned long)&event) == 0);
	return event;
}

static void test_legacy_events(void)
{
	poll_table input = { .events = EPOLLIN };

	fixture();
	touch();
	assert(focal_poll(&file, &input) == 0);
	assert(focal_poll(&file, &all_events) == FOCAL_WAKE_EVENT_INT);
	assert(focal_poll(&file, &all_events) == 0);
	assert(command(FF_IOC_RELEASE_POLL, FOCAL_WAKE_EVENT_RESUME) == 0);
	/* A truncated numeric event (RESUME=3 -> POLLIN=1) is also rejected. */
	assert(focal_poll(&file, &input) == 0);
	assert(focal_poll(&file, &all_events) == FOCAL_WAKE_EVENT_RESUME);
	for (int irq_first = 0; irq_first < 2; irq_first++) {
		clear_events();
		if (irq_first)
			touch();
		assert(command(FF_IOC_RELEASE_POLL, FOCAL_WAKE_EVENT_DISABLE) == 0);
		if (!irq_first)
			touch();
		assert(focal_poll(&file, &all_events) == FOCAL_WAKE_EVENT_DISABLE);
		assert(focal_poll(&file, &all_events) == 0);
	}
	assert(command(FF_IOC_RELEASE_POLL, FOCAL_WAKE_EVENT_DISABLE) == 0);
	assert(command(FF_IOC_RELEASE_POLL, FOCAL_WAKE_EVENT_ENABLE) == 0);
	assert(focal_poll(&file, &all_events) == FOCAL_WAKE_EVENT_ENABLE);
	finish();
}

static void test_explicit_events(void)
{
	__u32 value = 99;

	fixture();
	assert(command(FOCAL_IOC_GET_EVENT, (unsigned long)&value) == -EINVAL);
	assert(command(FOCAL_IOC_SET_EVENT_MODE, (unsigned long)&value) == -EINVAL);
	assert(command(FOCAL_IOC_SET_EVENT_MODE, 0) == -EFAULT);
	/* Vendor compat arguments stay scalar; the new commands take pointers. */
	assert(focal_compat_ioctl(&file, FF_IOC_IRQ_EN, 1) == 0);
	assert(fake.compat_calls == 0 && fake.enables == 1);
	value = FOCAL_EVENT_MODE_EXPLICIT;
	fake.compat_argument = &value;
	assert(focal_compat_ioctl(&file, FOCAL_IOC_SET_EVENT_MODE, 0x1234) == 0);
	assert(fake.compat_calls == 1 && data->explicit_events);
	assert(focal_compat_ioctl(&file, FOCAL_IOC_GET_EVENT, 0x1234) == 0);
	assert(fake.compat_calls == 2 && value == FOCAL_WAKE_EVENT_NONE);
	touch();
	assert(focal_poll(&file, &all_events) & EPOLLIN);
	assert(focal_poll(&file, &all_events) & EPOLLIN);
	assert(get_event() == FOCAL_WAKE_EVENT_INT);
	assert(focal_poll(&file, &all_events) == 0);
	assert(get_event() == FOCAL_WAKE_EVENT_NONE);
	assert(command(FF_IOC_RELEASE_POLL, FOCAL_WAKE_EVENT_RESUME) == 0);
	touch();
	assert(get_event() == FOCAL_WAKE_EVENT_RESUME);
	assert(get_event() == FOCAL_WAKE_EVENT_INT);
	touch();
	fake.fail_copy_to = true;
	assert(command(FOCAL_IOC_GET_EVENT, (unsigned long)&value) == -EFAULT);
	assert(focal_poll(&file, &all_events) & EPOLLIN);
	fake.fail_copy_to = false;
	fake.after_copy_to = touch;
	assert(get_event() == FOCAL_WAKE_EVENT_INT);
	/* An IRQ arriving during the copy belongs to the next acknowledgement. */
	assert(get_event() == FOCAL_WAKE_EVENT_INT);
	assert(get_event() == FOCAL_WAKE_EVENT_NONE);
	finish();
}

static void test_suspend_resume(void)
{
	u8 packet[5] = { FOCAL_SPI_READ_ONLY, 0, 0, 1, 0 };
	unsigned int enables;

	fixture();
	assert(command(FF_IOC_IRQ_EN, 1) == 0);
	assert(command(FF_IOC_IRQ_EN, 1) == 0);
	assert(fake.enables == 1);
	for (int i = 0; i < 2; i++) {
		touch();
		enables = fake.enables;
		assert(focal_spi_suspend(&fake.spi.dev) == 0);
		assert(focal_spi_suspend(&fake.spi.dev) == 0);
		assert(data->suspended && fake.irq_depth == 1);
		assert(focal_read(&file, (char *)packet, sizeof(packet), NULL) == -EHOSTDOWN);
		assert(focal_write(&file, (char *)packet, sizeof(packet), NULL) == -EHOSTDOWN);
		assert(command(FF_IOC_RESET_DEV, 0) == -EHOSTDOWN);
		/* Simulate a pending edge delivered immediately on unmask. */
		fake.after_enable = touch;
		assert(focal_spi_resume(&fake.spi.dev) == 0);
		assert(focal_spi_resume(&fake.spi.dev) == 0);
		fake.after_enable = NULL;
		assert(!data->suspended && fake.enables == enables + 1);
		assert(focal_poll(&file, &all_events) == FOCAL_WAKE_EVENT_RESUME);
		assert(focal_poll(&file, &all_events) == FOCAL_WAKE_EVENT_INT);
		assert(focal_poll(&file, &all_events) == 0);
	}
	assert(fake.transfers == 0 && fake.gpio_writes == 0);
	assert(fake.setups == 2 && fake.directions == 2);
	/* Sleep must preserve a pending stop, disabled IRQ and asserted reset. */
	enables = fake.enables;
	assert(command(FF_IOC_RELEASE_POLL, FOCAL_WAKE_EVENT_DISABLE) == 0);
	assert(command(FF_IOC_IRQ_EN, 0) == 0);
	assert(command(FF_IOC_POWER_OFF, 0) == 0);
	assert(focal_spi_suspend(&fake.spi.dev) == 0);
	assert(focal_spi_resume(&fake.spi.dev) == 0);
	assert(data->reset_asserted && fake.gpio.value == 1);
	assert(fake.enables == enables && fake.irq_depth == 1);
	assert(focal_poll(&file, &all_events) == FOCAL_WAKE_EVENT_DISABLE);
	assert(focal_poll(&file, &all_events) == FOCAL_WAKE_EVENT_RESUME);
	assert(focal_poll(&file, &all_events) == 0);
	assert(command(FF_IOC_POWER_ON, 0) == 0);
	assert(!data->reset_asserted && fake.gpio.value == 0);
	finish();
}

static void test_resume_recovery(void)
{
	fixture();
	assert(command(FF_IOC_IRQ_EN, 1) == 0);
	assert(focal_spi_suspend(&fake.spi.dev) == 0);
	fake.direction_error = -EIO;
	assert(focal_spi_resume(&fake.spi.dev) == -EIO);
	assert(!data->suspended && data->resume_error == -EIO);
	assert(fake.enables == 1 && fake.setups == 0);
	assert(focal_poll(&file, &all_events) == EPOLLERR);
	fake.direction_error = 0;
	fake.setup_error = -ETIMEDOUT;
	assert(focal_spi_resume(&fake.spi.dev) == -ETIMEDOUT);
	assert(data->resume_error == -ETIMEDOUT && fake.enables == 1);
	fake.setup_error = 0;
	assert(command(FF_IOC_RESET_DEV, 0) == 0);
	assert(!data->suspended && !data->resume_error && fake.enables == 2);
	assert(fake.error_logs == 2);
	/* A backend restart can also retry restoration without leaking a ref. */
	assert(focal_spi_suspend(&fake.spi.dev) == 0);
	fake.setup_error = -EIO;
	assert(focal_spi_resume(&fake.spi.dev) == -EIO);
	assert(focal_release(NULL, &file) == 0);
	file.private_data = &data->miscdev;
	assert(focal_open(NULL, &file) == -EIO);
	assert(data->refcount.count == 1);
	fake.setup_error = 0;
	assert(focal_open(NULL, &file) == 0);
	assert(!data->resume_error && fake.irq_depth == 1);
	finish();
}

static void test_file_lifetime(void)
{
	struct file other = { 0 };
	u8 packet[5] = { 0 };

	fixture();
	other.private_data = &data->miscdev;
	assert(focal_open(NULL, &other) == -EBUSY);
	assert(data->refcount.count == 2);
	assert(command(FF_IOC_IRQ_EN, 1) == 0);
	explicit_mode();
	touch();
	assert(focal_release(NULL, &file) == 0);
	assert(data->refcount.count == 1 && fake.irq_depth == 1);
	assert(focal_open(NULL, &other) == 0);
	file = other;
	assert(!data->explicit_events && !data->irq_requested);
	assert(focal_poll(&file, &all_events) == 0);
	assert(command(FF_IOC_IRQ_EN, 1) == 0);
	assert(fake.enables == 2);
	assert(focal_spi_suspend(&fake.spi.dev) == 0);
	assert(focal_release(NULL, &file) == 0);
	file.private_data = &data->miscdev;
	assert(focal_open(NULL, &file) == -EHOSTDOWN);
	assert(focal_spi_resume(&fake.spi.dev) == 0);
	assert(fake.enables == 2 && fake.irq_depth == 1);
	assert(focal_open(NULL, &file) == 0);
	assert(!data->irq_requested && focal_poll(&file, &all_events) == 0);
	/* Unbind frees hardware resources before the remaining file is closed. */
	assert(command(FF_IOC_IRQ_EN, 1) == 0);
	focal_spi_remove(&fake.spi);
	assert(!data->initialized && fake.irq_freed);
	managed_cleanup();
	assert(fake.frees == 0 && data->refcount.count == 1);
	other.private_data = &data->miscdev;
	assert(focal_open(NULL, &other) == -ENODEV);
	assert(focal_read(&file, (char *)packet, sizeof(packet), NULL) == -ENODEV);
	assert(focal_write(&file, (char *)packet, sizeof(packet), NULL) == -ENODEV);
	assert(command(FF_IOC_IRQ_EN, 1) == -ENODEV);
	assert(command(FOCAL_IOC_GET_EVENT, 0) == -ENODEV);
	assert(focal_poll(&file, &all_events) == (EPOLLERR | EPOLLHUP));
	assert(focal_poll(&file, &all_events) == (EPOLLERR | EPOLLHUP));
	assert(focal_release(NULL, &file) == 0);
	assert(fake.frees == 1);
	tests++;
}

static void test_transfers(void)
{
	u8 buffer[1016] = { FOCAL_SPI_READ_WRITE, 6, 0, 0xf8, 3 };
	u8 bad[5] = { FOCAL_SPI_READ_WRITE, 6, 0, 1, 0 };
	u8 long_data[32], short_data[5] = { FOCAL_SPI_BACK_DATA };
	u8 output[32] = { FOCAL_SPI_BACK_DATA };
	unsigned int transfers;

	fixture();
	assert(focal_read(&file, (char *)buffer, 11, NULL) == 11);
	assert(fake.last_tx == 6 && fake.last_rx == sizeof(buffer));
	assert(buffer[1015] == 0xa6);
	assert(focal_read(&file, (char *)bad, sizeof(bad), NULL) == -EMSGSIZE);
	assert(focal_read(&file, (char *)bad, 4, NULL) == -EINVAL);
	bad[0] = FOCAL_SPI_READ_ONLY;
	fake.transfer_error = -ETIMEDOUT;
	assert(focal_read(&file, (char *)bad, sizeof(bad), NULL) == -ETIMEDOUT);
	assert(fake.error_logs == 1 && bad[0] == FOCAL_SPI_READ_ONLY);
	assert(focal_write(&file, (char *)bad, sizeof(bad), NULL) == -ETIMEDOUT);
	assert(fake.error_logs == 2);
	/* Replacing the backup cache must clear the old tail without SPI I/O. */
	transfers = fake.transfers;
	memset(long_data, 0x6b, sizeof(long_data));
	long_data[0] = FOCAL_SPI_BACK_DATA;
	assert(focal_write(&file, (char *)long_data, sizeof(long_data), NULL) == 32);
	assert(focal_write(&file, (char *)short_data, sizeof(short_data), NULL) == 5);
	assert(focal_read(&file, (char *)output, sizeof(output), NULL) == 32);
	assert(!memcmp(output, short_data, sizeof(short_data)));
	for (size_t i = sizeof(short_data); i < sizeof(output); i++)
		assert(output[i] == 0);
	assert(fake.transfers == transfers);
	finish();
}

int main(void)
{
	test_legacy_events();
	test_explicit_events();
	test_suspend_resume();
	test_resume_recovery();
	test_file_lifetime();
	test_transfers();
	printf("PASS: %u focused driver regressions\n", tests);
	return 0;
}
