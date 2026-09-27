# MEM-1 memory-layout contract

This document is the design gate for kernel virtual memory. It records the
intended mapping policy before `kernel/mm/pagetable.c` or `kernel/mm/vmm.c`
exists, so PMM allocation policy is not accidentally mixed into virtual-memory
policy.

## BOOT-2 state

At entry, SYPAS uses the UEFI firmware identity map. The loader hands a
sorted, page-aligned, non-overlapping ownership map. Exact PT_LOAD page ranges
are `SYPAS_MEM_KERNEL`; holes between loadable segments remain `SYPAS_MEM_LOADER`.
No firmware memory is reclaimed while CR3 is firmware-owned.

## MEM-1 target

| Region | Mapping policy | Permission policy |
|---|---|---|
| Kernel image | higher-half canonical virtual region | derived from ELF `PF_R/PF_W/PF_X`; never W+X |
| Kernel page tables / stacks | kernel virtual region | RW, NX |
| Physical direct map | documented bounded physical window | RW, NX by default; MMIO explicit |
| Framebuffer | explicit kernel mapping | RW, NX, write-combining policy later |
| ACPI tables | temporary/direct-map access | RO/NX after parsing where practical |
| User addresses | absent until ABI-1 | separate address spaces later |
| Firmware boot-services memory | retained until CR3 switch proves safe | reclaim only after its mappings are unused |

The exact virtual bases and direct-map size are intentionally not chosen yet;
they must be fixed together with the page-table format and higher-half linker
layout. Any proposal must document canonical-address constraints, 4 KiB/huge
page use, guard pages, recursive/self-map choice (if any), TLB invalidation,
and failure behaviour.

## Required safety work in MEM-1

- TSS with IST stacks for double fault, NMI, and machine check;
- safe panic handling that does not read an already-corrupt fault stack;
- a clear transition from loader stack to kernel-owned stack;
- permission translation from retained `elf_segment_t.flags` to page tables;
- explicit tests for NX, W^X, mapping bounds and firmware-memory reclamation.
