# Debugging SYPAS

## First checks

```text
make doctor
make WERROR=1 test-unit
make test-media
make run
```

`make doctor` uses the same QEMU/OVMF discovery policy as `make test` and
`scripts/run-qemu.sh`. Override any discovered path with `QEMU`, `OVMF_CODE`,
or `OVMF_VARS`.

## Serial is the primary diagnostic channel

All boot progress and kernel diagnostics are emitted on COM1 at 115200 8N1.
`make run` exposes it on the terminal. Automated boot tests save each serial
transcript and CI uploads those logs as artifacts.

## Failure categories

- **Loader error before handoff:** the UEFI console prints a `SYPAS loader
  error` and EFI status. Capture the exact status and message.
- **Kernel panic:** serial output includes `SYPAS KERNEL PANIC`, an interrupt
  frame, control registers, and `SYSTEM STATUS: HALTED`.
- **No serial output:** first confirm OVMF is used (the BIOS ISO entry is only
  a UEFI-required diagnostic), then re-run `make test-media` to independently
  validate the ISO/ESP contents.

## Symbolized debugging

The runtime kernel payload currently remains `build/kernel.elf`; retain it
alongside a serial log when investigating a fault. The build uses deterministic
source prefix maps, so file paths in debug information do not depend on the
checkout directory. A separate stripped release payload is planned with the
MEM-1/VMM work, when a GDB workflow is introduced.
