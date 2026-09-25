# SYPAS Testing

## Principles

- A milestone is "done" only when it runs and its failure paths are
  exercised. Tests emit machine-readable results.
- Failures are never hidden: the report format in every development
  report includes PASS/FAIL per category and "NOT TESTED" where true.

## Boot test matrix (`make test-boot`)

`tests/boot/boot_test.py` boots the release ISO under OVMF in QEMU for
each configuration in the matrix (currently 1 CPU/1 GiB, 2/2, 4/4) and
inspects the serial transcript.

- **PASS** requires the literal `SYSTEM STATUS: RUNNING` marker, which
  the kernel prints only after: boot protocol validation, GDT/IDT
  install, PMM self-test pass, software-interrupt dispatch test pass,
  and a counted 250 ms PIT window (≥ 25 real IRQs).
- **FAIL** on `SYPAS KERNEL PANIC`, `SYPAS loader error`,
  `SYSTEM STATUS: HALTED`, unexpected QEMU exit, or timeout.
- Recorded per config: wall-clock time to RUNNING, kernel-side init
  time, PMM self-test line, timer verification line, serial log path,
  optional QMP framebuffer screendump.
- Output: JSON on stdout; exit code 0 only if every config passes.

## In-kernel self-tests (run on every boot, panic on failure)

| Test | What it proves |
|---|---|
| UART loopback | serial path real before we trust its output |
| boot protocol magic/version/size | loader↔kernel contract intact |
| PMM self-test (512 pages) | uniqueness, writability, no leak, alloc/free cost |
| `int $0x80` ×2 counted | IDT dispatch actually works |
| PIT 250 ms tick count | external interrupt delivery actually works |

## Failure-path checks done manually this phase

- Deliberately corrupted kernel path (file renamed) → loader reports
  `\SYPAS\KERNEL.ELF not found` + status code and halts. (Automated in
  the negative-path suite planned next phase.)
- Panic path exercised via test build with a forced `#UD`; full
  register dump verified on serial and framebuffer.

## Long-run tests

The idle heartbeat (10 s interval, uptime + free pages) exists to make
1 h/8 h/24 h leak runs observable. A soak-test harness is planned once
there is dynamic allocation beyond the PMM self-test to actually leak.

## Not tested / not claimed

- Physical hardware: NOT TESTED (docs/hardware-target.md).
- Multi-CPU *scheduling*: N/A (no scheduler); multi-CPU configs verify
  BSP-only operation.
- KVM acceleration: dev sandbox has no /dev/kvm; all timings are TCG.
