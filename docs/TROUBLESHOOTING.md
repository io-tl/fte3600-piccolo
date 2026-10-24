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

First confirm that module version 0.1 or newer is loaded. Older community variants
incorrectly rejected responses larger than the `read(2)` request count, which
can produce empty frames. If a current version still fails, report calibration output
and short redacted kernel-log lines, never fingerprint data.

## Reader stops responding after sleep or repeated use

Version 0.2 fixes races in event delivery, preserves notifications rejected
by a poll mask, and restores the transport and requested IRQ state after
system sleep. The default vendor event ABI is retained. These changes do not
establish the cause of every userspace hang or a reported five-minute stall.

While the reader is stuck, collect the following **before** restarting the
daemon, so the failing session is still represented in the logs:

```console
$ uname -r
$ cat /sys/module/focal_spi/version
$ sudo journalctl -b -u fprintd --since '-10 min' --no-pager
$ sudo journalctl -k -b --since '-10 min' --no-pager
```

On Fedora, include `rpm -q fprintd libfprint`; on Arch, include
`pacman -Q fprintd libfprint-ftexx00-bin`. State whether the failure follows
sleep, inactivity, or several verifications without sleep. Include whether
`fprintd-verify` also fails outside the lock screen. Redact unrelated journal
lines and personal information before sharing. Do not share fingerprint
templates, raw SPI payloads, or image dumps.

Kernel messages `SPI read failed`, `SPI write failed`, and
`failed to restore transport after sleep` identify transport failures without
logging biometric data. Their absence does not rule out a sensor/backend
protocol problem. After collecting the logs,
`sudo systemctl restart fprintd.service` can recover a stuck backend; this
interrupts any current fingerprint operation.

### Optional backend recovery after sleep

Some firmware loses sensor configuration in sleep, and a backend may not
reinitialize it. The module cannot reconstruct that opaque configuration.
If restarting fprintd consistently fixes this resume-specific problem, an
opt-in systemd unit can repeat that recovery after each sleep cycle:

```console
$ sudo install -Dm644 packaging/systemd/focaltech-fprintd-resume.service \
    /etc/systemd/system/focaltech-fprintd-resume.service
$ sudo systemctl daemon-reload
$ sudo systemctl enable focaltech-fprintd-resume.service
```

Enable it without `--now`: `sleep.target` activates it during the next sleep
cycle. It restarts only an already-active fprintd, after resume, and only when
`focal_spi` is loaded. It does not run a periodic restart or start an idle
daemon. To disable the workaround:

```console
$ sudo systemctl disable focaltech-fprintd-resume.service
```

For validation, run several successful `fprintd-verify` sessions over more
than five minutes without sleeping, then repeat after several sleep/wake
cycles. Keep the failure logs if either sequence still fails.

## Secure Boot rejects the module

DKMS can sign modules, but the generated key still has to be trusted by the
machine firmware. Follow the Secure Boot/MOK procedure supplied by your
distribution. Do not disable Secure Boot solely to install this module.

## The kernel module loads but fprintd finds no reader

The kernel transport is only half of the stack. A compatible FocalTech
libfprint backend is required. This project cannot redistribute an opaque
vendor package without clear source and redistribution terms.
