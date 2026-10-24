# Userspace ABI

The module implements the existing FocalTech `focal_moh_spi` character-device
ABI so compatible libfprint backends can communicate with the sensor.

## Packet header

Read and write buffers begin with a packed five-byte header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | packet type (`0x5a`, `0xa5`, or `0xb9`) |
| 1 | 2 | little-endian transmit length |
| 3 | 2 | little-endian receive length |
| 5 | variable | transmit payload |

All requests and responses are bounded to 32 KiB.

The read convention is unusual but intentional. The `count` passed to
`read(2)` covers only the header and transmit payload. The header's receive
length controls how many bytes are copied back to the caller and may be larger
than `count`. Known backends, for example, issue `read(fd, buffer, 11)` with a
six-byte command and request a response of up to 1016 bytes. Userspace must
allocate enough writable memory for the declared response.

## Ioctls

The raw command numbers `0x8086` through `0x808c` control reset, power/reset
state, IRQ delivery, the userspace debug flag, poll wakeups, and chip select.
They are retained verbatim for binary compatibility rather than re-encoded
with Linux `_IO*()` macros.

The values are declared in `focal_spi_uapi.h`. The existing scalar arguments
remain scalars, including for 32-bit applications on a 64-bit kernel.

## Event delivery

The default mode preserves the vendor's consume-on-poll interface. Event
values are `NONE=0`, `ENABLE=1`, `INT=2`, `RESUME=3`, `SUSPEND=4`, and
`DISABLE=5`. These are numeric event codes, not independent flags. Legacy
clients must request `POLLIN | POLLPRI | POLLOUT` so the kernel can deliver
every code intact. An event is retained if the requested mask would discard
or truncate it. A successful matching poll consumes it, as before.

Pending events are protected against concurrent IRQ, ioctl, and PM writers.
Stop (`DISABLE`) has priority and cancels pending touch/enable events. PM
notifications take priority over touches; suspend and resume coalesce into
the latest PM state, because userspace is frozen during the transition.
Repeated IRQs coalesce into one pending touch. The explicit vendor
`FF_IOC_RELEASE_POLL(NONE)` clears all pending events; `ENABLE` rearms after
a pending stop. These are notifications, not a count of captured frames.

### Explicit acknowledgment mode (0.2+)

Legacy consume-on-poll semantics cannot support epoll's repeated readiness
checks reliably. New clients should opt in to level-triggered delivery:

```c
#include "focal_spi_uapi.h"

__u32 mode = FOCAL_EVENT_MODE_EXPLICIT;
/* Check the return value of each ioctl in the application. */
ioctl(fd, FOCAL_IOC_SET_EVENT_MODE, &mode);
/* poll/epoll for POLLIN; readiness persists until GET_EVENT consumes it. */
__u32 event;
ioctl(fd, FOCAL_IOC_GET_EVENT, &event);
```

`GET_EVENT` returns one numeric event through its `__u32` pointer, or `NONE`
if no event is pending. Its syscall return value is zero on success. Drain
events until `NONE`, then wait again. A failed userspace copy returns `EFAULT`
and restores the event. `GET_EVENT` in legacy mode returns `EINVAL`.
`SET_EVENT_MODE` also accepts `FOCAL_EVENT_MODE_LEGACY`. Mode changes do not
discard events and should be made before starting polling threads. Closing
and reopening the device restores the legacy default.

In either mode, removal reports persistent `POLLERR | POLLHUP`; subsequent
I/O returns `ENODEV`. The file's state survives until its final close, even
if the driver was unbound in the meantime.

## System sleep

Suspend serializes with SPI and GPIO operations, masks/drains the IRQ, and
blocks new hardware I/O with `EHOSTDOWN`. Resume restores the reset GPIO's
logical state and SPI configuration, publishes `RESUME`, then unmasks the
IRQ only if userspace requested it. It does not pulse reset or replay opaque
sensor commands: sensor firmware/configuration remains the backend's job.

A transport restoration failure leaves IRQs masked, reports `POLLERR`, and
returns the restoration error from read/write. A new open or an explicit
reset/power ioctl retries restoration; it does not require a reboot. The
backup-data cache is not interpreted or replayed as SPI commands.
