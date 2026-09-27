# SYPAS

SYPAS is a from-scratch operating system. It has its own UEFI bootloader, boot protocol, and kernel, with its own userspace, graphics stack, and desktop shell planned for later phases.

The goal is a modern, polished desktop that runs well on older, low-end x86_64 machines.

**Current state: BOOT-2 complete — a hardened UEFI handoff to SYPAS's own kernel. MEM-1 (memory isolation) is next.**

```text
UEFI firmware

   ↓

SYPAS bootloader        (bootloader/uefi, PE32+ UEFI application)

   ↓  SYPAS Boot Protocol v1

SYPAS kernel            (kernel/, x86_64)

   ├── serial console   (16550, self-tested)
   ├── GDT / IDT        (32 exceptions, IRQs, verified dispatch)
   ├── 8259 PIC + PIT   (100 Hz, tick-verified)
   ├── physical memory  (bitmap allocator + self-test + measured cost)
   └── framebuffer console (GOP, 8x8 font scaled 2x)
```

## What works today

All of the claims below are tested. See `docs/performance.md` for test conditions and results.

* **SYPAS UEFI loader:** caps and validates `\SYPAS\KERNEL.ELF`, loads exact `PT_LOAD` ranges, validates the firmware memory map and ACPI RSDP, exits boot services through a recovery-safe handoff, and transfers control through SYPAS Boot Protocol v1.

* **SYPAS kernel:** GDT, a fully populated 256-vector IDT with controlled unexpected-vector diagnostics, PIC remapping, a 100 Hz PIT verified by counting real interrupts, a private software-interrupt self-test, a bitmap physical page allocator with explicit accounting and alloc/free/uniqueness self-tests, framebuffer text console, and serial console.

* **Reproducible boot media:** deterministic FAT16 ESP + dual-entry El Torito ISO built with SYPAS's own tooling (`tools/mkfat.py`, `tools/mkiso.py`). Legacy BIOS selects a diagnostic stub. UEFI selects the real ESP.

* **Automated boot test matrix:** boots every configuration in `tests/config/matrix.json` (the single source of truth for the machine matrix) and produces machine-readable results with `make test-boot`; `make test-kernel-fault` proves the panic path with a fault-injected build.

* **Host-side unit tests:** the loader's ELF validator, bounded relocator, UEFI→SYPAS memory-map translator, ACPI RSDP validator, and boot-protocol layout are unit-tested on the build host (`make test-unit`, ASan/UBSan where applicable).

* **CI configured:** `.github/workflows/ci.yml` builds with `-Werror`, runs unit/media/boot/fault tiers, and checks deterministic output from a separate checkout. Until a workflow result is visible for a revision, this is a configured test plan—not a claim that the revision has passed CI.

## What does NOT exist yet

Virtual memory under kernel control, scheduler, processes, syscalls, userspace, storage drivers, SypasFS, networking, audio, GPU drivers, window system, compositor, UI toolkit, shell, installer.

The roadmap in `docs/architecture.md` defines the order these pieces are planned to be implemented. Nothing here fakes those layers.

## Building

Requirements: gcc + binutils (x86_64 host), GNU make, Python 3.11 (the documented environment; see `docs/toolchain.md`), and the pinned Python packages: `pip install -r tools/requirements-dev.txt`.

`make run` and the QEMU test tiers require QEMU (`x86_64-softmmu`) and OVMF firmware. Paths can be overridden with `QEMU`, `OVMF_CODE`, and `OVMF_VARS`. Run `make doctor` to see exactly what your environment supports.

See `docs/toolchain.md` for more details.

```text
make            # build everything -> release/sypas-<version>.iso
make doctor     # check the dev environment, explain what's missing
make test       # run everything feasible on this machine
make run        # boot in QEMU (normal profile from tests/config/matrix.json)
make run-lowend # boot with the low-end profile
make test-unit  # host unit tests: ELF, relocator, memory map, ACPI, protocol
make test-media # ISO/FAT/catalog checks + Unicorn BIOS-stub test
make test-boot  # automated UEFI boot matrix, JSON output
make test-kernel-fault # fault-injected build must panic truthfully
make test-repro # compare ISO from this checkout with a second checkout
```

Built ISOs land in `release/` and are gitignored. A fresh clone has no binary artifact: run `make iso`, then `make run`, or use a tagged GitHub Release **only after one is published**. `VERSION` is the sole source for the development image name; see `docs/release-process.md`.

Try it on a UEFI x86_64 VM after building:

```text
make iso
QEMU=qemu-system-x86_64 OVMF_CODE=OVMF_CODE.fd OVMF_VARS=OVMF_VARS.fd make run-lowend
```

## VirtualBox (legacy BIOS warning)

SYPAS is UEFI-only. A default VirtualBox VM often starts in legacy BIOS mode. With the dual-entry ISO, it will print a diagnostic instead of ending at `No bootable medium found!`.

Before starting the VM:

1. Open **Settings > System > Motherboard**.
2. Check **Enable EFI (special OSes only)**.
3. Set **Base Memory** to **2048 MB** or more, then restart the VM.
4. Run `make iso`, then attach the generated `release/sypas-<version>.iso` as the optical disk.

For VMware, select **Firmware type: UEFI**.

For QEMU, boot with OVMF as shown in the command above.

The BIOS image is only a failure-path diagnostic. It is not a BIOS port of SYPAS.

## Repository map

```text
bootloader/uefi/       SYPAS UEFI loader (C + minimal asm, no gnu-efi)
bootloader/bios/       legacy-BIOS diagnostic stub (not a SYPAS BIOS port)
bootloader/protocols/  SYPAS Boot Protocol v1 (shared header)
kernel/                SYPAS kernel (C17 + x86_64 asm)
tools/                 image tooling (FAT16 + ISO builders)
scripts/               run/dev scripts
tests/boot/            automated UEFI boot tests
tests/media/           independent ISO/FAT checks + BIOS-stub test
benchmarks/            measurement harnesses and recorded results
docs/                  architecture, decisions, measurements
release/               bootable images
```

## Documentation

Start with `docs/architecture.md`, then `docs/boot-protocol.md` and `docs/kernel.md`.

Every major decision has a record in `docs/technology-decisions.md`.

Every external component is listed in `docs/third-party-components.md`.

Every performance number in `docs/performance.md` includes its test conditions.
