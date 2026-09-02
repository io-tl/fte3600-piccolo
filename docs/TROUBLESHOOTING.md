# Troubleshooting

## No `/dev/focal_moh_spi`

Check the ACPI ID, module state, and kernel log:

```console
$ grep -H . /sys/bus/acpi/devices/FTE*/hid 2>/dev/null
$ lsmod | grep focal_spi
$ sudo dmesg | grep -iE 'focal|FTE(3600|4800|6600|6900)'
```

A reset-GPIO error usually means the firmware uses a different unnamed
`GpioIo` index. Try `reset_gpio_index=1` only after checking the machine's ACPI
resources. If reset polarity is reversed, try `reset_active_low=N`.

## Sensor initialization or calibration fails

Keep the firmware SPI mode first. Try these options one at a time, unloading
the module between tests:

```console
$ sudo modprobe focal_spi spi_clock_hz=0
$ sudo modprobe focal_spi cs_active_high=0
$ sudo modprobe focal_spi cs_active_high=1
```

The default 4 MHz clock matches the original userspace transport. A firmware
clock (`spi_clock_hz=0`) can help controllers that reject that override.

## fprintd cannot open the device

Only one process can own the transport. Stop any manual test or enrollment
process, then restart fprintd. Sandboxed service units may also need a local
systemd override allowing `/dev/focal_moh_spi`; do not replace the distribution
unit file.

## Enrollment repeatedly says `enroll-swipe-too-short`

First confirm that module version 0.1 is loaded. Older community variants
incorrectly rejected responses larger than the `read(2)` request count, which
can produce empty frames. If version 0.1 still fails, report calibration output
and short redacted kernel-log lines, never fingerprint data.

## Secure Boot rejects the module

DKMS can sign modules, but the generated key still has to be trusted by the
machine firmware. Follow the Secure Boot/MOK procedure supplied by your
distribution. Do not disable Secure Boot solely to install this module.

## The kernel module loads but fprintd finds no reader

The kernel transport is only half of the stack. A compatible FocalTech
libfprint backend is required. This project cannot redistribute an opaque
vendor package without clear source and redistribution terms.
