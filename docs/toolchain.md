# SYPAS Toolchain

## Build toolchain (host)

| Tool | Version used | Role |
|---|---|---|
| gcc | 12.2 (Debian 12.2.0-14) | C compiler, assembler driver (kernel + loader) |
| GNU binutils (ld, objcopy, nm, objdump) | 2.40 | linking; ELF→PE32+ conversion (`--target efi-app-x86_64`) |
| GNU make | 4.3 | build orchestration |
| Python | 3.11 | build tools (`tools/mkfat.py`, `tools/mkiso.py`), tests |
| pycdlib | 1.14 | El Torito ISO assembly (build-time only) |

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
4. `start.S` self-applies any `R_X86_64_RELATIVE` relocations at run
   time (with `-Bsymbolic` PIC the set is typically empty; verified in
   the build by `nm -u` = no undefined symbols).

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

QEMU profiles (see `scripts/run-qemu.sh` and docs/hardware-target.md):
lowend 2 CPU/2 GiB, normal 4 CPU/4 GiB, upper 8 CPU/8 GiB — all
`-machine q35 -cpu max`, TCG (this sandbox has no KVM).

## Reproducibility

- `tools/mkfat.py` emits deterministic FAT16 images (fixed timestamps,
  fixed volume id); `tools/mkiso.py` freezes the mastering clock.
- Verified: two `make clean && make iso` runs produce byte-identical
  ISOs (sha256 `39347b2c…d373cc` for v0.1.0).

## Not yet in place (honest gaps)

- clang-format/config and static analysis (cppcheck/clang-tidy) —
  planned before the codebase grows past bring-up size.
- CI pipeline — `make test-boot` is CI-ready (JSON output, exit codes)
  but no hosted runner is wired up.
- Debugger workflow docs (`qemu -s -S` + gdb works today; a
  `scripts/debug.sh` will land with Phase 3/4 work).
