# Security policy

Please report memory-safety, permission, or biometric-data exposure issues
through the repository's private security-advisory form. If private reporting
is not enabled, open a minimal issue asking the maintainer for a private
channel without including vulnerability details.

The module creates `/dev/focal_moh_spi` with mode `0600`. Do not loosen that
mode through udev rules. The driver intentionally does not log raw transfers
or fingerprint-derived statistics.

Never include fingerprint images, enrolled templates, proprietary binaries,
or identifying ACPI data in a security report. A minimal reproducer and a
redacted kernel log are preferred.
