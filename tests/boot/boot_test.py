#!/usr/bin/env python3
"""
SYPAS automated boot test.

Boots the SYPAS ISO under OVMF in QEMU across the machine matrix
(tests/config/matrix.json — the single source of truth) and validates
the serial transcript.  Machine-readable JSON on stdout; exit code 0
only if every configuration passes.

Normal mode, per configuration:
  PASS requires  "SYSTEM STATUS: RUNNING"
  FAIL on        "SYPAS KERNEL PANIC", "SYPAS loader error", or timeout
  Also records:  wall-clock time to the RUNNING marker (includes OVMF
                 firmware time), kernel-side init time, pmm self-test.

--expect-panic mode (fault-injection ISOs, e.g. make test-kernel-fault):
  PASS requires  "SYPAS KERNEL PANIC" AND a truthful register dump
                 (RIP=/CR0= lines) AND "SYSTEM STATUS: HALTED"
  FAIL on        "SYSTEM STATUS: RUNNING" (fault did not fire), timeout,
                 or a panic without the dump (silent wedge).

Optionally captures a framebuffer screenshot via QMP screendump.
"""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

MATRIX_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "config", "matrix.json")

RUN_MARK = "SYSTEM STATUS: RUNNING"
PANIC_MARK = "SYPAS KERNEL PANIC"
HALT_MARK = "SYSTEM STATUS: HALTED"
FAIL_MARKS = [PANIC_MARK, "SYPAS loader error", HALT_MARK]
DUMP_MARKS = ["RIP=", "CR0="]   # proof the register dump was emitted


def load_matrix():
    with open(MATRIX_PATH) as f:
        matrix = json.load(f)
    return matrix


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


def run_one(args, matrix, cfg, timeout=90):
    tmpdir = tempfile.mkdtemp(prefix=f"sypas-boot-{cfg['name']}-")
    serial_path = os.path.join(tmpdir, "serial.log")
    vars_path = os.path.join(tmpdir, "OVMF_VARS.fd")
    qmp_path = os.path.join(tmpdir, "qmp.sock")
    shutil.copyfile(args.ovmf_vars, vars_path)

    cmd = [
        args.qemu,
        "-machine", matrix["machine"], "-cpu", matrix["cpu"],
        "-smp", str(cfg["smp"]), "-m", str(cfg["mem_mib"]),
        "-drive", f"if=pflash,format=raw,readonly=on,file={args.ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={vars_path}",
        "-cdrom", args.iso,
        "-serial", f"file:{serial_path}",
        "-display", "none",
        "-qmp", f"unix:{qmp_path},server=on,wait=off",
        "-no-reboot",
    ]

    start = time.monotonic()
    with open(os.path.join(tmpdir, "qemu-stderr.log"), "wb") as qemu_err:
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                stderr=qemu_err)
    result = {"config": cfg["name"], "smp": cfg["smp"],
              "mem_mib": cfg["mem_mib"],
              "mode": "expect-panic" if args.expect_panic else "normal",
              "result": "TIMEOUT", "boot_wall_s": None,
              "serial_log": serial_path}
    try:
        while time.monotonic() - start < timeout:
            if proc.poll() is not None:
                result["result"] = "QEMU_EXITED"
                break
            text = ""
            if os.path.exists(serial_path):
                with open(serial_path, "r", errors="replace") as f:
                    text = f.read()

            if args.expect_panic:
                if RUN_MARK in text:
                    result["result"] = "FAIL"
                    result["detail"] = "fault did not fire, system RUNNING"
                    break
                if PANIC_MARK in text and HALT_MARK in text:
                    dump_ok = all(m in text for m in DUMP_MARKS)
                    result["result"] = "PASS" if dump_ok else "FAIL"
                    if not dump_ok:
                        result["detail"] = "panic without register dump"
                    result["boot_wall_s"] = round(time.monotonic() - start, 2)
                    break
            else:
                if RUN_MARK in text:
                    result["result"] = "PASS"
                    result["boot_wall_s"] = round(time.monotonic() - start, 2)
                    break
                if any(m in text for m in FAIL_MARKS):
                    result["result"] = "FAIL"
                    break
            time.sleep(0.25)

        text = ""
        if os.path.exists(serial_path):
            with open(serial_path, "r", errors="replace") as f:
                text = f.read()
        for line in text.splitlines():
            if "kernel initialized in" in line:
                result["kernel_init"] = line.strip()
            if "selftest:" in line:
                result["pmm_selftest"] = line.strip()
            if "IRQs in 250 ms window" in line:
                result["timer_check"] = line.strip()
            if args.expect_panic and line.startswith("reason :"):
                result["panic_reason"] = line.strip()

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
        # Keep the serial log for CI artifact upload; drop the rest.
        for name in ("OVMF_VARS.fd", "qmp.sock"):
            p = os.path.join(tmpdir, name)
            if os.path.exists(p):
                os.unlink(p)
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
    ap.add_argument("--expect-panic", action="store_true",
                    help="invert pass criteria for fault-injection images")
    ap.add_argument("--config", action="append", default=None,
                    help="run only this matrix config (repeatable)")
    args = ap.parse_args()

    for what, path in (("QEMU", args.qemu), ("OVMF_CODE", args.ovmf_code),
                       ("OVMF_VARS", args.ovmf_vars)):
        if not os.path.exists(path):
            print(f"boot_test: {what} not found at {path} — "
                  f"run 'make doctor' for environment details", file=sys.stderr)
            sys.exit(2)

    matrix = load_matrix()
    configs = [c for c in matrix["configs"] if c.get("boot_test")]
    if args.config:
        configs = [c for c in matrix["configs"] if c["name"] in args.config]
        missing = set(args.config) - {c["name"] for c in configs}
        if missing:
            print(f"unknown config(s): {sorted(missing)}", file=sys.stderr)
            sys.exit(2)

    results = [run_one(args, matrix, cfg, args.timeout) for cfg in configs]
    ok = all(r["result"] == "PASS" for r in results)
    print(json.dumps({"overall": "PASS" if ok else "FAIL",
                      "matrix": os.path.relpath(MATRIX_PATH),
                      "results": results}, indent=2))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
