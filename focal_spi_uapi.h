/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
#ifndef FOCAL_SPI_UAPI_H
#define FOCAL_SPI_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Existing vendor ABI: these numbers and values must not change. */
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

/* Opt-in interface: poll/epoll report readiness, GET_EVENT consumes it. */
#define FOCAL_EVENT_MODE_LEGACY	0
#define FOCAL_EVENT_MODE_EXPLICIT	1
#define FOCAL_IOC_SET_EVENT_MODE	_IOW('F', 0x20, __u32)
#define FOCAL_IOC_GET_EVENT	_IOR('F', 0x21, __u32)

#endif
