#!/bin/sh
# Prepare the known FocalTech backend package without its conflicting unit.

set -eu

EXPECTED_SHA256='b48c93c3732f90aabbcc520e5538faeffbb87bb6847a01d03e14ea157f1d36c1'

usage()
{
	echo "usage: $0 INPUT.deb OUTPUT.deb" >&2
	exit 2
}

[ "$#" -eq 2 ] || usage
command -v dpkg-deb >/dev/null 2>&1 || {
	echo "error: dpkg-deb is required (install dpkg-dev)" >&2
	exit 1
}

[ -f "$1" ] || {
	echo "error: input package does not exist: $1" >&2
	exit 1
}

input=$(realpath "$1")
output=$(realpath -m "$2")

[ ! -e "$output" ] || {
	echo "error: refusing to overwrite: $output" >&2
	exit 1
}

actual_sha256=$(sha256sum "$input" | cut -d ' ' -f 1)
[ "$actual_sha256" = "$EXPECTED_SHA256" ] || {
	echo "error: backend SHA-256 does not match the validated artifact" >&2
	echo "expected: $EXPECTED_SHA256" >&2
	echo "actual:   $actual_sha256" >&2
	exit 1
}

[ "$(dpkg-deb -f "$input" Package)" = 'libfprint-2-2' ] || {
	echo "error: unexpected Debian package name" >&2
	exit 1
}
[ "$(dpkg-deb -f "$input" Architecture)" = 'amd64' ] || {
	echo "error: this helper supports only the validated amd64 backend" >&2
	exit 1
}

workdir=$(mktemp -d "${TMPDIR:-/tmp}/focaltech-backend.XXXXXX")
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM

dpkg-deb --raw-extract "$input" "$workdir/root"

# The vendor archive duplicates a file owned by Ubuntu's fprintd package.
rm -f "$workdir/root/usr/lib/systemd/system/fprintd.service"

install -d -m 0755 \
	"$workdir/root/usr/lib/systemd/system/fprintd.service.d"
printf '%s\n' \
	'[Service]' \
	'DeviceAllow=/dev/focal_moh_spi rw' \
	> "$workdir/root/usr/lib/systemd/system/fprintd.service.d/focaltech-spi.conf"

# Rebuild the payload checksum list after changing package contents.
(
	cd "$workdir/root"
	find . -type f ! -path './DEBIAN/*' -exec md5sum {} \; |
		sed 's#  \./#  #'
) > "$workdir/root/DEBIAN/md5sums"

dpkg-deb --build --root-owner-group "$workdir/root" "$output"
echo "created: $output"
