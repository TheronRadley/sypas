# SYPAS Boot Protocol v1

Normative header: `bootloader/protocols/sypas_bootproto.h` (shared
verbatim by loader and kernel). This document explains the contract.

## Overview

The SYPAS bootloader is a UEFI application. It loads the SYPAS kernel
(a static ELF64 executable) from the boot volume, collects platform
information, terminates firmware boot services, and transfers control.

```
UEFI firmware
  → \EFI\BOOT\BOOTX64.EFI      (SYPAS loader)
      reads \SYPAS\KERNEL.ELF  (SYPAS kernel)
      → kernel entry, RDI = &sypas_bootinfo
```

## Versioning rules

- `magic` = `"SYPASBP1"` (0x3150425341505953 little-endian).
- Versioning is **major/minor** (`version_major` / `version_minor`),
  and the compatibility rule is explicit — it is exactly what the
  kernel's validation code enforces, no more and no less:
  - **major mismatch → refuse.** `version_major` changes on any
    incompatible layout or semantic change.
  - **known major, newer minor → accept.** Within one major the
    structure only ever grows: existing fields never move, change
    width, or change meaning. A kernel built against minor N accepts
    any minor ≥ N, uses the prefix it knows, and ignores trailing
    bytes.
  - **unknown trailing fields → ignore;** fields newer than the
    consumer's minor are probed with `SYPAS_BI_HAS(bi, field)` (which
    checks `size`) before use, never assumed.
  - The kernel additionally requires `size` ≥ the v1.0 prefix it
    needs (`SYPAS_BOOTINFO_V1_0_SIZE`).
- The layout is mechanically enforced: `sypas_bootproto.h` contains
  `_Static_assert`s on every field offset and the total size, compiled
  by the loader, the kernel, and the host unit tests
  (`tests/unit/test_bootproto.c`). Moving a field fails every build.
- **No undocumented loader/kernel assumptions.** If the kernel needs a
  fact about the machine, it must come from this structure or be
  discovered by the kernel itself.

## Machine state at kernel entry

| Item | State |
|---|---|
| CPU mode | 64-bit long mode |
| Interrupts | disabled (`cli`) |
| Direction flag | clear |
| Paging | firmware identity map (UEFI maps all memory 1:1) |
| GDT/IDT | firmware's — kernel must install its own immediately |
| RSP | top of a dedicated 64 KiB loader-allocated stack |
| RDI | physical address of `sypas_bootinfo_t` |
| Boot services | terminated (`ExitBootServices` completed) |
| Runtime services | present but **not** callable in v1 (not remapped) |
| Watchdog | disabled by the loader |

## Structure contents

See the header for exact layout. Summary:

- **memmap / memmap_count** — array of `{base, length, type}` entries,
  page-aligned, adjacent same-type ranges coalesced. Types:
  `USABLE`, `RESERVED`, `ACPI_RECLAIM`, `ACPI_NVS`, `MMIO`, `LOADER`
  (bootinfo, memory map, kernel stack, loader image — reclaimable
  later), `KERNEL` (the loaded kernel image: the loader splits its
  span out of the loader-allocated ranges and tags it, so the map is a
  real ownership map), `FIRMWARE` (EFI boot/runtime services memory;
  v1 keeps it reserved because the active page tables live there).
- **fb_*** — GOP linear framebuffer: base, width, height, pitch, and
  byte-order format (`XRGB8888` = B,G,R,X in memory; `XBGR8888` =
  R,G,B,X). `SYPAS_FB_NONE` if the GOP mode is blt-only or absent —
  the kernel then runs serial-only.
- **acpi_rsdp** — physical RSDP address from the EFI configuration
  table (ACPI 2.0 GUID preferred, 1.0 fallback), 0 if absent.
- **efi_system_table** — physical address, kept for later runtime
  services support; unused in v1.
- **kernel_phys_base / kernel_size** — span of loaded PT_LOAD segments.
- **stack_base / stack_size** — the stack RSP points into. This is
  **the** boot stack: the kernel keeps running on it (its entry stub
  only re-aligns RSP) until it builds its own page tables and switches
  to a kernel-owned stack. The kernel does not allocate a second boot
  stack.
- **cmdline** — NUL-terminated, empty in v1 (loader has no config
  parsing yet; field exists so the ABI does not change when it does).

## Kernel loading rules (v1)

- Kernel format: ELF64, `ET_EXEC`, `EM_X86_64`, little-endian.
- The image is **fully validated before any memory is touched**:
  `bootloader/uefi/elf.c` (`elf_plan_load()`) proves every header
  offset, segment file range, size relation (`p_filesz ≤ p_memsz`),
  physical address computation (overflow-checked), page-granular
  segment disjointness, total span limit, and that `e_entry` lands
  inside a loaded segment — then returns a load plan. The loader only
  executes the plan: allocate, zero, copy. A malformed image is
  reported (`kernel image rejected: <reason>`) and the loader halts.
- The same validator code is compiled on the build host and
  unit-tested against malformed/truncated/adversarial images
  (`tests/unit/test_elf.c`, run by `make test-unit` under
  ASan/UBSan), including the real `kernel.elf` build artifact.
- Every planned segment is placed at its `p_paddr` via
  `AllocatePages(AllocateAddress)`; `p_filesz` bytes copied,
  remainder zeroed.
- The SYPAS kernel links at physical 4 MiB. If that range is not free
  the loader reports and halts — it does not silently relocate.
  (Removed in Phase 4 when the loader builds kernel page tables.)

## ExitBootServices sequence

1. Buffer sizing is a loop, not a guess: `GetMemoryMap` size query →
   allocate with slack → `GetMemoryMap`; if the firmware still reports
   `EFI_BUFFER_TOO_SMALL` (the map may legitimately grow, and
   `DescriptorSize` may exceed our struct — UEFI 2.10 §7.2), free,
   grow, retry (bounded).
2. All allocation happens **strictly before** the ExitBootServices
   sequence: after a failed `ExitBootServices`, UEFI permits only
   `GetMemoryMap` and `ExitBootServices` again.
3. `GetMemoryMap` → `ExitBootServices` with the **latest** map key;
   on failure retry the pair (up to 4 attempts).
4. After success: no firmware calls of any kind; the EFI map is
   translated into SYPAS format in the pre-allocated buffer, with the
   kernel image span split out of loader ranges and tagged
   `SYPAS_MEM_KERNEL`.

## Error handling

Any loader failure prints a specific message plus the EFI status code to
the firmware console and halts. The loader never jumps to a kernel it
could not fully load.
