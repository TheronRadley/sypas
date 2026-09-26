#!/usr/bin/env python3
"""
SYPAS development environment checker (`make doctor`).

Reports PASS/FAIL/absent for every tool the build and test tiers need,
and states exactly which make targets each missing piece blocks —
instead of letting `make test-boot` die with a FileNotFoundError.

Exit code: 0 if the core build works here, 1 if it cannot.
"""

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys


def run_version(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        first = (out.stdout or out.stderr).splitlines()
        return first[0].strip() if first else "(no output)"
    except (OSError, subprocess.TimeoutExpired):
        return None


def module_version(name):
    try:
        mod = __import__(name)
        return getattr(mod, "__version__", "unknown version")
    except ImportError:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu",
                    default=os.path.expanduser("~/sysroot/bin/qemu-system-x86_64"))
    ap.add_argument("--ovmf-code",
                    default=os.path.expanduser("~/firmware/OVMF_CODE.fd"))
    ap.add_argument("--ovmf-vars",
                    default=os.path.expanduser("~/firmware/OVMF_VARS.fd"))
    args = ap.parse_args()

    core_ok = True
    rows = []

    def check(name, ok, detail, needed_for, core=False):
        nonlocal core_ok
        rows.append((name, ok, detail, needed_for))
        if core and not ok:
            core_ok = False

    # --- Core toolchain (build) -----------------------------------------
    for tool, core in (("gcc", True), ("ld", True), ("objcopy", True),
                       ("make", True), ("nm", True)):
        v = run_version([tool, "--version"])
        check(tool, v is not None, v or "not found",
              "build (all targets)", core=core)

    v = run_version([sys.executable, "--version"])
    check("python3", v is not None, v or "not found",
          "build images, all test tooling", core=True)

    # --- Python modules ---------------------------------------------------
    v = module_version("pycdlib")
    check("pycdlib", v is not None, f"pycdlib {v}" if v else
          "not found — pip install -r tools/requirements-dev.txt",
          "make iso / test-media", core=True)
    v = module_version("unicorn")
    check("unicorn", v is not None, f"unicorn {v}" if v else
          "not found — pip install -r tools/requirements-dev.txt",
          "make test-media (BIOS stub execution test)")

    # --- Emulation tier ----------------------------------------------------
    qemu_path = args.qemu if os.path.exists(args.qemu) \
        else shutil.which("qemu-system-x86_64")
    v = run_version([qemu_path, "--version"]) if qemu_path else None
    check("qemu-system-x86_64", v is not None,
          f"{v} ({qemu_path})" if v else
          f"not found (looked at {args.qemu} and $PATH) — "
          "set QEMU=/path/to/qemu-system-x86_64",
          "make test-boot / test-kernel-fault / benchmark / run")

    for name, path in (("OVMF_CODE", args.ovmf_code),
                       ("OVMF_VARS", args.ovmf_vars)):
        ok = os.path.isfile(path)
        check(name, ok, path if ok else
              f"not found at {path} — set {name}=/path/to/{name}.fd",
              "make test-boot / test-kernel-fault / benchmark / run")

    kvm = os.path.exists("/dev/kvm")
    check("/dev/kvm", kvm,
          "available" if kvm else "absent — QEMU runs TCG (slower, still valid)",
          "faster emulation only (optional)")

    arch = platform.machine()
    check("architecture", arch in ("x86_64", "AMD64"), arch,
          "host-side unit tests exercise x86_64 boot code", core=False)

    # --- Report -------------------------------------------------------------
    width = max(len(r[0]) for r in rows)
    print("SYPAS environment doctor")
    print("========================")
    for name, ok, detail, needed in rows:
        status = "PASS" if ok else "MISS"
        print(f"{name:<{width}}  {status}  {detail}")
        if not ok:
            print(f"{'':<{width}}        needed for: {needed}")
    print()
    if core_ok:
        print("core build: OK — `make iso`, `make test-unit`, "
              "`make test-media` will work here")
    else:
        print("core build: BROKEN — fix the entries marked MISS above")
    missing_emu = any(not ok for name, ok, *_ in rows
                      if name in ("qemu-system-x86_64", "OVMF_CODE", "OVMF_VARS"))
    if missing_emu:
        print("emulation tier: unavailable — `make test` will skip "
              "test-boot/test-kernel-fault and say so")
    return 0 if core_ok else 1


if __name__ == "__main__":
    sys.exit(main())
