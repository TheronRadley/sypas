# SYPAS Engineering Decision Records

Format per record:
Decision / Why / Expected RAM cost / Expected CPU cost / Expected
complexity / Advantages / Disadvantages / Alternatives / Why rejected /
How to replace later.

---

## DR-1: SYPAS-native UEFI loader built as ELF→PE via objcopy, no gnu-efi

- **Decision:** implement the bootloader as freestanding C with our own
  minimal UEFI headers, compiled PIC, linked as a base-0 shared object,
  converted with `objcopy --target efi-app-x86_64`, self-relocating at
  entry.
- **Why:** SYPAS must own its boot path; gnu-efi/EDK2 app frameworks
  drag in foreign build systems and hide the boot contract.
- **RAM cost:** ~10 KiB loader image. **CPU cost:** negligible.
- **Complexity:** moderate one-time cost (PE conversion, ABI bridging).
- **Advantages:** full control, tiny, no submodules, honest
  understanding of every byte between firmware and kernel.
- **Disadvantages:** we maintain UEFI structure definitions ourselves;
  spec errors are on us (mitigated: verified against OVMF at runtime).
- **Alternatives:** gnu-efi (rejected: framework lock-in), Limine
  (rejected: explicitly out per project rules), mingw toolchain
  (rejected: unavailable in dev environment; adds a second toolchain).
- **Replace later:** if a native PE-emitting toolchain (clang/lld)
  becomes standard in our dev image, drop the objcopy step; the C code
  is unaffected.

## DR-2: Boot protocol is a versioned single struct, physical pointers

- **Decision:** one packed, versioned, grow-only structure
  (`sypas_bootinfo_t`) with physical addresses valid under the firmware
  identity map.
- **Why:** explicit, testable contract; no undocumented assumptions.
- **Complexity:** low. **Costs:** one 4 KiB page + map buffers.
- **Alternatives:** Multiboot2 (foreign semantics, legacy baggage),
  Limine protocol (third-party ownership). Rejected: SYPAS owns its ABI.
- **Replace later:** version bumps; `size` field allows additive growth
  without breaking older kernels.

## DR-3: v1 kernel runs on firmware identity mapping, linked at 4 MiB

- **Decision:** defer kernel-owned page tables to Phase 4; keep EFI
  boot-services memory reserved meanwhile; kernel is non-PIE at a fixed
  physical address the loader allocates explicitly.
- **Why:** smallest honest step to a running, testable kernel; page tables
  deserve their own tested phase rather than a rushed version.
- **RAM cost:** real — firmware memory stays reserved (measured ~66 MiB
  of 2 GiB held by `SYPAS_MEM_FIRMWARE` + loader under OVMF; see
  docs/performance.md). Reclaimed in Phase 4.
- **Disadvantages:** no W^X yet, no higher-half, fixed-base fragility
  (loader fails loudly if 4 MiB is taken — has not occurred on OVMF).
- **Alternatives:** loader-built page tables now. Rejected: increases
  the untested surface of the very first boot milestone.
- **Replace later:** Phase 4 builds kernel page tables, moves the kernel
  higher-half, reclaims firmware + loader memory.

## DR-4: Bring-up interrupts on 8259 PIC + PIT (100 Hz)

- **Decision:** legacy PIC/PIT first; APIC/HPET/TSC-deadline in the SMP
  phase.
- **Why:** universally present, tiny programming model, lets us verify
  real interrupt delivery on day one.
- **CPU cost:** 100 wakeups/s when idle (~0% measurable in QEMU).
- **Disadvantages:** single-CPU only; PIT is coarse (10 ms).
- **Alternatives:** APIC immediately. Rejected: needs ACPI MADT parsing;
  belongs with SMP where it is required rather than decorative.
- **Replace later:** APIC/IOAPIC module supersedes pic.c; PIT remains a
  calibration/fallback source. Timer design goal stays tickless — no
  high-frequency polling anywhere in the system contract.

## DR-5: Physical memory = bitmap allocator with next-fit cursor

