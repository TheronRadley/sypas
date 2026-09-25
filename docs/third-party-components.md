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
| gcc + binutils | 12.2 / 2.40 | compiler, linker, ELF→PE conversion | GPLv3 (with runtime exception) | mature toolchain; allowed foundation | no | permanent | n/a (toolchains are explicitly allowed infrastructure) |
| GNU make | 4.3 | build orchestration | GPLv3 | ubiquitous, sufficient | no | permanent | n/a |
| Python | 3.11 | build tools + test harness | PSF | fast iteration for tooling | no | permanent for tooling | n/a |
| pycdlib | 1.14 | dual-entry El Torito (BIOS + EFI) ISO9660 assembly | LGPLv2.1 | ISO mastering is standards plumbing, not OS architecture | no | could be replaced | write a SYPAS `mkiso` in tools/ if pycdlib ever limits us |
| QEMU | 9.2.4 (built from source) | primary development/test platform | GPLv2 | explicit project decision: QEMU is the virtualization target | no | permanent (dev) | n/a |
| OVMF (edk2 firmware blobs from the QEMU source tree) | edk2-stable202411 | UEFI firmware for QEMU boot tests | BSD-2-Clause-Patent (+ OpenSSL for crypto parts) | testing SYPAS's real UEFI path requires real UEFI firmware | no | permanent (dev) | n/a |
| Unicorn | 2.x (test environment) | execute the 16-bit BIOS diagnostic stub and capture INT 10h output | GPLv2 | lightweight CPU emulation makes the BIOS failure path testable without QEMU | no | test-only | replace only if a smaller 16-bit execution harness is needed |

### Dependencies built only to run QEMU in the dev sandbox

glib 2.78.4 (LGPLv2.1+), pcre2 10.44 (BSD-3), libffi (frida meson port,
MIT), zlib 1.3.1 (zlib), pixman 0.40.0 (MIT), pkgconf 2.3.0 (ISC),
meson/ninja/cmake (Apache-2.0). These exist solely so the QEMU binary
links in the sandbox; none touch SYPAS code or images.

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
