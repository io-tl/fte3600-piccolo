# Changelog

## 0.1 - 2026-09-02

- Support ACPI IDs FTE3600, FTE4800, FTE6600, and FTE6900.
- Preserve firmware SPI mode and chip-select polarity.
- Prefer a named reset GPIO with a configurable unnamed-ACPI fallback.
- Correct reset handling through logical GPIO polarity.
- Preserve the vendor read ABI where response length may exceed `read(2)`
  count while bounding every transfer to 32 KiB.
- Use unaligned little-endian header access and per-device state.
- Serialize transfers and allow only one userspace owner.
- Remove fingerprint-content statistics and proprietary packaging artifacts.
- Add DKMS, Arch packaging, documentation, and CI checks.
- Document the external userspace backend and add local Ubuntu/Arch packaging
  helpers without redistributing its binary.
