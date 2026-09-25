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

Architectural principles that serve several of these at once:

- event-driven everything: no polling loops, no idle compositor frames
  (already true today: the kernel idles in `hlt`, waking only on
  interrupts);
- GPU-composited effects rather than CPU-side animation (future phases);
- lazy loading over permanent preloading;
- dynamic quality reduction over feature removal;
- modular services over permanently running daemons.

## Non-negotiable engineering rule

**Real performance over marketing.** No component is described as fast,
light, or optimized without a measurement recorded in
`docs/performance.md` (benchmark, test conditions, configuration, method,
result). No hardware is listed as supported without a test on that
hardware or an explicit "QEMU/OVMF only" annotation.

## Long-term structure

```
Firmware (UEFI)
   ↓
SYPAS Bootloader          — bootloader/
   ↓  SYPAS Boot Protocol (versioned)
SYPAS Kernel              — kernel/
   ├── CPU / SMP
   ├── Memory Manager
   ├── Scheduler
   ├── IPC
   ├── Syscalls (SYPAS ABI, not Linux, not POSIX-on-the-wire)
   ├── Device model / drivers
   ├── Storage → SypasFS → VFS
   ├── Networking
   ├── Input
   ├── Graphics
   ├── Audio
   ├── Power
   └── Security
   ↓
SYPAS Core Services       — userspace/services/
   ↓
SYPAS Graphics / Window System — graphics/
   ↓
SYPAS Compositor
   ↓
SYPAS UI Toolkit          — ui/
   ↓
SYPAS Shell               — shell/
   ↓
SYPAS Applications        — apps/
```

Directories are created **when their first real code lands** — no empty
scaffolding, no placeholder modules.

## Current state (Phase 2 milestone: "SYPAS boots its own kernel")

Implemented and tested under QEMU/OVMF (see docs/performance.md and
docs/testing.md for evidence):

| Layer | Status |
|---|---|
| UEFI bootloader | ✅ SYPAS-native PE32+ app, no gnu-efi, no Limine |
| Boot protocol | ✅ SYPAS Boot Protocol v1, versioned, documented |
| Kernel entry, GDT, IDT, exceptions | ✅ with panic register dumps |
| Serial debug console | ✅ 16550, loopback self-tested |
| PIC/PIT interrupts | ✅ delivery verified by counting ticks |
| Physical page allocator | ✅ bitmap, self-tested, cost measured |
| Framebuffer console | ✅ GOP linear FB, 8x8 font 2x scale |
| Virtual memory (kernel-owned page tables) | ❌ next (Phase 4) |
| Everything above memory | ❌ future phases |

### Known v1 limitations (deliberate, documented)

1. The kernel runs on the firmware's identity-mapped page tables.
   EFI boot-services memory is therefore kept reserved
   (`SYPAS_MEM_FIRMWARE`) until the kernel owns its page tables.
2. The kernel links at fixed physical 4 MiB; the loader fails loudly if
   that range is occupied. Position independence or loader-built page
   tables will remove this (Phase 4).
3. Single CPU only; the 8259 PIC is a bring-up device that the APIC
   replaces in the SMP phase.
4. No TSS/IST yet: a kernel-stack overflow during an exception would
   triple-fault rather than produce a clean dump.
5. PIT at 100 Hz is the bring-up timer; the target design is tickless
   with TSC-deadline/HPET.

## Development order

Boot → CPU → Memory → Interrupts → Timers → Scheduler → Processes →
Syscalls → Userspace → Storage → Filesystem → Drivers → Graphics →
Windowing → UI → Shell → Applications → Networking/Audio → Gaming →
Optimization → Installer/Recovery/Updates → Release.

Every phase must leave a working, tested artifact. A milestone counts as
done only when it builds, runs, is tested, handles failure, is
documented, and (where applicable) measured.

## Next milestones

1. **Phase 3/4:** kernel-owned page tables, higher-half kernel, W^X
   enforcement, reclaim of firmware/bootloader memory.
2. **Phase 5:** kernel heap on top of the PMM.
3. **Phase 6+:** APIC, HPET/TSC time, then SMP bring-up.
4. **Phase 9/10/11:** scheduler, processes/threads, SYPAS syscall ABI v1.
