# Contributing

Reports for FTE4800, FTE6600, FTE6900, and additional FTE3600 machines are
especially useful. Please include:

- laptop or tablet model and the ACPI hardware ID;
- kernel and distribution versions;
- `modinfo focal_spi` parameter values;
- the `focaltech-ftexx00` lines from the kernel log;
- whether detection, calibration, enrollment, and verification work.

Do not attach fingerprint images, `/var/lib/fprint`, proprietary libraries,
full ACPI dumps, serial numbers, or unrelated logs. A short redacted excerpt
is enough.

Patches should compile with `make W=1`, preserve the five-byte userspace ABI,
keep all transfer lengths bounded, and avoid global per-device state. Update
the changelog and documentation for user-visible behavior.

Use tabs for kernel C indentation and follow the Linux kernel coding style.
Commits should be small enough to review and explain why a change is needed.
