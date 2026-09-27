# SYPAS Architecture

## Product vision

SYPAS targets older and lower-end x86_64 computers (2–4 cores, 2–8 GiB
RAM, integrated graphics, SATA SSD/HDD) while remaining visually modern.
Priority order when requirements conflict:

1. stability
2. responsiveness
3. performance
4. security
5. low memory usage
6. gaming performance
7. visual quality
8. additional features

Architectural principles:

- event-driven components rather than polling loops or idle compositor
  frames;
- a **damage-driven compositor with software and accelerated backends** —
  acceleration is optional and measured, not assumed;
- lazy loading over permanent preloading;
- dynamic quality reduction over feature removal; and
- modular services over permanently running daemons.

## Non-negotiable engineering rule

**Real performance over marketing.** No component is described as fast,
light, or optimized without a measurement recorded in `docs/performance.md`.
No hardware is listed as supported without a test on that hardware or an
explicit “QEMU/OVMF only” annotation.

## Long-term structure

```text
Firmware (UEFI)
   ↓
SYPAS Bootloader          — bootloader/
   ↓  SYPAS Boot Protocol (versioned)
SYPAS Kernel              — kernel/
   ├── CPU / SMP
   ├── PMM / VMM
   ├── Scheduler
   ├── IPC / syscall ABI
   ├── Device model / DMA / drivers
   ├── Storage → SypasFS → VFS
   ├── Networking / input / audio / power / security
   └── Graphics
   ↓
SYPAS Core Services → Graphics / Window System → Compositor → UI → Shell
```

Directories are created **when their first real code lands** — no empty
scaffolding or placeholder subsystems.

## Current milestone: BOOT-2 — hardened boot handoff

Implemented in the source tree:

| Layer | Status |
|---|---|
| UEFI bootloader | SYPAS-native PE32+ app, no gnu-efi or Limine |
| Loader validation | bounded kernel file, host-tested ELF policy, bounded self-relocation |
| Boot protocol | versioned SYPAS Boot Protocol v1, mechanically layout-asserted |
| Ownership map | validated, sorted, exact PT_LOAD ownership ranges |
| ACPI handoff | validated RSDP pointer, ACPI 2.0 preferred |
| Kernel entry, GDT, IDT | controlled exception diagnostics; all 256 gates installed |
| Serial debug console | 16550, loopback self-tested |
| PIC/PIT interrupts | delivery verified by counting ticks |
| Physical page allocator | bitmap, self-tested, explicitly accounted statistics |
| Framebuffer console | GOP linear framebuffer, 8x8 font scaled 2x |
| Kernel-owned virtual memory | **not implemented — MEM-1 is next** |

### Known BOOT-2 limits

1. The kernel runs on the firmware identity mapping. EFI boot-services memory
   remains reserved until SYPAS owns CR3.
2. The kernel links at a fixed physical 4 MiB and the loader fails closed if
   that range is occupied.
3. The PIC/PIT path is BSP-only bring-up infrastructure.
4. TSS/IST emergency stacks do not exist yet; double-fault/stack-fault
   diagnostics are therefore not robust. They are a required part of MEM-1.
5. SYPAS has no user/kernel isolation, driver model, or secure-boot policy
   yet; see `docs/threat-model.md`.

## Named milestones

Milestones are capability contracts, not an ever-growing phase-number list.
A milestone is complete only when it builds, runs, handles failure, is tested,
documented, and measured where performance is claimed.

| ID | Milestone | Scope |
|---|---|---|
| BOOT-1 | Boot bring-up | bootable UEFI kernel handoff |
| BOOT-2 | Boot hardening | validated loader, ownership and test contracts |
| MEM-1 | Memory isolation | page tables, higher half, NX/W^X, TSS/IST, firmware reclaim |
| MEM-2 | Dynamic kernel memory | VMM-backed heap; PMM remains a separate layer |
| CPU-1 | APIC/SMP | ACPI MADT, LAPIC/IOAPIC, time source, per-CPU state |
| EXEC-1 | Kernel execution | kernel threads, context switch, run queue, preemption |
| ABI-1 | User/kernel contract | address spaces, processes, documented syscall ABI and IPC |
| IO-1 | Device/DMA core | PCI, resources, interrupts, DMA ownership and block layer |
| FS-1 | Storage and VFS | storage drivers, VFS and SypasFS |
| GFX-1 | Compositor | input, software compositor, windows and damage tracking |
| DESK-1 | Desktop shell | UI toolkit, shell and applications |

## Next: MEM-1

MEM-1 deliberately keeps layers separate:

```text
PMM (physical frames)
        ↓
VMM (virtual mappings and permissions)
        ↓
kernel heap
```

The implementation order is page-table ownership, higher-half mapping,
NX/W^X, TSS/IST emergency stacks, framebuffer/direct-map policy, then safe
firmware-memory reclamation. `docs/memory-layout.md` is the design contract
that must be updated before the first MEM-1 mapping code lands.
