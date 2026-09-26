# SYPAS Testing

## Principles

- A milestone is "done" only when it runs and its failure paths are
  exercised. Tests emit machine-readable results.
- Failures are never hidden: the report format in every development
  report includes PASS/FAIL per category and "NOT TESTED" where true.

## Test entry points

| Command | What it runs | Needs |
|---|---|---|
| `make test` | everything feasible on this machine (skips QEMU tiers with an explicit SKIP message if QEMU/OVMF are absent) | — |
| `make test-unit` | host unit tests: loader ELF validator vs. malformed images (ASan/UBSan), boot protocol layout + `SYPAS_BI_HAS` rules | gcc |
| `make test-media` | independent ISO/El Torito/FAT parser + BIOS stub execution under Unicorn | python |
| `make test-boot` | UEFI boot matrix, serial-transcript validation, JSON output | QEMU + OVMF |
| `make test-kernel-fault` (alias `test-faults`) | fault-injected build must panic truthfully (see below) | QEMU + OVMF |
| `make doctor` | reports exactly which of the above your environment can run | python |

CI (`.github/workflows/ci.yml`) runs all of these on every push/PR,
plus a build-determinism check, and uploads the JSON results and
serial transcripts as artifacts.

## Boot test matrix (`make test-boot`)

`tests/boot/boot_test.py` boots the release ISO under OVMF in QEMU for
each configuration in **`tests/config/matrix.json`** — the single
source of truth for the machine matrix (the same file drives
`benchmarks/boot/run.py` and `scripts/run-qemu.sh`; docs quote it, not
the other way around) — and inspects the serial transcript.

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

## Negative-path tests (automated: `make test-kernel-fault`)

`make test-kernel-fault` builds a separate ISO with
`-DSYPAS_TEST_FAULT` (a `ud2` injected late in boot) into
`build/fault/`, boots it, and passes only if the transcript shows
`SYPAS KERNEL PANIC` **and** a real register dump (`RIP=`, `CR0=`
lines) **and** `SYSTEM STATUS: HALTED` — a panic without the dump, a
wedge, or a system that reaches RUNNING all fail
(`tests/boot/boot_test.py --expect-panic`).

Loader negative paths covered without QEMU by `make test-unit`: the
ELF validator rejects truncated headers, bad class/endian/machine/
type, out-of-bounds program headers and segments, `p_filesz >
p_memsz`, address-arithmetic overflows, page-overlapping and excessive
segments, and entry points outside loaded segments — plus every
load-critical truncation of the real `kernel.elf`.

Manually verified earlier (2026-09-25): ISO built without a kernel →
`SYPAS loader error: \SYPAS\KERNEL.ELF not found (status
0x800000000000000E)`, halted. PASS.

## Media and legacy-BIOS diagnostic tests (2026-09-25)

- `make test-media`: **PASS** without QEMU. The independent parser in
  `tests/media/validate_media.py` found the ISO9660 primary descriptor at
  sector 16, the El Torito boot record at sector 17, a valid catalog
  checksum, a bootable/no-emulation BIOS default entry and a bootable/
  no-emulation EFI platform `0xEF` section entry. Both catalog payloads
  were byte-identical to `build/biosstub.bin` and `build/esp.img`; the
  embedded FAT16 walk found `BOOTX64.EFI` and `KERNEL.ELF` byte-identical
  to their build outputs.
- `tests/media/test_bios_stub.py`: **PASS** under Unicorn 2.1.4. The
  harness captured every INT 10h AH=0Eh byte, matched the complete
  diagnostic (including VirtualBox/VMware/QEMU guidance), and observed
  the stub reach HLT.
- Determinism: **PASS**. Two clean `make iso` builds produced the same
  SHA-256, `3cb2a6d07fbc543f45b3dbed9f0aeb0b0cdcb352510863c2fdfeda2c95e00d61`.

## UEFI boot re-run status (2026-09-25)

- `make test-boot`: **NOT TESTED in this sandbox turn**. Neither QEMU
  (`qemu-system-x86_64`) nor the configured OVMF files
  (`$HOME/firmware/OVMF_CODE.fd` and `OVMF_VARS.fd`) are available here.
  The existing Phase 2 QEMU/OVMF PASS record above remains historical;
  this media change was validated structurally and with the no-QEMU
  Unicorn test, not represented as a new UEFI boot result.

## Long-run tests

The idle heartbeat (10 s interval, uptime + free pages) exists to make
1 h/8 h/24 h leak runs observable. A soak-test harness is planned once
there is dynamic allocation beyond the PMM self-test to actually leak.

## Not tested / not claimed

- Physical hardware: NOT TESTED (docs/hardware-target.md).
- Multi-CPU *scheduling*: N/A (no scheduler); multi-CPU configs verify
  BSP-only operation.
- KVM acceleration: dev sandbox has no /dev/kvm; all timings are TCG.
