# SYPAS Toolchain

## Build toolchain (host)

| Tool | Version used | Role |
|---|---|---|
| gcc | 12.2 (Debian 12.2.0-14) | C compiler, assembler driver (kernel + loader) |
| GNU binutils (ld, objcopy, nm, objdump) | 2.40 | linking; ELF→PE32+ conversion (`--target efi-app-x86_64`) |
| GNU make | 4.3 | build orchestration |
| Python | 3.11 | build tools (`tools/mkfat.py`, `tools/mkiso.py`), tests |
| pycdlib | pinned in `tools/requirements-dev.txt` | El Torito ISO assembly (build-time only) |
| unicorn | pinned in `tools/requirements-dev.txt` | BIOS stub execution test |

Python dependencies are pinned: `pip install -r
tools/requirements-dev.txt`. Run `make doctor` to see what your
environment is missing and which make targets that blocks.

The kernel and loader build with the host gcc in freestanding mode; no
cross-compiler is required for x86_64-on-x86_64. A dedicated
`x86_64-elf` cross toolchain becomes worthwhile when userspace/libc
work starts (it isolates kernel builds from host libc headers
completely); tracked for Phase 14.

### Bootloader build pipeline

UEFI applications are PE32+ with MS-ABI entry points. SYPAS builds one
without mingw or gnu-efi:

1. compile with `-fpic -ffreestanding -fshort-wchar -mno-red-zone`;
   EFI function pointers are declared `__attribute__((ms_abi))`;
2. link as a base-0 ELF shared object (`-shared -Bsymbolic`,
   `bootloader/uefi/loader.ld`);
3. `objcopy --target efi-app-x86_64` converts to PE32+;
4. `start.S` self-applies `R_X86_64_RELATIVE` relocations at run time
   and **fails closed**: any other relocation type returns
   `EFI_LOAD_ERROR` to the firmware instead of running unrelocated
   code (with `-Bsymbolic` PIC the set is RELATIVE-only; `nm -u` in
   the build verifies no undefined symbols);
5. the firmware→C boundary satisfies the Microsoft x64 calling
   convention explicitly — `start.S` reserves the caller-side 32-byte
   home/shadow area and keeps 16-byte alignment when calling the
   `ms_abi` `efi_main`, rather than relying on a tail-jump surviving
   one particular firmware.

### Kernel build flags

`-std=c17 -O2 -g -ffreestanding -fno-pic -fno-pie -fno-stack-protector
-mno-red-zone -mgeneral-regs-only -mcmodel=small -nostdlib`
linked by `kernel/kernel.ld` at physical 4 MiB.

`-mgeneral-regs-only` guarantees no SSE/x87 leaks into interrupt
handlers before FPU state management exists.

## Test platform

| Tool | Version | Notes |
|---|---|---|
| QEMU (x86_64-softmmu) | 9.2.4 | built from source in the dev sandbox |
| OVMF / edk2 firmware | edk2-stable202411 blobs shipped in the QEMU 9.2.4 source tree | UEFI firmware for boot testing |

QEMU profiles come from `tests/config/matrix.json` — the single source
of truth consumed by `scripts/run-qemu.sh`, `tests/boot/boot_test.py`
and `benchmarks/boot/run.py` — all `-machine q35 -cpu max`, TCG (this
sandbox has no KVM).

## Reproducibility

- `tools/mkfat.py` and `tools/mkiso.py` honor the reproducible-builds
  `SOURCE_DATE_EPOCH` convention, with a fixed documented fallback
  epoch so a plain `make iso` is deterministic with no setup
  (see release/README.md):
  `same source + same toolchain + same SOURCE_DATE_EPOCH = same image`.
- CI verifies this on every run: two clean builds must produce the
  same SHA-256 or the build fails.

## CI

`.github/workflows/ci.yml` runs on every push/PR: toolchain version
report → environment doctor → build with `-Werror` → host unit tests →
media validation (independent parser + Unicorn BIOS-stub execution) →
determinism check → UEFI boot matrix → fault injection — and uploads
`toolchain.txt`, the JSON test results, and all serial transcripts as
artifacts. Compiler warnings, media mismatches, boot timeouts,
unexpected panics, and determinism failures all fail the build.

## Not yet in place (honest gaps)

- clang-format/config and static analysis (cppcheck/clang-tidy) —
  planned before the codebase grows past bring-up size.
- Debugger workflow docs (`qemu -s -S` + gdb works today; a
  `scripts/debug.sh` will land with Phase 3/4 work).