- **Decision:** 1 bit per 4 KiB page, next-fit scan, byte-skip fast path.
- **Why:** correct, small (64 KiB of bitmap per 2 GiB RAM), trivially
  testable; performance measured (~112 cycles/alloc, ~46 cycles/free —
  see docs/performance.md) is far from being a bottleneck at this phase.
- **Disadvantages:** O(n) worst case when nearly full; no locality/zone
  awareness; no contiguous multi-page allocation API yet.
- **Alternatives:** buddy allocator (more code, unneeded until we have
  DMA/hugepage consumers), freelist stack (poor contiguity for future
  needs).
- **Replace later:** buddy or zoned allocator when virtual memory and
  drivers create measured demand; the `pmm_*` API is the stable seam.

## DR-6: No Rust in the tree yet

- **Decision:** postpone Rust until components with high parser/protocol
  content arrive (SypasFS structures, package metadata, network
  protocols).
- **Why:** current code is register pokes and descriptor tables — the
  unsafe surface Rust cannot remove; a second toolchain now buys little.
- **Replace later:** Rust lands with its first justified component;
  language policy already reserves its place.

## DR-7: Deterministic SYPAS-owned image tooling (mkfat.py / mkiso.py)

- **Decision:** SYPAS builds its own FAT16 ESP writer; ISO mastering uses
  pycdlib.
- **Why:** dev sandbox lacks mtools/xorriso; more importantly,
  deterministic media (fixed timestamps/ids) makes boot images
  byte-reproducible, which the release process will depend on.
- **Disadvantages:** our FAT writer supports 8.3 names only (boot files
  are named to fit, deliberately).
- **Verification:** independent Python FAT parser + OVMF itself reads the
  volume (boot tests).
- **Replace later:** mkiso in native tooling if pycdlib limits us.

## DR-8: QEMU 9.2.4 + OVMF (edk2-stable202411) as the test platform

- **Decision:** all "it works" claims at this phase mean q35 + OVMF,
  TCG, 1/2/4-CPU 1/2/4-GiB matrix. Physical hardware: **NOT TESTED**,
  stated everywhere.
- **Why:** honest, reproducible, automatable in the sandbox.
- **Replace later:** hardware matrix per docs/hardware-target.md when
  physical machines are available.

## DR-9: BIOS El Torito entry is a diagnostic stub, not a BIOS port

- **Decision:** make the default El Torito catalog entry a 2048-byte,
  16-bit real-mode image that prints the UEFI-only requirement and halts;
  put the real FAT16 ESP in a bootable EFI platform (0xEF) section entry.
  The stub is linked at 0x7C00 and uses only BIOS INT 10h AH=0Eh.
- **Why:** a legacy-BIOS VM otherwise reports the unhelpful firmware-level
  "No bootable medium found!". A deterministic message gives VirtualBox,
  VMware, and QEMU users the exact firmware setting to change while
  preserving SYPAS's UEFI-only architecture.
- **Expected RAM cost:** one 2 KiB ISO payload, loaded only on the failure
  path. **Expected CPU cost:** one BIOS teletype call per message byte,
  then zero (HLT loop).
- **Expected complexity:** low and isolated; `make test-media` validates
  both catalog entries and executes the image under Unicorn without QEMU.
- **Advantages:** useful failure diagnostics, no BIOS compatibility claims,
  no changes to the UEFI boot protocol or kernel, deterministic media.
- **Disadvantages:** text is BIOS/code-page dependent and cannot recover or
  boot SYPAS; very old firmware may still ignore nonstandard El Torito
  behavior.
- **Alternatives:** continue emitting an EFI-only catalog (rejected:
  leaves users with a misleading firmware error), or port the loader to
  BIOS/CSM (rejected: violates the UEFI-only target and creates a second
  boot ABI).
- **Replace later:** if SYPAS ever chooses a different media masterer, keep
  the stub ABI and catalog ordering; replace only the ISO plumbing after
  equivalent independent media tests exist.
