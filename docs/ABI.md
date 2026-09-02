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

Poll wake values are also used directly as poll masks by the established
backend. This interface should be treated as compatibility ABI, not as a
design for new userspace software.
