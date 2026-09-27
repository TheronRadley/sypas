#!/usr/bin/env python3
"""One resolution policy for optional QEMU/OVMF tooling.

Make, the interactive QEMU script and `make doctor` all use this module so a
working binary on PATH cannot be reported as present by one entry point and
missing by another.  Explicit QEMU/OVMF_* environment variables remain the
highest-priority override at each caller.
"""

from __future__ import annotations

import argparse
import os
import shutil
from pathlib import Path


HOME = Path.home()

CANDIDATES = {
    "qemu": [HOME / "sysroot/bin/qemu-system-x86_64"],
    "ovmf-code": [
        HOME / "firmware/OVMF_CODE.fd",
        Path("/usr/share/OVMF/OVMF_CODE_4M.fd"),
        Path("/usr/share/OVMF/OVMF_CODE.fd"),
    ],
    "ovmf-vars": [
        HOME / "firmware/OVMF_VARS.fd",
        Path("/usr/share/OVMF/OVMF_VARS_4M.fd"),
        Path("/usr/share/OVMF/OVMF_VARS.fd"),
    ],
}


def resolve(kind: str) -> str | None:
    """Return an existing default for *kind*, or None if it is unavailable."""
    if kind == "qemu":
        path = shutil.which("qemu-system-x86_64")
        if path:
            return path
    for candidate in CANDIDATES[kind]:
        if candidate.is_file() and (kind != "qemu" or os.access(candidate, os.X_OK)):
            return str(candidate)
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=sorted(CANDIDATES))
    args = parser.parse_args()
    value = resolve(args.kind)
    if value:
        print(value)
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
