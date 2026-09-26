#!/usr/bin/env python3
"""
SYPAS boot-time benchmark.

Boots the ISO N times per configuration and reports wall-clock time from
QEMU process start to the `SYSTEM STATUS: RUNNING` serial marker, plus
the kernel-reported init time. Results are printed as JSON with the
exact configuration, so numbers are never quoted without context.

Note: sandbox timings are TCG (no KVM); firmware (OVMF) time dominates.
"""

import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

MATRIX_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "..", "tests", "config", "matrix.json")

RUN_MARK = "SYSTEM STATUS: RUNNING"


def one_boot(args, smp, mem):
    serial = tempfile.NamedTemporaryFile(prefix="sypas-bench-", suffix=".log",
                                         delete=False)
    vars_tmp = tempfile.NamedTemporaryFile(prefix="sypas-vars-", suffix=".fd",
                                           delete=False)
    with open(args.ovmf_vars, "rb") as f:
        vars_tmp.write(f.read())
    vars_tmp.close()

    cmd = [args.qemu, "-machine", "q35", "-cpu", "max",
           "-smp", str(smp), "-m", str(mem),
           "-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf_code}",
           "-drive", f"if=pflash,format=raw,file={vars_tmp.name}",
           "-cdrom", args.iso, "-serial", f"file:{serial.name}",
           "-display", "none", "-no-reboot"]
    t0 = time.monotonic()
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    wall = None
    kinit = None
    try:
        while time.monotonic() - t0 < args.timeout:
            with open(serial.name, errors="replace") as f:
                text = f.read()
            if RUN_MARK in text:
                wall = time.monotonic() - t0
                for line in text.splitlines():
                    if "kernel initialized in" in line:
                        kinit = line.split("initialized in", 1)[1].strip()
                break
            time.sleep(0.1)
    finally:
        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()
        os.unlink(vars_tmp.name)
        os.unlink(serial.name)
    return wall, kinit


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--qemu",
                    default=os.path.expanduser("~/sysroot/bin/qemu-system-x86_64"))
    ap.add_argument("--ovmf-code",
                    default=os.path.expanduser("~/firmware/OVMF_CODE.fd"))
    ap.add_argument("--ovmf-vars",
                    default=os.path.expanduser("~/firmware/OVMF_VARS.fd"))
    args = ap.parse_args()

    with open(MATRIX_PATH) as f:
        matrix = json.load(f)

    out = {"method": "wall clock QEMU start -> serial RUNNING marker",
           "accel": "TCG (no KVM in sandbox)",
           "machine": f"{matrix['machine']}, -cpu {matrix['cpu']}",
           "matrix": os.path.relpath(MATRIX_PATH),
           "configs": []}
    for cfg in (c for c in matrix["configs"] if c.get("benchmark")):
        smp, mem = cfg["smp"], cfg["mem_mib"]
        walls, kinit_ms, kinit_raw = [], [], []
        for _ in range(args.runs):
            w, k = one_boot(args, smp, mem)
            if w is None:
                print(f"boot failed/timeout at smp={smp}", file=sys.stderr)
                sys.exit(1)
            walls.append(w)
            kinit_raw.append(k)
            m = re.match(r"(\d+)\s*ms", k or "")
            if m:
                kinit_ms.append(int(m.group(1)))
        entry = {
            "config": cfg["name"], "smp": smp, "mem_mib": mem,
            "runs": args.runs,
            "wall_to_running_s": {
                "median": round(statistics.median(walls), 2),
                "min": round(min(walls), 2),
                "max": round(max(walls), 2)},
        }
        if kinit_ms:
            entry["kernel_init_ms"] = {
                "median": statistics.median(kinit_ms),
                "min": min(kinit_ms),
                "max": max(kinit_ms)}
        else:
            # keep the raw line rather than silently dropping the data
            entry["kernel_reported_init_raw"] = kinit_raw[-1]
        out["configs"].append(entry)
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
