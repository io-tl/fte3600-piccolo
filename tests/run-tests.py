#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile unchanged driver functions against host-side kernel/hardware mocks."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "focal_spi.c").read_text()
FUNCTIONS = (
    "focal_file_data", "focal_free", "focal_put", "focal_debug",
    "focal_wake_userspace", "focal_next_event", "focal_update_irq",
    "focal_io_available", "focal_restore_transport", "focal_assert_reset",
    "focal_deassert_reset",
    "focal_hw_reset", "focal_spi_transfer", "focal_open", "focal_release",
    "focal_spi_irq_handler", "focal_read", "focal_write", "focal_ioctl",
    "focal_compat_ioctl", "focal_poll", "focal_spi_remove",
    "focal_spi_suspend", "focal_spi_resume",
)


def extract(pattern):
    matches = re.findall(pattern, SOURCE, re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError(f"expected exactly one driver declaration: {pattern}")
    return matches[0]


declarations = ['#include "focal_spi_uapi.h"']
declarations.extend(re.findall(r"^#define FOCAL_.*$", SOURCE, re.MULTILINE))
for name in ("focal_spi_packet", "focal_fp_data"):
    declarations.append(extract(rf"^struct {name} \{{[\s\S]*?^\}}[^\n]*;"))
for name in FUNCTIONS:
    declarations.append(extract(rf"^static [^\n]*\b{name}\([\s\S]*?^\}}"))

with tempfile.TemporaryDirectory(prefix="focal-driver-tests-") as work:
    work = Path(work)
    (work / "driver_under_test.inc").write_text("\n\n".join(declarations) + "\n")
    binary = work / "test_driver"
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-pthread", "-I", str(ROOT), "-I", str(work),
    ]
    command += shlex.split(os.environ.get("TEST_CFLAGS", ""))
    command += [str(ROOT / "tests/test_driver.c"), "-o", str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True, timeout=60)
