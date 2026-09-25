#!/usr/bin/env python3
"""Execute the 16-bit BIOS diagnostic under Unicorn without QEMU."""

import argparse
from pathlib import Path

try:
    from unicorn import Uc, UcError, UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INTR, UC_MODE_16
    from unicorn.x86_const import (
        UC_X86_REG_AX,
        UC_X86_REG_CS,
        UC_X86_REG_DS,
        UC_X86_REG_ES,
        UC_X86_REG_IP,
        UC_X86_REG_SP,
        UC_X86_REG_SS,
    )
except ImportError as exc:  # pragma: no cover - exercised when dependency is absent
    raise SystemExit("test_bios_stub.py requires Unicorn; install it with: pip install unicorn") from exc

LOAD_ADDRESS = 0x7C00
MEMORY_SIZE = 0x10000

EXPECTED = (
    "SYPAS 0.1.0 - This machine booted in legacy BIOS mode, but SYPAS requires "
    "UEFI firmware (x86_64).\r\n"
    "VirtualBox: Settings > System > Motherboard > check 'Enable EFI' and set "
    "Base Memory to 2048 MB, then restart the VM.\r\n"
    "VMware: Firmware type UEFI. QEMU: boot with OVMF. System halted.\r\n"
)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stub", required=True)
    args = ap.parse_args()
    code = Path(args.stub).read_bytes()
    if len(code) != 2048:
        raise AssertionError(f"BIOS stub must be 2048 bytes, got {len(code)}")

    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, MEMORY_SIZE)
    uc.mem_write(LOAD_ADDRESS, code)
    uc.reg_write(UC_X86_REG_CS, 0)
    uc.reg_write(UC_X86_REG_DS, 0)
    uc.reg_write(UC_X86_REG_ES, 0)
    uc.reg_write(UC_X86_REG_SS, 0)
    uc.reg_write(UC_X86_REG_IP, LOAD_ADDRESS)
    uc.reg_write(UC_X86_REG_SP, 0x7000)

    output = bytearray()
    reached_hlt = False

    def on_interrupt(machine, interrupt, _user_data):
        if interrupt != 0x10:
            raise AssertionError(f"stub invoked unexpected BIOS interrupt 0x{interrupt:02x}")
        ax = machine.reg_read(UC_X86_REG_AX)
        if (ax >> 8) != 0x0E:
            raise AssertionError(f"stub invoked INT 10h with AH=0x{ax >> 8:02x}")
        output.append(ax & 0xFF)

    def on_code(machine, address, size, _user_data):
        nonlocal reached_hlt
        opcode = machine.mem_read(address, 1)[0]
        if opcode == 0xF4:  # HLT
            reached_hlt = True
            machine.emu_stop()

    uc.hook_add(UC_HOOK_INTR, on_interrupt)
    uc.hook_add(UC_HOOK_CODE, on_code)
    try:
        uc.emu_start(LOAD_ADDRESS, 0, count=200000)
    except UcError as exc:
        raise AssertionError(f"Unicorn could not execute BIOS stub: {exc}") from exc

    actual = bytes(output).decode("ascii")
    if actual != EXPECTED:
        raise AssertionError(f"BIOS diagnostic output mismatch:\n{actual!r}")
    if not reached_hlt:
        raise AssertionError("BIOS diagnostic stub did not reach HLT")

    print("test_bios_stub: PASS (INT 10h output captured; HLT reached)")


if __name__ == "__main__":
    main()
