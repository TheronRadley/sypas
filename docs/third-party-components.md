# Third-Party Components

Rule: reuse infrastructure where sensible; own the operating-system
architecture. Every external component used by SYPAS is recorded here.
"Runtime-critical" means it ships inside, or executes as part of, a
running SYPAS system.

## Shipped inside SYPAS images

| Name | Version | Purpose | License | Why used | Runtime-critical | Temporary? | Replacement strategy |
|---|---|---|---|---|---|---|---|
| font8x8 (Daniel Hepper / Marcel Sondaar, IBM PD VGA fonts) | master (vendored `kernel/graphics/font8x8_basic.h`) | bitmap glyphs for the boot/panic framebuffer console | Public Domain | a hand-drawn font adds zero architectural value at this phase | yes (console rendering) | permanent for the boot console; superseded for the desktop | SYPAS UI typography (vector fonts) replaces it for the shell; boot console may keep it indefinitely |

## Build/test infrastructure (never ships in the OS)

| Name | Version | Purpose | License | Why used | Runtime-critical | Temporary? | Replacement strategy |
|---|---|---|---|---|---|---|---|
| gcc + binutils | reference: 12.2 / 2.40; CI: Ubuntu 24.04 APT | compiler, linker, ELF→PE conversion | GPLv3 (with runtime exception) | mature toolchain; allowed foundation | no | permanent | n/a (toolchains are explicitly allowed infrastructure) |
| GNU make | reference: 4.3; CI: Ubuntu 24.04 APT | build orchestration | GPLv3 | ubiquitous, sufficient | no | permanent | n/a |
| Python | reference: 3.11; CI: Ubuntu 24.04 | build tools + test harness | PSF | fast iteration for tooling | no | permanent for tooling | n/a |
| pycdlib | 1.21.0 (pinned) | dual-entry El Torito (BIOS + EFI) ISO9660 assembly | LGPLv2.1 | ISO mastering is standards plumbing, not OS architecture | no | could be replaced | write a SYPAS `mkiso` in tools/ if pycdlib ever limits us |
| QEMU | reference: 9.2.4; CI: Ubuntu 24.04 APT | primary development/test platform | GPLv2 | explicit project decision: QEMU is the virtualization target | no | permanent (dev) | n/a |
| OVMF | reference: edk2-stable202411; CI: Ubuntu 24.04 package | UEFI firmware for QEMU boot tests | BSD-2-Clause-Patent (+ OpenSSL for crypto parts) | testing SYPAS's real UEFI path requires real UEFI firmware | no | permanent (dev) | n/a |
| Unicorn | 2.1.4 (pinned) | execute the 16-bit BIOS diagnostic stub and capture INT 10h output | GPLv2 | lightweight CPU emulation makes the BIOS failure path testable without QEMU | no | test-only | replace only if a smaller 16-bit execution harness is needed |

The reference sandbox's QEMU build dependencies are not SYPAS dependencies
and are intentionally not treated as a reproducible environment contract.
CI uses Ubuntu packages instead; exact tool versions are reported per run.

## Specifications used (not code)

- UEFI Specification 2.10 — `bootloader/uefi/efi.h` is written from it.
- ELF64 (System V gABI), PE/COFF — image formats.
- FAT specification — `tools/mkfat.py` (build tool) and the future ESP
  driver.
- Intel SDM / AMD APM — GDT/IDT/PIC/PIT/CPUID programming.

## Explicit non-dependencies

SYPAS does **not** use and will not silently adopt: the Linux kernel,
any BSD kernel, gnu-efi/EDK2 application libraries (SYPAS's loader is
self-contained), Limine or any third-party bootloader, existing desktop
environments, Electron/browser runtimes.
