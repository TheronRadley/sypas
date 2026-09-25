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
- `version` bumps on any semantic change; the kernel refuses versions it
  does not understand.
- `size` = number of valid bytes; the structure only ever grows, so a
  newer loader works with an older kernel and vice versa within a major
  version.
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
  later), `KERNEL`, `FIRMWARE` (EFI boot/runtime services memory; v1
  keeps it reserved because the active page tables live there).
- **fb_*** — GOP linear framebuffer: base, width, height, pitch, and
  byte-order format (`XRGB8888` = B,G,R,X in memory; `XBGR8888` =
  R,G,B,X). `SYPAS_FB_NONE` if the GOP mode is blt-only or absent —
  the kernel then runs serial-only.
- **acpi_rsdp** — physical RSDP address from the EFI configuration
  table (ACPI 2.0 GUID preferred, 1.0 fallback), 0 if absent.
- **efi_system_table** — physical address, kept for later runtime
  services support; unused in v1.
- **kernel_phys_base / kernel_size** — span of loaded PT_LOAD segments.
- **stack_base / stack_size** — the stack RSP points into.
- **cmdline** — NUL-terminated, empty in v1 (loader has no config
  parsing yet; field exists so the ABI does not change when it does).

## Kernel loading rules (v1)

- Kernel format: ELF64, `ET_EXEC`, `EM_X86_64`.
- Every `PT_LOAD` segment is placed at its `p_paddr` via
  `AllocatePages(AllocateAddress)`; `p_filesz` bytes copied,
  remainder zeroed.
- The SYPAS kernel links at physical 4 MiB. If that range is not free
  the loader reports and halts — it does not silently relocate.
  (Removed in Phase 4 when the loader builds kernel page tables.)

## ExitBootServices sequence

1. All allocations done **before** the final `GetMemoryMap` (any
   later allocation would invalidate the map key).
2. `GetMemoryMap` → `ExitBootServices`; on `EFI_INVALID_PARAMETER`
   retry the pair (up to 4 attempts).
3. After success: no firmware calls of any kind; the EFI map is
   translated into SYPAS format in a pre-allocated buffer.

## Error handling

Any loader failure prints a specific message plus the EFI status code to
the firmware console and halts. The loader never jumps to a kernel it
could not fully load.
