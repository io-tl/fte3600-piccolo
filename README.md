# FocalTech FTEXX00 SPI transport

An out-of-tree Linux kernel module for FocalTech `FTE3600`, `FTE4800`,
`FTE6600`, and `FTE6900` fingerprint readers connected over SPI. It provides
the `/dev/focal_moh_spi` transport expected by FocalTech-compatible libfprint
backends and is packaged for DKMS.

Version 0.1 has been validated end-to-end on an X-Plus Piccolo with an
`FTE3600`: sensor calibration, enrollment, verification, fprintd, DMS lock,
and SDDM login. The other three ACPI IDs are supported by the original vendor
transport but still need reports from their owners.

## Scope and licensing

This repository contains only the GPL-2.0-only kernel transport. It does not
contain firmware, fingerprint images, ACPI dumps, or a proprietary libfprint
binary. The kernel module alone is not enough for enrollment: users also need
a compatible userspace libfprint backend obtained from a lawful source.

## Requirements

- A recent Linux kernel with SPI, ACPI, GPIOLIB, and module support
- Headers for every kernel that DKMS should build against
- DKMS 2.x or 3.x
- A compatible FocalTech userspace libfprint backend

The current build is tested on Linux 7.1. CI checks older distribution kernels
before each release.

Confirm that the firmware exposes a supported device before installing:

```console
$ grep -H . /sys/bus/acpi/devices/FTE*/hid 2>/dev/null
```

## Install with DKMS

DKMS 3.x can add a source checkout directly:

```console
$ sudo dkms add .
$ sudo dkms build -m focaltech-ftexx00 -v 0.1
$ sudo dkms install -m focaltech-ftexx00 -v 0.1
$ sudo modprobe focal_spi
```

For DKMS 2.x, package managers should install `focal_spi.c`, `Makefile`, and
`dkms.conf` into `/usr/src/focaltech-ftexx00-0.1`, then run the last three DKMS
commands. Do not use `make install` when DKMS owns the module.

On Arch Linux and derivatives:

```console
$ makepkg -si
$ sudo modprobe focal_spi
```

At this point, `stat /dev/focal_moh_spi` should show the kernel transport. A
userspace backend is still required before `fprintd-list` can find the reader.

## Install the userspace backend

The FTE3600 SPI backend is not part of upstream libfprint. The only backend
currently validated with this driver is an opaque x86-64 library distributed
inside this community Ubuntu package:

`libfprint-2-2_1.94.4+tod1-0ubuntu1~22.04.2_spi_20250112_amd64.deb`

Its known SHA-256 is:

```text
b48c93c3732f90aabbcc520e5538faeffbb87bb6847a01d03e14ea157f1d36c1
```

This project does not mirror that binary, cannot audit its source, and does
not claim redistribution rights for it. Prefer a package supplied by the
computer manufacturer. If you choose the
[community copy](https://github.com/oneXfive/ubuntu_spi/blob/main/libfprint-2-2_1.94.4%2Btod1-0ubuntu1~22.04.2_spi_20250112_amd64.deb),
verify the checksum before installation. Never install a similarly named file
with a different checksum without reviewing its provenance.

Installing this backend replaces the distribution's normal libfprint library.
Other fingerprint readers may stop working, and a distribution update may
replace the FocalTech backend. Keep a live password login and do not enable
fingerprint-only authentication.

### Ubuntu 22.04/24.04

Install the module above first, then download and verify the backend:

```console
$ curl -fL -o libfprint-ftexx00.deb \
    https://raw.githubusercontent.com/oneXfive/ubuntu_spi/main/libfprint-2-2_1.94.4%2Btod1-0ubuntu1~22.04.2_spi_20250112_amd64.deb
$ echo 'b48c93c3732f90aabbcc520e5538faeffbb87bb6847a01d03e14ea157f1d36c1  libfprint-ftexx00.deb' | sha256sum -c -
```

The original package tries to own `fprintd.service`, which conflicts with the
distribution package. The supplied helper removes that duplicate unit, adds a
small systemd device-access drop-in, regenerates package checksums, and builds
a local `.deb` without changing the library:

```console
$ sudo apt install dpkg-dev fprintd libpam-fprintd
$ ./tools/repack-backend-deb.sh libfprint-ftexx00.deb \
    libfprint-ftexx00-local.deb
$ sudo apt install --allow-downgrades ./libfprint-ftexx00-local.deb
$ sudo apt-mark hold libfprint-2-2
$ sudo systemctl daemon-reload
$ sudo systemctl restart fprintd.service
```

The hold prevents an ordinary Ubuntu update from silently restoring the
unsupported upstream backend. It also blocks libfprint security updates. To
return to Ubuntu's package:

```console
$ sudo apt-mark unhold libfprint-2-2
$ sudo apt install --reinstall libfprint-2-2
```

Do not use `dpkg --force-overwrite` or `--force-depends`. A dependency error
means the binary is not compatible with that Ubuntu release.

### Arch Linux

Install `fprintd` and build the separate binary-backend package template. It
downloads the same checked artifact, extracts only the shared library, and
adds the fprintd device-access drop-in:

```console
$ sudo pacman -S --needed fprintd
$ cd packaging/arch/libfprint-ftexx00-bin
$ makepkg -si
$ sudo systemctl daemon-reload
$ sudo systemctl restart fprintd.service
```

Pacman will ask to replace the official `libfprint` package because both
provide the same ABI. Review that transaction before accepting it. The backend
package depends on `focaltech-ftexx00-dkms`, so install the module package from
the repository root first.

### Verify the complete stack

Check the loaded module, device node, backend, and fprintd in that order:

```console
$ modinfo focal_spi | grep -E '^(version|alias):'
$ stat /dev/focal_moh_spi
$ ldd /usr/lib/libfprint-2.so.2 2>/dev/null || \
    ldd /usr/lib/x86_64-linux-gnu/libfprint-2.so.2
$ fprintd-list "$USER"
```

Enrollment can then be started with `fprintd-enroll`. If fprintd reports no
device, follow [Troubleshooting](docs/TROUBLESHOOTING.md) and include which
backend package and checksum were used.

## Hardware adaptation

The driver prefers a firmware `reset-gpios` property. When vendor ACPI exposes
only an unnamed `GpioIo`, it safely falls back to GPIO-I/O resource zero and
assumes an active-low reset. The SPI mode and chip-select polarity are kept
from firmware; the vendor transport's 4 MHz clock is used by default.

Module parameters cover known firmware variations:

| Parameter | Default | Meaning |
| --- | ---: | --- |
| `spi_clock_hz` | `4000000` | SPI clock override; `0` keeps firmware speed |
| `cs_active_high` | `-1` | `-1` firmware, `0` active-low, `1` active-high |
| `reset_gpio_index` | `0` | unnamed ACPI `GpioIo` resource used for reset |
| `reset_active_low` | `Y` | polarity for the unnamed GPIO fallback |

For example, `/etc/modprobe.d/focaltech-ftexx00.conf` can contain:

```text
options focal_spi spi_clock_hz=0 reset_gpio_index=1
```

See [Troubleshooting](docs/TROUBLESHOOTING.md) before changing parameters.

## Development

```console
$ make
$ modinfo ./focal_spi.ko
$ make clean
```

The ABI notes are in [docs/ABI.md](docs/ABI.md). Hardware reports and patches
are welcome.

## Credits

The transport originated in FocalTech's Linux module and the
FTEXX00-Ubuntu community work. Version 0.1 hardens and generalizes it from the
X-Plus Piccolo bring-up while preserving the established userspace ABI.

License: [GPL-2.0-only](LICENSE).
