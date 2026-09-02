# Userspace smoke test

`list_devices.c` uses only public libfprint APIs to enumerate, open, and close
available readers. It is optional and is not linked into the kernel module.

```console
$ cc -Wall -Wextra -O2 tools/list_devices.c \
    $(pkg-config --cflags --libs libfprint-2) -o list_devices
$ ./list_devices
```

Successful enumeration does not replace enrollment and verification tests.

## Ubuntu backend package helper

`repack-backend-deb.sh` accepts only the known, checksum-validated FocalTech
backend archive. It removes the duplicate `fprintd.service`, installs a
device-access drop-in, regenerates the Debian payload checksums, and creates a
new local package. It does not download or redistribute the backend.

See the repository [README](../README.md#ubuntu-22042404) for the exact command
and the licensing and update caveats.
