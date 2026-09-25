#!/usr/bin/env python3
"""
SYPAS automated boot test.

Boots the SYPAS ISO under OVMF in QEMU across the hardware-target matrix
and validates the serial transcript.  Machine-readable JSON on stdout;
exit code 0 only if every configuration passes.

Checked per configuration:
  PASS requires  "SYSTEM STATUS: RUNNING"
  FAIL on        "SYPAS KERNEL PANIC", "SYPAS loader error", or timeout
  Also records:  wall-clock time to the RUNNING marker (includes OVMF
                 firmware time), kernel-side init time, pmm self-test.

Optionally captures a framebuffer screenshot via QMP screendump.
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

CONFIGS = [
    {"name": "lowend", "smp": 2, "mem": 2048},
    {"name": "normal", "smp": 4, "mem": 4096},
    {"name": "single", "smp": 1, "mem": 1024},
]

RUN_MARK = "SYSTEM STATUS: RUNNING"
FAIL_MARKS = ["SYPAS KERNEL PANIC", "SYPAS loader error", "SYSTEM STATUS: HALTED"]


def qmp_screendump(sock_path, out_path):
    try:
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(5)
        s.connect(sock_path)
        f = s.makefile("rw")
        f.readline()                                  # greeting
        f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n")
        f.flush()
        f.readline()
        f.write(json.dumps({"execute": "screendump",
                            "arguments": {"filename": out_path}}) + "\n")
        f.flush()
        for _ in range(10):
            resp = json.loads(f.readline())
            if "return" in resp or "error" in resp:
                break
        s.close()
        return os.path.exists(out_path)
    except OSError:
        return False


def run_one(args, cfg, timeout=90):
    serial_log = tempfile.NamedTemporaryFile(
        prefix=f"sypas-serial-{cfg['name']}-", suffix=".log", delete=False)
    vars_tmp = tempfile.NamedTemporaryFile(prefix="sypas-vars-", suffix=".fd",
                                           delete=False)
    with open(args.ovmf_vars, "rb") as f:
        vars_tmp.write(f.read())
    vars_tmp.close()
    qmp_path = tempfile.mktemp(prefix="sypas-qmp-")

    cmd = [
        args.qemu,
        "-machine", "q35", "-cpu", "max",
        "-smp", str(cfg["smp"]), "-m", str(cfg["mem"]),
        "-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={vars_tmp.name}",
        "-cdrom", args.iso,
        "-serial", f"file:{serial_log.name}",
        "-display", "none",
        "-qmp", f"unix:{qmp_path},server=on,wait=off",
        "-no-reboot",
    ]

    start = time.monotonic()
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    result = {"config": cfg["name"], "smp": cfg["smp"], "mem_mib": cfg["mem"],
              "result": "TIMEOUT", "boot_wall_s": None,
              "serial_log": serial_log.name}
    try:
        while time.monotonic() - start < timeout:
            if proc.poll() is not None:
                result["result"] = "QEMU_EXITED"
                break
            with open(serial_log.name, "r", errors="replace") as f:
                text = f.read()
            if RUN_MARK in text:
                result["result"] = "PASS"
                result["boot_wall_s"] = round(time.monotonic() - start, 2)
                break
            if any(m in text for m in FAIL_MARKS):
                result["result"] = "FAIL"
                break
            time.sleep(0.25)

        with open(serial_log.name, "r", errors="replace") as f:
            text = f.read()
        for line in text.splitlines():
            if "kernel initialized in" in line:
                result["kernel_init"] = line.strip()
            if "selftest:" in line:
                result["pmm_selftest"] = line.strip()
            if "IRQs in 250 ms window" in line:
                result["timer_check"] = line.strip()

        if result["result"] == "PASS" and args.screenshot:
            shot = f"{args.screenshot}-{cfg['name']}.ppm"
            if qmp_screendump(qmp_path, shot):
                result["screenshot"] = shot
    finally:
        proc.terminate()
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()
        os.unlink(vars_tmp.name)
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--qemu", default=os.path.expanduser(
        "~/sysroot/bin/qemu-system-x86_64"))
    ap.add_argument("--ovmf-code", default=os.path.expanduser(
        "~/firmware/OVMF_CODE.fd"))
    ap.add_argument("--ovmf-vars", default=os.path.expanduser(
        "~/firmware/OVMF_VARS.fd"))
    ap.add_argument("--screenshot", default=None,
                    help="path prefix for QMP framebuffer dumps")
    ap.add_argument("--timeout", type=int, default=90)
    args = ap.parse_args()

    results = [run_one(args, cfg, args.timeout) for cfg in CONFIGS]
    ok = all(r["result"] == "PASS" for r in results)
    print(json.dumps({"overall": "PASS" if ok else "FAIL",
                      "results": results}, indent=2))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
